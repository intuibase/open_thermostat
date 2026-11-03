"""Run in HA: docker exec -i homeassistant python < test/test_ha_alerts.py."""
import json
import sys
import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock, patch

sys.path.insert(0, '/config')
from custom_components.open_thermostat import alerts, config_flow
from custom_components.open_thermostat.const import (
    CONF_TOPIC_PREFIX, CONF_NOTIFY_SERVICE, CONF_BATTERY_ALERTS,
    CONF_BATTERY_REMINDER, CONF_BATTERY_REMINDER_TIME,
)


class AlertsTest(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.hass = SimpleNamespace(
            config=SimpleNamespace(language='pl'),
            services=SimpleNamespace(async_call=AsyncMock()),
        )
        self.entry = SimpleNamespace(
            entry_id='test', data={CONF_TOPIC_PREFIX: 'therm'},
            options={CONF_NOTIFY_SERVICE: 'notify.phone'},
        )
        self.alerts = alerts.IntegrationAlerts(self.hass, self.entry)
        self.create = Mock()
        self.dismiss = Mock()
        for name, mock in [('async_create', self.create), ('async_dismiss', self.dismiss)]:
            patcher = patch.object(alerts.persistent_notification, name, mock)
            patcher.start()
            self.addCleanup(patcher.stop)

    async def status(self, value):
        await self.alerts._handle_status(SimpleNamespace(payload=value))

    async def rooms(self, level, **other):
        await self.alerts._handle_rooms(SimpleNamespace(payload=json.dumps({
            'rooms': [{'id': '12345678', 'name': 'Salon', 'batteryLevel': level, **other}],
        })))

    async def test_offline_dedup_reconnect_and_unknown_status(self):
        await self.status('off')
        await self.status('off')
        await self.status('garbage')
        self.assertEqual(self.hass.services.async_call.await_count, 1)
        self.assertIn('offline', self.create.call_args.args[1])
        await self.status('on')
        self.dismiss.assert_any_call(self.hass, 'open_thermostat_test_offline')
        await self.status('off')
        self.assertEqual(self.hass.services.async_call.await_count, 2)

    async def test_battery_crossing_reminder_recovery_invalid(self):
        await self.status('on')
        for invalid in [-1, None, True, 101, float('nan'), '10']:
            await self.rooms(invalid)
        await self.rooms(20)
        self.create.assert_not_called()
        await self.rooms(19)
        await self.rooms(18)
        self.assertEqual(self.hass.services.async_call.await_count, 1)
        await self.alerts._daily_reminder(None)
        self.assertEqual(self.hass.services.async_call.await_count, 2)
        await self.rooms(50)
        self.dismiss.assert_any_call(self.hass, 'open_thermostat_test_battery')
        await self.rooms(10)
        self.assertEqual(self.hass.services.async_call.await_count, 3)
        await self.status('off')
        before = self.hass.services.async_call.await_count
        await self.alerts._daily_reminder(None)
        self.assertEqual(self.hass.services.async_call.await_count, before)

    async def test_retained_room_data_before_status(self):
        await self.rooms(10)
        self.create.assert_not_called()
        await self.status('on')
        self.assertEqual(self.hass.services.async_call.await_count, 1)

    async def test_room_removal_and_multiple_batteries(self):
        await self.status('on')
        rooms = [
            {'id': '12345678', 'name': 'Salon', 'batteryLevel': 10},
            {'id': 'abcdef12', 'name': 'Kuchnia', 'batteryLevel': 5},
        ]
        await self.alerts._handle_rooms(SimpleNamespace(payload=json.dumps({'rooms': rooms})))
        self.assertIn('Salon: 10%', self.create.call_args.args[1])
        self.assertIn('Kuchnia: 5%', self.create.call_args.args[1])
        await self.alerts._handle_rooms(SimpleNamespace(payload=json.dumps({'rooms': rooms[1:]})))
        self.assertNotIn('Salon', self.create.call_args.args[1])
        self.assertEqual(self.hass.services.async_call.await_count, 1)
        await self.alerts._handle_rooms(SimpleNamespace(payload='{"rooms": []}'))
        self.dismiss.assert_any_call(self.hass, 'open_thermostat_test_battery')

    async def test_disabled_battery_alerts_keep_offline(self):
        self.entry.options[CONF_BATTERY_ALERTS] = False
        await self.status('on')
        await self.rooms(5)
        self.create.assert_not_called()
        await self.status('off')
        self.create.assert_called_once()

    async def test_missing_service_does_not_break_persistent_notification(self):
        self.hass.services.async_call.side_effect = alerts.HomeAssistantError('missing')
        with self.assertLogs(alerts._LOGGER, level='ERROR'):
            await self.status('off')
        self.create.assert_called_once()

    async def test_schedule_and_unload(self):
        unsubscribe = Mock()
        self.entry.options[CONF_BATTERY_REMINDER_TIME] = '13:45:00'
        with (
            patch.object(alerts.mqtt, 'async_wait_for_mqtt_client', new=AsyncMock()),
            patch.object(alerts.mqtt, 'async_subscribe', new=AsyncMock(return_value=unsubscribe)),
            patch.object(alerts, 'async_track_time_change', return_value=unsubscribe) as timer,
        ):
            await self.alerts.async_start()
            self.assertEqual(timer.call_args.kwargs, {'hour': 13, 'minute': 45, 'second': 0})
            await self.alerts.async_stop()
            self.assertEqual(unsubscribe.call_count, 3)
            self.entry.options[CONF_BATTERY_REMINDER] = False
            timer.reset_mock()
            await self.alerts.async_start()
            timer.assert_not_called()
            await self.alerts.async_stop()

    async def test_options_form_and_defaults(self):
        flow = config_flow.OpenThermostatOptionsFlow()
        flow.hass = SimpleNamespace(services=SimpleNamespace(
            async_services=lambda: {'notify': {'phone': {}, 'send_message': {}}},
        ))
        with patch.object(type(flow), 'config_entry', new_callable=lambda: property(lambda _: self.entry)):
            result = await flow.async_step_init()
            values = result['data_schema']({'create_dashboard': False})
            self.assertTrue(values[CONF_BATTERY_ALERTS])
            self.assertEqual(values[CONF_BATTERY_REMINDER_TIME], '12:00:00')
            self.assertEqual(values[CONF_NOTIFY_SERVICE], 'notify.phone')
            saved = await flow.async_step_init(values)
            self.assertEqual(saved['data'], values)


if __name__ == '__main__':
    unittest.main()
