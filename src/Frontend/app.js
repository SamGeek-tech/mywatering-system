async function fetchDevices() {
  const api = document.getElementById('apiBase').value;
  const res = await fetch(api + '/devices');
  const devices = await res.json();
  const container = document.getElementById('devices');
  container.innerHTML = '';
  for (const d of devices) {
    const el = document.createElement('div');
    el.className = 'card';
    const moisture = d.latest?.sensors?.find(s => s.name === 'moisture1')?.value ?? 'N/A';
    el.innerHTML = `<div><strong>${d.deviceId}</strong></div><div class="val">${moisture}%</div><div>Last: ${d.lastSeen}</div>`;
    container.appendChild(el);
  }
}

document.getElementById('refresh').addEventListener('click', fetchDevices);
window.onload = fetchDevices;