"""OpenThermostat room entities and optional dashboard."""

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers import device_registry as dr

from .const import CONF_CREATE_DASHBOARD, CONF_TOPIC_PREFIX, DOMAIN, PLATFORMS
from .dashboard import RoomDashboard
from .heating import HeatingTracker


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Set up the thermostat hub and its room entities."""
    prefix = entry.data[CONF_TOPIC_PREFIX]
    state = hass.data.setdefault(DOMAIN, {})[entry.entry_id] = {"duration_minutes": {}}
    tracker = HeatingTracker(hass, entry)
    state["heating_tracker"] = tracker
    await tracker.async_start()
    dr.async_get(hass).async_get_or_create(
        config_entry_id=entry.entry_id,
        identifiers={(DOMAIN, prefix)},
        name="OpenThermostat",
        manufacturer="intuibase",
        model="OpenThermostat",
    )
    if entry.options.get(CONF_CREATE_DASHBOARD, False):
        dashboard = RoomDashboard(hass, prefix, entry.entry_id)
        await dashboard.async_start()
        state["dashboard"] = dashboard
    try:
        await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    except Exception:
        if dashboard := state.get("dashboard"):
            await dashboard.async_stop()
        hass.data[DOMAIN].pop(entry.entry_id, None)
        await tracker.async_stop()
        raise
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Unload the room climate platform and its MQTT subscriptions."""
    unloaded = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    if unloaded:
        await hass.data[DOMAIN][entry.entry_id]["heating_tracker"].async_stop()
        if dashboard := hass.data[DOMAIN][entry.entry_id].get("dashboard"):
            await dashboard.async_stop()
        hass.data[DOMAIN].pop(entry.entry_id, None)
    return unloaded
