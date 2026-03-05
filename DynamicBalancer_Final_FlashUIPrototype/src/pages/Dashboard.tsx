import React from 'react';
import { HeroDial } from '../components/HeroDial';
import { useDevice } from '../contexts/DeviceContext';
import { Activity, Zap, AlertTriangle, CheckCircle2 } from 'lucide-react';

export const Dashboard: React.FC = () => {
  const { telemetry, deviceState } = useDevice();
  const qualityPct = Math.round(telemetry.quality * 100);

  return (
    <div className="p-8 max-w-6xl mx-auto space-y-8">
      <header className="flex justify-between items-end border-b border-zinc-800 pb-6">
        <div>
          <h1 className="text-3xl font-semibold tracking-tight text-zinc-100 mb-2">Dynamic Spin Balancer</h1>
          <p className="text-zinc-500 font-mono text-sm">System Status: {deviceState.motorStateLabel.toUpperCase()}</p>
        </div>
        <div className="flex gap-4 text-right">
          <div>
            <div className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-1">RPM</div>
            <div className="text-2xl font-mono text-emerald-400">{Math.round(telemetry.rpm)}</div>
          </div>
          <div>
            <div className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-1">Vibration</div>
            <div className="text-2xl font-mono text-amber-400">{telemetry.vibMag.toFixed(3)}g</div>
          </div>
        </div>
      </header>

      <div className="grid grid-cols-1 lg:grid-cols-12 gap-8">
        {/* Left Column: Hero Dial */}
        <div className="lg:col-span-5">
          <HeroDial />
        </div>

        {/* Right Column: Quick Stats & Actions */}
        <div className="lg:col-span-7 space-y-6">
          {/* Quality Score */}
          <div className="bg-zinc-900 border border-zinc-800 rounded-2xl p-6 flex items-center justify-between">
            <div className="flex items-center gap-4">
              <div className={`w-12 h-12 rounded-full flex items-center justify-center ${qualityPct > 80 ? 'bg-emerald-500/20 text-emerald-400' : 'bg-amber-500/20 text-amber-400'}`}>
                {qualityPct > 80 ? <CheckCircle2 size={24} /> : <AlertTriangle size={24} />}
              </div>
              <div>
                <h3 className="text-lg font-medium text-zinc-200">Signal Quality</h3>
                <p className="text-sm text-zinc-500">Confidence in measurement</p>
              </div>
            </div>
            <div className="text-3xl font-mono font-light text-zinc-100">{qualityPct}%</div>
          </div>

          {/* Correction Guidance */}
          <div className="bg-zinc-900 border border-zinc-800 rounded-2xl p-6">
            <h3 className="text-lg font-medium text-zinc-200 mb-4 flex items-center gap-2">
              <Zap size={18} className="text-amber-400" />
              Correction Guidance
            </h3>
            
            <div className="grid grid-cols-2 gap-4">
              <div className="bg-zinc-950 border border-zinc-800 rounded-xl p-4">
                <div className="text-xs text-emerald-500 uppercase tracking-wider font-semibold mb-2">Add Weight At</div>
                <div className="text-2xl font-mono text-zinc-100 mb-1">{Math.round(telemetry.addDeg)}°</div>
                <div className="text-sm text-zinc-500">Vib: {telemetry.vibMag.toFixed(3)}g</div>
              </div>
              <div className="bg-zinc-950 border border-zinc-800 rounded-xl p-4">
                <div className="text-xs text-red-500 uppercase tracking-wider font-semibold mb-2">Heavy Spot At</div>
                <div className="text-2xl font-mono text-zinc-100 mb-1">{Math.round(telemetry.removeDeg || telemetry.heavyDeg)}°</div>
                <div className="text-sm text-zinc-500">Vib: {telemetry.vibMag.toFixed(3)}g</div>
              </div>
            </div>
          </div>

          {/* Noise Check */}
          <div className="bg-zinc-900 border border-zinc-800 rounded-2xl p-6">
            <h3 className="text-lg font-medium text-zinc-200 mb-4 flex items-center gap-2">
              <Activity size={18} className="text-sky-400" />
              Noise Floor
            </h3>
            <div className="flex items-center gap-4">
              <div className="flex-1 h-3 bg-zinc-950 rounded-full overflow-hidden">
                <div 
                  className={`h-full rounded-full transition-all ${telemetry.noiseRms > 0.01 ? 'bg-red-500' : 'bg-emerald-500'}`}
                  style={{ width: `${Math.min(100, telemetry.noiseRms * 5000)}%` }}
                />
              </div>
              <span className="text-sm font-mono text-zinc-400 w-16 text-right">{telemetry.noiseRms.toFixed(3)}</span>
            </div>
            {telemetry.noiseRms > 0.01 && (
              <p className="text-xs text-red-400 mt-2 flex items-center gap-1">
                <AlertTriangle size={12} /> High noise detected. Check sensor mounting.
              </p>
            )}
          </div>
        </div>
      </div>
    </div>
  );
};
