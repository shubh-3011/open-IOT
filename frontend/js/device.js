/* ═══════════════════════════════════════════════════════════════════════════
   OPEN IoT – Device Detail Logic
   ═══════════════════════════════════════════════════════════════════════════ */

// Require auth
if (!requireAuth()) throw new Error('Not authenticated');
loadUserInfo();

// Get device ID from URL path: /device/{device_id}
const pathParts = window.location.pathname.split('/');
const DEVICE_ID = pathParts[pathParts.length - 1];

let currentDevice = null;

// ── Load Device ───────────────────────────────────────────────────────────
async function loadDevice() {
    try {
        const device = await api(`/api/devices/${DEVICE_ID}`);
        currentDevice = device;
        renderDevice(device);
    } catch (err) {
        showToast('Device not found: ' + err.message, 'error');
    }
}

// ── Render Device ─────────────────────────────────────────────────────────
function renderDevice(device) {
    document.title = `Open IoT – ${device.name}`;
    document.getElementById('device-title').textContent = device.name;
    document.getElementById('device-icon').textContent = getDeviceIcon(device.device_type);
    document.getElementById('device-type-label').textContent = device.device_type.toUpperCase();

    // Status
    const isOnline = device.is_online;
    const adopted = device.is_adopted;
    const dot = document.getElementById('status-dot');
    const text = document.getElementById('status-text');

    if (!adopted) {
        dot.className = 'status-dot pending';
        text.textContent = 'Pending Adoption';
        text.style.color = 'var(--warning)';
    } else if (isOnline) {
        dot.className = 'status-dot online';
        text.textContent = 'Online';
        text.style.color = 'var(--online)';
    } else {
        dot.className = 'status-dot offline';
        text.textContent = 'Offline';
        text.style.color = 'var(--offline)';
    }

    // Device info
    document.getElementById('info-device-id').textContent = device.device_id;
    document.getElementById('info-firmware').textContent = device.firmware_version || '–';
    document.getElementById('info-ip').textContent = device.ip_address || '–';
    document.getElementById('info-mac').textContent = device.mac_address || '–';
    document.getElementById('info-chip').textContent = device.chip_model || '–';
    document.getElementById('info-last-seen').textContent = device.last_seen
        ? new Date(device.last_seen).toLocaleString()
        : '–';
    document.getElementById('info-created').textContent = device.created_at
        ? new Date(device.created_at).toLocaleString()
        : '–';

    // Sensor data
    renderSensors(device.last_state);
}

// ── Render Sensors ────────────────────────────────────────────────────────
function renderSensors(state) {
    const grid = document.getElementById('sensor-grid');
    const empty = document.getElementById('sensor-empty');

    if (!state || typeof state !== 'object') {
        if (empty) empty.classList.remove('hidden');
        return;
    }

    const sensorKeys = Object.keys(state).filter(k => !k.startsWith('_'));
    if (sensorKeys.length === 0) {
        if (empty) empty.classList.remove('hidden');
        return;
    }

    if (empty) empty.classList.add('hidden');
    grid.innerHTML = '';

    const sensorIcons = {
        temperature: '🌡️',
        humidity: '💧',
        pressure: '🔵',
        light: '☀️',
        motion: '🏃',
        door: '🚪',
        voltage: '⚡',
        current: '🔌',
        power: '💡',
        co2: '🌿',
        pm25: '🌫️',
        soil_moisture: '🌱',
        water_level: '🌊',
        gas: '🔥',
        distance: '📏',
    };

    const sensorUnits = {
        temperature: '°C',
        humidity: '%',
        pressure: 'hPa',
        light: 'lux',
        voltage: 'V',
        current: 'A',
        power: 'W',
        co2: 'ppm',
        pm25: 'µg/m³',
        soil_moisture: '%',
        water_level: 'cm',
        distance: 'cm',
    };

    sensorKeys.forEach(key => {
        const val = state[key];
        if (val === null || val === undefined) return;

        const icon = sensorIcons[key] || '📊';
        const unit = sensorUnits[key] || '';
        const displayVal = typeof val === 'number' ? val.toFixed(1) : val;

        const card = document.createElement('div');
        card.className = 'sensor-card';
        card.innerHTML = `
      <div class="sensor-label">${icon} ${key.replace(/_/g, ' ')}</div>
      <div class="sensor-value">${displayVal}</div>
      <div class="sensor-unit">${unit}</div>
    `;
        grid.appendChild(card);
    });
}

// ── Send Test Command ─────────────────────────────────────────────────────
async function sendTestCommand() {
    try {
        await api(`/api/devices/${DEVICE_ID}/command`, {
            method: 'POST',
            body: JSON.stringify({
                command: 'ping',
                params: { timestamp: Date.now() },
            }),
        });
        showToast('Command sent!', 'success');
    } catch (err) {
        showToast('Failed: ' + err.message, 'error');
    }
}

// ── Delete Device ─────────────────────────────────────────────────────────
async function deleteDevice() {
    if (!confirm('Are you sure you want to delete this device? This cannot be undone.')) {
        return;
    }

    try {
        await api(`/api/devices/${DEVICE_ID}`, { method: 'DELETE' });
        showToast('Device deleted', 'success');
        setTimeout(() => { window.location.href = '/dashboard'; }, 1000);
    } catch (err) {
        showToast('Failed: ' + err.message, 'error');
    }
}

// ── WebSocket for live updates ────────────────────────────────────────────
function handleWsMessage(msg) {
    if (msg.type === 'device_update' && msg.device_id === DEVICE_ID) {
        renderSensors(msg.data);
    }
}

// ── Init ──────────────────────────────────────────────────────────────────
loadDevice();
connectWebSocket(handleWsMessage);

// Refresh every 15s
setInterval(loadDevice, 15000);
