"""Validate permanent room IDs reported by OpenThermostat firmware."""

import re

_ROOM_ID = re.compile(r"[0-9a-f]{8}\Z")


def room_key(room: dict) -> str | None:
    """Return the room ID only for the current firmware format."""
    value = room.get("id")
    return value if isinstance(value, str) and _ROOM_ID.fullmatch(value) else None
