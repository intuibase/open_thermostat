"""Room climate entities controlled through temporary MQTT overrides."""

import asyncio
import json
import logging
import time
import uuid

from homeassistant.components import mqtt
from homeassistant.components.mqtt.models import ReceiveMessage
from homeassistant.components.climate import ATTR_TEMPERATURE, ClimateEntity
from homeassistant.components.climate.const import ClimateEntityFeature, HVACAction, HVACMode
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import UnitOfTemperature
from homeassistant.core import HomeAssistant, callback
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .room_identity import room_device_name, room_key
from .const import CONF_TOPIC_PREFIX, DOMAIN
from .program import room_program_name

_LOGGER = logging.getLogger(__name__)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    """Subscribe to the existing aggregate room status topic."""
    prefix = entry.data[CONF_TOPIC_PREFIX]
    durations = hass.data[DOMAIN][entry.entry_id]["duration_minutes"]
    hub = dr.async_get(hass).async_get_device_by_identifier(("open_thermostat", prefix), entry.entry_id)
    rooms: dict[str, OpenThermostatRoom] = {}
    pending: dict[str, asyncio.Future[bool]] = {}
    online = False

    async def send_temperature(room_name: str, temperature: int, seconds: int) -> bool:
        request_id = uuid.uuid4().hex
        reply = asyncio.get_running_loop().create_future()
        pending[request_id] = reply
        command = json.dumps({
            "requestId": request_id,
            "roomName": room_name,
            "temperature": temperature,
            "validSeconds": seconds,
        })
        try:
            await mqtt.async_publish(hass, f"{prefix}/command/temporary", command, qos=0, retain=False)
            return await asyncio.wait_for(reply, timeout=10)
        finally:
            pending.pop(request_id, None)

    @callback
    def handle_reply(message: ReceiveMessage) -> None:
        try:
            payload = json.loads(message.payload)
        except (TypeError, ValueError):
            return
        if not isinstance(payload, dict):
            return
        request_id = payload.get("requestId")
        if not isinstance(request_id, str):
            return
        reply = pending.get(request_id)
        if reply is not None and not reply.done() and isinstance(payload.get("success"), bool):
            reply.set_result(payload["success"])

    @callback
    def handle_status(message: ReceiveMessage) -> None:
        nonlocal online
        online = message.payload == "on"
        for entity in rooms.values():
            entity.set_online(online)

    @callback
    def handle_rooms(message: ReceiveMessage) -> None:
        try:
            payload = json.loads(message.payload)
        except (TypeError, ValueError):
            _LOGGER.warning("Invalid OpenThermostat room status on %s", message.topic)
            return
        data = payload.get("rooms") if isinstance(payload, dict) else None
        if not isinstance(data, list):
            return

        added = []
        for index, room_data in enumerate(data):
            if not isinstance(room_data, dict):
                continue
            key = room_key(room_data)
            if key is None:
                continue
            entity = rooms.get(key)
            if entity is None:
                entity = OpenThermostatRoom(
                    prefix, index, key, hub.id if hub else None, durations, send_temperature
                )
                rooms[key] = entity
                added.append(entity)
            entity.update_room(
                room_data, online, payload.get("activeProgram"), payload.get("boilerStarted")
            )
        active_keys = {key for room in data if isinstance(room, dict) if (key := room_key(room)) is not None}
        for key, entity in rooms.items():
            if key not in active_keys:
                entity.set_online(False)
        if added:
            async_add_entities(added)
        if dashboard := hass.data[DOMAIN][entry.entry_id].get("dashboard"):
            dashboard.set_rooms(active_keys)

    await mqtt.async_wait_for_mqtt_client(hass)
    entry.async_on_unload(
        await mqtt.async_subscribe(hass, f"{prefix}/status", handle_status)
    )
    entry.async_on_unload(
        await mqtt.async_subscribe(hass, f"{prefix}/room_data", handle_rooms)
    )
    entry.async_on_unload(
        await mqtt.async_subscribe(hass, f"{prefix}/response/temporary", handle_reply)
    )


class OpenThermostatRoom(ClimateEntity):
    """Display room readings and request a timed temperature override."""

    _attr_should_poll = False
    _attr_has_entity_name = True
    _attr_supported_features = ClimateEntityFeature.TARGET_TEMPERATURE
    _attr_temperature_unit = UnitOfTemperature.CELSIUS
    _attr_min_temp = 10
    _attr_max_temp = 40
    _attr_target_temperature_step = 0.1
    _attr_hvac_modes = [HVACMode.HEAT]
    _attr_hvac_mode = HVACMode.HEAT

    def __init__(self, prefix: str, index: int, key: str, hub_id: str | None, durations: dict[str, int], send_temperature) -> None:
        self._key = key
        self._index = index
        self._durations = durations
        self._send_temperature = send_temperature
        self._room_name: str | None = None
        self._confirmed_target: float | None = None
        self._confirmed_until = 0.0
        self._hub_id = hub_id
        self._device_identifier = f"{prefix}_room_{key}"
        self._attr_unique_id = f"{prefix}_room_{key}_climate"
        self._attr_name = None
        self._attr_device_info = DeviceInfo(
            identifiers={("open_thermostat", self._device_identifier)},
            name=room_device_name(None, index),
            manufacturer="intuibase",
            model="OpenThermostat room",
            via_device_id=self._hub_id,
        )
        self._attr_available = False

    async def async_set_temperature(self, **kwargs: float) -> None:
        """Send a timed override and wait for the thermostat to acknowledge it."""
        temperature = kwargs.get(ATTR_TEMPERATURE)
        if temperature is None:
            return
        if not self.available or self._room_name is None:
            raise HomeAssistantError("Thermostat room is unavailable")
        minutes = self._durations.get(self._key, 120)
        try:
            accepted = await self._send_temperature(
                self._room_name, round(temperature * 100), minutes * 60
            )
        except TimeoutError as exc:
            raise HomeAssistantError("Thermostat did not confirm the temperature change") from exc
        if not accepted:
            raise HomeAssistantError("Thermostat rejected the temperature change")
        self._confirmed_target = temperature
        self._confirmed_until = time.monotonic() + 60
        self._attr_target_temperature = temperature
        self.async_write_ha_state()

    def set_online(self, online: bool) -> None:
        """Track the device's existing MQTT online status."""
        self._attr_available = online
        if self.entity_id is not None:
            self.async_write_ha_state()

    def update_room(
        self, data: dict, online: bool, active_program: object, boiler_started: object
    ) -> None:
        """Apply a room status snapshot."""
        name = data.get("name")
        if isinstance(name, str) and name:
            self._room_name = name
            self._attr_device_info = DeviceInfo(
                identifiers={("open_thermostat", self._device_identifier)},
                name=room_device_name(name, self._index),
                manufacturer="intuibase",
                model="OpenThermostat room",
                via_device_id=self._hub_id,
            )
        temperature = data.get("currentTemp")
        self._attr_current_temperature = (
            temperature / 100 if isinstance(temperature, (int, float)) else None
        )
        humidity = data.get("currentHumidity")
        self._attr_current_humidity = (
            humidity / 100 if isinstance(humidity, (int, float)) else None
        )
        battery = data.get("batteryLevel")
        set_temperature = data.get("tempSet")
        attributes = {}
        if isinstance(battery, (int, float)) and 0 <= battery <= 100:
            attributes["battery_level"] = battery
        if isinstance(set_temperature, (int, float)):
            attributes["temperature_set"] = set_temperature / 100
        reported_target = (
            set_temperature / 100 if isinstance(set_temperature, (int, float)) else None
        )
        if self._confirmed_target is not None and (
            reported_target == self._confirmed_target or time.monotonic() > self._confirmed_until
        ):
            self._confirmed_target = None
        self._attr_target_temperature = (
            self._confirmed_target if self._confirmed_target is not None else reported_target
        )
        if program_name := room_program_name(data, active_program):
            attributes["program_name"] = program_name
        self._attr_extra_state_attributes = attributes
        if not data.get("enabled", True):
            self._attr_hvac_modes = [HVACMode.OFF]
            self._attr_hvac_mode = HVACMode.OFF
            self._attr_hvac_action = HVACAction.OFF
        else:
            self._attr_hvac_modes = [HVACMode.HEAT]
            self._attr_hvac_mode = HVACMode.HEAT
            heating = (
                data.get("shouldContinueHeating", False) and boiler_started
                if isinstance(boiler_started, bool)
                else data.get("isBeingHeated", False)
            )
            self._attr_hvac_action = (
                HVACAction.HEATING if heating else HVACAction.IDLE
            )
        self._attr_available = online
        if self.entity_id is not None:
            self.async_write_ha_state()
