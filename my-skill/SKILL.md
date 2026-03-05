# DynamicBalancer Skill Notes

This is the technical playbook for agents working in this repo.
Use it to keep firmware safety intact while moving the React prototype into an ESP32-served production UI.

## 1) Scope
- Firmware: `DynamicBalancer_Final.ino` (2243 lines, Waveshare ESP32-S3-DEV-KIT-N8R8)
- UI source: `DynamicBalancer_Final_FlashUIPrototype/src/*` (React/TypeScript, Vite, HashRouter)
- Deploy artifact target: `data/` (minified web assets for LittleFS)
- Shared handoff log: `CLAUDE_READINESS.md`
- Test tooling: `_ws_monitor.py`, `_rpm_sweep_test.py`, `_debug_esc.py`, `_test_rpm.py`
- Upload target: COM4 (primary), COM3 (alternate)

Only dynamic balancer guidance belongs here.

## 2) Quick Start Checklist
1. Read `AGENTS.md`.
2. Read latest entry in `CLAUDE_READINESS.md`.
3. Confirm API contract from `DynamicBalancer_Final.ino`.
4. Implement smallest safe change.
5. Record handoff summary in `CLAUDE_READINESS.md`.

## 3) Firmware Architecture Map

### Core globals
- `g_telem` (`Telemetry`) and `g_state` (`State`) are global volatile snapshots.
- `g_set` (`Settings`) persisted in NVS namespace `balancer`.
- Wi-Fi credentials persisted in NVS namespace `wifi`.

### Tasks
- `samplingTask` (core 1, priority 2, 8192 stack):
  - reads MPU6050 (Y-axis accel, ±2g, 16384 LSB/g) + AS5047P (SPI 4 MHz, ANGLECOM register)
  - estimates RPM via delta-angle windowed method (50 ms windows)
  - runs motor state transitions (IDLE → SPINUP → MEASURE → RESULTS)
  - performs synchronous detection (Fourier correlation: `y·cos(θ)`, `y·sin(θ)`)
  - computes LED on/off each loop
  - auto-stops motor at STEP_RESULTS (preserves results state for UI)
  - handles adaptive noise-gate measurement (repeat windows until `noiseRms ≤ target` or timeout)
- `netTask` (core 0, priority 1, 6144 stack):
  - periodic websocket broadcast via `wsBroadcast()` every `wsPublishMs`
- `loop()` intentionally idle (`delay(1000)`)

### Motor Control
- PI controller: KP=0.015, KI=0.005 (stable-first tuning)
- Feedforward table (6-point piecewise linear, from 2026-03-02 sweep):
  - 300→1020, 1700→1040, 2600→1060, 3600→1080, 4700→1100, 5500→1115 µs
- ESC arming: 800 µs pulse
- ESC range: 1000–2000 µs (ESC_MIN_US..ESC_MAX_US)

### Persistence
- Profiles: `/profiles.json` on LittleFS
- Sessions index: `/sessions/index.json`
- Session files: `/sessions/<id>.json`
- Settings: NVS namespace `balancer` (with schema version migration)
- NVS migration: `setVer` key tracks schema version; v0→v1 converts legacy `noiseFloorTarget` from raw ADC counts to g-scale

## 4) High-Risk Rules (Do Not Violate)
1. Keep `samplingTask` deterministic and non-blocking.
2. No filesystem writes in `samplingTask`.
3. No network scans or route-side heavy logic in timing-critical paths.
4. Keep `loop()` free of periodic heavy work.
5. Respect setting constraints before save:
- `escIdleUs`/`escMaxUs` constrained to `1000..2000` (ESC_MIN_US..ESC_MAX_US)
- `windowDeg` constrained `0.1..10.0`
- `samplePeriodUs` constrained `750..100000`
- `measureWindowMs` constrained `200..15000`
- `noiseFloorTarget` constrained `0.0..0.5` (g)
- `wsPublishMs` constrained `50..10000`
- `profile.phaseOffsetDeg` constrained `-180..180`
- `correctionRadiusMm` constrained `1.0..500.0`
- `rpmStableTol` constrained `10..1000`
- `rpmStableHoldMs` constrained `100..30000`
6. Preserve fallback UI and `/setup` recovery path.

## 5) API Contract Snapshot (synced 2026-03-05)

### REST

#### Pages
- `GET /` — serves LittleFS `/index.html` if present, else embedded fallback HTML
- `GET /setup` — embedded Wi-Fi config portal (always available including AP mode)
- `GET /diag` — embedded SPI health-check diagnostic page

#### Wi-Fi
- `GET /wifi/scan` -> `{ ssids: [{ssid,rssi,enc}] }`
- `POST /wifi/save` body `{ ssid, password }` → saves to NVS + reboots
- `GET /wifi/status` -> `{ apMode, ssidSaved, connected, ip, staIp, apIp, mdns }`

#### Settings
- `GET /settings` -> grouped object:
  - `model { zeroOffsetDeg, windowDeg, correctionRadiusMm }`
  - `led { mode, targetDeg }`
  - `sampling { samplePeriodUs, measureWindowMs, noiseFloorTarget, wsPublishMs }`
  - `motor { escIdleUs, escMaxUs, rpmStableTol, rpmStableHoldMs }`
- `PATCH /settings` partial update with same groups

#### Commands
- `POST /cmd/led_mode` `{ mode: "off|zero|heavy|add|remove|target" }`
- `POST /cmd/led_target` `{ targetDeg }`
- `POST /cmd/start_test` `{ profileId }`
- `POST /cmd/stop` `{}`
- `POST /cmd/set_esc` `{ us }` — direct ESC µs override (0 = cancel), for sweep testing
- `POST /cmd/save_session` `{ name, notes }` (`409 no_results` or `409 stale_result` when no current-run snapshot is available)

#### Profiles (CRUD on LittleFS)
- `GET /profiles`
- `POST /profiles` `{ id,name,rpm,spinupMs,dwellMs,repeats,phaseOffsetDeg? }`
- `PATCH /profiles/:id`
- `DELETE /profiles/:id`

#### Sessions
- `GET /sessions`
- `GET /sessions/:id`

#### Diagnostics
- `GET /diag/raw` -> `{ rawAngleDeg, rpmEMA, wrapCount, lastWrapDtUs, sampleCount, sweepEscUs, escMaxUs }`
- `GET /diag/spi_test` -> full AS5047 register dump (ERRFL, DIAGAGC, MAG, ANGLE, ANGLECOM, pins, RPM; optional `?reinit=1`)

### WebSocket
Route: `/ws`

Broadcast root:
- `type: "telemetry"`
- `telemetry: { rpm, vibMag, phaseDeg, quality, temp, noiseRms, timestamp, heavyDeg, addDeg, removeDeg, ledOn, ledMode, ledTargetDeg }`
- `state: { motorState, profileName, runStep, phaseGuidanceStale, activeProfileId, activeProfilePhaseOffsetDeg, hasResultSnapshot, errors[] }`

Unit notes:
- `vibMag`: 1× vibration amplitude in g (MPU6050 raw / 16384.0)
- `noiseRms`: RMS acceleration in g
- `quality`: 0..1 (signal-to-noise ratio)
- `temp`: float or null (no dedicated temp sensor currently wired)

Enum values in payload are numeric:
- `motorState`: `0 stopped`, `1 running`, `2 fault`
- `runStep`: `0 idle`, `1 spinup`, `2 measure`, `3 results`
- `ledMode`: `0 off`, `1 zero`, `2 heavy`, `3 add`, `4 remove`, `5 target`

Broadcast cadence: every `wsPublishMs` (default 200 ms), driven by `netTask` on core 0.

### Measurement completion behavior
- Default mode: one measure window (`measureWindowMs`) then results. Motor auto-stops at STEP_RESULTS.
- Adaptive mode: if `sampling.noiseFloorTarget > 0`, firmware repeats measure windows until `telemetry.noiseRms <= noiseFloorTarget` or timeout guard is reached (`windowMs×4`, clamped 6–60s).
- Synchronous detection: Fourier correlation accumulating `y·cos(θ)` and `y·sin(θ)` where `y` is raw MPU Y-axis and `θ` is zero-offset-adjusted AS5047 angle.

### Motor state machine
- IDLE → SPINUP: `POST /cmd/start_test` with valid profileId
- SPINUP → MEASURE: RPM within `rpmStableTol` for `rpmStableHoldMs`, or spinup timeout
- MEASURE → RESULTS: window complete (single or adaptive), motor auto-idles
- RESULTS → IDLE: `POST /cmd/stop`
- Defensive guard: if STEP_RESULTS but motor still running, force-stops

## 6) UI Integration Playbook

### Current status (as of 2026-03-03)
Live device I/O is wired. Simulator fallback toggle is preserved.

### Architecture
- `src/Protocol.ts` — TypeScript types mirroring firmware payloads and enum mapping
- `src/contexts/DeviceContext.tsx` — WS `/ws` telemetry/state ingest + REST endpoint calls + simulator fallback
- `src/App.tsx` — `HashRouter` for ESP32-safe deep links (`/#/wizard`, `/#/setup`, etc.)
- Pages: Dashboard, Wizard, Diagnostics, Profiles, Sessions, Setup

### Key UI behaviors
- Profile fields: `rpm`, `spinupMs`, `dwellMs`, `repeats`
- Quality scaling: `0..1` from firmware, displayed as percentage
- Motor state: numeric enum → label (`0→stopped`, `1→running`, `2→fault`)
- Telemetry: `vibMag`/`noiseRms` in g-scale, displayed to 3 decimal places
- Wizard: noise-gate 60s warning, physics-based estimated correction mass (`vibMag * 9.81 / (omega^2 * radius_m) * 1000`, clamped `0.001..10 g`), ABORT/RETRY/CANCEL, session save at step 4
- Setup/Wizard REST actions should use `try/catch` and show inline error feedback.
- Settings: grouped into model/led/sampling/motor sections in Setup page

### If further changes are needed
1. Edit `src/Protocol.ts` if API/WS payload contracts change.
2. Edit `src/contexts/DeviceContext.tsx` for REST/WS integration logic.
3. Edit page files in `src/pages/` for UI behavior.
4. Build and stage minified assets to `data/`.

## 7) Build And Stage Workflow
```bash
cd DynamicBalancer_Final_FlashUIPrototype
npm install
npm run build
```

From repo root (PowerShell):
```powershell
if (Test-Path .\data) { Remove-Item .\data\* -Recurse -Force -ErrorAction SilentlyContinue }
Copy-Item .\DynamicBalancer_Final_FlashUIPrototype\dist\* .\data\ -Recurse -Force
```

Firmware compile/upload (ESP32-S3):
```powershell
arduino-cli compile --fqbn esp32:esp32:esp32s3 --board-options CDCOnBoot=cdc --board-options USBMode=hwcdc DynamicBalancer_Final.ino
arduino-cli upload -p COM4 --fqbn esp32:esp32:esp32s3 --board-options CDCOnBoot=cdc --board-options USBMode=hwcdc DynamicBalancer_Final.ino
```

LittleFS build and flash:
```powershell
mklittlefs.exe -c data -p 256 -b 4096 -s 0x160000 littlefs.bin
esptool.exe --chip esp32s3 --port COM4 --no-stub --baud 115200 write_flash 0x290000 littlefs.bin
```

Note: Serial link can be noisy at high baud on this host — `--no-stub --baud 115200` is more reliable than 921600.

## 8) Validation Checklist
1. `GET /settings` and `PATCH /settings` round-trip works from UI.
2. `sampling.noiseFloorTarget` persists across reboot when changed (constrained to 0.0..0.5 g).
3. Start/stop commands drive state transitions (`runStep`: 0→1→2→3, auto-stop at 3).
4. WS telemetry live-updates dashboard values (`vibMag`/`noiseRms` in decimal g-scale).
5. Profile CRUD works and survives reboot (LittleFS load path).
6. Save session creates index + session file and appears in UI.
7. AP mode still exposes `/setup` and root fallback when no `data/index.html`.
8. Hash-route refresh works: `/#/wizard`, `/#/profiles`, etc. load React app (not 404).
9. Firmware routes coexist: `/profiles` returns API JSON, `/setup` returns firmware HTML.
10. Noise-gate adaptive mode: `noiseFloorTarget>0` causes repeated measure windows.
11. LED mode commands accepted: `off`, `zero`, `heavy`, `add`, `remove`, `target`.
12. Diagnostics endpoints respond: `/diag/raw`, `/diag/spi_test`.

## 9) Test Tooling Scripts (repo root)
- `_ws_monitor.py` — real-time WebSocket telemetry capture during a test run
- `_rpm_sweep_test.py` — automated ESC ramp with RPM accuracy analysis (produces `_rpm_sweep_results.csv`)
- `_debug_esc.py` — manual ESC override + raw angle diagnostic via `/cmd/set_esc` and `/diag/raw`
- `_test_rpm.py` — quick test-start + WS state tracking to results

Scripts should target the current runtime host from `/wifi/status`
(latest known session example: `ws://172.20.10.3/ws` / `http://172.20.10.3`).
Requires: `pip install requests websockets websocket-client`.

## 10) Default Profiles And Presets
Default profiles (created on first boot if `/profiles.json` missing):
- `1750 RPM` (spinup 3000 ms, dwell 5000 ms, 1 repeat)
- `2600 RPM` (spinup 4000 ms, dwell 5000 ms, 1 repeat)
- `3600 RPM` (spinup 5000 ms, dwell 5000 ms, 1 repeat)
- `4600 RPM` (spinup 6000 ms, dwell 5000 ms, 1 repeat)

Targets matched to stable ESC commutation bands (from 2026-03-02 sweep).

Wi-Fi defaults: SSID `Julius iPhone`, password `abcd1234`.
AP: `BalancerSetup` (open), dual AP+STA mode.
mDNS: `balance.local` (STA mode only).

## 10.1) Calibration SOP (Mandatory)
For any future profile calibration, follow this sequence exactly:

1. Capture no-weight baseline for every target profile.
2. Capture trial-weight run for every target profile:
   - known mass
   - known radius
   - known placement angle
3. Compute per-profile vector delta (`V_trial - V_no_weight`).
4. Convert to per-profile `phaseOffsetDeg` and apply branch correction:
   - `phaseOffsetDeg = wrap_to_-180_180(phaseOffsetDeg + 180)`
5. Write explicit offsets via `PATCH /profiles/:id` for each profile.
6. Physically confirm each profile with LED `add` targeting:
   - place a small trial mass at Add target
   - required pass condition: vibration decreases
   - if vibration increases, flip that profile by `±180` and re-validate.

Hard rules:
- Do not rely on auto-seeded/interpolated offsets for final calibration.
- New profiles must be explicitly calibrated and explicitly written.
- Calibration is not complete until physical confirmation passes per profile.

## 11) Handoff Rules (Codex + Claude)
For each substantial pass:
- add one dated entry to `CLAUDE_READINESS.md`
- include files changed + validation + open risks
- clearly state next action for the other agent

Do not claim completion if defaults/presets are still pending user confirmation.
