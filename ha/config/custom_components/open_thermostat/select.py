"""Local presentation settings for the OpenThermostat dashboard."""

from homeassistant.components.select import SelectEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback
from homeassistant.helpers.restore_state import RestoreEntity

from .const import CONF_TOPIC_PREFIX, DOMAIN

CHART_PERIODS = ("day", "week", "month")


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    """Create the energy chart period selector."""
    async_add_entities([EnergyChartPeriod(entry.data[CONF_TOPIC_PREFIX])])


class EnergyChartPeriod(SelectEntity, RestoreEntity):
    """Choose aggregation and visible range for dashboard energy charts."""

    _attr_should_poll = False
    _attr_has_entity_name = True
    _attr_translation_key = "energy_chart_period"
    _attr_icon = "mdi:chart-bar"
    _attr_options = list(CHART_PERIODS)

    def __init__(self, prefix: str) -> None:
        self._attr_unique_id = f"{prefix}_hub_energy_chart_period"
        self._attr_current_option = "day"
        self._attr_device_info = DeviceInfo(identifiers={(DOMAIN, prefix)})

    async def async_added_to_hass(self) -> None:
        """Restore the last selected chart period."""
        await super().async_added_to_hass()
        previous = await self.async_get_last_state()
        if previous is not None and previous.state in CHART_PERIODS:
            self._attr_current_option = previous.state

    async def async_select_option(self, option: str) -> None:
        """Select a chart aggregation period."""
        if option in CHART_PERIODS:
            self._attr_current_option = option
            self.async_write_ha_state()
