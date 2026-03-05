import React from 'react';
import { NavLink, Outlet } from 'react-router-dom';
import { Activity, Settings, List, PlayCircle, BarChart2, Wifi, Zap } from 'lucide-react';
import { useDevice } from '../contexts/DeviceContext';

export const Layout: React.FC = () => {
  const { deviceState, simulatorMode, setSimulatorMode, connectionError } = useDevice();

  const navItems = [
    { to: '/', icon: <Activity size={20} />, label: 'Dashboard' },
    { to: '/wizard', icon: <PlayCircle size={20} />, label: 'Balance Wizard' },
    { to: '/diagnostics', icon: <BarChart2 size={20} />, label: 'Diagnostics' },
    { to: '/profiles', icon: <List size={20} />, label: 'Profiles' },
    { to: '/sessions', icon: <Activity size={20} />, label: 'Sessions' },
    { to: '/setup', icon: <Settings size={20} />, label: 'Setup' },
  ];

  return (
    <div className="flex h-screen bg-zinc-950 text-zinc-100 font-sans">
      {/* Sidebar */}
      <aside className="w-64 bg-zinc-900 border-r border-zinc-800 flex flex-col">
        <div className="p-6 flex items-center gap-3 border-b border-zinc-800">
          <div className="w-8 h-8 rounded-full bg-emerald-500 flex items-center justify-center">
            <Zap size={18} className="text-zinc-950" />
          </div>
          <div>
            <h1 className="font-semibold text-lg tracking-tight">Spin Balancer</h1>
            <p className="text-xs text-zinc-500 font-mono">v1.2.0-beta</p>
          </div>
        </div>

        <nav className="flex-1 p-4 space-y-1">
          {navItems.map((item) => (
            <NavLink
              key={item.to}
              to={item.to}
              className={({ isActive }) =>
                `flex items-center gap-3 px-3 py-2.5 rounded-lg transition-colors ${
                  isActive
                    ? 'bg-zinc-800 text-emerald-400 font-medium'
                    : 'text-zinc-400 hover:bg-zinc-800/50 hover:text-zinc-200'
                }`
              }
            >
              {item.icon}
              {item.label}
            </NavLink>
          ))}
        </nav>

        <div className="p-4 border-t border-zinc-800">
          <div className="flex items-center justify-between mb-2">
            <span className="text-xs text-zinc-500 uppercase tracking-wider font-semibold">Status</span>
            <div className="flex items-center gap-1.5">
              <div className={`w-2 h-2 rounded-full ${deviceState.motorState === 1 ? 'bg-emerald-500 animate-pulse' : 'bg-zinc-600'}`} />
              <span className="text-xs font-mono text-zinc-400">{deviceState.motorStateLabel}</span>
            </div>
          </div>
          {connectionError && (
            <div className="mb-3 text-[11px] text-amber-400 bg-amber-500/10 border border-amber-500/20 rounded px-2 py-1">
              <p>{connectionError}</p>
              <p className="mt-1 text-zinc-400">If disconnected, try connecting to the <strong>BalancerSetup</strong> Wi-Fi network and navigating to <strong>192.168.4.1</strong>.</p>
            </div>
          )}
          <label className="flex items-center justify-between cursor-pointer group">
            <span className="text-sm text-zinc-400 group-hover:text-zinc-200 transition-colors">Simulator Mode</span>
            <div className={`relative inline-flex h-5 w-9 items-center rounded-full transition-colors ${simulatorMode ? 'bg-emerald-500' : 'bg-zinc-700'}`}>
              <input
                type="checkbox"
                className="sr-only"
                checked={simulatorMode}
                onChange={(e) => setSimulatorMode(e.target.checked)}
              />
              <span className={`inline-block h-3 w-3 transform rounded-full bg-white transition-transform ${simulatorMode ? 'translate-x-5' : 'translate-x-1'}`} />
            </div>
          </label>
        </div>
      </aside>

      {/* Main Content */}
      <main className="flex-1 overflow-auto relative">
        <Outlet />
      </main>
    </div>
  );
};
