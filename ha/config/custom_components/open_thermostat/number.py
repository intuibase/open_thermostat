"""Duration of a room's next temporary temperature change."""

import json
import logging

from homeassistant.components import mqtt
from homeassistant.components.mqtt.models import ReceiveMessage
from homeassistant.components.number import NumberEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback
from homeassistant.helpers.restore_state import RestoreEntity

from .room_identity import migrate_room_registry, room_key
from .const import CONF_TOPIC_PREFIX, DOMAIN

_LOGGER = logging.getLogger(__name__)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    """Create one duration control for each discovered room."""
    prefix = entry.data[CONF_TOPIC_PREFIX]
    durations = hass.data[DOMAIN][entry.entry_id]["duration_minutes"]
    hub = dr.async_get(hass).async_get_device_by_identifier((DOMAIN, prefix), entry.entry_id)
    hub_id = hub.id if hub else None
    entities: dict[str, OverrideDuration] = {}
    online = False

    @callback
    def handle_status(message: ReceiveMessage) -> None:
        nonlocal online
        online = message.payload == "on"
        for entity in entities.values():
            entity.set_online(online)

    @callback
    def handle_rooms(message: ReceiveMessage) -> None:
        try:
            payload = json.loads(message.payload)
        except (TypeError, ValueError):
            _LOGGER.warning("Invalid OpenThermostat room status on %s", message.topic)
            return
        rooms = payload.get("rooms") if isinstance(payload, dict) else None
        if not isinstance(rooms, list):
            return
        added = []
        for index, room in enumerate(rooms):
            if not isinstance(room, dict):
                continue
            key = room_key(room, index)
            migrate_room_registry(hass, prefix, index, key)
            entity = entities.get(key)
            if entity is None:
                entity = OverrideDuration(prefix, index, key, room.get("name"), hub_id, durations)
                entities[key] = entity
                added.append(entity)
            entity.set_online(online)
        active_keys = {room_key(room, index) for index, room in enumerate(rooms) if isinstance(room, dict)}
        for key, entity in entities.items():
            if key not in active_keys:
                entity.set_online(False)
        if added:
            async_add_entities(added)

    await mqtt.async_wait_for_mqtt_client(hass)
    entry.async_on_unload(await mqtt.async_subscribe(hass, f"{prefix}/status", handle_status))
    entry.async_on_unload(await mqtt.async_subscribe(hass, f"{prefix}/room_data", handle_rooms))


class OverrideDuration(NumberEntity, RestoreEntity):
    """Minutes used for the next climate set-temperature command."""

    _attr_should_poll = False
    _attr_has_entity_name = True
    _attr_name = "Temperature override duration"
    _attr_native_min_value = 10
    _attr_native_max_value = 720
    _attr_native_step = 10
    _attr_native_unit_of_measurement = "min"

    def __init__(self, prefix: str, index: int, key: str, room_name: str | None, hub_id: str | None, durations: dict[str, int]) -> None:
        self._key = key
        self._durations = durations
        self._attr_native_value = durations.setdefault(key, 120)
        self._attr_unique_id = f"{prefix}_room_{key}_override_duration"
        name = room_name if isinstance(room_name, str) and room_name else f"Room {index + 1}"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, f"{prefix}_room_{key}")},
            name=name,
            manufacturer="intuibase",
            model="OpenThermostat room",
            via_device_id=hub_id,
        )
        self._attr_available = False

    async def async_added_to_hass(self) -> None:
        await super().async_added_to_hass()
        previous = await self.async_get_last_state()
        if previous is not None:
            try:
                minutes = int(float(previous.state))
            except (TypeError, ValueError):
                return
            if self.native_min_value <= minutes <= self.native_max_value:
                self._attr_native_value = minutes
                self._durations[self._key] = minutes
                self.async_write_ha_state()

    async def async_set_native_value(self, value: float) -> None:
        minutes = int(value)
        self._attr_native_value = minutes
        self._durations[self._key] = minutes
        self.async_write_ha_state()

    def set_online(self, online: bool) -> None:
        self._attr_available = online
        if self.entity_id is not None:
            self.async_write_ha_state()
