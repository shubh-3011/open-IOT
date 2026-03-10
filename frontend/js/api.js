/* ═══════════════════════════════════════════════════════════════════════════
   OPEN IoT – API Client
   Handles auth tokens, API calls, toasts, and shared utilities
   ═══════════════════════════════════════════════════════════════════════════ */

const API_BASE = '';  // Same origin

// ── Token management ──────────────────────────────────────────────────────
function getToken() {
    return localStorage.getItem('openiot_token');
}

function setToken(token) {
    localStorage.setItem('openiot_token', token);
}

function removeToken() {
    localStorage.removeItem('openiot_token');
    localStorage.removeItem('openiot_user');
}

function getUser() {
    try {
        return JSON.parse(localStorage.getItem('openiot_user'));
    } catch { return null; }
}

function setUser(user) {
    localStorage.setItem('openiot_user', JSON.stringify(user));
}

function requireAuth() {
    if (!getToken()) {
        window.location.href = '/';
        return false;
    }
    return true;
}

function logout() {
    removeToken();
    window.location.href = '/';
}

// ── API fetch wrapper ─────────────────────────────────────────────────────
async function api(path, options = {}) {
    const token = getToken();
    const headers = {
        'Content-Type': 'application/json',
        ...(options.headers || {}),
    };
    if (token) {
        headers['Authorization'] = `Bearer ${token}`;
    }

    const res = await fetch(`${API_BASE}${path}`, {
        ...options,
        headers,
    });

    if (res.status === 401) {
        removeToken();
        window.location.href = '/';
        throw new Error('Session expired');
    }

    let data;
    const text = await res.text();
    try {
        data = JSON.parse(text);
    } catch {
        throw new Error(text || `Server error (${res.status})`);
    }

    if (!res.ok) {
        throw new Error(data.detail || `API error (${res.status})`);
    }
    return data;
}

// ── Form-encoded POST (for OAuth2 login) ──────────────────────────────────
async function apiForm(path, formData) {
    const res = await fetch(`${API_BASE}${path}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams(formData),
    });

    let data;
    const text = await res.text();
    try {
        data = JSON.parse(text);
    } catch {
        throw new Error(text || `Server error (${res.status})`);
    }

    if (!res.ok) {
        throw new Error(data.detail || `API error (${res.status})`);
    }
    return data;
}

// ── Toast notifications ───────────────────────────────────────────────────
function showToast(message, type = 'info') {
    const container = document.getElementById('toast-container');
    if (!container) return;

    const icons = { success: '✅', error: '❌', info: 'ℹ️', warning: '⚠️' };
    const toast = document.createElement('div');
    toast.className = `toast ${type}`;
    toast.innerHTML = `<span>${icons[type] || ''}</span><span>${message}</span>`;
    container.appendChild(toast);

    setTimeout(() => {
        toast.style.opacity = '0';
        toast.style.transform = 'translateX(100px)';
        toast.style.transition = 'all 0.3s ease';
        setTimeout(() => toast.remove(), 300);
    }, 4000);
}

// ── Sidebar toggle (mobile) ──────────────────────────────────────────────
function toggleSidebar() {
    document.getElementById('sidebar')?.classList.toggle('open');
}

// ── Load user info into sidebar ──────────────────────────────────────────
function loadUserInfo() {
    const user = getUser();
    if (!user) return;

    const avatarEl = document.getElementById('user-avatar');
    const nameEl = document.getElementById('user-name');
    const emailEl = document.getElementById('user-email');

    if (avatarEl) avatarEl.textContent = (user.display_name || user.username || 'U')[0].toUpperCase();
    if (nameEl) nameEl.textContent = user.display_name || user.username;
    if (emailEl) emailEl.textContent = user.email;
}

// ── Device type icons ─────────────────────────────────────────────────────
function getDeviceIcon(type) {
    const icons = {
        esp32: '🔌',
        esp8266: '📡',
        esp32s3: '🔌',
        esp32c3: '🎛️',
        raspberry_pi: '🍓',
        arduino: '🤖',
        generic: '📟',
    };
    return icons[type] || '📟';
}

// ── WebSocket connection ──────────────────────────────────────────────────
let ws = null;
let wsReconnectTimer = null;

function connectWebSocket(onMessage) {
    const token = getToken();
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws?token=${token || ''}`;

    ws = new WebSocket(wsUrl);

    ws.onopen = () => {
        console.log('🔌 WebSocket connected');
        if (wsReconnectTimer) {
            clearTimeout(wsReconnectTimer);
            wsReconnectTimer = null;
        }
    };

    ws.onmessage = (event) => {
        try {
            const data = JSON.parse(event.data);
            if (onMessage) onMessage(data);
        } catch (e) {
            console.error('WS parse error:', e);
        }
    };

    ws.onclose = () => {
        console.log('🔌 WebSocket disconnected, reconnecting in 5s...');
        wsReconnectTimer = setTimeout(() => connectWebSocket(onMessage), 5000);
    };

    ws.onerror = (err) => {
        console.error('WS error:', err);
        ws.close();
    };
}
