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

from .room_identity import room_device_name, room_key
from .const import CONF_TOPIC_PREFIX, DOMAIN
from .heating import HeatingTracker

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
    radiator_entities: dict[str, RadiatorPower] = {}
    tracker: HeatingTracker = hass.data[DOMAIN][entry.entry_id]["heating_tracker"]
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
            key = room_key(room)
            if key is None:
                continue
            entity = entities.get(key)
            if entity is None:
                entity = OverrideDuration(prefix, index, key, room.get("name"), hub_id, durations)
                entities[key] = entity
                added.append(entity)
            entity.set_online(online)
        active_keys = {key for room in rooms if isinstance(room, dict) if (key := room_key(room)) is not None}
        for key, entity in entities.items():
            if key not in active_keys:
                entity.set_online(False)
        if added:
            async_add_entities(added)

    await mqtt.async_wait_for_mqtt_client(hass)
    entry.async_on_unload(await mqtt.async_subscribe(hass, f"{prefix}/status", handle_status))
    entry.async_on_unload(await mqtt.async_subscribe(hass, f"{prefix}/room_data", handle_rooms))

    @callback
    def update_radiators() -> None:
        added = []
        for key, room in tracker.rooms.items():
            entity = radiator_entities.get(key)
            if entity is None:
                entity = RadiatorPower(prefix, room.index, key, room.name, hub_id, tracker)
                radiator_entities[key] = entity
                added.append(entity)
            entity.update_from_tracker()
        if added:
            async_add_entities(added)

    entry.async_on_unload(tracker.async_add_listener(update_radiators))


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
        name = room_device_name(room_name, index)
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


class RadiatorPower(NumberEntity, RestoreEntity):
    """Aggregate EN 442 rated output of all radiators in a room."""

    _attr_should_poll = False
    _attr_has_entity_name = True
    _attr_translation_key = "radiator_rated_power"
    _attr_native_min_value = 0
    _attr_native_max_value = 20000
    _attr_native_step = 1
    _attr_native_unit_of_measurement = "W"
    _attr_icon = "mdi:radiator"

    def __init__(
        self,
        prefix: str,
        index: int,
        key: str,
        room_name: str | None,
        hub_id: str | None,
        tracker: HeatingTracker,
    ) -> None:
        self._key = key
        self._tracker = tracker
        self._attr_native_value = tracker.rooms[key].rated_power
        self._attr_unique_id = f"{prefix}_room_{key}_radiator_power"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, f"{prefix}_room_{key}")},
            name=room_device_name(room_name, index),
            manufacturer="intuibase",
            model="OpenThermostat room",
            via_device_id=hub_id,
        )
        self._attr_available = tracker.online

    async def async_added_to_hass(self) -> None:
        await super().async_added_to_hass()
        previous = await self.async_get_last_state()
        if previous is not None:
            try:
                value = float(previous.state)
            except (TypeError, ValueError):
                value = 0
            if self.native_min_value <= value <= self.native_max_value:
                self._tracker.set_rated_power(self._key, value)
        self.update_from_tracker()

    async def async_set_native_value(self, value: float) -> None:
        self._tracker.set_rated_power(self._key, value)

    def update_from_tracker(self) -> None:
        room = self._tracker.rooms.get(self._key)
        self._attr_available = self._tracker.online and room is not None
        if room is not None:
            self._attr_native_value = room.rated_power
        if self.entity_id is not None:
            self.async_write_ha_state()
