export type MotorStateCode = 0 | 1 | 2;
export type RunStepCode = 0 | 1 | 2 | 3;
export type MotorStateLabel = 'stopped' | 'running' | 'fault';
export type LedMode = 'off' | 'zero' | 'heavy' | 'add' | 'remove' | 'target';

export interface Telemetry {
  rpm: number;
  vibMag: number; // 1× vibration amplitude in g
  phaseDeg: number;
  quality: number; // 0..1 from firmware
  temp: number | null;
  noiseRms: number; // RMS acceleration noise in g
  timestamp: number;
  heavyDeg: number;
  addDeg: number;
  removeDeg: number;
  ledOn: boolean;
  ledMode: LedMode;
  ledTargetDeg: number;
}

export interface DeviceState {
  motorState: MotorStateCode;
  motorStateLabel: MotorStateLabel;
  profileName: string;
  runStep: RunStepCode;
  phaseGuidanceStale: boolean;
  activeProfileId: string;
  activeProfilePhaseOffsetDeg: number | null;
  hasResultSnapshot: boolean;
  errors: string[];
}

export interface Profile {
  id: string;
  name: string;
  rpm: number;
  spinupMs: number;
  dwellMs: number;
  repeats: number;
  phaseOffsetDeg: number;
}

export interface SessionSummary {
  id: string;
  name: string;
  timestamp: number;
}

export interface SessionTelemetry {
  rpm: number;
  vibMag: number; // 1× vibration amplitude in g
  phaseDeg: number;
  quality: number; // 0..1 from firmware
  noiseRms: number; // RMS acceleration noise in g
  heavyDeg: number;
  addDeg: number;
  removeDeg: number;
}

export interface SessionDetail extends SessionSummary {
  notes: string;
  profileName: string;
  profilePhaseOffsetDegUsed?: number | null;
  telemetry: SessionTelemetry;
}

export interface Settings {
  model: {
    zeroOffsetDeg: number;
    windowDeg: number;
    correctionRadiusMm: number; // distance from axis to correction weight placement (mm)
  };
  led: {
    mode: number; // firmware enum 0..5
    targetDeg: number;
  };
  sampling: {
    samplePeriodUs: number;
    measureWindowMs: number;
    noiseFloorTarget: number; // 0 disables adaptive measure extension
    wsPublishMs: number;
  };
  motor: {
    escIdleUs: number;
    escMaxUs: number;
    rpmStableTol: number;
    rpmStableHoldMs: number;
  };
}

export interface WifiNetwork {
  ssid: string;
  rssi: number;
  secure: boolean;
}

export interface WifiStatus {
  apMode: boolean;
  connected: boolean;
  ssidSaved: string;
  ssid?: string;
  ip: string;
  staIp: string;
  apIp: string;
  mdns?: string;
}

// Endpoints:
// WS /ws
// POST /cmd/start_test { profileId }
// POST /cmd/stop
// POST /cmd/led_mode { mode: "off"|"zero"|"heavy"|"add"|"remove"|"target" }
// POST /cmd/led_target { targetDeg }
// GET /profiles
// POST /profiles
// PATCH /profiles/:id
// DELETE /profiles/:id
// GET /sessions
// GET /sessions/:id
// POST /cmd/save_session { name, notes }
// GET /settings
// PATCH /settings
// GET /wifi/scan
// POST /wifi/save { ssid, password }
// GET /wifi/status
