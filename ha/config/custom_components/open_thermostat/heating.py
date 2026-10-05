"""Shared room-heating state and energy estimation."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
import json
import logging

from homeassistant.components import mqtt
from homeassistant.components.mqtt.models import ReceiveMessage
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback

from .const import CONF_TOPIC_PREFIX
from .room_identity import room_key

_LOGGER = logging.getLogger(__name__)

DEFAULT_RADIATOR_EXPONENT = 1.3
DEFAULT_RETURN_TEMPERATURE_DROP = 10.0
MAX_INTEGRATION_INTERVAL = 300.0


def boolean_state(value: object) -> bool | None:
    """Normalize boolean states emitted by current and older firmware."""
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)) and value in (0, 1):
        return bool(value)
    if isinstance(value, str):
        normalized = value.strip().lower()
        if normalized in ("on", "true", "1"):
            return True
        if normalized in ("off", "false", "0"):
            return False
    return None


@dataclass
class RoomHeating:
    """Runtime and restored heating information for one room."""

    key: str
    index: int
    name: str | None
    current_temperature: float | None = None
    requested: bool = False
    rated_power: float = 0.0
    heating_seconds: float = 0.0
    estimated_energy_kwh: float = 0.0


class HeatingTracker:
    """Combine room demand and EMS state and integrate time and energy."""

    def __init__(self, hass: HomeAssistant, entry: ConfigEntry) -> None:
        self.hass = hass
        self.entry = entry
        self.prefix = entry.data[CONF_TOPIC_PREFIX]
        self.rooms: dict[str, RoomHeating] = {}
        self.online = False
        self.heating_active: bool | None = None
        self.hot_water_active: bool | None = None
        self.flow_temperature: float | None = None
        self.radiator_exponent = DEFAULT_RADIATOR_EXPONENT
        self.return_temperature_drop = DEFAULT_RETURN_TEMPERATURE_DROP
        self._last_update = hass.loop.time()
        self._listeners: set[Callable[[], None]] = set()
        self._unsubscribers: list[Callable[[], None]] = []

    async def async_start(self) -> None:
        """Subscribe to the MQTT topics needed for heating calculations."""
        await mqtt.async_wait_for_mqtt_client(self.hass)
        self._unsubscribers.extend([
            await mqtt.async_subscribe(self.hass, f"{self.prefix}/status", self._handle_status),
            await mqtt.async_subscribe(self.hass, f"{self.prefix}/room_data", self._handle_rooms),
            await mqtt.async_subscribe(self.hass, f"{self.prefix}/ems_status", self._handle_ems),
        ])

    async def async_stop(self) -> None:
        """Stop MQTT subscriptions after accounting for the last interval."""
        self._integrate()
        for unsubscribe in self._unsubscribers:
            unsubscribe()
        self._unsubscribers.clear()
        self._listeners.clear()

    @callback
    def async_add_listener(self, listener: Callable[[], None]) -> Callable[[], None]:
        """Register an entity listener and immediately expose current data."""
        self._listeners.add(listener)
        listener()

        @callback
        def remove_listener() -> None:
            self._listeners.discard(listener)

        return remove_listener

    @callback
    def _notify(self) -> None:
        for listener in tuple(self._listeners):
            listener()

    def _integrate(self) -> None:
        now = self.hass.loop.time()
        elapsed = min(max(now - self._last_update, 0.0), MAX_INTEGRATION_INTERVAL)
        self._last_update = now
        if elapsed <= 0 or not self.online:
            return
        for room in self.rooms.values():
            if not self.is_heating(room.key):
                continue
            room.heating_seconds += elapsed
            power = self.estimated_power(room.key)
            if power is not None:
                room.estimated_energy_kwh += power * elapsed / 3_600_000

    def is_heating(self, key: str) -> bool:
        """Return whether this room receives central-heating energy now."""
        room = self.rooms.get(key)
        return bool(
            room
            and room.requested
            and self.heating_active is True
            and self.hot_water_active is False
            and self.online
        )

    def estimated_power(self, key: str) -> float | None:
        """Estimate current radiator output using the EN 442 power curve."""
        room = self.rooms.get(key)
        if not room or not self.is_heating(key) or room.rated_power <= 0:
            return 0.0 if room else None
        if self.flow_temperature is None or room.current_temperature is None:
            return None
        return_temperature = self.flow_temperature - self.return_temperature_drop
        mean_water_temperature = (self.flow_temperature + return_temperature) / 2
        delta_t = max(mean_water_temperature - room.current_temperature, 0.0)
        return room.rated_power * (delta_t / 50.0) ** self.radiator_exponent

    @callback
    def set_rated_power(self, key: str, value: float) -> None:
        """Apply a user-configured aggregate radiator power for a room."""
        self._integrate()
        if room := self.rooms.get(key):
            room.rated_power = max(value, 0.0)
            self._notify()

    @callback
    def restore_totals(self, key: str, seconds: float | None = None, energy: float | None = None) -> None:
        """Restore counters from Home Assistant entity state."""
        if room := self.rooms.get(key):
            if seconds is not None:
                room.heating_seconds = max(room.heating_seconds, seconds)
            if energy is not None:
                room.estimated_energy_kwh = max(room.estimated_energy_kwh, energy)
            self._notify()

    @callback
    def _handle_status(self, message: ReceiveMessage) -> None:
        self._integrate()
        self.online = message.payload == "on"
        self._notify()

    @callback
    def _handle_rooms(self, message: ReceiveMessage) -> None:
        try:
            payload = json.loads(message.payload)
        except (TypeError, ValueError):
            _LOGGER.warning("Invalid OpenThermostat room status on %s", message.topic)
            return
        rooms = payload.get("rooms") if isinstance(payload, dict) else None
        if not isinstance(rooms, list):
            return
        self._integrate()
        boiler_started = boolean_state(payload.get("boilerStarted"))
        active_keys: set[str] = set()
        for index, value in enumerate(rooms):
            if not isinstance(value, dict) or (key := room_key(value)) is None:
                continue
            active_keys.add(key)
            room = self.rooms.get(key)
            if room is None:
                room = self.rooms[key] = RoomHeating(key, index, value.get("name"))
            room.index = index
            room.name = value.get("name") if isinstance(value.get("name"), str) else room.name
            temperature = value.get("currentTemp")
            room.current_temperature = (
                temperature / 100
                if isinstance(temperature, (int, float)) and not isinstance(temperature, bool)
                else None
            )
            requested = value.get("isBeingHeated")
            if requested is None:
                requested = value.get("isBeingHeatedState")
            room.requested = (
                boolean_state(requested) is True
                or (
                    boiler_started is True
                    and boolean_state(value.get("shouldContinueHeating")) is True
                )
            )
        for key in set(self.rooms) - active_keys:
            self.rooms[key].requested = False
        self._notify()

    @callback
    def _handle_ems(self, message: ReceiveMessage) -> None:
        try:
            payload = json.loads(message.payload)
        except (TypeError, ValueError):
            _LOGGER.warning("Invalid OpenThermostat EMS status on %s", message.topic)
            return
        if not isinstance(payload, dict):
            return
        self._integrate()
        self.heating_active = boolean_state(payload.get("heatingActive"))
        self.hot_water_active = boolean_state(payload.get("warmWaterActive"))
        temperature = payload.get("currentFlowTemperature")
        self.flow_temperature = (
            temperature / 10
            if isinstance(temperature, (int, float)) and not isinstance(temperature, bool)
            else None
        )
        self._notify()
