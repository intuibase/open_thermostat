# REST API

Send HTTP requests to the thermostat. These routes match `src/REST.h`.
Configuration and status payloads use JSON. Program names in URLs omit `.json`
and are case-sensitive.

## Status (GET)

| Path | Response |
|---|---|
| `/status` | Rooms, active program, boiler, EMS, weather, time and uptime. |
| `/status/rooms` | Room readings, states and `adaptiveStart` diagnostics. |
| `/status/boiler` | Boiler controller state, outputs and manual-test state. |
| `/status/ems` | Available EMS readings. |
| `/status/devices` | Detected BLE sensor addresses. |
| `/status/wifi` | Detected Wi-Fi networks. |
| `/status/programs` | Available program names. |
| `/status/version` | Version information. |
| `/params/boiler` | Parameters exposed by the boiler. |

## Rooms and programs

| Method | Path | Action |
|---|---|---|
| GET / POST | `/config/rooms` | Read / replace the complete room catalog. |
| GET / POST | `/config/programs/<name>` | Read / save a program's room settings array. |
| DELETE | `/config/programs/<name>` | Delete a program. |
| GET / POST | `/config/program/current` | Read / change the active program, e.g. `{"program":"Comfort"}`. |
| POST | `/config/temporary` | Set a temporary target for one room. |
| DELETE | `/config/rooms/<id>/thermal-model` | Reset the room's AOS model in RAM and NVS. |

Example room catalog (`POST` replaces the entire array):

```json
[
  {
    "id": "a0000000",
    "name": "Living room",
    "sensor": "02:00:00:00:00:00",
    "valves": [0],
    "adaptive_start": {
      "thermalLearningEnabled": true,
      "optimalStartEnabled": true,
      "shadowMode": true,
      "maxLeadMinutes": 240,
      "marginMinutes": 10
    }
  }
]
```

Room IDs are permanent identities. Valve references can use zero-based configured
output indices or matching configured labels. Removing a room still referenced by
a program is rejected. Up to eight rooms can enable AOS learning or prediction;
this restriction does not limit the circuit list.

Example program:

```json
[
  {
    "room_id": "a0000000",
    "enabled": true,
    "base_temp": 1900,
    "temp_margin_up": 5,
    "temp_margin_down": 15,
    "temperatures": [
      {
        "name": "Morning_comfort",
        "enabled": true,
        "time_from": "06:30",
        "time_to": "08:00",
        "temp": 2150,
        "days": [1, 2, 3, 4, 5]
      }
    ]
  }
]
```

Room targets and margins use hundredths of °C: `2150` means 21.5°C and `15` means
0.15°C. Optional `boiler_temp` in a weekly override uses whole °C. Status fields
have individual scales: EMS `currentFlowTemperature` uses tenths of °C, whereas
`adaptiveStart.outdoorTemp` and `actualSupplyTemp` use °C.

Schedule intervals include both endpoint minutes; overlapping entries select
the highest room target. See [weekly overrides](USER_GUIDE.md#weekly-temperature-overrides)
for weekday and overnight behavior.

A temporary 21.5°C room target for two hours:

```http
POST /config/temporary
Content-Type: application/json

{"roomName":"Living room","temperature":2150,"validSeconds":7200}
```

## Device configuration

| Method | Path | Configuration |
|---|---|---|
| GET / POST | `/config/wifi` | SSID and password; GET does not return the password. |
| GET / POST | `/config/device` | Hostname, HTTP port, services, NTP, Bluetooth, MQTT, weather and log transport. |
| GET / POST | `/config/boiler` | Boiler mode, supply limits, outdoor source and heating curve. |
| GET / POST | `/config/hardware` | I2C/EMS pins, expanders and relay outputs. |
| GET / POST | `/config/debug` | Registered log-category flags; applied immediately. |

Wi-Fi, device and hardware settings are stored for restart. Room and program saves
reload the active configuration immediately. Configuration POSTs write complete
payloads rather than partial patches; preserve other required fields.

An example output mapping fragment inside `/config/hardware`:

```json
{
  "i2c": [8, 9],
  "ems": [17, 18],
  "ems_forwarder": [13, 5],
  "gpio_extenders": [{"type": "pcf8575", "address": 32, "label": "Floor1"}],
  "boiler_pin": null,
  "valve_pins": [
    {"source": "ext:Floor1", "pin": 0, "label": "Living radiator"},
    {"source": "ext:Floor1", "pin": 1, "label": "Bedroom"}
  ]
}
```

I2C addresses in JSON are decimal (`32` = `0x20`); the UI displays hexadecimal.
Use `builtin` for an ESP32 pin or `ext:<label>` for an expander's local pin.
See [circuits and I/O](CIRCUITS_AND_IO.md) for supported hardware and driver limits.

## Output testing and updates

`GET /hardware/i2c/scan` lists responding addresses with guessed device types.
`POST /hardware/test/gpio` starts a direct output override, e.g.:

```json
{"duration":30,"boiler":false,"valves":[true,false]}
```

`duration` is seconds; valve booleans represent open states. Send
`DELETE /hardware/test/gpio` to stop the test.

`GET /config/reboot` restarts the device. `POST /ota` and `POST /otafs` upload
firmware and filesystem images; the multipart format is implemented by
`data/html/update.html`. `/recovery` provides the recovery page.

`POST /ota` requires an application OTA partition: it works with the current
dual-slot `esp32s3` layout, but not the factory-only `esp32dev` layout, which
requires USB firmware updates. `POST /otafs` is available for both layouts.
Filesystem images replace stored configuration; follow the
[preservation procedure](../README.md#preserve-configuration-before-a-filesystem-update)
before uploading one.

See [AOS configuration and diagnostics](ADAPTIVE_OPTIMAL_START.md) and
[network settings](NETWORK_AND_SERVICES.md) for field explanations.
