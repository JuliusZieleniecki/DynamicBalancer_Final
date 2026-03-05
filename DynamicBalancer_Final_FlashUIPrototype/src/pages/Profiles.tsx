import React, { useState, useEffect } from 'react';
import { useDevice } from '../contexts/DeviceContext';
import { Plus, Edit2, Trash2, Save, X } from 'lucide-react';
import { Profile } from '../Protocol';

const emptyProfile: Profile = {
  id: '',
  name: '',
  rpm: 2500,
  spinupMs: 2500,
  dwellMs: 3500,
  repeats: 1,
  phaseOffsetDeg: 0,
};

export const Profiles: React.FC = () => {
  const { profiles, refreshProfiles, createProfile, updateProfile, deleteProfile } = useDevice();
  const [editingId, setEditingId] = useState<string | null>(null);
  const [editForm, setEditForm] = useState<Partial<Profile>>({});
  const [showCreate, setShowCreate] = useState(false);
  const [createForm, setCreateForm] = useState<Profile>(emptyProfile);
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    refreshProfiles().catch(() => undefined);
  }, [refreshProfiles]);

  const handleEdit = (profile: Profile) => {
    setEditingId(profile.id);
    setEditForm(profile);
  };

  const handleSave = async () => {
    if (!editingId) return;
    setBusy(true);
    try {
      await updateProfile(editingId, editForm);
      setEditingId(null);
    } finally {
      setBusy(false);
    }
  };

  const handleCreate = async () => {
    if (!createForm.id.trim()) return;
    setBusy(true);
    try {
      await createProfile({
        ...createForm,
        id: createForm.id.trim(),
        name: createForm.name.trim() || createForm.id.trim(),
      });
      setCreateForm(emptyProfile);
      setShowCreate(false);
    } finally {
      setBusy(false);
    }
  };

  const handleDelete = async (id: string) => {
    setBusy(true);
    try {
      await deleteProfile(id);
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="p-8 max-w-6xl mx-auto space-y-8">
      <header className="flex justify-between items-end border-b border-zinc-800 pb-6">
        <div>
          <h1 className="text-3xl font-semibold tracking-tight text-zinc-100 mb-2">Test Profiles</h1>
          <p className="text-zinc-500 font-mono text-sm">Manage target RPM and run timing</p>
        </div>
        <button onClick={() => setShowCreate((v) => !v)} className="bg-emerald-500 hover:bg-emerald-400 text-zinc-950 font-semibold px-4 py-2 rounded-lg flex items-center gap-2 transition-colors">
          <Plus size={18} />
          New Profile
        </button>
      </header>

      {showCreate && (
        <div className="bg-zinc-900 border border-zinc-800 rounded-2xl p-6 space-y-4">
          <h3 className="text-lg text-zinc-100 font-medium">Create Profile</h3>
          <div className="grid grid-cols-1 md:grid-cols-4 gap-4">
            <input
              type="text"
              placeholder="ID (e.g. 2500)"
              value={createForm.id}
              onChange={(e) => setCreateForm({ ...createForm, id: e.target.value })}
              className="bg-zinc-950 border border-zinc-700 rounded-lg px-3 py-2 text-zinc-200"
            />
            <input
              type="text"
              placeholder="Name"
              value={createForm.name}
              onChange={(e) => setCreateForm({ ...createForm, name: e.target.value })}
              className="bg-zinc-950 border border-zinc-700 rounded-lg px-3 py-2 text-zinc-200"
            />
            <input
              type="number"
              placeholder="RPM"
              value={createForm.rpm}
              onChange={(e) => setCreateForm({ ...createForm, rpm: parseInt(e.target.value, 10) || 2500 })}
              className="bg-zinc-950 border border-zinc-700 rounded-lg px-3 py-2 text-zinc-200"
            />
            <input
              type="number"
              placeholder="Spinup (ms)"
              value={createForm.spinupMs}
              onChange={(e) => setCreateForm({ ...createForm, spinupMs: parseInt(e.target.value, 10) || 2500 })}
              className="bg-zinc-950 border border-zinc-700 rounded-lg px-3 py-2 text-zinc-200"
            />
            <input
              type="number"
              placeholder="Dwell (ms)"
              value={createForm.dwellMs}
              onChange={(e) => setCreateForm({ ...createForm, dwellMs: parseInt(e.target.value, 10) || 3500 })}
              className="bg-zinc-950 border border-zinc-700 rounded-lg px-3 py-2 text-zinc-200"
            />
            <input
              type="number"
              placeholder="Repeats"
              value={createForm.repeats}
              onChange={(e) => setCreateForm({ ...createForm, repeats: parseInt(e.target.value, 10) || 1 })}
              className="bg-zinc-950 border border-zinc-700 rounded-lg px-3 py-2 text-zinc-200"
            />
            <input
              type="number"
              placeholder="Phase Offset (deg)"
              min="-180"
              max="180"
              value={createForm.phaseOffsetDeg}
              onChange={(e) => setCreateForm({ ...createForm, phaseOffsetDeg: parseFloat(e.target.value) || 0 })}
              className="bg-zinc-950 border border-zinc-700 rounded-lg px-3 py-2 text-zinc-200"
            />
          </div>
          <div className="flex gap-3">
            <button onClick={handleCreate} disabled={busy} className="bg-emerald-500 hover:bg-emerald-400 text-zinc-950 font-semibold px-4 py-2 rounded-lg">
              Save
            </button>
            <button
              onClick={() => {
                setShowCreate(false);
                setCreateForm(emptyProfile);
              }}
              className="bg-zinc-800 hover:bg-zinc-700 text-zinc-200 font-semibold px-4 py-2 rounded-lg"
            >
              Cancel
            </button>
          </div>
        </div>
      )}

      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-6">
        {profiles.map((profile) => (
          <div key={profile.id} className="bg-zinc-900 border border-zinc-800 rounded-2xl p-6 relative group">
            {editingId === profile.id ? (
              <div className="space-y-4">
                <div>
                  <label className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-1 block">Name</label>
                  <input
                    type="text"
                    value={editForm.name || ''}
                    onChange={(e) => setEditForm({ ...editForm, name: e.target.value })}
                    className="w-full bg-zinc-950 border border-zinc-700 rounded-lg px-3 py-2 text-zinc-200 focus:outline-none focus:border-emerald-500"
                  />
                </div>
                <div className="grid grid-cols-2 gap-4">
                  <div>
                    <label className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-1 block">RPM</label>
                    <input
                      type="number"
                      value={editForm.rpm || 0}
                      onChange={(e) => setEditForm({ ...editForm, rpm: parseInt(e.target.value, 10) || 0 })}
                      className="w-full bg-zinc-950 border border-zinc-700 rounded-lg px-3 py-2 text-zinc-200 focus:outline-none focus:border-emerald-500"
                    />
                  </div>
                  <div>
                    <label className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-1 block">Repeats</label>
                    <input
                      type="number"
                      value={editForm.repeats || 1}
                      onChange={(e) => setEditForm({ ...editForm, repeats: parseInt(e.target.value, 10) || 1 })}
                      className="w-full bg-zinc-950 border border-zinc-700 rounded-lg px-3 py-2 text-zinc-200 focus:outline-none focus:border-emerald-500"
                    />
                  </div>
                </div>
                <div>
                  <label className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-1 block">Phase Offset (deg)</label>
                  <input
                    type="number"
                    min="-180"
                    max="180"
                    value={editForm.phaseOffsetDeg || 0}
                    onChange={(e) => setEditForm({ ...editForm, phaseOffsetDeg: parseFloat(e.target.value) || 0 })}
                    className="w-full bg-zinc-950 border border-zinc-700 rounded-lg px-3 py-2 text-zinc-200 focus:outline-none focus:border-emerald-500"
                  />
                </div>
                <div className="grid grid-cols-2 gap-4">
                  <div>
                    <label className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-1 block">Spinup (ms)</label>
                    <input
                      type="number"
                      value={editForm.spinupMs || 0}
                      onChange={(e) => setEditForm({ ...editForm, spinupMs: parseInt(e.target.value, 10) || 0 })}
                      className="w-full bg-zinc-950 border border-zinc-700 rounded-lg px-3 py-2 text-zinc-200 focus:outline-none focus:border-emerald-500"
                    />
                  </div>
                  <div>
                    <label className="text-xs text-zinc-500 uppercase tracking-wider font-semibold mb-1 block">Dwell (ms)</label>
                    <input
                      type="number"
                      value={editForm.dwellMs || 0}
                      onChange={(e) => setEditForm({ ...editForm, dwellMs: parseInt(e.target.value, 10) || 0 })}
                      className="w-full bg-zinc-950 border border-zinc-700 rounded-lg px-3 py-2 text-zinc-200 focus:outline-none focus:border-emerald-500"
                    />
                  </div>
                </div>
                <div className="flex justify-end gap-2 pt-4">
                  <button onClick={() => setEditingId(null)} className="p-2 text-zinc-400 hover:text-zinc-200 transition-colors">
                    <X size={18} />
                  </button>
                  <button onClick={handleSave} disabled={busy} className="p-2 text-emerald-500 hover:text-emerald-400 transition-colors">
                    <Save size={18} />
                  </button>
                </div>
              </div>
            ) : (
              <>
                <div className="absolute top-4 right-4 opacity-0 group-hover:opacity-100 transition-opacity flex gap-2">
                  <button onClick={() => handleEdit(profile)} className="p-2 text-zinc-400 hover:text-sky-400 transition-colors bg-zinc-800 rounded-lg">
                    <Edit2 size={16} />
                  </button>
                  <button onClick={() => handleDelete(profile.id)} disabled={busy} className="p-2 text-zinc-400 hover:text-red-400 transition-colors bg-zinc-800 rounded-lg">
                    <Trash2 size={16} />
                  </button>
                </div>

                <h3 className="text-xl font-medium text-zinc-100 mb-2">{profile.name}</h3>
                <p className="text-xs text-zinc-500 font-mono mb-4">ID: {profile.id}</p>

                <div className="space-y-3">
                  <div className="flex justify-between items-center border-b border-zinc-800 pb-2">
                    <span className="text-sm text-zinc-500 uppercase tracking-wider font-semibold">Target RPM</span>
                    <span className="text-lg font-mono text-zinc-200">{profile.rpm}</span>
                  </div>
                  <div className="flex justify-between items-center border-b border-zinc-800 pb-2">
                    <span className="text-sm text-zinc-500 uppercase tracking-wider font-semibold">Spinup</span>
                    <span className="text-lg font-mono text-zinc-200">{profile.spinupMs}ms</span>
                  </div>
                  <div className="flex justify-between items-center border-b border-zinc-800 pb-2">
                    <span className="text-sm text-zinc-500 uppercase tracking-wider font-semibold">Dwell</span>
                    <span className="text-lg font-mono text-zinc-200">{profile.dwellMs}ms</span>
                  </div>
                  <div className="flex justify-between items-center pb-2">
                    <span className="text-sm text-zinc-500 uppercase tracking-wider font-semibold">Repeats</span>
                    <span className="text-lg font-mono text-zinc-200">{profile.repeats}</span>
                  </div>
                  <div className="flex justify-between items-center pb-2">
                    <span className="text-sm text-zinc-500 uppercase tracking-wider font-semibold">Phase Offset</span>
                    <span className="text-lg font-mono text-zinc-200">{profile.phaseOffsetDeg.toFixed(2)}°</span>
                  </div>
                </div>
              </>
            )}
          </div>
        ))}
      </div>
    </div>
  );
};
