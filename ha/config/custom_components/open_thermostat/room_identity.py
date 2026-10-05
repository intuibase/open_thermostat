"""Validate permanent room IDs reported by OpenThermostat firmware."""

import re

_ROOM_ID = re.compile(r"[0-9a-f]{8}\Z")


def room_key(room: dict) -> str | None:
    """Return the room ID only for the current firmware format."""
    value = room.get("id")
    return value if isinstance(value, str) and _ROOM_ID.fullmatch(value) else None


def room_device_name(room_name: object, index: int) -> str:
    """Return a clear, language-neutral thermostat device name."""
    name = room_name if isinstance(room_name, str) and room_name else f"Room {index + 1}"
    return f"OpenThermostat — {name}"
