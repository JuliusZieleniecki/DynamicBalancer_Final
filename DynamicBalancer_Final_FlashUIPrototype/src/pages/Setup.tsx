import React, { useState, useEffect } from 'react';
import { useDevice } from '../contexts/DeviceContext';
import { Wifi, Save, RefreshCw, CheckCircle2, AlertCircle, Server } from 'lucide-react';
import { Settings, WifiNetwork } from '../Protocol';

export const Setup: React.FC = () => {
  const { settings, updateSettings, refreshSettings, wifiStatus, refreshWifiStatus, scanWifi, connectWifi } = useDevice();
  const [networks, setNetworks] = useState<WifiNetwork[]>([]);
  const [scanning, setScanning] = useState(false);
  const [selectedSsid, setSelectedSsid] = useState('');
  const [password, setPassword] = useState('');
  const [connecting, setConnecting] = useState(false);
  const [saving, setSaving] = useState(false);
  const [saveError, setSaveError] = useState<string | null>(null);
  const [networkError, setNetworkError] = useState<string | null>(null);
  const [localSettings, setLocalSettings] = useState<Settings>(settings);

  useEffect(() => {
    setLocalSettings(settings);
  }, [settings]);

  useEffect(() => {
    refreshWifiStatus().catch((e: any) => setNetworkError(e?.message || 'Failed to refresh Wi-Fi status'));
    refreshSettings().catch((e: any) => setSaveError(e?.message || 'Failed to refresh settings'));
  }, [refreshWifiStatus, refreshSettings]);

  const handleScan = async () => {
    setScanning(true);
    setNetworkError(null);
    try {
      const nets = await scanWifi();
      setNetworks(nets);
    } catch (e: any) {
      setNetworkError(e?.message || 'Failed to scan Wi-Fi networks');
    } finally {
      setScanning(false);
    }
  };

  const handleConnect = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!selectedSsid) return;
    setConnecting(true);
    setNetworkError(null);
    try {
      await connectWifi(selectedSsid, password);
      setPassword('');
    } catch (e: any) {
      setNetworkError(e?.message || 'Failed to save Wi-Fi credentials');
    } finally {
      setConnecting(false);
    }
  };

  const handleSaveSettings = async () => {
    setSaving(true);
    setSaveError(null);
    try {
      await updateSettings(localSettings);
    } catch (e: any) {
      setSaveError(e?.message || 'Failed to save settings');
    } finally {
      setSaving(false);
    }
  };

  return (
    <div className="p-8 max-w-5xl mx-auto space-y-8">
      <header className="border-b border-zinc-800 pb-6">
        <h1 className="text-3xl font-semibold tracking-tight text-zinc-100 mb-2">Device Setup</h1>
        <p className="text-zinc-500 font-mono text-sm">Configure firmware settings and connectivity</p>
      </header>

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-8">
        <div className="bg-zinc-900 border border-zinc-800 rounded-2xl p-6 space-y-6">
          <div className="flex items-center gap-3 mb-6">
            <div className="w-10 h-10 rounded-full bg-zinc-800 flex items-center justify-center text-zinc-400">
              <Server size={20} />
            </div>
            <h2 className="text-xl font-medium text-zinc-100">Firmware Config</h2>
          </div>

          <div className="space-y-5">
            <div className="grid grid-cols-2 gap-4">
              <div>
                <label className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-2 block">Zero Offset (deg)</label>
                <input
                  type="number"
                  step="0.1"
                  value={localSettings.model.zeroOffsetDeg}
                  onChange={(e) =>
                    setLocalSettings({
                      ...localSettings,
                      model: { ...localSettings.model, zeroOffsetDeg: parseFloat(e.target.value) || 0 },
                    })
                  }
                  className="w-full bg-zinc-950 border border-zinc-700 rounded-xl px-4 py-3 text-zinc-200 focus:outline-none focus:border-emerald-500 font-mono"
                />
              </div>
              <div>
                <label className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-2 block">LED Window (deg)</label>
                <input
                  type="number"
                  step="0.1"
                  value={localSettings.model.windowDeg}
                  onChange={(e) =>
                    setLocalSettings({
                      ...localSettings,
                      model: { ...localSettings.model, windowDeg: parseFloat(e.target.value) || 0.1 },
                    })
                  }
                  className="w-full bg-zinc-950 border border-zinc-700 rounded-xl px-4 py-3 text-zinc-200 focus:outline-none focus:border-emerald-500 font-mono"
                />
              </div>
            </div>

            <div>
              <label className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-2 block">Correction Radius (mm)</label>
              <input
                type="number"
                step="0.5"
                min="1"
                max="500"
                value={localSettings.model.correctionRadiusMm}
                onChange={(e) =>
                  setLocalSettings({
                    ...localSettings,
                    model: { ...localSettings.model, correctionRadiusMm: parseFloat(e.target.value) || 25 },
                  })
                }
                className="w-full bg-zinc-950 border border-zinc-700 rounded-xl px-4 py-3 text-zinc-200 focus:outline-none focus:border-emerald-500 font-mono"
              />
              <p className="text-[11px] text-zinc-500 mt-1">
                Distance from the rotation axis to where correction weights are placed. Used for mass estimates.
              </p>
            </div>

            <div className="grid grid-cols-2 gap-4">
              <div>
                <label className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-2 block">Sample Period (us)</label>
                <input
                  type="number"
                  value={localSettings.sampling.samplePeriodUs}
                  onChange={(e) =>
                    setLocalSettings({
                      ...localSettings,
                      sampling: { ...localSettings.sampling, samplePeriodUs: parseInt(e.target.value, 10) || 2000 },
                    })
                  }
                  className="w-full bg-zinc-950 border border-zinc-700 rounded-xl px-4 py-3 text-zinc-200 focus:outline-none focus:border-emerald-500 font-mono"
                />
              </div>
              <div>
                <label className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-2 block">WS Publish (ms)</label>
                <input
                  type="number"
                  value={localSettings.sampling.wsPublishMs}
                  onChange={(e) =>
                    setLocalSettings({
                      ...localSettings,
                      sampling: { ...localSettings.sampling, wsPublishMs: parseInt(e.target.value, 10) || 200 },
                    })
                  }
                  className="w-full bg-zinc-950 border border-zinc-700 rounded-xl px-4 py-3 text-zinc-200 focus:outline-none focus:border-emerald-500 font-mono"
                />
              </div>
            </div>

            <div className="grid grid-cols-2 gap-4">
              <div>
                <label className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-2 block">RPM Stable Tol</label>
                <input
                  type="number"
                  step="1"
                  value={localSettings.motor.rpmStableTol}
                  onChange={(e) =>
                    setLocalSettings({
                      ...localSettings,
                      motor: { ...localSettings.motor, rpmStableTol: parseFloat(e.target.value) || 120 },
                    })
                  }
                  className="w-full bg-zinc-950 border border-zinc-700 rounded-xl px-4 py-3 text-zinc-200 focus:outline-none focus:border-emerald-500 font-mono"
                />
              </div>
              <div>
                <label className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-2 block">RPM Hold (ms)</label>
                <input
                  type="number"
                  value={localSettings.motor.rpmStableHoldMs}
                  onChange={(e) =>
                    setLocalSettings({
                      ...localSettings,
                      motor: { ...localSettings.motor, rpmStableHoldMs: parseInt(e.target.value, 10) || 900 },
                    })
                  }
                  className="w-full bg-zinc-950 border border-zinc-700 rounded-xl px-4 py-3 text-zinc-200 focus:outline-none focus:border-emerald-500 font-mono"
                />
              </div>
            </div>

            <div className="grid grid-cols-2 gap-4">
              <div>
                <label className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-2 block">Measure Window (ms)</label>
                <input
                  type="number"
                  value={localSettings.sampling.measureWindowMs}
                  onChange={(e) =>
                    setLocalSettings({
                      ...localSettings,
                      sampling: { ...localSettings.sampling, measureWindowMs: parseInt(e.target.value, 10) || 3000 },
                    })
                  }
                  className="w-full bg-zinc-950 border border-zinc-700 rounded-xl px-4 py-3 text-zinc-200 focus:outline-none focus:border-emerald-500 font-mono"
                />
              </div>
              <div>
                <label className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-2 block">Noise Target (RMS)</label>
                <input
                  type="number"
                  step="0.1"
                  value={localSettings.sampling.noiseFloorTarget}
                  onChange={(e) =>
                    setLocalSettings({
                      ...localSettings,
                      sampling: { ...localSettings.sampling, noiseFloorTarget: parseFloat(e.target.value) || 0 },
                    })
                  }
                  className="w-full bg-zinc-950 border border-zinc-700 rounded-xl px-4 py-3 text-zinc-200 focus:outline-none focus:border-emerald-500 font-mono"
                />
                <p className="text-[11px] text-zinc-500 mt-1">
                  Lower is stricter (better noise floor, longer runs). Higher is looser (faster runs, more noise). Set 0 for single-window mode.
                </p>
              </div>
            </div>

            <div className="grid grid-cols-2 gap-4">
              <div>
                <label className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-2 block">ESC Idle (us)</label>
                <input
                  type="number"
                  value={localSettings.motor.escIdleUs}
                  onChange={(e) =>
                    setLocalSettings({
                      ...localSettings,
                      motor: { ...localSettings.motor, escIdleUs: parseInt(e.target.value, 10) || 1000 },
                    })
                  }
                  className="w-full bg-zinc-950 border border-zinc-700 rounded-xl px-4 py-3 text-zinc-200 focus:outline-none focus:border-emerald-500 font-mono"
                />
              </div>
              <div>
                <label className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-2 block">ESC Max (us)</label>
                <input
                  type="number"
                  value={localSettings.motor.escMaxUs}
                  onChange={(e) =>
                    setLocalSettings({
                      ...localSettings,
                      motor: { ...localSettings.motor, escMaxUs: parseInt(e.target.value, 10) || 1800 },
                    })
                  }
                  className="w-full bg-zinc-950 border border-zinc-700 rounded-xl px-4 py-3 text-zinc-200 focus:outline-none focus:border-emerald-500 font-mono"
                />
              </div>
            </div>

            <button
              onClick={handleSaveSettings}
              disabled={saving}
              className={`w-full mt-4 font-bold py-3 rounded-xl flex items-center justify-center gap-2 transition-colors ${saving ? 'bg-zinc-800 text-zinc-500' : 'bg-emerald-500 hover:bg-emerald-400 text-zinc-950'}`}
            >
              {saving ? <RefreshCw size={18} className="animate-spin" /> : <Save size={18} />}
              {saving ? 'Saving...' : 'Save Configuration'}
            </button>
            {saveError && (
              <div className="mt-2 text-xs text-red-400 bg-red-500/10 border border-red-500/20 rounded-lg px-3 py-2 flex items-center gap-2">
                <AlertCircle size={14} />
                {saveError}
              </div>
            )}
          </div>
        </div>

        <div className="bg-zinc-900 border border-zinc-800 rounded-2xl p-6 space-y-6">
          <div className="flex items-center justify-between mb-6">
            <div className="flex items-center gap-3">
              <div className={`w-10 h-10 rounded-full flex items-center justify-center ${wifiStatus.connected ? 'bg-emerald-500/20 text-emerald-400' : 'bg-zinc-800 text-zinc-400'}`}>
                <Wifi size={20} />
              </div>
              <h2 className="text-xl font-medium text-zinc-100">Network</h2>
            </div>
            <button
              onClick={() => {
                setNetworkError(null);
                refreshWifiStatus().catch((e: any) => setNetworkError(e?.message || 'Failed to refresh Wi-Fi status'));
              }}
              className="text-xs text-sky-400 hover:text-sky-300 flex items-center gap-1 transition-colors"
            >
              <RefreshCw size={12} />
              Refresh
            </button>
          </div>
          {networkError && (
            <div className="mb-4 text-xs text-red-400 bg-red-500/10 border border-red-500/20 rounded-lg px-3 py-2 flex items-center gap-2">
              <AlertCircle size={14} />
              {networkError}
            </div>
          )}

          <div className="bg-zinc-950 border border-zinc-800 rounded-xl p-4 space-y-3">
            <div className="flex justify-between items-center">
              <span className="text-sm text-zinc-500 uppercase tracking-wider font-semibold">Status</span>
              <span className={`text-xs font-semibold px-2 py-1 rounded-md ${wifiStatus.connected ? 'text-emerald-400 bg-emerald-500/10' : 'text-zinc-400 bg-zinc-800'}`}>
                {wifiStatus.connected ? (
                  <span className="inline-flex items-center gap-1">
                    <CheckCircle2 size={14} /> Connected
                  </span>
                ) : (
                  'Not Connected'
                )}
              </span>
            </div>
            <div className="flex justify-between items-center">
              <span className="text-sm text-zinc-500 uppercase tracking-wider font-semibold">Saved SSID</span>
              <span className="text-sm font-mono text-zinc-200">{wifiStatus.ssidSaved || '--'}</span>
            </div>
            <div className="flex justify-between items-center">
              <span className="text-sm text-zinc-500 uppercase tracking-wider font-semibold">IP</span>
              <span className="text-sm font-mono text-zinc-200">{wifiStatus.ip || '--'}</span>
            </div>
            <div className="flex justify-between items-center">
              <span className="text-sm text-zinc-500 uppercase tracking-wider font-semibold">mDNS</span>
              <span className="text-sm font-mono text-zinc-200">{wifiStatus.mdns || '--'}</span>
            </div>
            <div className="flex justify-between items-center">
              <span className="text-sm text-zinc-500 uppercase tracking-wider font-semibold">STA IP</span>
              <span className="text-sm font-mono text-zinc-200">{wifiStatus.staIp || '--'}</span>
            </div>
            <div className="flex justify-between items-center">
              <span className="text-sm text-zinc-500 uppercase tracking-wider font-semibold">AP IP</span>
              <span className="text-sm font-mono text-zinc-200">{wifiStatus.apIp || '--'}</span>
            </div>
            <div className="flex justify-between items-center">
              <span className="text-sm text-zinc-500 uppercase tracking-wider font-semibold">AP Mode</span>
              <span className={`text-xs font-semibold px-2 py-1 rounded-md ${wifiStatus.apMode ? 'text-sky-400 bg-sky-500/10' : 'text-zinc-500 bg-zinc-800'}`}>
                {wifiStatus.apMode ? 'Active' : 'Inactive'}
              </span>
            </div>
          </div>

          <form onSubmit={handleConnect} className="space-y-4">
            <div>
              <div className="flex items-center justify-between mb-2">
                <label className="text-xs text-zinc-500 uppercase tracking-wider font-semibold block">Available Networks</label>
                <button type="button" onClick={handleScan} disabled={scanning} className="text-xs text-sky-400 hover:text-sky-300 flex items-center gap-1 transition-colors">
                  <RefreshCw size={12} className={scanning ? 'animate-spin' : ''} />
                  Scan
                </button>
              </div>
              <select
                value={selectedSsid}
                onChange={(e) => setSelectedSsid(e.target.value)}
                className="w-full bg-zinc-950 border border-zinc-700 rounded-xl px-4 py-3 text-zinc-200 focus:outline-none focus:border-emerald-500"
              >
                <option value="">Select a network...</option>
                {networks.map((n) => (
                  <option key={`${n.ssid}-${n.rssi}`} value={n.ssid}>
                    {n.ssid} ({n.rssi}dBm){n.secure ? ' [secured]' : ' [open]'}
                  </option>
                ))}
              </select>
            </div>

            <div>
              <label className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-2 block">Password</label>
              <input
                type="password"
                value={password}
                onChange={(e) => setPassword(e.target.value)}
                className="w-full bg-zinc-950 border border-zinc-700 rounded-xl px-4 py-3 text-zinc-200 focus:outline-none focus:border-emerald-500"
                placeholder="Enter Wi-Fi password"
              />
            </div>

            <button
              type="submit"
              disabled={!selectedSsid || connecting}
              className={`w-full font-bold py-3 rounded-xl flex items-center justify-center gap-2 transition-colors ${!selectedSsid || connecting ? 'bg-zinc-800 text-zinc-500 cursor-not-allowed' : 'bg-sky-500 hover:bg-sky-400 text-zinc-950'}`}
            >
              {connecting ? <RefreshCw size={18} className="animate-spin" /> : <Wifi size={18} />}
              {connecting ? 'Saving and rebooting...' : 'Save Wi-Fi Credentials'}
            </button>

            <div className="bg-amber-500/10 border border-amber-500/20 rounded-xl p-4 flex items-start gap-3 mt-4">
              <AlertCircle size={18} className="text-amber-500 shrink-0 mt-0.5" />
              <p className="text-xs text-amber-400/80 leading-relaxed">
                After saving Wi-Fi credentials, the ESP32 reboots. If STA fails, connect to AP mode at SSID
                {' '}<code className="text-amber-300">BalancerSetup</code>.
              </p>
            </div>
          </form>
        </div>
      </div>
    </div>
  );
};
