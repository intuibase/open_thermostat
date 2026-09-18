"""Stable room identity and migration of legacy index-based HA registry entries."""

import re

from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers import device_registry as dr, entity_registry as er

from .const import DOMAIN

_ROOM_ID = re.compile(r"[0-9a-f]{8}\Z")
_SUFFIXES = {
    "climate": ("climate",),
    "sensor": ("temperature", "temperature_set", "humidity", "battery", "program", "name"),
    "binary_sensor": ("enabled", "heating"),
    "number": ("override_duration",),
}


def room_key(room: dict, index: int) -> str:
    """Use the persisted room ID; support older firmware without IDs."""
    value = room.get("id")
    return value if isinstance(value, str) and _ROOM_ID.fullmatch(value) else str(index)


@callback
def migrate_room_registry(hass: HomeAssistant, prefix: str, index: int, key: str) -> None:
    """Retain user-facing entity IDs, history and device settings after the upgrade."""
    if key == str(index):
        return
    registry = er.async_get(hass)
    old_base = f"{prefix}_room_{index}"
    new_base = f"{prefix}_room_{key}"
    for platform, suffixes in _SUFFIXES.items():
        for suffix in suffixes:
            old_unique_id = f"{old_base}_{suffix}"
            new_unique_id = f"{new_base}_{suffix}"
            entity_id = registry.async_get_entity_id(platform, DOMAIN, old_unique_id)
            if entity_id and not registry.async_get_entity_id(platform, DOMAIN, new_unique_id):
                registry.async_update_entity(entity_id, new_unique_id=new_unique_id)

    devices = dr.async_get(hass)
    device = devices.async_get_device(identifiers={(DOMAIN, old_base)})
    if device and not devices.async_get_device(identifiers={(DOMAIN, new_base)}):
        identifiers = set(device.identifiers)
        identifiers.discard((DOMAIN, old_base))
        identifiers.add((DOMAIN, new_base))
        devices.async_update_device(device.id, new_identifiers=identifiers)
