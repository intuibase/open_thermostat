#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: cleanup_open_thermostat_mqtt.sh [--execute] BROKER_HOST [MQTT_BASE]

Lists retained MQTT topics belonging to OpenThermostat. With --execute, clears
the listed retained messages. MQTT_BASE defaults to ib-therm.

Optional environment variables:
  MQTT_PORT      Broker port (default: 1883)
  MQTT_USERNAME  Broker username
  MQTT_PASSWORD  Broker password

This removes retained MQTT messages only. It does not remove Home Assistant
entity registry entries or recorder history.
EOF
}

execute=false
if [[ ${1:-} == "--execute" ]]; then
  execute=true
  shift
fi

if [[ $# -lt 1 || $# -gt 2 ]]; then
  usage >&2
  exit 2
fi

broker_host=$1
mqtt_base=${2:-ib-therm}
mqtt_port=${MQTT_PORT:-1883}

for command in mosquitto_sub mosquitto_pub python3; do
  command -v "$command" >/dev/null || {
    echo "Missing required command: $command" >&2
    exit 1
  }
done

auth_args=()
if [[ -n ${MQTT_USERNAME:-} ]]; then
  auth_args+=( -u "$MQTT_USERNAME" )
fi
if [[ -n ${MQTT_PASSWORD:-} ]]; then
  auth_args+=( -P "$MQTT_PASSWORD" )
fi

snapshot=$(mktemp)
topics=$(mktemp)
trap 'rm -f "$snapshot" "$topics"' EXIT

# A timeout is expected after all retained messages have been delivered.
mosquitto_sub -h "$broker_host" -p "$mqtt_port" "${auth_args[@]}" \
  --retained-only -W 3 -F $'%t\t%p' \
  -t 'homeassistant/#' -t "$mqtt_base/#" -t 'open_thermostat/#' \
  >"$snapshot" || status=$?
if [[ ${status:-0} -ne 0 && ${status:-0} -ne 27 ]]; then
  echo "Unable to read retained MQTT messages (mosquitto_sub exit ${status})." >&2
  exit "$status"
fi

python3 - "$mqtt_base" "$snapshot" >"$topics" <<'PY'
import json
import sys

base = sys.argv[1]
snapshot = sys.argv[2]
legacy_bases = {base, "open_thermostat"}
selected = set()

with open(snapshot, encoding="utf-8") as messages:
  for raw_line in messages:
    topic, separator, payload = raw_line.rstrip("\n").partition("\t")
    if not separator:
      continue

    if any(topic == item or topic.startswith(item + "/") for item in legacy_bases):
      selected.add(topic)
      continue

    if not topic.startswith("homeassistant/") or not topic.endswith("/config"):
      continue

    parts = topic.split("/")
    node_id = parts[2] if len(parts) >= 5 else ""
    if node_id in legacy_bases:
      selected.add(topic)
      continue

    try:
      config = json.loads(payload)
    except json.JSONDecodeError:
      continue

    unique_id = str(config.get("unique_id", config.get("uniq_id", "")))
    device = config.get("device", config.get("dev", {}))
    identifiers = device.get("identifiers", device.get("ids", [])) if isinstance(device, dict) else []
    if isinstance(identifiers, str):
      identifiers = [identifiers]

    if any(unique_id == item or unique_id.startswith(item + "_") for item in legacy_bases):
      selected.add(topic)
    elif any(str(identifier) in legacy_bases or str(identifier).startswith("open_thermostat_room_") for identifier in identifiers):
      selected.add(topic)

for topic in sorted(selected):
    print(topic)
PY

count=$(wc -l <"$topics")
if [[ $count -eq 0 ]]; then
  echo "No retained OpenThermostat MQTT messages found."
  exit 0
fi

echo "Retained OpenThermostat MQTT messages ($count):"
cat "$topics"

if [[ $execute != true ]]; then
  echo
  echo "Dry run only. Re-run with --execute before the broker host to clear them."
  exit 0
fi

while IFS= read -r topic; do
  mosquitto_pub -h "$broker_host" -p "$mqtt_port" "${auth_args[@]}" -r -n -t "$topic"
done <"$topics"

echo "Cleared $count retained MQTT messages. Restart the thermostat to publish current Discovery."
