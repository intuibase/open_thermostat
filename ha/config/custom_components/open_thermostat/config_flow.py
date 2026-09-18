"""Configure OpenThermostat rooms from MQTT discovery or a topic prefix."""

import json

import voluptuous as vol

from homeassistant import config_entries
from homeassistant.core import callback
from homeassistant.data_entry_flow import FlowResult
from homeassistant.helpers.service_info.mqtt import MqttServiceInfo

from .const import CONF_CREATE_DASHBOARD, CONF_TOPIC_PREFIX, DOMAIN


def valid_prefix(value: str) -> str:
    """Keep a prefix to one MQTT topic level."""
    if not value or "/" in value or "+" in value or "#" in value:
        raise vol.Invalid("Enter a single MQTT topic level")
    return value


class OpenThermostatConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    """Add one integration entry per thermostat MQTT prefix."""

    VERSION = 1

    @staticmethod
    @callback
    def async_get_options_flow(config_entry: config_entries.ConfigEntry) -> config_entries.OptionsFlow:
        """Allow dashboard creation to be changed after setup."""
        return OpenThermostatOptionsFlow()

    async def async_step_mqtt(self, discovery_info: MqttServiceInfo) -> FlowResult:
        """Offer setup after a room status message is received."""
        try:
            payload = json.loads(discovery_info.payload)
        except (TypeError, ValueError):
            return self.async_abort(reason="invalid_status")
        if not isinstance(payload, dict) or not isinstance(payload.get("rooms"), list):
            return self.async_abort(reason="invalid_status")

        prefix = discovery_info.topic.removesuffix("/room_data")
        try:
            self._prefix = valid_prefix(prefix)
        except vol.Invalid:
            return self.async_abort(reason="invalid_topic")
        await self.async_set_unique_id(self._prefix)
        self._abort_if_unique_id_configured()
        return await self.async_step_confirm()

    async def async_step_confirm(self, user_input: dict | None = None) -> FlowResult:
        """Confirm MQTT discovery."""
        if user_input is not None:
            return self.async_create_entry(
                title=f"OpenThermostat ({self._prefix})",
                data={CONF_TOPIC_PREFIX: self._prefix},
                options={CONF_CREATE_DASHBOARD: user_input.get(CONF_CREATE_DASHBOARD, False)},
            )
        return self.async_show_form(
            step_id="confirm",
            description_placeholders={"prefix": self._prefix},
            data_schema=vol.Schema({vol.Optional(CONF_CREATE_DASHBOARD, default=False): bool}),
        )

    async def async_step_user(self, user_input: dict | None = None) -> FlowResult:
        """Allow manual setup when MQTT discovery has not appeared yet."""
        errors = {}
        if user_input is not None:
            prefix = user_input[CONF_TOPIC_PREFIX]
            try:
                valid_prefix(prefix)
            except vol.Invalid:
                errors["base"] = "invalid_topic"
            else:
                await self.async_set_unique_id(prefix)
                self._abort_if_unique_id_configured()
                return self.async_create_entry(
                    title=f"OpenThermostat ({prefix})",
                    data={CONF_TOPIC_PREFIX: prefix},
                    options={CONF_CREATE_DASHBOARD: user_input.get(CONF_CREATE_DASHBOARD, False)},
                )
        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema(
                {
                    vol.Required(CONF_TOPIC_PREFIX, default="ib-therm"): str,
                    vol.Optional(CONF_CREATE_DASHBOARD, default=False): bool,
                }
            ),
            errors=errors,
        )


class OpenThermostatOptionsFlow(config_entries.OptionsFlowWithReload):
    """Configure the optional room dashboard."""

    async def async_step_init(self, user_input: dict | None = None) -> FlowResult:
        """Show the dashboard switch."""
        if user_input is not None:
            return self.async_create_entry(data=user_input)
        return self.async_show_form(
            step_id="init",
            data_schema=vol.Schema({
                vol.Optional(
                    CONF_CREATE_DASHBOARD,
                    default=self.config_entry.options.get(CONF_CREATE_DASHBOARD, False),
                ): bool,
            }),
        )
