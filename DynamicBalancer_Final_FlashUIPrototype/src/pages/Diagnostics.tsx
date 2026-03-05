import React, { useState, useEffect, useMemo } from 'react';
import { useDevice } from '../contexts/DeviceContext';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, AreaChart, Area } from 'recharts';
import { Activity, AlertTriangle, Zap } from 'lucide-react';

export const Diagnostics: React.FC = () => {
  const { telemetry } = useDevice();
  const [history, setHistory] = useState<any[]>([]);
  const qualityPct = Math.round(telemetry.quality * 100);

  useEffect(() => {
    setHistory((prev) => {
      const newHistory = [
        ...prev,
        {
          time: new Date().toLocaleTimeString([], { hour12: false, second: '2-digit', fractionalSecondDigits: 1 }),
          rpm: telemetry.rpm,
          vibMag: telemetry.vibMag,
          phaseDeg: telemetry.phaseDeg,
          noiseRms: telemetry.noiseRms,
        },
      ];
      if (newHistory.length > 50) newHistory.shift();
      return newHistory;
    });
  }, [telemetry]);

  const spectrumData = useMemo(() => {
    const data = [];
    const baseFreq = telemetry.rpm / 60;
    for (let i = 0; i < 100; i++) {
      const freq = i * 2;
      let mag = Math.random() * 0.01;

      if (Math.abs(freq - baseFreq) < 5) {
        mag += telemetry.vibMag * (1 - Math.abs(freq - baseFreq) / 5);
      }
      if (Math.abs(freq - baseFreq * 2) < 5) {
        mag += telemetry.vibMag * 0.3 * (1 - Math.abs(freq - baseFreq * 2) / 5);
      }

      data.push({ freq, mag });
    }
    return data;
  }, [telemetry.rpm, telemetry.vibMag]);

  return (
    <div className="p-8 max-w-6xl mx-auto space-y-8">
      <header className="border-b border-zinc-800 pb-6">
        <h1 className="text-3xl font-semibold tracking-tight text-zinc-100 mb-2">Live Diagnostics</h1>
        <p className="text-zinc-500 font-mono text-sm">Real-time sensor telemetry and analysis</p>
      </header>

      <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
        <div className="bg-zinc-900 border border-zinc-800 rounded-2xl p-6">
          <div className="flex items-center justify-between mb-4">
            <h3 className="text-sm font-medium text-zinc-400 uppercase tracking-wider">Signal Quality</h3>
            <Activity size={18} className={qualityPct > 80 ? 'text-emerald-500' : 'text-amber-500'} />
          </div>
          <div className="text-4xl font-mono text-zinc-100 mb-2">{qualityPct}%</div>
          <p className="text-xs text-zinc-500">Confidence in phase measurement</p>
        </div>

        <div className="bg-zinc-900 border border-zinc-800 rounded-2xl p-6">
          <div className="flex items-center justify-between mb-4">
            <h3 className="text-sm font-medium text-zinc-400 uppercase tracking-wider">Noise Floor (RMS)</h3>
            <AlertTriangle size={18} className={telemetry.noiseRms > 0.01 ? 'text-red-500' : 'text-zinc-500'} />
          </div>
          <div className="text-4xl font-mono text-zinc-100 mb-2">{telemetry.noiseRms.toFixed(3)}</div>
          <p className="text-xs text-zinc-500">Sensor noise level</p>
        </div>

        <div className="bg-zinc-900 border border-zinc-800 rounded-2xl p-6">
          <div className="flex items-center justify-between mb-4">
            <h3 className="text-sm font-medium text-zinc-400 uppercase tracking-wider">MCU Temp</h3>
            <Zap size={18} className="text-sky-500" />
          </div>
          <div className="text-4xl font-mono text-zinc-100 mb-2">
            {telemetry.temp == null ? '--' : `${telemetry.temp.toFixed(1)}C`}
          </div>
          <p className="text-xs text-zinc-500">Internal sensor temperature</p>
        </div>
      </div>

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <div className="bg-zinc-900 border border-zinc-800 rounded-2xl p-6 h-80">
          <h3 className="text-sm font-medium text-zinc-400 uppercase tracking-wider mb-6">Vibration Magnitude (g)</h3>
          <ResponsiveContainer width="100%" height="100%">
            <LineChart data={history} margin={{ top: 5, right: 5, left: -20, bottom: 0 }}>
              <CartesianGrid strokeDasharray="3 3" stroke="#27272a" vertical={false} />
              <XAxis dataKey="time" stroke="#52525b" fontSize={10} tickFormatter={() => ''} />
              <YAxis stroke="#52525b" fontSize={10} domain={[0, 'auto']} />
              <Tooltip contentStyle={{ backgroundColor: '#18181b', borderColor: '#27272a', color: '#e4e4e7' }} />
              <Line type="monotone" dataKey="vibMag" stroke="#fbbf24" strokeWidth={2} dot={false} isAnimationActive={false} />
            </LineChart>
          </ResponsiveContainer>
        </div>

        <div className="bg-zinc-900 border border-zinc-800 rounded-2xl p-6 h-80">
          <h3 className="text-sm font-medium text-zinc-400 uppercase tracking-wider mb-6">RPM</h3>
          <ResponsiveContainer width="100%" height="100%">
            <LineChart data={history} margin={{ top: 5, right: 5, left: -20, bottom: 0 }}>
              <CartesianGrid strokeDasharray="3 3" stroke="#27272a" vertical={false} />
              <XAxis dataKey="time" stroke="#52525b" fontSize={10} tickFormatter={() => ''} />
              <YAxis stroke="#52525b" fontSize={10} domain={[0, 'auto']} />
              <Tooltip contentStyle={{ backgroundColor: '#18181b', borderColor: '#27272a', color: '#e4e4e7' }} />
              <Line type="monotone" dataKey="rpm" stroke="#34d399" strokeWidth={2} dot={false} isAnimationActive={false} />
            </LineChart>
          </ResponsiveContainer>
        </div>
      </div>

      <div className="bg-zinc-900 border border-zinc-800 rounded-2xl p-6 h-96">
        <div className="flex items-center justify-between mb-6">
          <h3 className="text-sm font-medium text-zinc-400 uppercase tracking-wider">Estimated 1× Harmonic Spectrum</h3>
          <div className="text-xs text-zinc-500 font-mono">1x RPM = {(telemetry.rpm / 60).toFixed(1)} Hz</div>
        </div>
        <ResponsiveContainer width="100%" height="100%">
          <AreaChart data={spectrumData} margin={{ top: 5, right: 5, left: -20, bottom: 20 }}>
            <defs>
              <linearGradient id="colorMag" x1="0" y1="0" x2="0" y2="1">
                <stop offset="5%" stopColor="#8b5cf6" stopOpacity={0.3} />
                <stop offset="95%" stopColor="#8b5cf6" stopOpacity={0} />
              </linearGradient>
            </defs>
            <CartesianGrid strokeDasharray="3 3" stroke="#27272a" vertical={false} />
            <XAxis dataKey="freq" stroke="#52525b" fontSize={10} label={{ value: 'Frequency (Hz)', position: 'insideBottom', offset: -10, fill: '#71717a', fontSize: 10 }} />
            <YAxis stroke="#52525b" fontSize={10} domain={[0, 'auto']} />
            <Tooltip contentStyle={{ backgroundColor: '#18181b', borderColor: '#27272a', color: '#e4e4e7' }} />
            <Area type="monotone" dataKey="mag" stroke="#8b5cf6" fillOpacity={1} fill="url(#colorMag)" isAnimationActive={false} />
          </AreaChart>
        </ResponsiveContainer>
      </div>
    </div>
  );
};
