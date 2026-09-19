"""Match the program label shown by the thermostat's room tiles."""


def room_program_name(room: dict, active_program: object) -> str | None:
    """Prefer a temporary override, then a room exception, then the main program."""
    remaining = room.get("temporaryProgramSecondsLeft")
    if isinstance(remaining, (int, float)) and not isinstance(remaining, bool) and remaining > 0:
        seconds = int(remaining)
        hours, seconds = divmod(seconds, 3600)
        minutes, seconds = divmod(seconds, 60)
        parts = []
        if hours:
            parts.append(f"{hours}h")
        if minutes:
            parts.append(f"{minutes}m")
        if seconds:
            parts.append(f"{seconds}s")
        return f"Temporary ({' '.join(parts)})"

    room_program = room.get("currentProgram")
    if isinstance(room_program, str) and room_program:
        return room_program
    if isinstance(active_program, str) and active_program:
        return active_program
    return None
