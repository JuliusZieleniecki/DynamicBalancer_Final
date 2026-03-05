# DynamicBalancer Final (ESP32-S3 Wheel Balancer)

DynamicBalancer is an ESP32-based dynamic spin balancer for wheels/rotors.  
This repo contains:
- Firmware: `DynamicBalancer_Final.ino`
- React web UI source: `DynamicBalancer_Final_FlashUIPrototype/`
- LittleFS deploy assets: `data/`

The system spins to a profile RPM, measures 1x vibration magnitude/phase, and guides physical correction with LED targeting (`heavy`, `add`, `remove`, `target`).

## 1) Hardware Pinout (Waveshare ESP32-S3-DEV-KIT-N8R8)

Firmware pin mapping in this repo:

| Function | GPIO |
|---|---:|
| Status / guidance LED | 16 |
| ESC signal (PWM us) | 17 |
| AS5047 CS | 5 |
| SPI SCK (AS5047) | 12 |
| SPI MISO (AS5047) | 13 |
| SPI MOSI (AS5047) | 11 |
| I2C SDA (MPU6050) | 8 |
| I2C SCL (MPU6050) | 9 |

If you change board/pins, update `DynamicBalancer_Final.ino`.

## 2) Wi-Fi Credentials (Safe Pattern)

Hard-coded credentials are removed from tracked firmware source.

1. Copy template:
   - `credentials.example.h` -> `credentials.h`
2. Set values in `credentials.h`:
   - `DB_DEFAULT_WIFI_SSID`
   - `DB_DEFAULT_WIFI_PASS`
3. `credentials.h` is gitignored and will not be committed.

On first boot, if no Wi-Fi is stored in NVS, firmware uses `credentials.h` defaults.  
If blank, STA connect is skipped and AP fallback remains available (`BalancerSetup`).

## 3) Install And Flash

### Firmware (Arduino IDE)
1. Install board package: `esp32` by Espressif.
2. Select board: Waveshare ESP32-S3 dev kit variant compatible with your USB/flash layout.
3. Install required libs from firmware header:
   - `ESPAsyncWebServer`, `AsyncTCP`, `ArduinoJson`, `I2Cdev`, `MPU6050`, `AS5X47`, `ESP32Servo`
4. Build/flash `DynamicBalancer_Final.ino`.

### Web UI Build -> LittleFS
1. `cd DynamicBalancer_Final_FlashUIPrototype`
2. `npm install`
3. `npm run build`
4. Copy built files to repo `data/` (source of truth for on-device hosted UI assets).
5. Upload LittleFS image/content so `/index.html` exists on device.

Firmware fallback behavior is preserved:
- `/` serves LittleFS `/index.html` if present.
- Otherwise embedded fallback page is served.

## 4) Use And Control

1. Power on balancer.
2. Connect via STA IP (or AP fallback).
3. Open:
   - `http://balance.local` (STA+mDNS)
   - or direct IP from `/wifi/status`
4. Select profile and run test.
5. Use LED mode:
   - `heavy`: points to heavy spot
   - `add`: points opposite heavy spot (where to add correction mass)
   - `remove`: points to heavy spot (where to remove mass)
6. Apply physical correction, retest, iterate until vibration is minimized.

## 5) Preferences And What They Mean

Global settings (`/settings`) with enforced firmware ranges:

| Path | Range | Meaning |
|---|---|---|
| `model.windowDeg` | `0.1..10.0` deg | LED on-window around target angle |
| `model.correctionRadiusMm` | `1.0..500.0` mm | Radius reference for correction calculations/reporting |
| `sampling.samplePeriodUs` | `750..100000` us | Sensor loop sample period |
| `sampling.measureWindowMs` | `200..15000` ms | Measurement capture duration |
| `sampling.noiseFloorTarget` | `0..0.5` g | Vibration floor threshold reference |
| `sampling.wsPublishMs` | `50..10000` ms | Telemetry publish interval |
| `motor.escIdleUs` | `1000..2000` us | ESC idle pulse |
| `motor.escMaxUs` | `1000..2000` us | ESC max pulse cap |
| `motor.rpmStableTol` | `10..1000` RPM | Allowed RPM error before stable timer resets |
| `motor.rpmStableHoldMs` | `100..30000` ms | Time inside tolerance before capture starts |

Per-profile settings (`/profiles`):

| Field | Meaning |
|---|---|
| `id`, `name` | Profile identity/label |
| `rpm` | Target spin RPM |
| `spinupMs` | Spin-up timeout window |
| `dwellMs` | Measurement dwell duration |
| `repeats` | Repeat count (if used by workflow/UI) |
| `phaseOffsetDeg` | Per-profile phase correction, constrained to `-180..180` |

## 6) Calibration Flow (Mandatory SOP)

Use this flow whenever you recalibrate profiles:

```mermaid
flowchart TD
  A[Start Calibration Campaign] --> B[No-weight baseline for all profiles]
  B --> C[Known trial-weight run for all profiles<br/>fixed mass + fixed angle]
  C --> D[Compute vector delta per profile<br/>V_trial - V_no_weight]
  D --> E[Derive raw phaseOffsetDeg per profile]
  E --> F[Apply branch rule:<br/>phaseOffsetDeg = wrap(raw + 180)]
  F --> G[PATCH /profiles/:id for every profile]
  G --> H[Physical confirmation per profile<br/>LED mode=add, add small trial mass]
  H --> I{vibMag decreases?}
  I -->|Yes| J[Keep profile offset]
  I -->|No| K[Revert profile and test opposite branch]
  J --> L{All profiles confirmed?}
  K --> L
  L -->|No| H
  L -->|Yes| M[Calibration complete]
```

Rules:
- Calibrate all active profiles, not just one.
- Always do no-weight first, then known trial-weight.
- Default branch apply is `+180` wrapped.
- Final truth is physical confirmation per profile (vibration must improve when following LED add guidance).

## 7) Core API Endpoints

- `GET /wifi/status`, `GET /wifi/scan`, `POST /wifi/save`
- `GET /settings`, `PATCH /settings`
- `GET /profiles`, `POST /profiles`, `PATCH /profiles/:id`, `DELETE /profiles/:id`
- `POST /cmd/start_test`, `POST /cmd/stop`
- `POST /cmd/led_mode`, `POST /cmd/led_target`
- `GET /diag/raw`
- `WS /ws` (`type: telemetry`, with `telemetry` + `state`)

## 8) Safety Notes

- Keep wheel secured before spin-up.
- Use small correction masses during validation loops.
- Do not block or add storage/network writes inside `samplingTask()`.
- Keep `loop()` lightweight to avoid timing jitter in control/measurement paths.
