# DynamicBalancer Project Rules

## Mission
Refine this repository so `DynamicBalancer_Final.ino` and the React prototype in `DynamicBalancer_Final_FlashUIPrototype/` converge into one deployable web UI served by ESP32 LittleFS from `/data`.

## Scope Lock
- This repo is for the dynamic spin balancer system only.
- Legacy slot-car assumptions, fixes, and harness rules are intentionally removed.
- If a note does not help this ESP32 balancer firmware/UI, do not keep it.

## Source Of Truth
1. Firmware behavior and API contract: `DynamicBalancer_Final.ino`
2. UI source: `DynamicBalancer_Final_FlashUIPrototype/src/*`
3. Flash/deploy artifact target: `data/` (minified build output)
4. Team handoff state: `CLAUDE_READINESS.md`
5. Detailed engineering playbook: `my-skill/SKILL.md`

## Non-Negotiable Firmware Safety
1. Keep `samplingTask()` deterministic and non-blocking.
2. Do not add filesystem/network writes in the sensor loop.
3. Keep `loop()` lightweight; avoid jitter-inducing work there.
4. Validate settings ranges before persistence.
Current key constraints:
- `windowDeg` 0.1..10.0 (deg)
- `samplePeriodUs` 750..100000 (us)
- `measureWindowMs` 200..15000 (ms)
- `noiseFloorTarget` 0..0.5 (g)
- `wsPublishMs` 50..10000 (ms)
- `profile.phaseOffsetDeg` -180..180 (deg, via `/profiles`)
- `correctionRadiusMm` 1.0..500.0 (mm)
- `escIdleUs`/`escMaxUs` 1000..2000 (µs)
- `rpmStableTol` 10..1000 (RPM)
- `rpmStableHoldMs` 100..30000 (ms)
5. Preserve fallback UX:
`/` serves LittleFS `/index.html` when present, else embedded fallback page.
6. Keep `/setup` and Wi-Fi recovery path working in AP mode.

## API And UI Contract Discipline
1. Preserve existing endpoint paths unless migration is explicit and documented.
2. If API payloads change, update all three:
- firmware handler in `.ino`
- `DynamicBalancer_Final_FlashUIPrototype/src/Protocol.ts`
- docs (`CLAUDE_READINESS.md` + prototype `README.md`)
3. Keep WebSocket payload compatibility (`type: "telemetry"` + `telemetry` + `state`).

## Build And Deploy Direction
1. Develop in `DynamicBalancer_Final_FlashUIPrototype/` source files.
2. Do not treat generated/minified files as the source of truth.
3. After build, copy deploy assets into `data/` for LittleFS upload.
4. Keep fallback embedded HTML in firmware for recovery/debug.

## Codex + Claude Collaboration Contract
1. Every substantial change set must append a dated entry in `CLAUDE_READINESS.md`.
2. Each entry must include:
- what changed
- files touched
- validation run
- known risks/open items
- next handoff action
3. Use clear "next agent can continue from here" notes; avoid ambiguous status.

## Preset Governance
1. Do not silently hardcode new user presets.
2. Track requested default flash presets explicitly in `CLAUDE_READINESS.md`.
3. Any preset update must include where it is stored (NVS key, profile file, or UI default).

## Practical Rule
When in doubt, choose the smallest change that improves firmware/UI alignment without adding timing risk.
