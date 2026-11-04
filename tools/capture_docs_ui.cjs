// Render the repository UI against deterministic, read-only demonstration data.
// Usage: node tools/capture_docs_ui.cjs (requires playwright or PLAYWRIGHT_MODULE).
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const root = path.resolve(__dirname, '..');
const output = path.join(root, 'docs/img/ui');
const adaptive = { thermalLearningEnabled: true, optimalStartEnabled: true,
  shadowMode: true, maxLeadMinutes: 240, marginMinutes: 10 };
const rooms = ['Living room', 'Bedroom', 'Office', 'Kitchen', 'Bathroom', 'Kids room', 'Hallway', 'Guest room'].map((name, i) => ({
  id: `a000000${i}`, name, sensor: `02:00:00:00:00:0${i}`, valves: [i], adaptive_start: adaptive
}));
const program = rooms.map(room => ({ room_id: room.id, enabled: true,
  base_temp: 1900, temp_margin_up: 5, temp_margin_down: 15, temperatures: [
    { name: 'Morning_comfort', enabled: true, temp: 2150, time_from: '06:30',
      time_to: '08:00', days: [1, 2, 3, 4, 5] },
    { name: 'Evening', enabled: true, temp: 2100, time_from: '16:00',
      time_to: '22:00', days: [0, 1, 2, 3, 4, 5, 6] }
  ] }));
const statusRooms = rooms.map((room, i) => ({ ...room, enabled: true,
  meanTemp: [2080, 2010, 1940, 2090, 2160, 2050, 1980, 2110][i],
  tempSet: i === 2 ? 2200 : 2100, currentHumidity: 4500,
  batteryLevel: 86 - i * 7, shouldContinueHeating: i !== 4 && i !== 7,
  currentProgram: 'Evening', temporaryProgramSecondsLeft: i === 2 ? 5400 : 0,
  adaptiveStart: { status: 'WAITING', modelStatus: 'Calibrated',
    targetTime: 1793770200, targetTemp: 2150, startTime: 1793764200,
    predictedMinutes: 90, delayMinutes: 10, marginMinutes: 10,
    fallback: false, limited: false, unreachable: false,
    heatingWindows: 18, coolingWindows: 8, warmupWindows: 4, warmupRate: 0.45,
    confidence: 100, predictions: 4, meanErrorMinutes: -8, maeMinutes: 12,
    outdoorTemp: 5, actualSupplyTemp: 46 }
}));
const fixtures = {
  '/status': { rooms: statusRooms, activeProgram: 'Comfort', time: '2026-11-03 18:00:00',
    uptime: 180000, boiler: { boiler: true, outdoorTemperature: 500 }, openweather: {},
    ems: { heatingActive: true, warmWaterActive: false, currentFlowTemperature: 460,
      selectedFlowTemperature: 48, pressure: 15, pumpEnabled: true, fanEnabled: true,
      burningGas: true, currentBurnerPower: 28, heatingEnabled: true, warmWaterEnabled: true } },
  '/status/rooms': statusRooms, '/config/rooms': rooms,
  '/status/devices': rooms.map(room => room.sensor),
  '/config/hardware': { i2c: [8, 9], ems: [17, 18], ems_forwarder: [13, 5],
    gpio_extenders: [{ type: 'pcf8575', address: 32, label: 'IOExtender' }],
    boiler_pin: null, valve_pins: rooms.map((room, i) => ({ source: 'ext:IOExtender', pin: i, label: room.name })) },
  '/status/boiler': { manualTest: { active: false } },
  '/config/wifi': { ssid: 'Home-demo' },
  '/status/wifi': [{ ssid: 'Home-demo' }, { ssid: 'Guest-demo' }],
  '/config/device': { hostname: 'heating-demo', listenPort: 80, rtcEnabled: true,
    emsEnabled: true, emsForwarderEnabled: false, ntpEnabled: true, ntpHost: 'pool.ntp.org',
    ntpUtcOffset: 3600, ntpDaylightUtcOffset: 3600,
    timeZone: 'CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00',
    bt: { scanTime: 60, scanInterval: 60 },
    loggerEnabled: true, loggerHost: '192.0.2.20', loggerPort: 8701, loggerTimeoutMs: 10000,
    mqtt: { enabled: true, publishHomeAssistantDiscovery: false, brokerAddress: '192.0.2.10',
      brokerPort: 1883, username: 'thermostat-demo', password: '', clientId: 'heating-demo',
      base: 'heating-demo', keepAlive: 60, interval: 30 },
    openweather: { enabled: true, appid: 'DEMO_API_KEY', latitude: '52.23', longitude: '21.01', interval: 120 } },
  '/config/debug': { Room: false, HeatCtrl: false, TempReader: false, REST: false,
    OpenWeather: false, Boiler: true, EmsControl: true, EmsVerbose: false,
    MQTT: false, App: true, Config: true, RTC: true, AOS: true },
  '/status/programs': ['default', 'Comfort', 'Away'],
  '/config/program/current': { program: 'Comfort' },
  '/config/boiler': JSON.parse(fs.readFileSync(path.join(root, 'data/cfg/cfgboiler.json'))),
  '/params/boiler': { maximumHeatingTemperature: 75 }
};
const server = http.createServer((req, res) => {
  const url = new URL(req.url, 'http://localhost');
  if (req.method !== 'GET') { res.writeHead(405); return res.end('Documentation preview is read-only'); }
  const fixture = fixtures[url.pathname] ?? (url.pathname.startsWith('/config/programs/') ? program : undefined);
  if (fixture !== undefined) { res.setHeader('Content-Type', 'application/json'); return res.end(JSON.stringify(fixture)); }
  const file = path.resolve(root, 'data/html', '.' + (url.pathname === '/' ? '/index.html' : url.pathname));
  if (!file.startsWith(path.join(root, 'data/html') + path.sep) || !fs.existsSync(file)) {
    res.writeHead(404); return res.end();
  }
  res.setHeader('Content-Type', file.includes('.js') ? 'application/javascript' : file.includes('.css') ? 'text/css' : 'text/html');
  if (file.endsWith('.gz')) res.setHeader('Content-Encoding', 'gzip');
  res.end(fs.readFileSync(file));
});
(async () => {
  let browser;
  try {
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    browser = await chromium.launch({ headless: true });
    const page = await browser.newPage({ viewport: { width: 1440, height: 1000 }, deviceScaleFactor: 2,
      locale: 'en-GB', timezoneId: 'Europe/Warsaw', reducedMotion: 'reduce' });
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    page.on('dialog', dialog => { errors.push(dialog.message()); dialog.dismiss(); });
    await page.route('**/*', route => new URL(route.request().url()).hostname === '127.0.0.1'
      ? route.continue() : route.abort());
    fs.mkdirSync(output, { recursive: true });
    const shot = async (name, locator) => {
      const options = { path: path.join(output, name + '.png'), animations: 'disabled' };
      if (locator) await locator.screenshot(options);
      else {
        // Include navigation and content without padding short screens to viewport height.
        const bottom = await page.evaluate(() => Math.max(
          document.querySelector('#maincontainer').getBoundingClientRect().bottom + window.scrollY,
          ...Array.from(document.querySelectorAll('.modal.show'), modal => modal.getBoundingClientRect().bottom + window.scrollY)
        ));
        if (bottom + 24 > page.viewportSize().height) {
          await page.screenshot({ ...options, fullPage: true });
        } else {
          await page.screenshot({ ...options, clip: { x: 0, y: 0, width: 1440, height: Math.ceil(bottom + 24) } });
        }
      }
      console.log(name + '.png');
    };
    const navigate = async name => {
      await page.locator('#navbarDropdownMenuLink').click();
      await page.locator(`[data-page="${name}"]`).click();
    };
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    await page.getByText('Temporary (1h 30m', { exact: false }).waitFor();
    await shot('home');
    await page.getByText('Living room', { exact: true }).click();
    await page.locator('#temporaryTempModal.show').waitFor();
    await shot('temporary-temperature');
    await page.locator('#temporaryTempModal').getByRole('button', { name: 'Cancel' }).click();
    await page.locator('#temporaryTempModal').waitFor({ state: 'hidden' });
    await navigate('room_management');
    await page.getByText('Status: WAITING', { exact: false }).waitFor();
    await shot('rooms-aos');
    await page.locator('#roomProgramsTab').click();
    await page.locator('#tempOverridesTemp').waitFor({ state: 'visible' });
    await shot('program-overrides');
    await navigate('program');
    await page.locator('#programselectform').waitFor({ state: 'visible' });
    await shot('active-program');
    await navigate('curve');
    await page.locator('#boilereditform').waitFor({ state: 'visible' });
    await shot('boiler');
    await page.locator('#pills-curve-tab').click();
    await page.locator('#minHCTemp').waitFor({ state: 'visible' });
    await page.waitForFunction(() => heatingCurveChart.width > 500);
    // Allow Bootstrap's tab transition, Chart.js resize and canvas animation to finish.
    await page.waitForTimeout(1500);
    await shot('heating-curve');
    await navigate('network');
    await page.waitForFunction(() => document.getElementById('DeviceHostname')?.value === 'heating-demo'
      && document.querySelector('#DebugOptionsCard input'));
    await shot('network-wifi', page.locator('#WiFiSettings'));
    await shot('network-web', page.locator('#WebInterfaceCard').locator('..'));
    await shot('network-hardware-services', page.locator('#RTCCard').locator('..'));
    await shot('network-time', page.locator('#NTPCard').locator('..'));
    await shot('network-bluetooth', page.locator('#BTCard').locator('..'));
    await shot('network-weather', page.locator('#OpenWeatherMapCard').locator('..'));
    await shot('network-mqtt', page.locator('#MQTTCard').locator('..'));
    await shot('network-logging', page.locator('#DebugCard').locator('..'));
    await shot('network-debug-options', page.locator('#DebugSettings'));
    await navigate('hardware');
    await page.locator('#iotestValve2').waitFor({ state: 'attached' });
    await shot('hardware');
    await page.locator('#pills-iotest-tab').click();
    await page.locator('#iotestStartBtn').waitFor({ state: 'visible' });
    await shot('hardware-io-test');
    await navigate('update');
    await page.locator('#updateform').waitFor({ state: 'visible' });
    await shot('firmware-update');
    if (errors.length) throw new Error(errors.join('\n'));
  } finally {
    if (browser) await browser.close();
    server.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
