/* ═══════════════════════════════════════════════════════════════════════════
   OPEN IoT – Add Device Logic
   ═══════════════════════════════════════════════════════════════════════════ */

// Require auth
if (!requireAuth()) throw new Error('Not authenticated');
loadUserInfo();

let createdDevice = null;

// ── Create Device ─────────────────────────────────────────────────────────
async function createDevice(e) {
    e.preventDefault();
    const btn = document.getElementById('create-btn');
    btn.disabled = true;
    btn.innerHTML = '<div class="spinner"></div>';

    try {
        const name = document.getElementById('device-name').value;
        const deviceType = document.getElementById('device-type').value;

        const data = await api('/api/devices/create', {
            method: 'POST',
            body: JSON.stringify({ name, device_type: deviceType }),
        });

        createdDevice = data;

        // Display QR and params
        document.getElementById('qr-image').src = data.qr_code;
        document.getElementById('param-device-id').textContent = data.device_id;
        document.getElementById('param-token').textContent = data.adoption_token;
        document.getElementById('param-mqtt-user').textContent = data.mqtt_username;
        document.getElementById('param-mqtt-pass').textContent = data.mqtt_password;

        goStep(2);
        showToast('Device created! Scan the QR code to adopt.', 'success');
    } catch (err) {
        showToast(err.message || 'Failed to create device', 'error');
        btn.disabled = false;
        btn.innerHTML = '<span>Generate QR Code</span><span>→</span>';
    }
}

// ── Step Navigation ───────────────────────────────────────────────────────
function goStep(step) {
    // Update step indicators
    for (let i = 1; i <= 3; i++) {
        const el = document.getElementById(`step-${i}`);
        el.classList.remove('active', 'done');
        if (i < step) el.classList.add('done');
        if (i === step) el.classList.add('active');
    }

    // Show/hide cards
    document.getElementById('step1-card').classList.toggle('hidden', step !== 1);
    document.getElementById('step2-card').classList.toggle('hidden', step !== 2);
    document.getElementById('step3-card').classList.toggle('hidden', step !== 3);
}

// ── Copy Params ───────────────────────────────────────────────────────────
function copyParams() {
    if (!createdDevice) return;

    const text = `Open IoT Device Configuration
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Device ID:    ${createdDevice.device_id}
Token:        ${createdDevice.adoption_token}
MQTT User:    ${createdDevice.mqtt_username}
MQTT Pass:    ${createdDevice.mqtt_password}
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━`;

    navigator.clipboard.writeText(text).then(() => {
        showToast('Configuration copied to clipboard!', 'success');
    }).catch(() => {
        showToast('Failed to copy', 'error');
    });
}
