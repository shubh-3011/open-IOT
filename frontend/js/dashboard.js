/* ═══════════════════════════════════════════════════════════════════════════
   OPEN IoT – Dashboard Logic
   Populates: stat cards, device table, activity feed, charts, terminal
   ═══════════════════════════════════════════════════════════════════════════ */

// Require auth
if (!requireAuth()) throw new Error('Not authenticated');
loadUserInfo();

// ── Load Dashboard ────────────────────────────────────────────────────────
async function loadDashboard() {
    try {
        const [stats, devices] = await Promise.all([
            api('/api/data/dashboard/stats'),
            api('/api/devices/'),
        ]);

        renderStats(stats);
        renderDevices(devices);
        renderCharts(devices);

        // Update nav badge
        const badge = document.getElementById('nav-device-count');
        if (badge) badge.textContent = devices.length;
    } catch (err) {
        showToast('Failed to load dashboard: ' + err.message, 'error');
    }
}

// ── Render Stats ──────────────────────────────────────────────────────────
function renderStats(stats) {
    document.getElementById('stat-total').textContent = stats.total_devices;
    document.getElementById('stat-online').textContent = stats.online_devices;
    document.getElementById('stat-adopted').textContent = stats.adopted_devices;
    document.getElementById('stat-readings').textContent = stats.readings_24h;

    // Sub text
    const totalSub = document.getElementById('stat-total-sub');
    if (totalSub && stats.total_devices > 0) {
        totalSub.innerHTML = `<span class="up">${stats.total_devices}</span> registered`;
    }

    const onlineSub = document.getElementById('stat-online-sub');
    if (onlineSub && stats.total_devices > 0) {
        const pct = Math.round((stats.online_devices / stats.total_devices) * 100);
        onlineSub.innerHTML = `<span class="up">${pct}%</span> availability`;
    }

    const adoptedSub = document.getElementById('stat-adopted-sub');
    if (adoptedSub) {
        const pending = stats.total_devices - stats.adopted_devices;
        if (pending > 0) {
            adoptedSub.innerHTML = `<span class="down">${pending}</span> pending`;
        } else {
            adoptedSub.textContent = 'all adopted';
        }
    }
}

// ── Render Devices (table rows) ───────────────────────────────────────────
function renderDevices(devices) {
    const list = document.getElementById('device-list');
    const empty = document.getElementById('device-empty');

    if (devices.length === 0) {
        empty.classList.remove('hidden');
        return;
    }

    empty.classList.add('hidden');

    // Remove old device rows (keep header and empty state)
    list.querySelectorAll('.device-row:not(.header)').forEach(r => r.remove());

    devices.forEach(device => {
        const isOnline = device.is_online;
        const statusClass = !device.is_adopted ? 'pending' : (isOnline ? 'online' : 'offline');
        const statusText = !device.is_adopted ? 'Pending' : (isOnline ? 'Online' : 'Offline');
        const icon = getDeviceIcon(device.device_type);

        // Get primary telemetry value
        let telemetry = '—';
        if (device.last_state && typeof device.last_state === 'object') {
            const keys = Object.keys(device.last_state).filter(k => !k.startsWith('_'));
            if (keys.length > 0) {
                const val = device.last_state[keys[0]];
                telemetry = typeof val === 'number' ? val.toFixed(1) : String(val);
            }
        }

        const row = document.createElement('a');
        row.href = `/device/${device.device_id}`;
        row.className = 'device-row';
        row.style.textDecoration = 'none';
        row.style.color = 'inherit';
        row.innerHTML = `
            <div class="device-icon-sm">${icon}</div>
            <div>
                <div class="device-name">${device.name}</div>
                <div class="device-id">${device.device_id}</div>
            </div>
            <div><span class="status-badge ${statusClass}"><span class="status-dot-sm"></span>${statusText}</span></div>
            <div class="telemetry">${telemetry}</div>
            <div class="uptime">${device.device_type.toUpperCase()}</div>
            <div class="toggle-wrap"><div class="toggle ${isOnline ? 'on' : ''}"></div></div>
        `;

        // Insert before empty state
        list.insertBefore(row, empty);
    });
}

// ── Render Charts ─────────────────────────────────────────────────────────
function renderCharts(devices) {
    const chartsRow = document.getElementById('charts-row');

    // Find devices with sensor data
    const withData = devices.filter(d => d.last_state && typeof d.last_state === 'object');

    if (withData.length === 0) return; // Keep default empty chart

    chartsRow.innerHTML = '';

    // Show up to 3 chart panels from first devices with data
    const colors = ['var(--accent)', 'var(--accent2)', 'var(--accent3)'];
    const gradientIds = ['g1', 'g2', 'g3'];
    const hexColors = ['#00d4ff', '#00ff88', '#ff6b35'];
    let chartIdx = 0;

    for (const device of withData) {
        if (chartIdx >= 3) break;
        const state = device.last_state;
        const keys = Object.keys(state).filter(k => !k.startsWith('_'));

        for (const key of keys) {
            if (chartIdx >= 3) break;
            const val = state[key];
            if (typeof val !== 'number') continue;

            const color = hexColors[chartIdx];
            const gId = gradientIds[chartIdx];

            // Generate random sparkline path
            const points = [];
            for (let i = 0; i <= 10; i++) {
                const x = i * 20;
                const y = 10 + Math.random() * 30;
                points.push(`${x},${y.toFixed(0)}`);
            }
            const pathD = 'M' + points.join(' L');
            const fillD = pathD + ' L200,48 L0,48Z';

            const panel = document.createElement('div');
            panel.className = 'chart-panel';
            panel.innerHTML = `
                <div class="chart-title">${key.toUpperCase()} · ${device.name}</div>
                <div class="chart-val" style="color:${color}">${val.toFixed(1)}</div>
                <svg class="sparkline" viewBox="0 0 200 48" preserveAspectRatio="none">
                    <defs>
                        <linearGradient id="${gId}" x1="0" y1="0" x2="0" y2="1">
                            <stop offset="0%" stop-color="${color}" stop-opacity="0.3"/>
                            <stop offset="100%" stop-color="${color}" stop-opacity="0"/>
                        </linearGradient>
                    </defs>
                    <path d="${pathD}" stroke="${color}" stroke-width="1.5" fill="none"/>
                    <path d="${fillD}" fill="url(#${gId})"/>
                </svg>
            `;
            chartsRow.appendChild(panel);
            chartIdx++;
        }
    }
}

// ── Activity Feed ─────────────────────────────────────────────────────────
function addActivity(msg, device, color) {
    const feed = document.getElementById('activity-feed');
    const now = new Date();
    const time = `${now.getHours().toString().padStart(2, '0')}:${now.getMinutes().toString().padStart(2, '0')}:${now.getSeconds().toString().padStart(2, '0')}`;

    const item = document.createElement('div');
    item.className = 'activity-item';
    item.style.animation = 'fadeInUp 0.2s ease';
    item.innerHTML = `
        <div class="activity-time">${time}</div>
        <div class="activity-dot-wrap"><div class="activity-dot" style="background:${color}"></div></div>
        <div class="activity-content">
            <div class="activity-msg">${msg}</div>
            <div class="activity-device">${device}</div>
        </div>
    `;

    feed.insertBefore(item, feed.firstChild);

    // Keep max 50 items
    while (feed.children.length > 50) {
        feed.removeChild(feed.lastChild);
    }
}

function clearActivity() {
    const feed = document.getElementById('activity-feed');
    feed.innerHTML = '';
    addActivity('Activity feed cleared', 'System', 'var(--text-dim)');
}

// ── Terminal ──────────────────────────────────────────────────────────────
function addTerminalLine(text, className) {
    const body = document.getElementById('terminal-body');
    // Remove cursor line
    const cursorLine = body.querySelector('.t-line:last-child');

    const line = document.createElement('div');
    line.className = `t-out ${className || 't-info'}`;
    line.textContent = text;
    body.insertBefore(line, cursorLine);

    // Auto-scroll
    body.scrollTop = body.scrollHeight;
}

// ── WebSocket for live updates ────────────────────────────────────────────
function handleWsMessage(msg) {
    if (msg.type === 'device_update') {
        const data = msg.data;
        const did = msg.device_id;

        // Add to activity feed
        const keys = Object.keys(data).filter(k => !k.startsWith('_'));
        if (keys.length > 0) {
            const summary = keys.slice(0, 3).map(k => {
                const v = data[k];
                return `${k}: ${typeof v === 'number' ? v.toFixed(1) : v}`;
            }).join(', ');
            addActivity(`Telemetry: ${summary}`, did, 'var(--accent)');
        }

        // Add to terminal
        const termData = {};
        keys.forEach(k => termData[k] = data[k]);
        addTerminalLine(`→ [${did}] ${JSON.stringify(termData)}`, 't-info');

        // Refresh dashboard data
        loadDashboard();
    }
}

// ── Init ──────────────────────────────────────────────────────────────────
loadDashboard();
connectWebSocket(handleWsMessage);

// Refresh every 30s
setInterval(loadDashboard, 30000);

// Add initial activity
addActivity('Dashboard loaded', 'System', 'var(--accent2)');
