# OpenThermostat for ESP32

An open thermostat project enabling control of a central heating boiler and up to 8 separate heating circuits.

![Main screen](docs/img/screen.jpg)

OpenThermostat can control the boiler using an ON/OFF relay or directly via the EMS2 protocol (e.g., via the EMS interface board from [EMS Interface Board](https://bbqkees-electronics.nl/product/ems-interface-board-v3-1/)).

## Features:

- Remote temperature reading of rooms via Bluetooth from Xiaomi thermometers (displaying temperature, battery status, and humidity)
- Configuration and access through WIFI - web interface
- Support for battery-powered real-time clock
- NTP time synchronization
- Sending status and temperature via MQTT
- Integration with Home Assistant
- Can work with any boiler as a simple ON/OFF thermostat
- Compatible with EMS2 compliant boilers
- Ability to read external temperature from EMS2 boilers
- Ability to read external temperature from the internet using OpenWeather data (weather display on web interface)
- Displaying the status of EMS2 boilers, such as operating mode, temperatures, system pressure, etc.
- Ability to freely configure the heating curve for EMS2 compliant boilers
- Ability to configure any number of programs
- Ability to set room temperatures with an accuracy of 0.1°C
- Ability to override the basic configuration for a circuit (room) at any time for any selected days of the week
- Ability to forward EMS2 packets (allows connection of a second EMS32 board with EMS-ESP32)
- REST API

## Home Assistant climate panels

The MQTT connection already provides room sensors. To also get one `climate` entity per room, copy `ha/config/custom_components/open_thermostat` to the `custom_components` directory in your Home Assistant configuration and restart Home Assistant. With the MQTT integration enabled, Home Assistant should discover **OpenThermostat Rooms** from the existing `<MQTT base>/room_data` messages. Confirm the discovered integration once; it will then add climate entities as rooms appear. You can also add the integration manually and enter the MQTT base configured in the thermostat (`ib-therm` in this project's configuration).

The **OpenThermostat Rooms** integration contains a main OpenThermostat device with memory, uptime, boiler energy, water usage, and outdoor temperature sensors, plus one device per room. Each room device contains a climate entity and sensors for current temperature, temperature set by the program, humidity, battery, program name, heating enabled, and heating activity when the thermostat reports those values. Temperature set, battery level, and program name are also available as attributes of the climate entity. The existing OpenThermostat MQTT device page continues to show its sensors, so some readings appear in both integrations. Room entities use the permanent ID from `rooms.json`; messages without room IDs are ignored.

The integration includes light and dark brand images in `brand/`. Home Assistant 2026.3 or newer loads them directly from the custom integration directory, so copy the complete `open_thermostat` directory when updating it.

During setup, you can choose to create a separate **OpenThermostat** dashboard in the HA sidebar. For an integration already installed, open **Settings → Devices & services → OpenThermostat Rooms → Configure** and enable **Create an OpenThermostat room dashboard**. The integration fills this dashboard with a standard thermostat card for each room reported over MQTT, updates the cards when rooms or entity IDs change, and removes the sidebar panel when the option is disabled. It manages the contents of this dashboard, so edits to its cards can be replaced by the next room update. Other HA dashboards are untouched.

Changing the target temperature in an HA climate panel creates a temporary override on the thermostat. Each room has a **Temperature override duration** number entity (10–720 minutes, default 120) that controls the next change. HA sends a non-retained JSON command to `<MQTT base>/command/temporary` with `requestId`, `roomName`, `temperature` in hundredths of a degree Celsius, and `validSeconds`. New firmware validates the command, applies the override, replies on `<MQTT base>/response/temporary`, and immediately publishes updated `room_data`. HA reports an error if the thermostat rejects the command or does not reply within 10 seconds. The thermostat's program configuration remains unchanged. Upload the new firmware before using this control from HA.

The climate `hvac_action` uses the same condition as the thermostat web UI's flame icon: `shouldContinueHeating` for that room and `boilerStarted` for the boiler. New firmware publishes `boilerStarted` in `room_data`; with older firmware the climate entity falls back to `isBeingHeated`. The separate **Heating** binary sensor keeps the thermostat's original `isBeingHeatedState` value, based on the boiler and the room valve. The optional dashboard shows the current temperature below the target temperature on each thermostat card. For a manually created dashboard, use this card configuration (the climate details popup uses a different layout):

```yaml
type: thermostat
entity: climate.biuro
show_current_as_primary: false
```

The `room_data` MQTT message includes each room's `currentProgram` and `temporaryProgramSecondsLeft`. New firmware also publishes `activeProgram` at the top level. The integration displays a temporary override first, then a room-specific program, then the active main program. Upload the new firmware to make the main program available for rooms without a room-specific program.

For low battery alerts, copy `ha/config/custom_components/open_thermostat/blueprints/low_battery.yaml` to `<HA config>/blueprints/automation/open_thermostat/low_battery.yaml`. In **Settings → Automations & scenes → Blueprints**, create an automation from **OpenThermostat - low room battery**. Select the room battery sensors, set the percentage threshold, and choose the notification action. The default action creates a persistent HA notification; it can be replaced with a mobile notification action. The blueprint ignores `-1` (no battery reading) and alerts when a valid level first falls below the threshold or returns low after an unavailable reading. Home Assistant does not load blueprints directly from `custom_components`.

## Room and program configuration

Rooms and heating programs are stored separately. `data/cfg/rooms.json` contains each room's permanent eight-character ID, name, temperature sensor and valves. Files in `data/programs` refer to rooms by `room_id` and contain only heating settings. Programs do not share room settings with one another.

The web interface provides separate **Rooms** and **Programs** tabs and separate save buttons. Creating a room generates its permanent ID. Renaming or reordering the room does not change that ID. A program can only add settings for a room that already exists in the room catalog.

This firmware only supports the separated format. Build and upload the filesystem from the versioned `data` directory together with the firmware. For PlatformIO, use `pio run -e <environment> -t buildfs` followed by the matching filesystem upload command. Uploading the firmware without the new filesystem leaves heating disabled because `/cfg/rooms.json` is required.

The firmware publishes room IDs in `room_data`, and the custom Home Assistant integration uses them as stable device and entity identities.

## Disclaimer

This project is provided "as is" without any guarantees or warranty. In association with the product, the developer makes no warranties of any kind, either express or implied, including but not limited to warranties of merchantability, fitness for a particular purpose, of title, or of noninfringement of third party rights. Use of the product by a user is at the user’s risk. In no event shall the developer be liable for any damages, including but not limited to direct, indirect, special, incidental, or consequential damages, losses, or expenses arising in connection with the use of this project.

## ⚠️ Safety Note

**WARNING: This device operates at high voltage (110V / 230V)**

This thermostat is an project that operates at high voltages, including 110V or 230V. The system includes components such as a power supply, relays, and valve actuators, which can pose a risk to health and life if improperly installed or used.

**WARNING: Installation and operation of this device should only be performed by individuals with the appropriate qualifications and certifications, in accordance with the regulations applicable in their country.**

The creators of this project disclaim all responsibility for any damages, injuries, material losses, or other consequences resulting from improper use, installation, or maintenance of the device. The user assumes full responsibility for complying with local regulations regarding the operation of electrical devices. This includes, but is not limited to, any potential damage to heating systems, such as boilers, resulting from incorrect use or installation.

**Recommendations:**
1. Ensure that the power supply is disconnected before starting the installation.
2. Always follow local electrical codes and the manufacturer's recommendations.
3. Consult a qualified electrician if you have any doubts regarding the installation or operation of the device.

Failure to follow these recommendations may result in serious injury or death. Installation and use of the device are at the user’s own risk.
Depending on the method chosen for implementing the hardware part, the project may require working with high voltages, such as 230V. This requires proper qualifications and adherence to regulations in the respective country.
