# DynamicBalancer Flash UI Prototype

React/TypeScript UI source for the Dynamic Spin Balancer ESP32 project.

This folder is the editable UI source.
The deploy target for the ESP32 web server is minified build output in repo root `data/`.

## Firmware Contract (Current — synced 2026-03-05)
Server implemented in `../DynamicBalancer_Final.ino` (2243 lines):

- WS: `/ws` (telemetry broadcast every `wsPublishMs`, default 200 ms)
- Pages:
  - `GET /` — serves LittleFS `index.html` if present, else embedded fallback HTML
  - `GET /setup` — embedded Wi-Fi config portal (always available in AP mode)
  - `GET /diag` — embedded SPI health-check diagnostic page
- Commands:
  - `POST /cmd/start_test` `{ profileId }`
  - `POST /cmd/stop` `{}`
  - `POST /cmd/led_mode` `{ mode: "off|zero|heavy|add|remove|target" }`
  - `POST /cmd/led_target` `{ targetDeg }`
  - `POST /cmd/save_session` `{ name, notes }` (`409 no_results` or `409 stale_result` when no current-run snapshot is available)
  - `POST /cmd/set_esc` `{ us }` — direct ESC µs override (0 = cancel), for sweep testing
- Data:
  - `GET /settings`, `PATCH /settings`
  - `GET /profiles`, `POST /profiles`, `PATCH /profiles/:id`, `DELETE /profiles/:id`
  - `GET /sessions`, `GET /sessions/:id`
  - `GET /wifi/scan`, `POST /wifi/save`, `GET /wifi/status`
- Diagnostics:
  - `GET /diag/raw` → `{ rawAngleDeg, rpmEMA, wrapCount, lastWrapDtUs, sampleCount, sweepEscUs, escMaxUs }`
  - `GET /diag/spi_test` → full AS5047 register dump (ERRFL, DIAGAGC, MAG, ANGLE, ANGLECOM, pins, RPM)

Settings payload groups:
- `model { zeroOffsetDeg, windowDeg, correctionRadiusMm }`
- `led { mode, targetDeg }`
- `sampling { samplePeriodUs, measureWindowMs, noiseFloorTarget, wsPublishMs }`
- `motor { escIdleUs, escMaxUs, rpmStableTol, rpmStableHoldMs }`

Wi-Fi status response: `{ apMode, ssidSaved, connected, ip, staIp, apIp, mdns }`

Measurement behavior:
- If `sampling.noiseFloorTarget == 0`, each run uses a single `measureWindowMs` window.
- If `sampling.noiseFloorTarget > 0`, measurement windows repeat until `noiseRms <= noiseFloorTarget` or safety timeout (`windowMs×4`, clamped 6–60s).

Auto-stop: Motor idles automatically when measurement completes (STEP_RESULTS).

## Current UI Status
- Multi-page UI shell: Dashboard / Wizard / Diagnostics / Profiles / Sessions / Setup.
- `src/contexts/DeviceContext.tsx` uses live WS + REST against firmware endpoints.
- Simulator mode remains available for offline UI checks (g-scale aligned).
- App routing uses `HashRouter` for ESP32-safe hard refresh and deep links (`/#/wizard`, `/#/setup`, etc.).
- Wizard includes: noise-gate 60s warning with one-click fallback, physics-based estimated correction mass (`vibMag * 9.81 / (omega^2 * radius_m) * 1000`, clamped `0.001..10 g`), ABORT/RETRY/CANCEL controls, and session save at step 4.
- Setup and Wizard REST actions use `try/catch` and inline error messages instead of silent failures.
- Telemetry units: `vibMag` in g (1× amplitude), `noiseRms` in g (RMS).

## Development
```bash
cd DynamicBalancer_Final_FlashUIPrototype
npm install
npm run dev
```

## Production Build
```bash
cd DynamicBalancer_Final_FlashUIPrototype
npm run build
```

Vite outputs minified files to `DynamicBalancer_Final_FlashUIPrototype/dist/`.

## Stage Build To ESP32 Data Folder
From repo root (PowerShell):

```powershell
if (Test-Path .\data) { Remove-Item .\data\* -Recurse -Force -ErrorAction SilentlyContinue }
Copy-Item .\DynamicBalancer_Final_FlashUIPrototype\dist\* .\data\ -Recurse -Force
```

After staging, upload LittleFS/SPIFFS using your existing ESP32 upload flow.
Example tooling used in this repo:

```powershell
mklittlefs.exe -c data -p 256 -b 4096 -s 0x160000 littlefs.bin
esptool.exe --chip esp32s3 --port COM4 --no-stub --baud 115200 write_flash 0x290000 littlefs.bin
```

## Integration Rules
1. Treat `src/*` as source of truth.
2. Do not manually maintain minified files in `data/`.
3. Keep endpoint names and payload shapes in sync with `DynamicBalancer_Final.ino`.
4. Update `src/Protocol.ts` whenever API/WS payload contracts change.

## Default Presets (Current)
Default profiles created on first boot (from 2026-03-02 sweep calibration):
- `1750 RPM` (spinup 3000 ms, dwell 5000 ms, 1 repeat)
- `2600 RPM` (spinup 4000 ms, dwell 5000 ms, 1 repeat)
- `3600 RPM` (spinup 5000 ms, dwell 5000 ms, 1 repeat)
- `4600 RPM` (spinup 6000 ms, dwell 5000 ms, 1 repeat)

Key firmware defaults:
- `windowDeg`: 1.0° | `escIdleUs`: 1000 µs | `escMaxUs`: 1800 µs
- `measureWindowMs`: 3000 ms | `noiseFloorTarget`: 0.0 g (single-window mode)
- `correctionRadiusMm`: 25.0 mm
- Per-profile phase offsets are stored in `/profiles.json` as `phaseOffsetDeg` and applied only during active test-run measurement.
- Wi-Fi: SSID `Julius iPhone`, AP `BalancerSetup` (open), mDNS `balance.local`

Full settings schema and calibration values tracked in `../CLAUDE_READINESS.md`.

## Calibration SOP (Mandatory, As Of 2026-03-05)
Use this exact order for all profile recalibration work:

1. Run no-weight baseline for each profile.
2. Run with a known trial weight (known mass, known radius, known placement angle) for each profile.
3. Compute per-profile vector delta (`V_trial - V_no_weight`) from phase/magnitude data.
4. Convert to per-profile `phaseOffsetDeg`, then apply the branch correction:
   - `phaseOffsetDeg = wrap_to_-180_180(phaseOffsetDeg + 180)`
5. PATCH `/profiles/:id` with explicit `phaseOffsetDeg` for every calibrated profile.
6. Physically confirm each profile:
   - LED mode `add`
   - add a small mass at UI Add target
   - vibration must decrease
   - if it increases, that profile is on the wrong branch; flip by `±180` and re-check.

Rules:
- Do not treat old auto-seeded profile offsets as calibration truth.
- New profiles must be explicitly calibrated and written via `/profiles`; do not rely on interpolation seed alone.
- Do not mark calibration complete until each profile has passed physical add/remove confirmation.

## Test Tooling Scripts (repo root)
- `_ws_monitor.py` — real-time WebSocket telemetry capture during a test run
- `_rpm_sweep_test.py` — automated ESC ramp with RPM accuracy analysis (produces `_rpm_sweep_results.csv`)
- `_debug_esc.py` — manual ESC override + raw angle diagnostic
- `_test_rpm.py` — quick test-start + WS state tracking

Use current runtime host from `/wifi/status` (example from latest session: `172.20.10.3`).
Requires: `pip install requests websockets websocket-client`.
