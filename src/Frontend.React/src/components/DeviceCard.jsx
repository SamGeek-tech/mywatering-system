import React from 'react';

const DeviceCard = ({ device }) => {
    const moisture = device.latest?.sensors?.find(s => s.name === 'moisture1')?.value ?? 'N/A';
    const battery = device.latest?.sensors?.find(s => s.name === 'battery')?.value ?? 'N/A';
    const lastSeen = new Date(device.lastSeen).toLocaleString();
    const isActive = (new Date() - new Date(device.lastSeen)) < 60000; // 1 minute threshold

    return (
        <div className="bg-bg-card border border-border rounded-2xl p-6 transition-all duration-300 hover:-translate-y-1 hover:shadow-lg hover:border-accent relative overflow-hidden group">
            <div className="flex justify-between items-center mb-6">
                <span className="font-semibold text-text-secondary text-sm">{device.deviceId}</span>
                <div className={`w-2 h-2 rounded-full shadow-[0_0_8px] ${isActive ? 'bg-success shadow-success' : 'bg-danger shadow-danger'}`}></div>
            </div>

            <div className="text-center mb-6">
                <span className="block text-xs text-text-secondary mb-1 uppercase tracking-wider">Moisture</span>
                <div className="text-5xl font-extrabold text-text-primary leading-none">
                    {moisture}<small className="text-2xl opacity-60">%</small>
                </div>
            </div>

            <div className="flex justify-around pt-4 border-t border-border">
                <div className="flex items-center gap-2 text-sm text-text-secondary">
                    <span>🔋</span> {battery}%
                </div>
                <div className="flex items-center gap-2 text-sm text-text-secondary">
                    <span>📶</span> -65dBm
                </div>
            </div>

            <div className="mt-4 text-center text-xs text-text-secondary opacity-70">
                Last seen: {lastSeen}
            </div>
        </div>
    );
};

export default DeviceCard;
