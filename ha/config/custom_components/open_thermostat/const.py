"""Constants for the OpenThermostat room integration."""

from homeassistant.const import Platform

DOMAIN = "open_thermostat"
CONF_TOPIC_PREFIX = "topic_prefix"
CONF_CREATE_DASHBOARD = "create_dashboard"
CONF_BATTERY_ALERTS = "battery_alerts"
CONF_BATTERY_THRESHOLD = "battery_threshold"
CONF_BATTERY_REMINDER = "battery_reminder"
CONF_BATTERY_REMINDER_TIME = "battery_reminder_time"
CONF_NOTIFY_SERVICE = "notify_service"
PLATFORMS = [
    Platform.BINARY_SENSOR,
    Platform.CLIMATE,
    Platform.NUMBER,
    Platform.SELECT,
    Platform.SENSOR,
]
