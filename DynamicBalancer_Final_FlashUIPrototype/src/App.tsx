/**
 * @license
 * SPDX-License-Identifier: Apache-2.0
 */

import React from 'react';
import { HashRouter, Routes, Route } from 'react-router-dom';
import { DeviceProvider } from './contexts/DeviceContext';
import { Layout } from './components/Layout';
import { Dashboard } from './pages/Dashboard';
import { Wizard } from './pages/Wizard';
import { Diagnostics } from './pages/Diagnostics';
import { Profiles } from './pages/Profiles';
import { Sessions } from './pages/Sessions';
import { Setup } from './pages/Setup';

export default function App() {
  return (
    <DeviceProvider>
      <HashRouter>
        <Routes>
          <Route path="/" element={<Layout />}>
            <Route index element={<Dashboard />} />
            <Route path="wizard" element={<Wizard />} />
            <Route path="diagnostics" element={<Diagnostics />} />
            <Route path="profiles" element={<Profiles />} />
            <Route path="sessions" element={<Sessions />} />
            <Route path="setup" element={<Setup />} />
          </Route>
        </Routes>
      </HashRouter>
    </DeviceProvider>
  );
}
