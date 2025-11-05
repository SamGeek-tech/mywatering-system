let connection;

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
    el.onclick = () => showDetail(d.deviceId);
    container.appendChild(el);
  }
}

async function showDetail(deviceId) {
  document.getElementById('detail').style.display = 'block';
  document.getElementById('detail-title').innerText = deviceId;
  await loadLatest(deviceId);
  await loadOtaStatus(deviceId);
  subscribeSignalR(deviceId);
  document.getElementById('ota-send').onclick = () => sendOta(deviceId);
  document.getElementById('ota-refresh').onclick = () => loadOtaStatus(deviceId);
  document.getElementById('close-detail').onclick = () => { document.getElementById('detail').style.display = 'none'; if (connection) connection.stop(); };
}

async function loadLatest(deviceId) {
  const api = document.getElementById('apiBase').value;
  const res = await fetch(api + `/devices/${deviceId}/latest`);
  const d = await res.json();
  document.getElementById('detail-latest').innerText = JSON.stringify(d.latest ?? d, null, 2);
}

async function loadOtaStatus(deviceId) {
  const api = document.getElementById('apiBase').value;
  const res = await fetch(api + `/devices/${deviceId}/ota/status`);
  if (res.status === 200) {
    const s = await res.json();
    document.getElementById('ota-status').innerText = `${s.state} (${s.version}) at ${s.timestamp}`;
  } else {
    document.getElementById('ota-status').innerText = 'n/a';
  }
}

async function sendOta(deviceId) {
  const api = document.getElementById('apiBase').value;
  const version = document.getElementById('ota-version').value;
  const url = document.getElementById('ota-url').value;
  const checksum = document.getElementById('ota-checksum').value;
  const res = await fetch(api + `/devices/${deviceId}/ota`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ version, url, checksum })
  });
  if (res.ok) {
    alert('OTA command sent');
  } else {
    alert('OTA failed: ' + await res.text());
  }
}

function subscribeSignalR(deviceId) {
  if (connection) {
    connection.stop();
  }
  const hubUrl = (document.getElementById('apiBase').value.replace('/api','')) + '/telemetryHub';
  connection = new signalR.HubConnectionBuilder()
    .withUrl(hubUrl)
    .configureLogging(signalR.LogLevel.Information)
    .build();

  connection.on('otaStatus', (status) => {
    if (status) {
      document.getElementById('ota-status').innerText = `${status.state} (${status.version}) at ${status.timestamp}`;
    }
  });

  connection.on('telemetryReceived', (payload) => {
    console.log('telemetry', payload);
    // Optionally update latest
    document.getElementById('detail-latest').innerText = JSON.stringify(payload, null, 2);
  });

  connection.start().then(() => {
    connection.invoke('JoinDeviceGroup', deviceId).catch(err => console.error(err.toString()));
  }).catch(err => console.error(err.toString()));
}


document.getElementById('refresh').addEventListener('click', fetchDevices);
window.onload = fetchDevices;