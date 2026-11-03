"""Built-in MQTT availability and room battery notifications."""

from datetime import datetime
import json
import logging
import math

from homeassistant.components import mqtt, persistent_notification
from homeassistant.components.mqtt.models import ReceiveMessage
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.event import async_track_time_change
from homeassistant.util import dt as dt_util

from .const import (
    CONF_BATTERY_ALERTS, CONF_BATTERY_THRESHOLD, CONF_BATTERY_REMINDER,
    CONF_BATTERY_REMINDER_TIME, CONF_NOTIFY_SERVICE, CONF_TOPIC_PREFIX,
)
from .room_identity import room_key

_LOGGER = logging.getLogger(__name__)


class IntegrationAlerts:
    """Watch all rooms automatically and keep one notification per alert type."""

    def __init__(self, hass: HomeAssistant, entry: ConfigEntry) -> None:
        self.hass = hass
        self.entry = entry
        self.prefix = entry.data[CONF_TOPIC_PREFIX]
        self.online: bool | None = None
        self.rooms: dict[str, tuple[str, float]] = {}
        self._notified_low: set[str] = set()
        self._unsubscribers = []

    def _notification_id(self, kind: str) -> str:
        return f"open_thermostat_{self.entry.entry_id}_{kind}"

    async def async_start(self) -> None:
        """Subscribe and schedule reminders in the HA local timezone."""
        await mqtt.async_wait_for_mqtt_client(self.hass)
        try:
            self._unsubscribers.append(await mqtt.async_subscribe(
                self.hass, f"{self.prefix}/status", self._handle_status,
            ))
            self._unsubscribers.append(await mqtt.async_subscribe(
                self.hass, f"{self.prefix}/room_data", self._handle_rooms,
            ))
            if self.entry.options.get(CONF_BATTERY_ALERTS, True) and self.entry.options.get(CONF_BATTERY_REMINDER, True):
                time = dt_util.parse_time(self.entry.options.get(CONF_BATTERY_REMINDER_TIME, "12:00:00"))
                self._unsubscribers.append(async_track_time_change(
                    self.hass, self._daily_reminder,
                    hour=time.hour, minute=time.minute, second=time.second,
                ))
        except Exception:
            await self.async_stop()
            raise
        if not self.entry.options.get(CONF_BATTERY_ALERTS, True):
            persistent_notification.async_dismiss(self.hass, self._notification_id("battery"))

    async def async_stop(self) -> None:
        """Remove MQTT subscriptions and the reminder timer on unload."""
        for unsubscribe in self._unsubscribers:
            unsubscribe()
        self._unsubscribers.clear()

    async def _handle_status(self, message: ReceiveMessage) -> None:
        if message.payload not in {"on", "off"}:
            return
        online = message.payload == "on"
        if self.online == online:
            return
        self.online = online
        if online:
            persistent_notification.async_dismiss(self.hass, self._notification_id("offline"))
            await self._check_batteries()
        else:
            # Do not remind using cached readings while the hub is disconnected.
            self.rooms.clear()
            self._notified_low.clear()
            message_text = (
                f"Termostat {self.prefix} jest offline."
                if self.hass.config.language == "pl"
                else f"Thermostat {self.prefix} is offline."
            )
            await self._notify("offline", message_text)

    async def _handle_rooms(self, message: ReceiveMessage) -> None:
        try:
            payload = json.loads(message.payload)
        except (TypeError, ValueError):
            return
        rooms = payload.get("rooms") if isinstance(payload, dict) else None
        if not isinstance(rooms, list):
            return
        readings = {}
        for room in rooms:
            if not isinstance(room, dict) or (key := room_key(room)) is None:
                continue
            level = room.get("batteryLevel")
            if isinstance(level, bool) or not isinstance(level, (int, float)):
                continue
            if not math.isfinite(level) or not 0 <= level <= 100:
                continue
            name = room.get("name")
            readings[key] = (name if isinstance(name, str) and name else key, level)
        self.rooms = readings
        await self._check_batteries()

    def _low_batteries(self) -> dict[str, tuple[str, float]]:
        threshold = self.entry.options.get(CONF_BATTERY_THRESHOLD, 20)
        return {key: reading for key, reading in self.rooms.items() if reading[1] < threshold}

    def _battery_message(self, low: dict[str, tuple[str, float]]) -> str:
        threshold = self.entry.options.get(CONF_BATTERY_THRESHOLD, 20)
        title = "Słabe baterie" if self.hass.config.language == "pl" else "Low batteries"
        rooms = ", ".join(f"{name}: {level:g}%" for name, level in low.values())
        return f"{title} ({self.prefix}, < {threshold:g}%): {rooms}."

    async def _check_batteries(self) -> None:
        if not self.entry.options.get(CONF_BATTERY_ALERTS, True) or self.online is not True:
            return
        low = self._low_batteries()
        newly_low = low.keys() - self._notified_low
        self._notified_low = set(low)
        if not low:
            persistent_notification.async_dismiss(self.hass, self._notification_id("battery"))
        elif newly_low:
            await self._notify("battery", self._battery_message(low))
        else:
            # Update the list after recovery without another phone notification.
            persistent_notification.async_create(
                self.hass, self._battery_message(low), title="OpenThermostat",
                notification_id=self._notification_id("battery"),
            )

    async def _daily_reminder(self, _now: datetime) -> None:
        if self.online is True and (low := self._low_batteries()):
            await self._notify("battery", self._battery_message(low))

    async def _notify(self, kind: str, message: str) -> None:
        persistent_notification.async_create(
            self.hass, message, title="OpenThermostat",
            notification_id=self._notification_id(kind),
        )
        service = self.entry.options.get(CONF_NOTIFY_SERVICE, "")
        if service.startswith("notify."):
            try:
                await self.hass.services.async_call(
                    "notify", service.removeprefix("notify."),
                    {"title": "OpenThermostat", "message": message}, blocking=True,
                )
            except HomeAssistantError:
                _LOGGER.exception("OpenThermostat notification service %s failed", service)
