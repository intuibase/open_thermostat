"""Room readings beside OpenThermostat climate entities."""

import json
import logging

from homeassistant.components import mqtt
from homeassistant.components.mqtt.models import ReceiveMessage
from homeassistant.components.sensor import SensorDeviceClass, SensorEntity, SensorStateClass
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import EntityCategory, PERCENTAGE, UnitOfInformation, UnitOfTemperature
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .room_identity import room_device_name, room_key
from .const import CONF_TOPIC_PREFIX, DOMAIN
from .program import room_program_name

_LOGGER = logging.getLogger(__name__)

METRICS = {
    "temperature": ("currentTemp", SensorDeviceClass.TEMPERATURE, 100, UnitOfTemperature.CELSIUS, "Temperature"),
    "temperature_set": ("tempSet", SensorDeviceClass.TEMPERATURE, 100, UnitOfTemperature.CELSIUS, "Temperature set"),
    "humidity": ("currentHumidity", SensorDeviceClass.HUMIDITY, 100, PERCENTAGE, "Humidity"),
    "battery": ("batteryLevel", SensorDeviceClass.BATTERY, 1, PERCENTAGE, "Battery"),
}

HUB_METRICS = {
    "device_status": {
        "memory_free": ("mem_free", SensorDeviceClass.DATA_SIZE, UnitOfInformation.BYTES, SensorStateClass.MEASUREMENT, "Free memory"),
        "memory_min_free": ("mem_min_free", SensorDeviceClass.DATA_SIZE, UnitOfInformation.BYTES, SensorStateClass.MEASUREMENT, "Minimum free memory"),
        "memory_max_alloc": ("mem_max_alloc", SensorDeviceClass.DATA_SIZE, UnitOfInformation.BYTES, SensorStateClass.MEASUREMENT, "Maximum allocable memory"),
        "rtc_temperature": ("temperature", SensorDeviceClass.TEMPERATURE, UnitOfTemperature.CELSIUS, SensorStateClass.MEASUREMENT, "RTC temperature"),
        "cpu_temperature": ("cpu_temperature", SensorDeviceClass.TEMPERATURE, UnitOfTemperature.CELSIUS, SensorStateClass.MEASUREMENT, "ESP32 CPU temperature"),
        "uptime": ("uptime", SensorDeviceClass.DURATION, "s", SensorStateClass.TOTAL_INCREASING, "Device uptime"),
    },
    "ems_metrics": {
        "energy": ("totalEnergyUsedKwh", SensorDeviceClass.ENERGY, "kWh", SensorStateClass.TOTAL, "Total energy consumption"),
        "energy_warm_water": ("warmWaterEnergyUsedKwh", SensorDeviceClass.ENERGY, "kWh", SensorStateClass.TOTAL, "Energy used for warm water heating"),
        "energy_heating": ("heatingEnergyUsedKwh", SensorDeviceClass.ENERGY, "kWh", SensorStateClass.TOTAL, "Energy used for space heating"),
        "warm_water_usage": ("warmWaterUsage", None, "L", SensorStateClass.MEASUREMENT, "Warm water usage"),
        "warm_water_avg_flow": ("warmWaterAvgFlow", None, "L/min", SensorStateClass.MEASUREMENT, "Average flow of warm water"),
        "outdoor_temperature": ("outdoorTemperature", SensorDeviceClass.TEMPERATURE, UnitOfTemperature.CELSIUS, SensorStateClass.MEASUREMENT, "Outdoor temperature"),
    },
    "ems_status": {
        "boiler_selected_hot_water_temperature": ("selectedWarmWaterTemperature", SensorDeviceClass.TEMPERATURE, UnitOfTemperature.CELSIUS, SensorStateClass.MEASUREMENT, "Boiler selected hot water temperature"),
        "boiler_selected_flow_temperature": ("selectedFlowTemperature", SensorDeviceClass.TEMPERATURE, UnitOfTemperature.CELSIUS, SensorStateClass.MEASUREMENT, "Boiler selected flow temperature"),
        "boiler_current_flow_temperature": ("currentFlowTemperature", SensorDeviceClass.TEMPERATURE, UnitOfTemperature.CELSIUS, SensorStateClass.MEASUREMENT, "Boiler current flow temperature"),
        "boiler_pressure": ("pressure", SensorDeviceClass.PRESSURE, "bar", SensorStateClass.MEASUREMENT, "Boiler pressure"),
        "boiler_burner_power": ("currentBurnerPower", SensorDeviceClass.POWER_FACTOR, PERCENTAGE, SensorStateClass.MEASUREMENT, "Boiler burner power"),
        "boiler_hot_water_flow": ("warmWaterFlow", None, "L/min", SensorStateClass.MEASUREMENT, "Boiler hot water flow"),
        "boiler_current_hot_water_temperature": ("currentWarmWaterTemperature", SensorDeviceClass.TEMPERATURE, UnitOfTemperature.CELSIUS, SensorStateClass.MEASUREMENT, "Boiler current hot water temperature"),
        "boiler_protocol_version": ("protocolVersion", None, None, None, "Boiler protocol version"),
        "boiler_service_code": ("serviceCode", None, None, None, "Boiler service code"),
        "boiler_display_code": ("displayCode", None, None, None, "Boiler display code"),
    },
}

HUB_DIVISORS = {
    "boiler_current_flow_temperature": 10,
    "boiler_pressure": 10,
    "boiler_hot_water_flow": 10,
    "boiler_current_hot_water_temperature": 10,
}


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    """Create sensors for room values published by the thermostat."""
    prefix = entry.data[CONF_TOPIC_PREFIX]
    hub = dr.async_get(hass).async_get_device_by_identifier((DOMAIN, prefix), entry.entry_id)
    hub_id = hub.id if hub else None
    sensors: dict[tuple[str, str], RoomMetric] = {}
    programs: dict[str, RoomProgram] = {}
    names: dict[str, RoomName] = {}
    hub_sensors: dict[tuple[str, str], HubMetric] = {}
    online = False

    @callback
    def handle_status(message: ReceiveMessage) -> None:
        nonlocal online
        online = message.payload == "on"
        for entity in sensors.values():
            entity.set_online(online)
        for entity in programs.values():
            entity.set_online(online)
        for entity in names.values():
            entity.set_online(online)
        for entity in hub_sensors.values():
            entity.set_online(online)

    @callback
    def handle_hub(message: ReceiveMessage, topic: str) -> None:
        try:
            payload = json.loads(message.payload)
        except (TypeError, ValueError):
            _LOGGER.warning("Invalid OpenThermostat status on %s", message.topic)
            return
        if not isinstance(payload, dict):
            return
        added = []
        for metric, (field, _device_class, _unit, _state_class, _name) in HUB_METRICS[topic].items():
            value = payload.get(field)
            key = (topic, metric)
            entity = hub_sensors.get(key)
            if entity is None:
                entity = HubMetric(prefix, topic, metric)
                hub_sensors[key] = entity
                added.append(entity)
            entity.update_value(value, online)
        if added:
            async_add_entities(added)

    def hub_handler(topic: str):
        @callback
        def handle(message: ReceiveMessage) -> None:
            handle_hub(message, topic)
        return handle

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
            for metric, (field, _device_class, _divisor, _unit, _name) in METRICS.items():
                metric_key = (key, metric)
                entity = sensors.get(metric_key)
                value = room.get(field)
                if entity is None:
                    if not isinstance(value, (int, float)) or isinstance(value, bool):
                        continue
                    entity = RoomMetric(prefix, index, key, room.get("name"), metric, hub_id)
                    sensors[metric_key] = entity
                    added.append(entity)
                entity.update_value(value, online)
            program_name = room_program_name(room, payload.get("activeProgram"))
            program = programs.get(key)
            if program is None and program_name is not None:
                program = RoomProgram(prefix, index, key, room.get("name"), hub_id)
                programs[key] = program
                added.append(program)
            if program is not None:
                program.update_value(program_name, room.get("temporaryProgramSecondsLeft"), online)
            room_name = room.get("name")
            name_entity = names.get(key)
            if name_entity is None and isinstance(room_name, str) and room_name:
                name_entity = RoomName(prefix, index, key, room_name, hub_id)
                names[key] = name_entity
                added.append(name_entity)
            if name_entity is not None:
                name_entity.update_value(room_name, online)
        active_keys = {key for room in rooms if isinstance(room, dict) if (key := room_key(room)) is not None}
        for (key, _metric), entity in sensors.items():
            if key not in active_keys:
                entity.set_online(False)
        for key, entity in programs.items():
            if key not in active_keys:
                entity.set_online(False)
        for key, entity in names.items():
            if key not in active_keys:
                entity.set_online(False)
        if added:
            async_add_entities(added)

    await mqtt.async_wait_for_mqtt_client(hass)
    entry.async_on_unload(
        await mqtt.async_subscribe(hass, f"{prefix}/status", handle_status)
    )
    entry.async_on_unload(
        await mqtt.async_subscribe(hass, f"{prefix}/room_data", handle_rooms)
    )
    for topic in HUB_METRICS:
        entry.async_on_unload(
            await mqtt.async_subscribe(
                hass, f"{prefix}/{topic}", hub_handler(topic)
            )
        )


class RoomMetric(SensorEntity):
    """A room temperature, humidity, or battery reading."""

    _attr_should_poll = False
    _attr_has_entity_name = True
    _attr_state_class = SensorStateClass.MEASUREMENT

    def __init__(self, prefix: str, index: int, key: str, room_name: str | None, metric: str, hub_id: str | None = None) -> None:
        field, device_class, divisor, unit, display_name = METRICS[metric]
        self._field = field
        self._divisor = divisor
        self._attr_device_class = device_class
        self._attr_native_unit_of_measurement = unit
        self._attr_name = display_name
        self._attr_unique_id = f"{prefix}_room_{key}_{metric}"
        name = room_device_name(room_name, index)
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, f"{prefix}_room_{key}")},
            name=name,
            manufacturer="intuibase",
            model="OpenThermostat room",
            via_device_id=hub_id,
        )
        self._attr_available = False

    def set_online(self, online: bool) -> None:
        """Update availability from the thermostat's MQTT status."""
        self._attr_available = online
        if self.entity_id is not None:
            self.async_write_ha_state()

    def update_value(self, value: object, online: bool) -> None:
        """Apply a raw sensor value."""
        self._attr_native_value = (
            value / self._divisor
            if isinstance(value, (int, float)) and not isinstance(value, bool)
            else None
        )
        self._attr_available = online
        if self.entity_id is not None:
            self.async_write_ha_state()


class RoomProgram(SensorEntity):
    """Human-readable program currently applied to a room."""

    _attr_should_poll = False
    _attr_has_entity_name = True
    _attr_name = "Program"

    def __init__(self, prefix: str, index: int, key: str, room_name: str | None, hub_id: str | None = None) -> None:
        self._attr_unique_id = f"{prefix}_room_{key}_program"
        name = room_device_name(room_name, index)
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, f"{prefix}_room_{key}")},
            name=name,
            manufacturer="intuibase",
            model="OpenThermostat room",
            via_device_id=hub_id,
        )
        self._attr_available = False

    def set_online(self, online: bool) -> None:
        """Update availability from the thermostat's MQTT status."""
        self._attr_available = online
        if self.entity_id is not None:
            self.async_write_ha_state()

    def update_value(self, name: str | None, remaining: object, online: bool) -> None:
        """Apply a program name and optional override countdown."""
        self._attr_native_value = name
        self._attr_extra_state_attributes = (
            {"temporary_seconds_left": remaining}
            if isinstance(remaining, (int, float)) and remaining > 0
            else {}
        )
        self._attr_available = online
        if self.entity_id is not None:
            self.async_write_ha_state()


class HubMetric(SensorEntity):
    """A reading published by the thermostat's main device."""

    _attr_should_poll = False
    _attr_has_entity_name = True

    def __init__(self, prefix: str, topic: str, metric: str) -> None:
        _field, device_class, unit, state_class, name = HUB_METRICS[topic][metric]
        if topic == "device_status":
            self._attr_entity_category = EntityCategory.DIAGNOSTIC
        self._attr_device_class = device_class
        self._attr_native_unit_of_measurement = unit
        self._attr_state_class = state_class
        self._attr_name = name
        self._attr_unique_id = f"{prefix}_hub_{metric}"
        self._divisor = HUB_DIVISORS.get(metric, 1)
        self._attr_device_info = DeviceInfo(identifiers={(DOMAIN, prefix)})
        self._attr_available = False

    def set_online(self, online: bool) -> None:
        self._attr_available = online
        if self.entity_id is not None:
            self.async_write_ha_state()

    def update_value(self, value: object, online: bool) -> None:
        self._attr_native_value = (
            value / self._divisor
            if isinstance(value, (int, float)) and not isinstance(value, bool)
            else value if isinstance(value, str) and value else None
        )
        self._attr_available = online
        if self.entity_id is not None:
            self.async_write_ha_state()


class RoomName(SensorEntity):
    """The room name published by the thermostat."""

    _attr_should_poll = False
    _attr_has_entity_name = True
    _attr_name = "Room name"

    def __init__(self, prefix: str, index: int, key: str, room_name: str, hub_id: str | None) -> None:
        self._attr_unique_id = f"{prefix}_room_{key}_name"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, f"{prefix}_room_{key}")},
            name=room_device_name(room_name, index),
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
        self._attr_native_value = value if isinstance(value, str) and value else None
        self._attr_available = online
        if self.entity_id is not None:
            self.async_write_ha_state()
