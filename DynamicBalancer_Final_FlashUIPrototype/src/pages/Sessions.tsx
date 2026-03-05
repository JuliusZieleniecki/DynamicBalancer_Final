import React, { useState, useEffect } from 'react';
import { useDevice } from '../contexts/DeviceContext';
import { Download, RefreshCw, FileJson, Activity } from 'lucide-react';
import { SessionDetail, SessionSummary } from '../Protocol';

export const Sessions: React.FC = () => {
  const { sessions, refreshSessions, loadSession, saveSession } = useDevice();
  const [selectedSession, setSelectedSession] = useState<SessionSummary | null>(null);
  const [selectedDetail, setSelectedDetail] = useState<SessionDetail | null>(null);
  const [loadingDetail, setLoadingDetail] = useState(false);

  useEffect(() => {
    refreshSessions().catch(() => undefined);
  }, [refreshSessions]);

  useEffect(() => {
    if (!selectedSession) return;
    setLoadingDetail(true);
    loadSession(selectedSession.id)
      .then(setSelectedDetail)
      .finally(() => setLoadingDetail(false));
  }, [selectedSession, loadSession]);

  const handleExportJson = (session: SessionDetail) => {
    const dataStr = `data:text/json;charset=utf-8,${encodeURIComponent(JSON.stringify(session, null, 2))}`;
    const anchor = document.createElement('a');
    anchor.setAttribute('href', dataStr);
    anchor.setAttribute('download', `${session.name.replace(/\s+/g, '_')}_${session.id}.json`);
    document.body.appendChild(anchor);
    anchor.click();
    anchor.remove();
  };

  const handleSaveSnapshot = async () => {
    await saveSession('UI Snapshot', 'Saved from Sessions page');
    await refreshSessions();
  };

  return (
    <div className="p-8 max-w-6xl mx-auto space-y-8 h-full flex flex-col">
      <header className="flex justify-between items-end border-b border-zinc-800 pb-6 shrink-0">
        <div>
          <h1 className="text-3xl font-semibold tracking-tight text-zinc-100 mb-2">Balancing Sessions</h1>
          <p className="text-zinc-500 font-mono text-sm">Review and export saved run data</p>
        </div>
        <div className="flex gap-3">
          <button onClick={() => refreshSessions()} className="bg-zinc-800 hover:bg-zinc-700 text-zinc-200 font-semibold px-4 py-2 rounded-lg flex items-center gap-2 transition-colors">
            <RefreshCw size={18} />
            Refresh
          </button>
          <button onClick={handleSaveSnapshot} className="bg-emerald-500 hover:bg-emerald-400 text-zinc-950 font-semibold px-4 py-2 rounded-lg flex items-center gap-2 transition-colors">
            <Download size={18} />
            Save Snapshot
          </button>
        </div>
      </header>

      <div className="flex-1 grid grid-cols-1 lg:grid-cols-12 gap-8 min-h-0">
        <div className="lg:col-span-4 bg-zinc-900 border border-zinc-800 rounded-2xl overflow-hidden flex flex-col">
          <div className="p-4 border-b border-zinc-800 bg-zinc-950">
            <h3 className="text-sm font-medium text-zinc-400 uppercase tracking-wider">Saved Sessions</h3>
          </div>
          <div className="flex-1 overflow-y-auto p-2 space-y-2">
            {sessions.length === 0 ? (
              <div className="p-8 text-center text-zinc-500">No sessions saved yet.</div>
            ) : (
              sessions.map((session) => (
                <button
                  key={session.id}
                  onClick={() => setSelectedSession(session)}
                  className={`w-full text-left p-4 rounded-xl transition-colors ${selectedSession?.id === session.id ? 'bg-zinc-800 border-emerald-500/50 border' : 'bg-zinc-950 border border-transparent hover:bg-zinc-800/50'}`}
                >
                  <div className="flex justify-between items-start mb-2">
                    <h4 className="font-medium text-zinc-200">{session.name}</h4>
                    <span className="text-xs font-mono text-zinc-500">{new Date(session.timestamp).toLocaleDateString()}</span>
                  </div>
                  <p className="text-xs text-zinc-500 font-mono">{session.id}</p>
                </button>
              ))
            )}
          </div>
        </div>

        <div className="lg:col-span-8 bg-zinc-900 border border-zinc-800 rounded-2xl overflow-hidden flex flex-col">
          {!selectedSession ? (
            <div className="flex-1 flex flex-col items-center justify-center text-zinc-500">
              <Activity size={48} className="mb-4 opacity-20" />
              <p>Select a session to view details</p>
            </div>
          ) : loadingDetail ? (
            <div className="flex-1 flex items-center justify-center text-zinc-500">Loading session details...</div>
          ) : selectedDetail ? (
            <>
              <div className="p-6 border-b border-zinc-800 flex justify-between items-start bg-zinc-950">
                <div>
                  <h2 className="text-2xl font-semibold text-zinc-100 mb-2">{selectedDetail.name}</h2>
                  <p className="text-zinc-400">{selectedDetail.notes || 'No notes'}</p>
                  <div className="text-xs font-mono text-zinc-500 mt-2">{new Date(selectedDetail.timestamp).toLocaleString()}</div>
                </div>
                <button onClick={() => handleExportJson(selectedDetail)} className="p-2 bg-zinc-800 hover:bg-zinc-700 text-zinc-300 rounded-lg transition-colors" title="Export JSON">
                  <FileJson size={18} />
                </button>
              </div>

              <div className="flex-1 overflow-y-auto p-6 space-y-6">
                <div className="bg-zinc-950 border border-zinc-800 rounded-xl p-4">
                  <div className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-2">Profile Name</div>
                  <div className="text-lg text-zinc-200">{selectedDetail.profileName}</div>
                </div>

                <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
                  <div className="bg-zinc-950 border border-zinc-800 rounded-xl p-4">
                    <div className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-1">RPM</div>
                    <div className="text-xl font-mono text-zinc-100">{Math.round(selectedDetail.telemetry.rpm)}</div>
                  </div>
                  <div className="bg-zinc-950 border border-zinc-800 rounded-xl p-4">
                    <div className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-1">Vibration</div>
                    <div className="text-xl font-mono text-zinc-100">{selectedDetail.telemetry.vibMag.toFixed(3)}g</div>
                  </div>
                  <div className="bg-zinc-950 border border-zinc-800 rounded-xl p-4">
                    <div className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-1">Quality</div>
                    <div className="text-xl font-mono text-zinc-100">{Math.round(selectedDetail.telemetry.quality * 100)}%</div>
                  </div>
                  <div className="bg-zinc-950 border border-zinc-800 rounded-xl p-4">
                    <div className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-1">Noise RMS</div>
                    <div className="text-xl font-mono text-zinc-100">{selectedDetail.telemetry.noiseRms.toFixed(3)}</div>
                  </div>
                </div>

                <div className="grid grid-cols-3 gap-4">
                  <div className="bg-zinc-950 border border-zinc-800 rounded-xl p-4">
                    <div className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-1">Heavy</div>
                    <div className="text-xl font-mono text-red-400">{Math.round(selectedDetail.telemetry.heavyDeg)}°</div>
                  </div>
                  <div className="bg-zinc-950 border border-zinc-800 rounded-xl p-4">
                    <div className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-1">Add</div>
                    <div className="text-xl font-mono text-emerald-400">{Math.round(selectedDetail.telemetry.addDeg)}°</div>
                  </div>
                  <div className="bg-zinc-950 border border-zinc-800 rounded-xl p-4">
                    <div className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-1">Remove</div>
                    <div className="text-xl font-mono text-amber-400">{Math.round(selectedDetail.telemetry.removeDeg)}°</div>
                  </div>
                </div>
              </div>
            </>
          ) : null}
        </div>
      </div>
    </div>
  );
};
