import React, { useEffect, useRef, useState } from 'react';
import { useDevice } from '../contexts/DeviceContext';
import { Play, Square, RotateCw, CheckCircle2, ArrowRight, Settings2, AlertTriangle } from 'lucide-react';
import { LedMode } from '../Protocol';

const MASS_MIN_G = 0.001;
const MASS_MAX_G = 10.0;
const NOISE_GATE_WARNING_MS = 60000;

function clamp(v: number, lo: number, hi: number): number {
  return Math.min(hi, Math.max(lo, v));
}

function fmtMass(g: number): string {
  if (g < 0.1) return `${(g * 1000).toFixed(1)} mg`;
  return `${g.toFixed(3)} g`;
}

export const Wizard: React.FC = () => {
  const { deviceState, telemetry, profiles, startTest, stopTest, setLedMode, saveSession, settings, updateSettings } = useDevice();
  const [step, setStep] = useState<number>(1);
  const [selectedProfile, setSelectedProfile] = useState<string>('');
  const [sessionSaved, setSessionSaved] = useState(false);
  const [savingSession, setSavingSession] = useState(false);
  const [actionBusy, setActionBusy] = useState(false);
  const [wizardError, setWizardError] = useState<string | null>(null);
  const [showNoiseGateWarning, setShowNoiseGateWarning] = useState(false);
  const noiseGateStartedAtRef = useRef<number | null>(null);

  const qualityPct = Math.round(telemetry.quality * 100);

  // Mass estimate: vibMag (g-force) is proportional to imbalance mass × radius × ω².
  // correctionMass ≈ vibMag × totalRotorMass / (ω² × correctionRadius) — but we don't know
  // rotor mass. Instead we note that without a calibration run, this is a rough guide only.
  // Simple heuristic: at the correction radius, the centrifugal force per gram is ω²×r.
  // vibMag in g (9.81 m/s²) ≈ imbalanceMass_kg × ω² × r_imbalance / 9.81
  // So correctionMass_g ≈ vibMag × 9.81 / (ω² × r_correction_m) × 1000
  const rpm = telemetry.rpm || 1;
  const correctionRadiusMm = settings.model.correctionRadiusMm || 25;
  const radiusIsDefault = Math.abs(correctionRadiusMm - 25.0) < 1e-6;
  const radiusIsUnusual = correctionRadiusMm < 5 || correctionRadiusMm > 300;
  const omega = (rpm * 2 * Math.PI) / 60;
  const rCorrectionM = correctionRadiusMm / 1000;
  const estCorrectionG = (omega > 1 && rCorrectionM > 0)
    ? clamp((telemetry.vibMag * 9.81) / (omega * omega * rCorrectionM) * 1000, MASS_MIN_G, MASS_MAX_G)
    : 0;
  const estLowG = estCorrectionG * 0.5;
  const estHighG = estCorrectionG * 2.0;
  const selectedProfileDef = profiles.find((p) => p.id === selectedProfile);
  const selectedProfileOffset = selectedProfileDef?.phaseOffsetDeg ?? null;

  useEffect(() => {
    if (!selectedProfile && profiles.length > 0) {
      setSelectedProfile(profiles[0].id);
    }
  }, [profiles, selectedProfile]);

  useEffect(() => {
    if (deviceState.runStep >= 1 && step === 1) {
      setStep(2);
    } else if (deviceState.runStep === 3 && step === 2) {
      setStep(3);
    }
  }, [deviceState.runStep, step]);

  useEffect(() => {
    const noiseTarget = settings.sampling.noiseFloorTarget;
    const inNoiseGatedMeasure = step === 2 && deviceState.runStep === 2 && noiseTarget > 0;
    const stillAboveTarget = telemetry.noiseRms > noiseTarget;

    if (!inNoiseGatedMeasure || !stillAboveTarget) {
      noiseGateStartedAtRef.current = null;
      if (showNoiseGateWarning) {
        setShowNoiseGateWarning(false);
      }
      return;
    }

    if (noiseGateStartedAtRef.current === null) {
      noiseGateStartedAtRef.current = Date.now();
      return;
    }

    if (!showNoiseGateWarning && Date.now() - noiseGateStartedAtRef.current >= NOISE_GATE_WARNING_MS) {
      setShowNoiseGateWarning(true);
    }
  }, [step, deviceState.runStep, settings.sampling.noiseFloorTarget, telemetry.noiseRms, showNoiseGateWarning]);

  const handleStart = async () => {
    if (!selectedProfile) return;
    noiseGateStartedAtRef.current = null;
    setShowNoiseGateWarning(false);
    setWizardError(null);
    try {
      await startTest(selectedProfile);
    } catch (e: any) {
      setWizardError(e?.message || 'Failed to start test');
    }
  };

  const handleStop = async () => {
    setWizardError(null);
    try {
      await stopTest();
    } catch (e: any) {
      setWizardError(e?.message || 'Failed to stop test');
    }
  };

  const handleLedMode = async (mode: LedMode) => {
    setWizardError(null);
    try {
      await setLedMode(mode);
    } catch (e: any) {
      setWizardError(e?.message || `Failed to set LED mode (${mode})`);
    }
  };

  const handleAbort = async () => {
    setActionBusy(true);
    setWizardError(null);
    try {
      await stopTest();
      noiseGateStartedAtRef.current = null;
      setShowNoiseGateWarning(false);
    } catch (e: any) {
      setWizardError(e?.message || 'Failed to abort test');
    } finally {
      setActionBusy(false);
    }
  };

  const handleCancel = async () => {
    setActionBusy(true);
    setWizardError(null);
    try {
      await stopTest();
      setStep(1);
      setSessionSaved(false);
      noiseGateStartedAtRef.current = null;
      setShowNoiseGateWarning(false);
    } catch (e: any) {
      setWizardError(e?.message || 'Failed to cancel wizard');
    } finally {
      setActionBusy(false);
    }
  };

  const handleRetryRun = async () => {
    if (!selectedProfile) return;
    setActionBusy(true);
    setWizardError(null);
    try {
      await stopTest();
      await startTest(selectedProfile);
      setStep(2);
      setSessionSaved(false);
      noiseGateStartedAtRef.current = null;
      setShowNoiseGateWarning(false);
    } catch (e: any) {
      setWizardError(e?.message || 'Failed to retry run');
    } finally {
      setActionBusy(false);
    }
  };

  const handleSetSingleWindow = async () => {
    setActionBusy(true);
    setWizardError(null);
    try {
      await updateSettings({
        sampling: {
          ...settings.sampling,
          noiseFloorTarget: 0,
        },
      });
      setShowNoiseGateWarning(false);
      noiseGateStartedAtRef.current = null;
    } catch (e: any) {
      setWizardError(e?.message || 'Failed to set single-window mode');
    } finally {
      setActionBusy(false);
    }
  };

  return (
    <div className="p-8 max-w-4xl mx-auto space-y-8">
      <header className="border-b border-zinc-800 pb-6">
        <h1 className="text-3xl font-semibold tracking-tight text-zinc-100 mb-2">Guided Balance Wizard</h1>
        <p className="text-zinc-500 font-mono text-sm">Step-by-step balancing process</p>
      </header>

      {wizardError && (
        <div className="text-xs text-red-400 bg-red-500/10 border border-red-500/20 rounded-lg px-3 py-2 flex items-center justify-between">
          <span>{wizardError}</span>
          <button onClick={() => setWizardError(null)} className="text-red-400 hover:text-red-300 ml-2 text-sm font-bold">&times;</button>
        </div>
      )}
      {deviceState.phaseGuidanceStale && deviceState.hasResultSnapshot && (
        <div className="text-xs text-amber-300 bg-amber-500/10 border border-amber-500/20 rounded-lg px-3 py-2">
          Guidance is stale while idle or spinup. Heavy/Add angles shown are from the last completed run.
        </div>
      )}
      {!deviceState.hasResultSnapshot && deviceState.motorStateLabel === 'stopped' && (
        <div className="text-xs text-sky-300 bg-sky-500/10 border border-sky-500/20 rounded-lg px-3 py-2">
          No calibrated result yet. Run a profile test to generate fresh heavy/add guidance.
        </div>
      )}

      <div className="flex items-center justify-between mb-8 relative">
        <div className="absolute left-0 top-1/2 -translate-y-1/2 w-full h-1 bg-zinc-800 -z-10 rounded-full" />
        <div className="absolute left-0 top-1/2 -translate-y-1/2 h-1 bg-emerald-500 -z-10 rounded-full transition-all duration-500" style={{ width: `${((step - 1) / 3) * 100}%` }} />

        {[
          { num: 1, label: 'Setup' },
          { num: 2, label: 'Measure' },
          { num: 3, label: 'Target' },
          { num: 4, label: 'Correct' },
        ].map((s) => (
          <div key={s.num} className="flex flex-col items-center gap-2 bg-zinc-950 px-2">
            <div className={`w-10 h-10 rounded-full flex items-center justify-center font-bold text-lg transition-colors ${step >= s.num ? 'bg-emerald-500 text-zinc-950' : 'bg-zinc-800 text-zinc-500'}`}>
              {step > s.num ? <CheckCircle2 size={24} /> : s.num}
            </div>
            <span className={`text-xs font-semibold uppercase tracking-wider ${step >= s.num ? 'text-emerald-400' : 'text-zinc-500'}`}>{s.label}</span>
          </div>
        ))}
      </div>

      <div className="bg-zinc-900 border border-zinc-800 rounded-3xl p-8 min-h-[400px] flex flex-col">
        <div className="mb-6 flex flex-wrap gap-3 justify-end">
          <button
            onClick={handleAbort}
            disabled={actionBusy || deviceState.motorStateLabel !== 'running'}
            className={`px-4 py-2 rounded-lg font-semibold transition-colors ${
              actionBusy || deviceState.motorStateLabel !== 'running'
                ? 'bg-zinc-800 text-zinc-500 cursor-not-allowed'
                : 'bg-red-500 hover:bg-red-400 text-zinc-950'
            }`}
          >
            ABORT TEST
          </button>
          <button
            onClick={handleRetryRun}
            disabled={actionBusy || !selectedProfile}
            className={`px-4 py-2 rounded-lg font-semibold transition-colors ${
              actionBusy || !selectedProfile
                ? 'bg-zinc-800 text-zinc-500 cursor-not-allowed'
                : 'bg-emerald-500 hover:bg-emerald-400 text-zinc-950'
            }`}
          >
            RETRY RUN
          </button>
          <button
            onClick={handleCancel}
            disabled={actionBusy}
            className={`px-4 py-2 rounded-lg font-semibold transition-colors ${
              actionBusy ? 'bg-zinc-800 text-zinc-500 cursor-not-allowed' : 'bg-zinc-700 hover:bg-zinc-600 text-zinc-100'
            }`}
          >
            CANCEL WIZARD
          </button>
        </div>

        {step === 1 && (
          <div className="flex-1 flex flex-col items-center justify-center text-center space-y-6">
            {selectedProfileOffset !== null && (
              <div className="w-full max-w-lg rounded-xl border border-zinc-700 bg-zinc-950 p-4 text-left">
                <div className="flex items-center gap-2 text-sm font-semibold text-zinc-200 mb-1">
                  <AlertTriangle size={16} />
                  Per-profile phase offset
                </div>
                <p className="text-xs text-zinc-300">
                  Selected profile offset is <strong>{selectedProfileOffset.toFixed(2)}°</strong>. It is applied only during active test-run computation, not at standstill.
                </p>
              </div>
            )}
            <div className="w-20 h-20 bg-zinc-800 rounded-full flex items-center justify-center text-emerald-400 mb-4">
              <Settings2 size={40} />
            </div>
            <h2 className="text-2xl font-medium text-zinc-100">Select Profile and Start</h2>
            <p className="text-zinc-400 max-w-md">Choose a test profile to spin up the wheel and capture imbalance measurements.</p>

            <div className="w-full max-w-xs space-y-4 pt-4">
              <select
                className="w-full bg-zinc-950 border border-zinc-700 rounded-xl px-4 py-3 text-zinc-200 focus:outline-none focus:border-emerald-500 transition-colors"
                value={selectedProfile}
                onChange={(e) => setSelectedProfile(e.target.value)}
              >
                {profiles.length === 0 && <option value="">No profiles loaded</option>}
                {profiles.map((p) => (
                  <option key={p.id} value={p.id}>
                    {p.name} ({p.rpm} RPM)
                  </option>
                ))}
              </select>

              <button
                onClick={handleStart}
                disabled={!selectedProfile}
                className={`w-full font-bold py-4 rounded-xl flex items-center justify-center gap-2 transition-colors shadow-lg ${selectedProfile ? 'bg-emerald-500 hover:bg-emerald-400 text-zinc-950 shadow-emerald-500/20' : 'bg-zinc-800 text-zinc-500 cursor-not-allowed'}`}
              >
                <Play size={20} fill="currentColor" />
                START TEST
              </button>
            </div>
          </div>
        )}

        {step === 2 && (
          <div className="flex-1 flex flex-col items-center justify-center text-center space-y-8">
            <div className="relative">
              <div className="w-32 h-32 rounded-full border-4 border-zinc-800 border-t-emerald-500 animate-spin" />
              <div className="absolute inset-0 flex items-center justify-center flex-col">
                <span className="text-3xl font-mono text-emerald-400">{Math.round(telemetry.rpm)}</span>
                <span className="text-xs text-zinc-500 uppercase tracking-widest font-semibold">RPM</span>
              </div>
            </div>

            <div>
              <h2 className="text-2xl font-medium text-zinc-100 mb-2">Measuring Imbalance...</h2>
              <p className="text-zinc-400">Wait for stabilization and completion of the measurement window.</p>
            </div>

            {showNoiseGateWarning && (
              <div className="w-full max-w-2xl rounded-xl border border-amber-500/40 bg-amber-500/10 p-4 text-left">
                <div className="text-sm font-semibold text-amber-300">Noise floor not low enough to finish yet</div>
                <p className="mt-1 text-sm text-zinc-200">
                  This run has measured for over 60s because current noise is still above target:
                  {' '}
                  <span className="font-mono text-amber-200">{telemetry.noiseRms.toFixed(3)}g</span>
                  {' > '}
                  <span className="font-mono text-amber-200">{settings.sampling.noiseFloorTarget.toFixed(3)}g</span>.
                </p>
                <p className="mt-1 text-xs text-zinc-300">
                  Keep measuring for cleaner data, or switch to single-window mode for deterministic completion.
                </p>
                <div className="mt-3 flex flex-wrap gap-2">
                  <button
                    onClick={handleSetSingleWindow}
                    disabled={actionBusy}
                    className={`rounded-lg px-3 py-2 text-sm font-semibold transition-colors ${
                      actionBusy
                        ? 'cursor-not-allowed bg-zinc-800 text-zinc-500'
                        : 'bg-amber-400 text-zinc-950 hover:bg-amber-300'
                    }`}
                  >
                    Set Noise Target To 0
                  </button>
                  <button
                    onClick={() => setShowNoiseGateWarning(false)}
                    className="rounded-lg bg-zinc-800 px-3 py-2 text-sm font-semibold text-zinc-200 hover:bg-zinc-700"
                  >
                    Dismiss
                  </button>
                </div>
              </div>
            )}

            <div className="grid grid-cols-2 gap-8 w-full max-w-md">
              <div className="bg-zinc-950 rounded-xl p-4 border border-zinc-800">
                <div className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-1">Vibration</div>
                <div className="text-2xl font-mono text-amber-400">{telemetry.vibMag.toFixed(3)}g</div>
              </div>
              <div className="bg-zinc-950 rounded-xl p-4 border border-zinc-800">
                <div className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-1">Quality</div>
                <div className="text-2xl font-mono text-emerald-400">{qualityPct}%</div>
              </div>
            </div>

            <button
              onClick={handleStop}
              className="w-full max-w-xs bg-red-500 hover:bg-red-400 text-zinc-950 font-bold py-4 rounded-xl flex items-center justify-center gap-2 transition-colors shadow-lg shadow-red-500/20"
            >
              <Square size={20} fill="currentColor" />
              STOP MOTOR
            </button>
          </div>
        )}

        {step === 3 && (
          <div className="flex-1 flex flex-col items-center justify-center text-center space-y-8">
            <div className={`w-24 h-24 rounded-full flex items-center justify-center transition-all duration-300 ${telemetry.ledOn ? 'bg-sky-500 shadow-[0_0_40px_rgba(14,165,233,0.6)] text-zinc-950 scale-110' : 'bg-zinc-800 text-zinc-500'}`}>
              <RotateCw size={48} className={telemetry.ledOn ? 'animate-pulse' : ''} />
            </div>

            <div>
              <h2 className="text-3xl font-medium text-zinc-100 mb-4">Rotate Wheel by Hand</h2>
              <p className="text-zinc-400 text-lg max-w-md mx-auto">
                Slowly rotate the wheel until the <strong className="text-sky-400">blue LED turns on</strong>.
              </p>
            </div>

            <div className="flex flex-wrap justify-center gap-4 w-full max-w-2xl">
              <button onClick={() => handleLedMode('add')} className={`px-6 py-3 rounded-xl font-semibold transition-colors ${telemetry.ledMode === 'add' ? 'bg-emerald-500 text-zinc-950' : 'bg-zinc-800 text-zinc-300 hover:bg-zinc-700'}`}>
                Target ADD Weight
              </button>
              <button onClick={() => handleLedMode('remove')} className={`px-6 py-3 rounded-xl font-semibold transition-colors ${telemetry.ledMode === 'remove' ? 'bg-red-500 text-zinc-950' : 'bg-zinc-800 text-zinc-300 hover:bg-zinc-700'}`}>
                Target REMOVE Weight
              </button>
              <button onClick={() => handleLedMode('zero')} className={`px-6 py-3 rounded-xl font-semibold transition-colors ${telemetry.ledMode === 'zero' ? 'bg-sky-500 text-zinc-950' : 'bg-zinc-800 text-zinc-300 hover:bg-zinc-700'}`}>
                Target ZERO Mark
              </button>
            </div>

            <div className="w-full max-w-md bg-zinc-950 rounded-xl p-6 border border-zinc-800 flex items-center justify-between">
              <div className="text-left">
                <div className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-1">Current Phase</div>
                <div className="text-3xl font-mono text-zinc-100">{Math.round(telemetry.phaseDeg)} deg</div>
              </div>
              <div className="text-right">
                <div className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-1">Target Angle</div>
                <div className="text-3xl font-mono text-sky-400">
                  {telemetry.ledMode === 'add'
                    ? Math.round(telemetry.addDeg)
                    : telemetry.ledMode === 'remove'
                      ? Math.round(telemetry.removeDeg || telemetry.heavyDeg)
                      : telemetry.ledMode === 'zero'
                        ? Math.round(settings.model.zeroOffsetDeg)
                        : '--'} deg
                </div>
              </div>
            </div>

            <button
              onClick={() => setStep(4)}
              disabled={!telemetry.ledOn}
              className={`w-full max-w-xs font-bold py-4 rounded-xl flex items-center justify-center gap-2 transition-colors ${telemetry.ledOn ? 'bg-zinc-100 text-zinc-950 hover:bg-white' : 'bg-zinc-800 text-zinc-500 cursor-not-allowed'}`}
            >
              PROCEED TO CORRECTION
              <ArrowRight size={20} />
            </button>
          </div>
        )}

        {step === 4 && (
          <div className="flex-1 flex flex-col items-center justify-center text-center space-y-8">
            <div className="w-20 h-20 bg-emerald-500/20 rounded-full flex items-center justify-center text-emerald-400 mb-2">
              <CheckCircle2 size={40} />
            </div>

            <div>
              <h2 className="text-3xl font-medium text-zinc-100 mb-4">Apply Correction</h2>
              <p className="text-zinc-400 text-lg max-w-md mx-auto">The wheel is positioned at the target angle. Apply the suggested correction.</p>
            </div>

            <div className="grid grid-cols-2 gap-6 w-full max-w-2xl">
              <div className={`bg-zinc-950 rounded-2xl p-6 border-2 transition-colors ${telemetry.ledMode === 'add' ? 'border-emerald-500' : 'border-zinc-800 opacity-50'}`}>
                <div className="text-sm text-emerald-500 uppercase tracking-wider font-bold mb-4">Add Weight At</div>
                <div className="text-4xl font-mono text-zinc-100 mb-2">{Math.round(telemetry.addDeg)} deg</div>
                {estCorrectionG > 0 && (<>
                  <div className="text-sm text-zinc-300">Est. mass: {fmtMass(estCorrectionG)}</div>
                  <div className="text-xs text-zinc-500 mt-1">Try range: {fmtMass(estLowG)} – {fmtMass(estHighG)}</div>
                </>)}
                <div className="text-[10px] text-zinc-600 mt-2">@ {correctionRadiusMm} mm radius, {Math.round(rpm)} RPM</div>
              </div>

              <div className={`bg-zinc-950 rounded-2xl p-6 border-2 transition-colors ${telemetry.ledMode === 'remove' ? 'border-red-500' : 'border-zinc-800 opacity-50'}`}>
                <div className="text-sm text-red-500 uppercase tracking-wider font-bold mb-4">Remove Weight At</div>
                <div className="text-4xl font-mono text-zinc-100 mb-2">{Math.round(telemetry.heavyDeg)} deg</div>
                {estCorrectionG > 0 && (<>
                  <div className="text-sm text-zinc-300">Est. mass: {fmtMass(estCorrectionG)}</div>
                  <div className="text-xs text-zinc-500 mt-1">Try range: {fmtMass(estLowG)} – {fmtMass(estHighG)}</div>
                </>)}
                <div className="text-[10px] text-zinc-600 mt-2">@ {correctionRadiusMm} mm radius, {Math.round(rpm)} RPM</div>
              </div>
            </div>

            <div className="w-full max-w-2xl bg-zinc-950 border border-zinc-800 rounded-xl p-4 text-left">
              <div className="text-xs uppercase tracking-wider text-zinc-500 font-semibold mb-1">Guide</div>
              <div className="text-sm text-zinc-300 space-y-1">
                <p>The <strong>angle</strong> is most important. Start with a small weight, re-test, and iterate.</p>
                <p>Mass estimates assume weight placed at <strong>{correctionRadiusMm} mm</strong> from the axis
                  (change in Setup). They are approximate — use as a starting point only.</p>
                                {selectedProfileOffset !== null && (
                  <p className="text-zinc-400 text-xs mt-2">
                    Profile phase offset in use: <strong>{selectedProfileOffset.toFixed(2)}�</strong>.
                  </p>
                )}
                {radiusIsUnusual && (
                  <p className="text-orange-400 text-xs mt-2">
                    Warning: correction radius is unusual (&lt;5 mm or &gt;300 mm). Double-check this value in Setup.
                  </p>
                )}
                {!radiusIsUnusual && radiusIsDefault && (
                  <p className="text-yellow-300 text-xs mt-2">
                    Warning: correction radius is still at default (25 mm). Confirm this matches your rig.
                  </p>
                )}
              </div>
            </div>

            {!sessionSaved && (
              <button
                onClick={async () => {
                  setSavingSession(true);
                  setWizardError(null);
                  try {
                    const profile = profiles.find((p) => p.id === selectedProfile);
                    await saveSession(
                      `${profile?.name || selectedProfile} run`,
                      `Heavy ${Math.round(telemetry.heavyDeg)}°, Add ${Math.round(telemetry.addDeg)}°, VibMag ${telemetry.vibMag.toFixed(3)}g, EstMass ${fmtMass(estCorrectionG)} @${correctionRadiusMm}mm, Quality ${qualityPct}%`,
                    );
                    setSessionSaved(true);
                  } catch (e: any) {
                    setWizardError(e?.message || 'Failed to save session');
                  } finally {
                    setSavingSession(false);
                  }
                }}
                disabled={savingSession}
                className="w-full max-w-md bg-sky-500 hover:bg-sky-400 text-zinc-950 font-bold py-4 rounded-xl flex items-center justify-center gap-2 transition-colors shadow-lg shadow-sky-500/20"
              >
                {savingSession ? 'Saving...' : 'SAVE SESSION'}
              </button>
            )}
            {sessionSaved && (
              <p className="text-sm text-emerald-400 font-semibold">Session saved to device.</p>
            )}

            <div className="flex gap-4 w-full max-w-md pt-4">
              <button
                onClick={() => {
                  handleCancel().catch(() => undefined);
                }}
                className="flex-1 bg-zinc-800 hover:bg-zinc-700 text-zinc-200 font-bold py-4 rounded-xl transition-colors"
              >
                FINISH
              </button>
              <button
                onClick={() => {
                  handleRetryRun().catch(() => undefined);
                }}
                className="flex-1 bg-emerald-500 hover:bg-emerald-400 text-zinc-950 font-bold py-4 rounded-xl transition-colors shadow-lg shadow-emerald-500/20"
              >
                RE-TEST
              </button>
            </div>
          </div>
        )}
      </div>
    </div>
  );
};

