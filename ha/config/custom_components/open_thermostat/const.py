"""Constants for the OpenThermostat room integration."""

from homeassistant.const import Platform

DOMAIN = "open_thermostat"
CONF_TOPIC_PREFIX = "topic_prefix"
CONF_CREATE_DASHBOARD = "create_dashboard"
PLATFORMS = [Platform.BINARY_SENSOR, Platform.CLIMATE, Platform.NUMBER, Platform.SENSOR]
