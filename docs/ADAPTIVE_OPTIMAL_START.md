# Adaptive optimal start

AOS can apply the next scheduled **room** temperature early. It leaves the saved
schedule, heating curve and boiler supply overrides unchanged. The default is
disabled. Each physical room has independent learning, scheduling and shadow
settings; these settings apply across programs. Up to eight rooms can use AOS.

In **Rooms and programs → Room configuration**, first enable thermal learning.
Enable optimal start predictions with shadow mode checked to inspect proposed
starts without changing heating. Disable shadow mode when ready to activate
early heating. Learning can remain enabled with scheduling disabled, or be
switched off to freeze an existing model.

## Configuration and API

Optional `adaptive_start` object in each `/cfg/rooms.json` room:

```json
{
  "thermalLearningEnabled": true,
  "optimalStartEnabled": true,
  "shadowMode": true,
  "maxLeadMinutes": 240,
  "marginMinutes": 10
}
```

Defaults when absent: learning and optimal start off, shadow on, 240-minute
maximum advance, 10-minute safety margin. Maximum advance accepts whole minutes
60–720; margin accepts 0–60. Invalid types/ranges are rejected by
`POST /config/rooms`. Configuration takes effect immediately on save.

`GET /status/rooms` and MQTT `<base>/room_data` include each room's
`adaptiveStart` diagnostics. Home Assistant MQTT discovery adds status, predicted
duration and mean absolute arrival error sensors. Settings are changed through
WWW/REST; this version does not add HA configuration switches.

`DELETE /config/rooms/<room-id>/thermal-model` clears the model in RAM and NVS.
The WWW reset button uses the same endpoint. Models are keyed by stable room ID
and bound to the temperature sensor address. Renaming a room preserves its
model; changing its sensor starts with a fresh model. Program/config reloads
preserve measurements, learned parameters and active temporary overrides, while
recalculating the AOS decision. A firmware restart restores parameters and
recalculates from current time; it does not restore raw history or an old
preheating latch.

## Model and measurements

The hardware-independent implementation is in `ThermalModel.h` and
`OptimalStart.h`; `Room` integrates it with the existing heating decision.

Each opted-in learning room allocates a 144-sample ring buffer, sampled no more
frequently than every five minutes. Eight histories consume about 27 KiB on the
tested ABI. Samples carry monotonic time, room/outdoor/actual supply temperature,
heat delivery and validity. Missing optional temperatures are represented
internally by NaN and omitted from diagnostics JSON.

The first-order model uses hourly units:

```text
emission = max(0, supply - room - 5)^1.3 / 50^1.3
room rate = gain * emission - loss * (room - outdoor)
```

The 5°C deduction approximates mean radiator temperature relative to supply;
it is an effective hydraulic assumption, not a measured return temperature.
Radiator nominal power is not required. Gain and loss are bounded effective
coefficients updated gradually (15% of each accepted estimate). Cooling windows
identify losses, heating windows identify gain after allowing for response delay.
Quantized temperatures are compared over non-overlapping windows of at least
30 minutes, rather than differentiated five minutes apart. Delay is smoothed
from heating onset to the first measured rise of at least 0.1°C, within 60 minutes.

Windows with missing/invalid samples, telemetry gaps, changing heat states (including changes between stored samples),
sudden changes exceeding 0.6°C per sample and implausible rates are excluded.
Valid flat or weakly cooling heating windows now update the effective heating
model too: delivered heat may only offset losses. For EMS, training requires fresh pump/heating activity and
actual supply data while the boiler is requested. Valve position alone does not
prove delivery. ON/OFF operation without telemetry learns a separate effective
warming rate from commanded heating; actual heat delivery cannot be confirmed
in that mode.

AOS reads actual supply for training. Predictions use the ordinary heating curve
at the current outdoor temperature, since idle boiler water can be cold. No
future program boiler override is promoted into preheating. With an untrained
physical model or missing outdoor/supply prediction, it uses the effective
ON/OFF rate (initially 1°C/hour) plus response delay (initially 10 minutes).

The model also observes **ordinary scheduled temperature increases**, including
the boiler's on/off pauses. A completed warmup must last 30 minutes to 12 hours,
begin at least 0.2°C below the new target, reach within 0.05°C, and have confirmed
heat at both ends of intervals covering at least 20% of its duration. Confirmed
heat requires trustworthy delivery and a measured supply more than 5°C above
room temperature. Ordinary heat-off pauses remain part of the cycle. A schedule
with a boiler supply override is excluded from this calibration, so boosted
heating cannot train a rate referenced to the ordinary curve. Missing room
readings, manual tests, overrides and active preheating cancel the observation
on every controller tick, including between learning samples. Gaps over six minutes,
temperature jumps, clock/config changes and intervening setpoint changes also
cancel the observation. These measurements do not require active AOS. Normal
heat transitions do not invalidate AOS arrival-error metrics.

After three completed warmups, `modelStatus` becomes `Calibrated`. The mean
observed rate (excluding the estimated initial response delay) anchors forecasts.
The running mean caps its weight denominator at 12 cycles. If all calibration
cycles have outdoor/curve measurements, gain/loss adjust this anchor for the
difference in radiator emission and room-to-outdoor delta from those observed
cycles. The references use the ordinary curve, matching forecast inputs, rather
than assuming that future boiler overrides will be applied early. With missing
reference inputs, the measured rate still works as an empirical prediction.
The ordinary 24-hour forecast and scheduler lead bounds continue to apply.
Room diagnostics expose `warmupWindows`, `warmupRate` and `calibrated`.

EMS supply/activity freshness is 3 minutes, outdoor freshness 30 minutes.
OpenWeather freshness is twice its configured interval, bounded to 30–120
minutes, and checks the provider observation timestamp when device time is valid.
The controller's existing outdoor-temperature safety fallback is not used as a
training measurement. Room measurements use the controller's existing freshness
checks. Manual GPIO tests invalidate training windows.

`confidence` is **training coverage**, not a probability or accuracy claim:
50% × min(heating windows / 12, 1) + 50% × min(cooling windows / 6, 1).
Without scheduled-warmup calibration, the physical model is used after both
thresholds have been reached. Training coverage still counts the short physical
windows; it is independent of the `Calibrated` status. The model continues
learning across days and firmware restarts. A finite-step thermal forecast checks
whether a target can be reached within 24 hours; impossible targets are reported.

## Schedule and priority

AOS resolves the existing inclusive `time_from`/`time_to` intervals and selects
maximum temperature among matching intervals, falling back to base temperature.
It searches the following 12 hours for the next effective temperature increase.
A decrease remains a normal schedule change. It accounts for weekdays, midnight,
overlapping entries and actual elapsed minutes through DST changes. Existing
weekday semantics of overnight intervals are preserved.

The start is target time minus forecast duration minus margin, bounded by both
the previous effective temperature transition and the configured advance limit.
A future intervening decrease therefore prevents starting before that decrease.
`limited` reports when those bounds truncate the required lead; `unreachable`
reports the physical forecast's failure to reach the target. Once started, the
room setpoint stays raised until the scheduled event, despite forecast changes.
The ordinary room hysteresis still controls heating on/off.

An active temporary override always wins and cancels the preheating latch.
AOS is recalculated after expiry; it never preheats through an active override.
Missing room data cancels the decision. Changes of wall time relative to monotonic
time, timezone offset, configuration and program invalidate/recalculate it.
Schedule candidates refresh every three minutes; reaching an event refreshes
immediately. Thermal forecasts refresh on the normal controller tick.

## Persistence and error metrics

NVS namespace `thermal-aos` holds a versioned, checksummed snapshot of parameters
and aggregate statistics for each room, never the sample history. Bad checksums,
unknown versions, changed sensors and nonfinite/out-of-range parameters cause a
safe default model. Dirty state is written at most once per six hours with retry
at the next interval after failure. Up to six hours of new learning can be lost
on power failure. Reset removes the room's key immediately.
Snapshot version 2 adds scheduled-warmup calibration. Valid version-1 snapshots
migrate existing parameters and counters without resetting them; the new warmup
counters start at zero. In-progress warmups are transient and are not restored.

For actual active preheating, clean observed cycles record arrival error in
minutes (`actual reached time - target time`), heating-duration error, mean
absolute errors and mean time from early first attainment to the deadline (an early-holding
proxy). Arrival means reaching within
0.05°C of the target. Cycles interrupted by an override, missing data, a clock
jump, a configuration reload or a detected anomaly do not correct the model.
Shadow forecasts do not claim arrival accuracy for heating they did not perform.
Coefficients adapt through accepted measured windows; timing errors do not
blindly shift the next start by the entire error.

## Validation and limits

Native tests execute production `Room`, `RoomConfig`, model and scheduler code.
They cover buffer limits, learning independently from control, four-week
quantized thermal simulations with +10°C/−20°C weather, different water
conditions and room speeds, ON/OFF fallback, missing data, anomalies, forecast
limits, override/shadow behavior, eight independent rooms, reloads, snapshot
validation, inclusive intervals, week rollover, overlapping schedules, DST and
clock jumps. A synthetic benchmark compares forecast arrival error against
starting at the schedule time and a fixed two-hour advance.

Run `pio test -e native --verbose`; build firmware with
`pio run -e esp32dev -e esp32s3`. Uploading firmware and filesystem is a separate
operation. The new WWW controls require updating the filesystem assets as well.

Real-installation shadow observations and on-device power-cycle/NVS validation
remain necessary before assessing the proposed −5/+10-minute comfort tolerance.
Solar gains, inter-room heat transfer and variable shared-boiler hydraulics are
not individually identified; simultaneous demand can make forecasts optimistic.
The synthetic benchmark does not establish real-building accuracy or savings.
Optional 2R2C, user-provided radiator ratings and HA control switches are deferred.

An offline learning comparison is available in `tools/compare_aos_learning.py`.
It replays the current C++ learner against a reconstructed legacy learner,
verifies the legacy baseline against the historical logged decisions/counters,
and compares short windows and actual scheduled warmups in selected rooms.
Pass `--room-id <room-id>` for each room to include; there are no installation-specific defaults.
Installation-specific reports and raw logs are maintained outside the public
documentation. The correction is implemented in firmware source; running the
comparison does not install it on the device. Historical replay results do not
replace validation on new data or on-device persistence checks.


## ESP32-S3 smoke test, 2026-10-20

Firmware and the filesystem built from `data_test_s3` were uploaded through OTA
to the test thermostat. All source JSON files, including Wi-Fi and network settings,
remained byte-for-byte unchanged. The normal room/program settings were restored
after the tests and the temporary `AosBoardTest` program was removed.

A temporary schedule exercised `SHADOW → PREHEATING → OVERRIDE → SHADOW` with
real BLE room readings. Shadow kept the scheduled setpoint at 23.5°C; active AOS
applied 24°C. The ordinary supply setpoint remained 38.87°C, despite a future
70°C program override. An HTTP reboot preserved the temporary AOS configuration
and selected program; the original configuration was then restored.

Chromium exercised the live room editor on desktop and a 390px mobile viewport
(without horizontal overflow or JavaScript errors), configuration save, diagnostics,
invalid-setting rejection, reset confirmation and the real NVS reset endpoint
for an untrained room. A visible loading-spinner issue found during the test was
fixed. These short tests do not validate learned coefficient persistence after
the six-hour write interval, a cold power cycle, or long-term arrival accuracy.

## Historical diagnostics and morning dumps

The independent **AOS** logging feature writes INFO records to the existing
serial/TCP log stream. It is enabled by default and can be toggled in
**Settings → Network and services → Debug settings → Debug log options → AOS**.
It does not require enabling the noisy `Room` feature, changing network settings
or enabling active preheating. The device does not store this log in flash;
keep the existing PC/network log receiver running overnight.

Each complete record starts with `AOS_DIAG ` followed by a flat JSON object
(schema `v: 1`). A snapshot is emitted about once a minute for each configured
room, including disabled rooms. Changes of scheduler status/target, five-minute
learning samples, cycle events, resets, reloads, clock changes and NVS write
results are recorded separately. No diagnostic collection changes the heating
policy. The TCP sink accepts whole records up to 4096 bytes; its internal-RAM
fallback queue is 16 KiB (preferred PSRAM queue stays 64 KiB).

Every record carries:

- Stable room ID/name, active program, firmware build, Unix time `ts`, monotonic
  `uptime_s`, random per-boot `boot` ID and a global diagnostic `seq` number.
  An unsynchronized clock is represented by `ts: null` and `time_valid: false`.
- Raw/averaged room temperature and sensor age; outdoor temperature/source;
  actual supply and ordinary-curve supply forecast. Missing numbers are JSON
  `null`, never fabricated zeroes or NaN.
- Boiler command, valve state, fresh EMS heating state, manual-test flag,
  delivered heat and trust/quality reasons. `ems_activity_missing`,
  `ems_not_heating`, `ems_supply_missing`, `stale_room` and `heat_state_changed`
  distinguish common reasons for rejecting input. `warmup_eligible` and
  `confirmed_heat` expose calibration inputs; `warmup_reset` retains cancellation
  between samples so offline replay can reproduce it.
- Learning/AOS/shadow flags; scheduled/effective room setpoints and hysteresis;
  override lifetime; status, application flag, next target, previous boundary,
  proposed start, forecast, margin, advance cap, fallback/limit/impossibility.
- Model gain/loss, delay and fallback rate; training coverage, observation
  counters, working-history size, dirty/restored flags, learning-window bounds,
  measured rate/emission and acceptance/rejection outcome.
- Active-cycle start/target/first attainment, clean/disturbed state and observed
  arrival/duration-error statistics. Shadow mode records forecasts but does not
  claim accuracy for preheating it did not perform.

`event: sample` is the learning decision for one newly stored sample.
`observation`/`window_*` in periodic snapshots describe the **last** such decision;
repeated snapshots are not additional accepted/rejected windows. Typical
outcomes are `insufficient_history`, `window_not_due`, `response_delay`,
`mixed_heating_states`, `temperature_jump`, `insufficient_warming`,
`heating_accepted`, `cooling_accepted` and `fallback_missing_inputs`.
`event: cycle_completed` distinguishes `metrics_recorded`, `disturbed` and
`learning_disabled`. Configuration reloads preserve the existing cancellation
behavior; records identify the reload and retain model statistics.

For an overnight observation, enable learning and optimal-start predictions for
the desired rooms, keeping **shadow mode on**. Keep **AOS** logging on and save
the full receiver dump in the morning. Sharing the complete dump is sufficient;
other feature records provide useful boiler/sensor/restart context. Logging alone
does not switch on thermal learning for rooms where it is disabled.

If the usual receiver is not running, start the standard-library-only capture
helper **on the configured log receiver computer** (port 8701):

```sh
python3 tools/capture_device_log.py --port 8701 --device <thermostat-ip> \
  --output logs/aos-night.log
```

Leave it running overnight and stop with Ctrl+C in the morning. It appends the
full unfiltered stream and accepts device reconnects without changing settings.
`--seconds 80` can be used for a bounded smoke test. Use one receiver on this port.

Extract a portable CSV and summary with the standard-library-only helper:

```sh
python3 tools/analyze_aos_log.py logs/aos-night.log \
  --csv /tmp/aos-history.csv --summary-json /tmp/aos-summary.json
```

Multiple rotated logs and `.gz` dumps are supported. The extractor ignores other
features, deduplicates overlapping `(boot, seq)` records, separates boots, reports
malformed/unsupported records and flags missing sequence numbers **inside** the
supplied dump. A gap can mean transport loss, filtering or incomplete input; it
does not prove a device restart. It summarizes accepted/rejected samples,
learning reasons, temperature ranges and latest model state per room. State
record counts are not durations; wall-clock jumps and missing intervals should
be examined using both `ts` and `uptime_s`. Preserve the original dump alongside
the exported CSV for analysis of actual arrival times and boiler interruptions.

The diagnostic path was checked on ESP32-S3 on 2026-10-20: the live TCP dump
contained complete schema-v1 records from all seven rooms, with no malformed
JSON or sequence gaps in the final capture. The `AOS` checkbox was verified in
the live debug UI. The capture helper's half-open-connection replacement is
covered by a loopback regression test. Native tests passed (90), as did both
extractor tests and the capture test; ESP32 and ESP32-S3 firmware builds passed.
Current room/program/network settings and the user's four learning/shadow room
selections were preserved. The temporary capture receiver is stopped after the
smoke test; keep the normal receiver or the capture helper running overnight.
