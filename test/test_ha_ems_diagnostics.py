"""Run inside the local HA container: docker exec -i homeassistant python < this_file."""
import asyncio
import json
import sys
import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock, patch

sys.path.insert(0, "/config")
from custom_components.open_thermostat import sensor, dashboard
from custom_components.open_thermostat.const import DOMAIN, CONF_TOPIC_PREFIX
from homeassistant.components.sensor import SensorDeviceClass, SensorStateClass
from homeassistant.const import EntityCategory, UnitOfTime


class EmsDiagnosticsTest(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.callbacks = {}
        self.entities = []
        self.hass = SimpleNamespace(
            data={DOMAIN: {"test_entry": {"heating_tracker": SimpleNamespace(
                rooms={}, async_add_listener=lambda _: lambda: None,
            )}}},
            config=SimpleNamespace(language="pl"),
        )
        self.entry = SimpleNamespace(
            data={CONF_TOPIC_PREFIX: "test_ems"}, entry_id="test_entry",
            async_on_unload=lambda _: None,
        )
        async def subscribe(_hass, topic, callback):
            self.callbacks[topic] = callback
            return lambda: None
        with (
            patch.object(sensor.dr, "async_get", return_value=SimpleNamespace(
                async_get_device_by_identifier=lambda *_: None,
            )),
            patch.object(sensor.mqtt, "async_wait_for_mqtt_client", new=AsyncMock()),
            patch.object(sensor.mqtt, "async_subscribe", side_effect=subscribe),
        ):
            await sensor.async_setup_entry(self.hass, self.entry, self.entities.extend)

    def send(self, topic, payload):
        self.callbacks[f"test_ems/{topic}"](SimpleNamespace(
            payload=payload, topic=f"test_ems/{topic}",
        ))

    async def test_mqtt_values_metadata_offline_and_reboot(self):
        self.send("status", "on")
        payload = {
            "enabled": True, "poll": 100, "fifo_ovf": 3,
            "poll_start_max_us": 42, "poll_service_max_us": 18000,
            "rx_capacity": 59, "tx_capacity": 59, "frame_capacity": 33,
            "window_duration_ms": 30000, "window_end_uptime_ms": 60000,
        }
        self.send("ems_diagnostics", json.dumps(payload))
        entities = {entity.unique_id: entity for entity in self.entities}
        self.assertEqual(len(entities), len(sensor.EMS_DIAGNOSTIC_METRICS))
        poll = entities["test_ems_hub_ems_poll"]
        timing = entities["test_ems_hub_ems_poll_start_max_us"]
        self.assertEqual(poll.native_value, 100)
        self.assertEqual(poll.state_class, SensorStateClass.TOTAL_INCREASING)
        self.assertEqual(timing.native_value, 42)
        self.assertEqual(timing.device_class, SensorDeviceClass.DURATION)
        self.assertEqual(timing.native_unit_of_measurement, UnitOfTime.MICROSECONDS)
        self.assertEqual(timing.extra_state_attributes["rx_capacity"], 59)
        self.assertTrue(all(e.entity_category == EntityCategory.DIAGNOSTIC for e in self.entities))
        self.assertTrue(poll.available)
        self.send("status", "off")
        self.assertFalse(poll.available)
        self.send("status", "on")
        payload.update(poll=1, poll_start_max_us=None, window_duration_ms=0)
        self.send("ems_diagnostics", json.dumps(payload))
        self.assertEqual(poll.native_value, 1)
        self.assertIsNone(timing.native_value)
        self.assertEqual(len(self.entities), len(entities))
        self.send("ems_diagnostics", "invalid JSON")
        self.send("ems_diagnostics", "[]")
        self.assertEqual(poll.native_value, 1)

    async def test_separate_diagnostics_card_in_both_languages(self):
        entries = [SimpleNamespace(
            unique_id=f"test_ems_hub_{metric}", entity_id=f"sensor.{metric}",
        ) for metric in sensor.EMS_DIAGNOSTIC_METRICS]
        entries.append(SimpleNamespace(
            unique_id="test_ems_hub_boiler_pressure", entity_id="sensor.boiler_pressure",
        ))
        registry = SimpleNamespace(async_get_entity_id=lambda *_: None)
        for language, expected_title in (("pl", "Diagnostyka EMS"), ("en", "EMS diagnostics")):
            self.hass.config.language = language
            with (
                patch.object(dashboard.er, "async_get", return_value=registry),
                patch.object(dashboard.dr, "async_get", return_value=Mock()),
                patch.object(dashboard.er, "async_entries_for_config_entry", return_value=entries),
            ):
                config = dashboard.RoomDashboard(self.hass, "test_ems", "test_entry")._dashboard_config()
            view = next(v for v in config["views"] if v["path"] == "diagnostics")
            card = next(c for c in view["cards"] if c.get("title") == expected_title)
            self.assertEqual(set(card["entities"]), {f"sensor.{m}" for m in sensor.EMS_DIAGNOSTIC_METRICS})
            info = next(c for c in view["cards"] if c.get("title") in {"Informacje EMS", "EMS information"})
            self.assertEqual(info["entities"], ["sensor.boiler_pressure"])


if __name__ == "__main__":
    unittest.main()
