"""Patch the installed esp32_utilities copy for Arduino 3's WiFiClient alias.

The upstream header forward-declares WiFiClient as a class, but Arduino 3
defines it as a typedef of NetworkClient. Include the real header instead.
Used by the ESP32 environments in platformio.ini; no external checkout is modified.
Remove this workaround once esp32_utilities includes this fix upstream.
"""

from pathlib import Path

Import("env")

project = Path(env.subst("$PROJECT_DIR")).resolve()
header = (
    Path(env.subst("$PROJECT_LIBDEPS_DIR"))
    / env.subst("$PIOENV")
    / "esp32_utilities/src/logger/LoggerSocketSink.h"
).resolve()

if not header.is_relative_to(project):
    raise RuntimeError("Arduino 3 compatibility patch requires a project-local library copy")

content = header.read_text()
patched = content.replace("class WiFiClient;", "#include <WiFiClient.h>")
if patched != content:
    header.write_text(patched)
    print("Applied esp32_utilities WiFiClient compatibility fix for Arduino 3")
