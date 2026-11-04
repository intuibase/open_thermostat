# OpenThermostat documentation

OpenThermostat controls a shared boiler and individually configured heating
circuits. Room temperatures come from Bluetooth thermometers; configuration
and monitoring are available through the web interface and Home Assistant.
Circuits can use ESP32 GPIO or external I/O expanders. Their number depends on
the configured outputs and hardware; there is no fixed eight-circuit limit.
The separate AOS feature currently supports up to eight participating rooms.

- [User guide](USER_GUIDE.md) — rooms, programs, weekly overrides, temporary
  temperatures, boiler control and adaptive optimal start.
- [Network and services](NETWORK_AND_SERVICES.md) — Wi-Fi, device address,
  time, Bluetooth thermometers, EMS, OpenWeather, MQTT and diagnostic logging.
- [Circuits, valves and I/O expanders](CIRCUITS_AND_IO.md) — output mapping,
  room assignments, I2C configuration and manual I/O testing.
- [Adaptive optimal start](ADAPTIVE_OPTIMAL_START.md) — thermal learning,
  predictions, configuration and diagnostics.
- [REST API](RESTAPI.md) — endpoints, configuration formats and examples.
- [Hardware](HARDWARE.md) — components, wiring and photographs.
- [Project README](../README.md) — installation, Home Assistant integration
  and filesystem updates.

Screenshots use example rooms and readings. The example AOS statistics are
illustrative, not measured validation results.
