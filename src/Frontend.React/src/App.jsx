import React, { useState, useEffect } from 'react';
import DeviceCard from './components/DeviceCard';

// Mock data for initial render
const MOCK_DEVICES = [
  {
    deviceId: 'device-001',
    lastSeen: new Date().toISOString(),
    latest: {
      sensors: [
        { name: 'moisture1', value: 45 },
        { name: 'battery', value: 88 }
      ]
    }
  },
  {
    deviceId: 'device-002',
    lastSeen: new Date(Date.now() - 120000).toISOString(),
    latest: {
      sensors: [
        { name: 'moisture1', value: 23 },
        { name: 'battery', value: 65 }
      ]
    }
  },
  {
    deviceId: 'device-003',
    lastSeen: new Date().toISOString(),
    latest: {
      sensors: [
        { name: 'moisture1', value: 78 },
        { name: 'battery', value: 92 }
      ]
    }
  }
];

function App() {
  const [devices, setDevices] = useState(MOCK_DEVICES);
  const [isSidebarOpen, setIsSidebarOpen] = useState(false);

  // TODO: Fetch real data from API
  // useEffect(() => {
  //   fetch('http://localhost:7071/api/devices')
  //     .then(res => res.json())
  //     .then(data => setDevices(data));
  // }, []);

  return (
    <div className="flex flex-col md:flex-row min-h-screen bg-bg-dark text-text-primary">
      {/* Mobile Header */}
      <div className="flex items-center justify-between px-4 py-3 bg-bg-card md:hidden">
        <a className="text-xl font-bold text-text-primary" href="/">Frontend.React</a>
        <button
          title="Navigation menu"
          className="text-text-secondary hover:text-white focus:outline-none"
          onClick={() => setIsSidebarOpen(!isSidebarOpen)}
        >
          <svg className="w-6 h-6" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M4 6h16M4 12h16M4 18h16"></path>
          </svg>
        </button>
      </div>

      {/* Sidebar */}
      <div className={`${isSidebarOpen ? 'block' : 'hidden'} md:block w-full md:w-64 md:fixed md:h-full z-10 bg-bg-card border-r border-border`}>
        <div className="hidden md:flex items-center justify-center h-16 border-b border-border">
          <a className="text-xl font-bold text-text-primary" href="/">Frontend.React</a>
        </div>

        <nav className="flex flex-col p-4 space-y-2">
          <a className="flex items-center px-4 py-2 text-text-secondary rounded-lg hover:bg-slate-800 hover:text-white transition-colors bg-slate-800 text-white" href="/">
            <span className="mr-3">📊</span> Dashboard
          </a>
        </nav>
      </div>

      {/* Main Content */}
      <main className="flex-1 md:ml-64 p-4">
        <div className="flex justify-end mb-4 px-4 py-2 bg-bg-card rounded-lg border border-border">
          <a href="https://react.dev" target="_blank" rel="noreferrer" className="text-accent hover:underline">About React</a>
        </div>

        <article className="content px-4">
          <div className="dashboard-container max-w-7xl mx-auto">
            <div className="dashboard-header flex justify-between items-center mb-8">
              <h1 className="text-3xl font-bold text-transparent bg-clip-text bg-gradient-to-r from-white to-slate-400">
                Watering Dashboard
              </h1>
              <div className="controls flex gap-4 bg-bg-card px-6 py-3 rounded-2xl border border-border">
                <button className="bg-accent text-black px-4 py-2 rounded-lg font-semibold hover:opacity-90 transition-all">
                  Refresh
                </button>
              </div>
            </div>

            <div className="device-grid grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-6">
              {devices.map(device => (
                <DeviceCard key={device.deviceId} device={device} />
              ))}
            </div>
          </div>
        </article>
      </main>
    </div>
  );
}

export default App;
