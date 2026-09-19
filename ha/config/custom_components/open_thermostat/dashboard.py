"""Optional, integration-managed Lovelace dashboard for thermostat rooms."""

import asyncio
import logging

from homeassistant.components import frontend
from homeassistant.components.lovelace.const import LOVELACE_DATA
from homeassistant.components.lovelace.dashboard import LovelaceStorage
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers import entity_registry as er
from homeassistant.util import slugify

from .const import DOMAIN

_LOGGER = logging.getLogger(__name__)


class RoomDashboard:
    """Manage a separate dashboard containing the discovered climate entities."""

    def __init__(self, hass: HomeAssistant, prefix: str, entry_id: str) -> None:
        self.hass = hass
        self.prefix = prefix
        self.path = f"open-thermostat-{slugify(prefix, separator='-')}-{entry_id[:8].lower()}"
        self._rooms: set[str] = set()
        self._store: LovelaceStorage | None = None
        self._unsubscribe = None
        self._refresh_task: asyncio.Task | None = None
        self._dirty = False

    async def async_start(self) -> None:
        """Register a Lovelace panel without changing the user's dashboards."""
        dashboards = self.hass.data[LOVELACE_DATA].dashboards
        if self.path in dashboards or frontend.async_panel_exists(self.hass, self.path):
            _LOGGER.warning("Cannot create OpenThermostat dashboard: %s is already used", self.path)
            return

        config = {
            "id": self.path,
            "url_path": self.path,
            "title": "OpenThermostat",
            "icon": "mdi:thermostat",
            "show_in_sidebar": True,
            "require_admin": False,
        }
        store = LovelaceStorage(self.hass, config)
        await store.async_save(self._dashboard_config())
        dashboards[self.path] = store
        try:
            frontend.async_register_built_in_panel(
                self.hass,
                "lovelace",
                frontend_url_path=self.path,
                sidebar_title=config["title"],
                sidebar_icon=config["icon"],
                show_in_sidebar=True,
                require_admin=False,
                config={"mode": "storage"},
            )
        except ValueError:
            dashboards.pop(self.path, None)
            _LOGGER.warning("Cannot register OpenThermostat dashboard at %s", self.path)
            return
        self._store = store
        self._unsubscribe = self.hass.bus.async_listen(
            er.EVENT_ENTITY_REGISTRY_UPDATED, self._entity_registry_updated
        )

    async def async_stop(self) -> None:
        """Remove the panel when the integration is unloaded or disabled."""
        if self._unsubscribe is not None:
            self._unsubscribe()
            self._unsubscribe = None
        if self._refresh_task is not None:
            self._refresh_task.cancel()
            try:
                await self._refresh_task
            except asyncio.CancelledError:
                pass
            self._refresh_task = None
        if self._store is not None:
            frontend.async_remove_panel(self.hass, self.path)
            self.hass.data[LOVELACE_DATA].dashboards.pop(self.path, None)
            self._store = None

    @callback
    def set_rooms(self, keys: set[str]) -> None:
        """Keep only rooms present in the latest MQTT status."""
        if keys != self._rooms:
            self._rooms = keys.copy()
            self._schedule_refresh()

    @callback
    def _entity_registry_updated(self, event) -> None:
        """Follow room entity creation and entity ID changes."""
        if event.data["entity_id"].startswith("climate."):
            self._schedule_refresh()

    @callback
    def _schedule_refresh(self) -> None:
        if self._store is None:
            return
        self._dirty = True
        if self._refresh_task is None or self._refresh_task.done():
            self._refresh_task = self.hass.async_create_task(
                self._refresh(), f"Refresh OpenThermostat dashboard {self.path}"
            )

    async def _refresh(self) -> None:
        while self._dirty:
            self._dirty = False
            await asyncio.sleep(0)
            config = self._dashboard_config()
            if config != await self._store.async_load(False):
                await self._store.async_save(config)

    @callback
    def _dashboard_config(self) -> dict:
        registry = er.async_get(self.hass)
        cards = []
        for key in sorted(self._rooms):
            unique_id = f"{self.prefix}_room_{key}_climate"
            entity_id = registry.async_get_entity_id("climate", DOMAIN, unique_id)
            if entity_id is not None:
                cards.append({
                    "type": "thermostat",
                    "entity": entity_id,
                    "show_current_as_primary": False,
                })
        return {"views": [{"title": "Pokoje", "path": "rooms", "cards": cards}]}
