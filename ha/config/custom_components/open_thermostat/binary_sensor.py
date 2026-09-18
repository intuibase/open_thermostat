"""Heating states for OpenThermostat rooms."""

import json
import logging

from homeassistant.components import mqtt
from homeassistant.components.binary_sensor import BinarySensorDeviceClass, BinarySensorEntity
from homeassistant.components.mqtt.models import ReceiveMessage
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import EntityCategory
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .room_identity import migrate_room_registry, room_key
from .const import CONF_TOPIC_PREFIX, DOMAIN

_LOGGER = logging.getLogger(__name__)

STATES = {
    "enabled": ("enabledState", "Heating enabled", None),
    "heating": ("isBeingHeatedState", "Heating", BinarySensorDeviceClass.HEAT),
}


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    """Subscribe to room state and availability messages."""
    prefix = entry.data[CONF_TOPIC_PREFIX]
    hub = dr.async_get(hass).async_get_device_by_identifier((DOMAIN, prefix), entry.entry_id)
    hub_id = hub.id if hub else None
    sensors: dict[tuple[str, str], RoomState] = {}
    connection = HubConnection(prefix)
    async_add_entities([connection])
    online = False

    @callback
    def handle_status(message: ReceiveMessage) -> None:
        nonlocal online
        online = message.payload == "on"
        connection.update_value(online)
        for entity in sensors.values():
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
            for state, (field, _name, _device_class) in STATES.items():
                value = room.get(field)
                state_key = (key, state)
                entity = sensors.get(state_key)
                if entity is None:
                    if value not in ("on", "off"):
                        continue
                    entity = RoomState(prefix, index, key, room.get("name"), state, hub_id)
                    sensors[state_key] = entity
                    added.append(entity)
                entity.update_value(value, online)
        active_keys = {room_key(room, index) for index, room in enumerate(rooms) if isinstance(room, dict)}
        for (key, _state), entity in sensors.items():
            if key not in active_keys:
                entity.set_online(False)
        if added:
            async_add_entities(added)

    await mqtt.async_wait_for_mqtt_client(hass)
    entry.async_on_unload(await mqtt.async_subscribe(hass, f"{prefix}/status", handle_status))
    entry.async_on_unload(await mqtt.async_subscribe(hass, f"{prefix}/room_data", handle_rooms))


class RoomState(BinarySensorEntity):
    """A heating status associated with its room device."""

    _attr_should_poll = False
    _attr_has_entity_name = True

    def __init__(self, prefix: str, index: int, key: str, room_name: str | None, state: str, hub_id: str | None) -> None:
        _field, name, device_class = STATES[state]
        self._attr_name = name
        self._attr_device_class = device_class
        self._attr_unique_id = f"{prefix}_room_{key}_{state}"
        name = room_name if isinstance(room_name, str) and room_name else f"Room {index + 1}"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, f"{prefix}_room_{key}")},
            name=name,
            manufacturer="intuibase",
            model="OpenThermostat room",
            via_device_id=hub_id,
        )
        self._attr_available = False

    def set_online(self, online: bool) -> None:
        self._attr_available = online
        if self.entity_id is not None:
            self.async_write_ha_state()

    def update_value(self, value: object, online: bool) -> None:
        self._attr_is_on = value == "on" if value in ("on", "off") else None
        self._attr_available = online
        if self.entity_id is not None:
            self.async_write_ha_state()


class HubConnection(BinarySensorEntity):
    """Connection status of the main thermostat device."""

    _attr_should_poll = False
    _attr_has_entity_name = True
    _attr_name = "Connected"
    _attr_device_class = BinarySensorDeviceClass.CONNECTIVITY
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(self, prefix: str) -> None:
        self._attr_unique_id = f"{prefix}_hub_connected"
        self._attr_device_info = DeviceInfo(identifiers={(DOMAIN, prefix)})
        self._attr_is_on = None

    def update_value(self, online: bool) -> None:
        self._attr_is_on = online
        if self.entity_id is not None:
            self.async_write_ha_state()
