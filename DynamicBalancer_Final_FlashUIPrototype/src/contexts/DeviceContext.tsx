import React, { createContext, useContext, useEffect, useState, useCallback, useRef } from 'react';
import {
  Telemetry,
  DeviceState,
  Profile,
  SessionSummary,
  SessionDetail,
  Settings,
  WifiStatus,
  WifiNetwork,
  LedMode,
  MotorStateCode,
  MotorStateLabel,
  RunStepCode,
} from '../Protocol';

interface DeviceContextType {
  telemetry: Telemetry;
  deviceState: DeviceState;
  profiles: Profile[];
  sessions: SessionSummary[];
  settings: Settings;
  wifiStatus: WifiStatus;
  simulatorMode: boolean;
  connectionError: string | null;
  setSimulatorMode: (mode: boolean) => void;
  refreshProfiles: () => Promise<void>;
  createProfile: (profile: Profile) => Promise<void>;
  updateProfile: (id: string, patch: Partial<Profile>) => Promise<void>;
  deleteProfile: (id: string) => Promise<void>;
  refreshSessions: () => Promise<void>;
  loadSession: (id: string) => Promise<SessionDetail>;
  startTest: (profileId: string) => Promise<void>;
  stopTest: () => Promise<void>;
  setLedMode: (mode: LedMode) => Promise<void>;
  setLedTarget: (targetDeg: number) => Promise<void>;
  saveSession: (name: string, notes: string) => Promise<string | null>;
  refreshSettings: () => Promise<void>;
  updateSettings: (newSettings: Partial<Settings>) => Promise<void>;
  scanWifi: () => Promise<WifiNetwork[]>;
  connectWifi: (ssid: string, password: string) => Promise<void>;
  refreshWifiStatus: () => Promise<void>;
}

const LED_MODE_BY_CODE: Record<number, LedMode> = {
  0: 'off',
  1: 'zero',
  2: 'heavy',
  3: 'add',
  4: 'remove',
  5: 'target',
};

const LED_CODE_BY_MODE: Record<LedMode, number> = {
  off: 0,
  zero: 1,
  heavy: 2,
  add: 3,
  remove: 4,
  target: 5,
};

const defaultTelemetry: Telemetry = {
  rpm: 0,
  vibMag: 0,
  phaseDeg: 0,
  quality: 0,
  temp: null,
  noiseRms: 0,
  timestamp: 0,
  heavyDeg: 0,
  addDeg: 180,
  removeDeg: 0,
  ledOn: false,
  ledMode: 'off',
  ledTargetDeg: 0,
};

const defaultDeviceState: DeviceState = {
  motorState: 0,
  motorStateLabel: 'stopped',
  profileName: 'idle',
  runStep: 0,
  phaseGuidanceStale: false,
  activeProfileId: '',
  activeProfilePhaseOffsetDeg: null,
  hasResultSnapshot: false,
  errors: [],
};

const defaultSettings: Settings = {
  model: {
    zeroOffsetDeg: 0,
    windowDeg: 1,
    correctionRadiusMm: 25,
  },
  led: {
    mode: 5,
    targetDeg: 0,
  },
  sampling: {
    samplePeriodUs: 2000,
    measureWindowMs: 3000,
    noiseFloorTarget: 0,
    wsPublishMs: 200,
  },
  motor: {
    escIdleUs: 1000,
    escMaxUs: 1800,
    rpmStableTol: 120,
    rpmStableHoldMs: 900,
  },
};

const defaultWifiStatus: WifiStatus = {
  apMode: true,
  connected: false,
  ssidSaved: '',
  ssid: '',
  ip: '',
  staIp: '',
  apIp: '192.168.4.1',
  mdns: '',
};

const DeviceContext = createContext<DeviceContextType | null>(null);

function modeFromCode(value: unknown): LedMode {
  if (typeof value === 'string') {
    if (value in LED_CODE_BY_MODE) return value as LedMode;
  }
  if (typeof value === 'number' && value in LED_MODE_BY_CODE) {
    return LED_MODE_BY_CODE[value];
  }
  return 'off';
}

function motorLabel(code: number): MotorStateLabel {
  if (code === 1) return 'running';
  if (code === 2) return 'fault';
  return 'stopped';
}

function asNumber(v: unknown, fallback = 0): number {
  if (typeof v === 'number' && Number.isFinite(v)) return v;
  const n = Number(v);
  return Number.isFinite(n) ? n : fallback;
}

async function apiJson<T>(path: string, init?: Omit<RequestInit, 'body'> & { body?: unknown }): Promise<T> {
  const headers = new Headers(init?.headers || {});
  const rawBody = init?.body;
  let body: BodyInit | undefined;
  if (rawBody === undefined || rawBody === null) {
    body = undefined;
  } else if (typeof rawBody === 'string' || rawBody instanceof FormData || rawBody instanceof URLSearchParams || rawBody instanceof Blob || rawBody instanceof ArrayBuffer || ArrayBuffer.isView(rawBody) || rawBody instanceof ReadableStream) {
    body = rawBody as BodyInit;
  } else {
    headers.set('Content-Type', 'application/json');
    body = JSON.stringify(rawBody);
  }

  const response = await fetch(path, { ...init, headers, body });
  const text = await response.text();
  if (!response.ok) {
    const details = text || response.statusText;
    throw new Error(`HTTP ${response.status}: ${details}`);
  }
  if (!text) return {} as T;
  return JSON.parse(text) as T;
}

function mapTelemetry(raw: any): Telemetry {
  return {
    rpm: asNumber(raw?.rpm),
    vibMag: asNumber(raw?.vibMag),
    phaseDeg: asNumber(raw?.phaseDeg),
    quality: asNumber(raw?.quality),
    temp: raw?.temp == null ? null : asNumber(raw?.temp),
    noiseRms: asNumber(raw?.noiseRms),
    timestamp: asNumber(raw?.timestamp),
    heavyDeg: asNumber(raw?.heavyDeg),
    addDeg: asNumber(raw?.addDeg, 180),
    removeDeg: asNumber(raw?.removeDeg),
    ledOn: Boolean(raw?.ledOn),
    ledMode: modeFromCode(raw?.ledMode),
    ledTargetDeg: asNumber(raw?.ledTargetDeg),
  };
}

function mapState(raw: any): DeviceState {
  const motor = asNumber(raw?.motorState, 0) as MotorStateCode;
  const runStep = asNumber(raw?.runStep, 0) as RunStepCode;
  return {
    motorState: motor,
    motorStateLabel: motorLabel(motor),
    profileName: String(raw?.profileName || 'idle'),
    runStep,
    phaseGuidanceStale: Boolean(raw?.phaseGuidanceStale),
    activeProfileId: String(raw?.activeProfileId || ''),
    activeProfilePhaseOffsetDeg: raw?.activeProfilePhaseOffsetDeg == null ? null : asNumber(raw?.activeProfilePhaseOffsetDeg, 0),
    hasResultSnapshot: Boolean(raw?.hasResultSnapshot),
    errors: Array.isArray(raw?.errors) ? raw.errors.map((e: unknown) => String(e)) : [],
  };
}

function mapProfile(raw: any): Profile {
  const rpm = asNumber(raw?.rpm, 2500);
  return {
    id: String(raw?.id || ''),
    name: String(raw?.name || raw?.id || ''),
    rpm,
    spinupMs: asNumber(raw?.spinupMs, 2500),
    dwellMs: asNumber(raw?.dwellMs, 3500),
    repeats: asNumber(raw?.repeats, 1),
    phaseOffsetDeg: asNumber(raw?.phaseOffsetDeg, 0),
  };
}

function mapSettings(raw: any): Settings {
  return {
    model: {
      zeroOffsetDeg: asNumber(raw?.model?.zeroOffsetDeg, 0),
      windowDeg: asNumber(raw?.model?.windowDeg, 1),
      correctionRadiusMm: asNumber(raw?.model?.correctionRadiusMm, 25),
    },
    led: {
      mode: asNumber(raw?.led?.mode, 5),
      targetDeg: asNumber(raw?.led?.targetDeg, 0),
    },
    sampling: {
      samplePeriodUs: asNumber(raw?.sampling?.samplePeriodUs, 2000),
      measureWindowMs: asNumber(raw?.sampling?.measureWindowMs, 3000),
      noiseFloorTarget: asNumber(raw?.sampling?.noiseFloorTarget, 0),
      wsPublishMs: asNumber(raw?.sampling?.wsPublishMs, 200),
    },
    motor: {
      escIdleUs: asNumber(raw?.motor?.escIdleUs, 1000),
      escMaxUs: asNumber(raw?.motor?.escMaxUs, 1800),
      rpmStableTol: asNumber(raw?.motor?.rpmStableTol, 120),
      rpmStableHoldMs: asNumber(raw?.motor?.rpmStableHoldMs, 900),
    },
  };
}

function mapWifiStatus(raw: any): WifiStatus {
  return {
    apMode: Boolean(raw?.apMode),
    connected: Boolean(raw?.connected),
    ssidSaved: String(raw?.ssidSaved || ''),
    ssid: String(raw?.ssidSaved || ''),
    ip: String(raw?.ip || ''),
    staIp: String(raw?.staIp || ''),
    apIp: String(raw?.apIp || ''),
    mdns: raw?.mdns ? String(raw.mdns) : '',
  };
}

function mapSessionSummary(raw: any): SessionSummary {
  return {
    id: String(raw?.id || ''),
    name: String(raw?.name || raw?.id || ''),
    timestamp: asNumber(raw?.timestamp, 0),
  };
}

function mapSessionDetail(raw: any): SessionDetail {
  return {
    id: String(raw?.id || ''),
    name: String(raw?.name || raw?.id || ''),
    notes: String(raw?.notes || ''),
    timestamp: asNumber(raw?.timestamp, 0),
    profileName: String(raw?.profileName || 'idle'),
    profilePhaseOffsetDegUsed: raw?.profilePhaseOffsetDegUsed == null ? null : asNumber(raw?.profilePhaseOffsetDegUsed, 0),
    telemetry: {
      rpm: asNumber(raw?.telemetry?.rpm),
      vibMag: asNumber(raw?.telemetry?.vibMag),
      phaseDeg: asNumber(raw?.telemetry?.phaseDeg),
      quality: asNumber(raw?.telemetry?.quality),
      noiseRms: asNumber(raw?.telemetry?.noiseRms),
      heavyDeg: asNumber(raw?.telemetry?.heavyDeg),
      addDeg: asNumber(raw?.telemetry?.addDeg),
      removeDeg: asNumber(raw?.telemetry?.removeDeg),
    },
  };
}

export const DeviceProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const [simulatorMode, setSimulatorMode] = useState(false);
  const [connectionError, setConnectionError] = useState<string | null>(null);
  const [telemetry, setTelemetry] = useState<Telemetry>(defaultTelemetry);
  const [deviceState, setDeviceState] = useState<DeviceState>(defaultDeviceState);
  const [profiles, setProfiles] = useState<Profile[]>([]);
  const [sessions, setSessions] = useState<SessionSummary[]>([]);
  const [settings, setSettings] = useState<Settings>(defaultSettings);
  const [wifiStatus, setWifiStatus] = useState<WifiStatus>(defaultWifiStatus);
  const simInterval = useRef<number | null>(null);

  const refreshProfiles = useCallback(async () => {
    if (simulatorMode) return;
    const payload = await apiJson<{ profiles?: any[] }>('/profiles');
    setProfiles((payload.profiles || []).map(mapProfile));
  }, [simulatorMode]);

  const refreshSessions = useCallback(async () => {
    if (simulatorMode) return;
    const payload = await apiJson<{ sessions?: any[] }>('/sessions');
    setSessions((payload.sessions || []).map(mapSessionSummary));
  }, [simulatorMode]);

  const refreshSettings = useCallback(async () => {
    if (simulatorMode) return;
    const payload = await apiJson<any>('/settings');
    setSettings(mapSettings(payload));
  }, [simulatorMode]);

  const refreshWifiStatus = useCallback(async () => {
    if (simulatorMode) return;
    const payload = await apiJson<any>('/wifi/status');
    setWifiStatus(mapWifiStatus(payload));
  }, [simulatorMode]);

  useEffect(() => {
    if (simulatorMode) return;
    Promise.allSettled([refreshProfiles(), refreshSessions(), refreshSettings(), refreshWifiStatus()]).catch(() => undefined);
  }, [simulatorMode, refreshProfiles, refreshSessions, refreshSettings, refreshWifiStatus]);

  useEffect(() => {
    if (simulatorMode) return;

    let socket: WebSocket | null = null;
    let reconnectTimer: number | null = null;
    let disposed = false;

    const connect = () => {
      const scheme = window.location.protocol === 'https:' ? 'wss' : 'ws';
      socket = new WebSocket(`${scheme}://${window.location.host}/ws`);

      socket.onopen = () => {
        setConnectionError(null);
      };

      socket.onmessage = (event) => {
        try {
          const msg = JSON.parse(event.data);
          if (msg?.type !== 'telemetry') return;
          if (msg?.telemetry) setTelemetry(mapTelemetry(msg.telemetry));
          if (msg?.state) setDeviceState(mapState(msg.state));
        } catch (err) {
          setConnectionError(`Bad WS payload: ${String(err)}`);
        }
      };

      socket.onerror = () => {
        setConnectionError('WebSocket connection error');
      };

      socket.onclose = () => {
        if (disposed) return;
        reconnectTimer = window.setTimeout(connect, 1500);
      };
    };

    connect();
    return () => {
      disposed = true;
      if (reconnectTimer !== null) window.clearTimeout(reconnectTimer);
      if (socket && socket.readyState <= WebSocket.OPEN) socket.close();
    };
  }, [simulatorMode]);

  // Keep optional simulator mode for offline UI checks.
  useEffect(() => {
    if (!simulatorMode) {
      if (simInterval.current) window.clearInterval(simInterval.current);
      return;
    }

    let currentPhase = 0;
    let currentRpm = 0;
    let targetRpm = 0;

    simInterval.current = window.setInterval(() => {
      setDeviceState((prev) => {
        if (prev.motorStateLabel === 'running') targetRpm = 3000;
        if (prev.motorStateLabel === 'stopped') targetRpm = 0;
        return prev;
      });

      if (currentRpm < targetRpm) currentRpm += 45;
      if (currentRpm > targetRpm) currentRpm -= 45;
      if (currentRpm < 0) currentRpm = 0;
      currentPhase = (currentPhase + (currentRpm * 6) / 60) % 360;

      setTelemetry((prev) => ({
        ...prev,
        rpm: currentRpm,
        phaseDeg: currentPhase,
        vibMag: currentRpm > 500 ? 0.05 + Math.random() * 0.1 : 0.005,
        quality: currentRpm > 500 ? 0.88 + Math.random() * 0.1 : 0.2,
        noiseRms: currentRpm > 500 ? 0.002 + Math.random() * 0.003 : 0.001,
        timestamp: Date.now(),
        heavyDeg: 45,
        addDeg: 225,
        removeDeg: 45,
        ledOn: Math.abs(((currentPhase - prev.ledTargetDeg + 540) % 360) - 180) <= settings.model.windowDeg,
      }));
    }, 100);

    return () => {
      if (simInterval.current) window.clearInterval(simInterval.current);
    };
  }, [simulatorMode, settings.model.windowDeg]);

  const createProfile = useCallback(async (profile: Profile) => {
    if (simulatorMode) {
      setProfiles((prev) => [...prev, profile]);
      return;
    }
    await apiJson('/profiles', { method: 'POST', body: profile });
    await refreshProfiles();
  }, [simulatorMode, refreshProfiles]);

  const updateProfile = useCallback(async (id: string, patch: Partial<Profile>) => {
    if (simulatorMode) {
      setProfiles((prev) => prev.map((p) => (p.id === id ? { ...p, ...patch } : p)));
      return;
    }
    await apiJson(`/profiles/${encodeURIComponent(id)}`, { method: 'PATCH', body: patch });
    await refreshProfiles();
  }, [simulatorMode, refreshProfiles]);

  const deleteProfile = useCallback(async (id: string) => {
    if (simulatorMode) {
      setProfiles((prev) => prev.filter((p) => p.id !== id));
      return;
    }
    await apiJson(`/profiles/${encodeURIComponent(id)}`, { method: 'DELETE' });
    await refreshProfiles();
  }, [simulatorMode, refreshProfiles]);

  const loadSession = useCallback(async (id: string): Promise<SessionDetail> => {
    if (simulatorMode) {
      const fallback: SessionDetail = {
        id,
        name: `Session ${id}`,
        notes: 'Simulator session',
        timestamp: Date.now(),
        profileName: 'sim',
        telemetry: {
          rpm: telemetry.rpm,
          vibMag: telemetry.vibMag,
          phaseDeg: telemetry.phaseDeg,
          quality: telemetry.quality,
          noiseRms: telemetry.noiseRms,
          heavyDeg: telemetry.heavyDeg,
          addDeg: telemetry.addDeg,
          removeDeg: telemetry.removeDeg,
        },
      };
      return fallback;
    }
    const payload = await apiJson<any>(`/sessions/${encodeURIComponent(id)}`);
    return mapSessionDetail(payload);
  }, [simulatorMode, telemetry]);

  const startTest = useCallback(async (profileId: string) => {
    if (simulatorMode) {
      setDeviceState((prev) => ({ ...prev, motorState: 1, motorStateLabel: 'running', runStep: 1 }));
      return;
    }
    await apiJson('/cmd/start_test', { method: 'POST', body: { profileId } });
  }, [simulatorMode]);

  const stopTest = useCallback(async () => {
    if (simulatorMode) {
      setDeviceState((prev) => ({ ...prev, motorState: 0, motorStateLabel: 'stopped', runStep: 0 }));
      return;
    }
    await apiJson('/cmd/stop', { method: 'POST', body: {} });
  }, [simulatorMode]);

  const setLedMode = useCallback(async (mode: LedMode) => {
    if (simulatorMode) {
      setTelemetry((prev) => ({ ...prev, ledMode: mode }));
      setSettings((prev) => ({ ...prev, led: { ...prev.led, mode: LED_CODE_BY_MODE[mode] } }));
      return;
    }
    await apiJson('/cmd/led_mode', { method: 'POST', body: { mode } });
    setSettings((prev) => ({ ...prev, led: { ...prev.led, mode: LED_CODE_BY_MODE[mode] } }));
  }, [simulatorMode]);

  const setLedTarget = useCallback(async (targetDeg: number) => {
    if (simulatorMode) {
      setTelemetry((prev) => ({ ...prev, ledTargetDeg: targetDeg, ledMode: 'target' }));
      setSettings((prev) => ({ ...prev, led: { ...prev.led, targetDeg, mode: 5 } }));
      return;
    }
    await apiJson('/cmd/led_target', { method: 'POST', body: { targetDeg } });
    setSettings((prev) => ({ ...prev, led: { ...prev.led, targetDeg, mode: 5 } }));
  }, [simulatorMode]);

  const saveSession = useCallback(async (name: string, notes: string) => {
    if (simulatorMode) {
      const id = Date.now().toString();
      setSessions((prev) => [...prev, { id, name, timestamp: Date.now() }]);
      return id;
    }
    const payload = await apiJson<{ id?: string }>('/cmd/save_session', { method: 'POST', body: { name, notes } });
    await refreshSessions();
    return payload.id || null;
  }, [simulatorMode, refreshSessions]);

  const updateSettings = useCallback(async (newSettings: Partial<Settings>) => {
    if (simulatorMode) {
      setSettings((prev) => ({ ...prev, ...newSettings }));
      return;
    }
    await apiJson('/settings', { method: 'PATCH', body: newSettings });
    await refreshSettings();
  }, [simulatorMode, refreshSettings]);

  const scanWifi = useCallback(async () => {
    if (simulatorMode) return [];
    const payload = await apiJson<{ ssids?: Array<{ ssid?: string; rssi?: number; enc?: boolean }> }>('/wifi/scan');
    return (payload.ssids || []).map((n) => ({
      ssid: String(n.ssid || ''),
      rssi: asNumber(n.rssi, -100),
      secure: Boolean(n.enc),
    }));
  }, [simulatorMode]);

  const connectWifi = useCallback(async (ssid: string, password: string) => {
    if (simulatorMode) {
      setWifiStatus((prev) => ({ ...prev, connected: true, ssidSaved: ssid, ssid, ip: '192.168.4.165' }));
      return;
    }
    await apiJson('/wifi/save', { method: 'POST', body: { ssid, password } });
    setWifiStatus((prev) => ({
      ...prev,
      ssidSaved: ssid,
      ssid,
      connected: false,
    }));
  }, [simulatorMode]);

  return (
    <DeviceContext.Provider
      value={{
        telemetry,
        deviceState,
        profiles,
        sessions,
        settings,
        wifiStatus,
        simulatorMode,
        connectionError,
        setSimulatorMode,
        refreshProfiles,
        createProfile,
        updateProfile,
        deleteProfile,
        refreshSessions,
        loadSession,
        startTest,
        stopTest,
        setLedMode,
        setLedTarget,
        saveSession,
        refreshSettings,
        updateSettings,
        scanWifi,
        connectWifi,
        refreshWifiStatus,
      }}
    >
      {children}
    </DeviceContext.Provider>
  );
};

export const useDevice = () => {
  const ctx = useContext(DeviceContext);
  if (!ctx) throw new Error('useDevice must be used within DeviceProvider');
  return ctx;
};
