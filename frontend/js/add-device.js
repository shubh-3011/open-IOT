/* ═══════════════════════════════════════════════════════════════════════════
   OPEN IoT – Add Device Logic
   ═══════════════════════════════════════════════════════════════════════════ */

// Require auth
if (!requireAuth()) throw new Error('Not authenticated');
loadUserInfo();

let createdDevice = null;
let adoptionMode = 'generate'; // 'generate' or 'scan'
let scannedQrData = null;

// ── Create Device ─────────────────────────────────────
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
            body: JSON.stringify({ 
                name, 
                device_type: deviceType,
                server_url: "http://13-203-204-195.nip.io:8000",
                mqtt_host: "13-203-204-195.nip.io"
            }),
        });

        createdDevice = data;

        // Display QR and params
        document.getElementById('qr-image').src = data.qr_code;
        document.getElementById('param-server-url').textContent = data.server_url || window.location.origin;
        document.getElementById('param-device-id').textContent = data.device_id;
        document.getElementById('param-token').textContent = data.adoption_token;
        document.getElementById('param-mqtt-host').textContent = data.mqtt_host || 'see server config';
        document.getElementById('param-mqtt-port').textContent = data.mqtt_port || 1883;
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

// ── Step Navigation ──────────────────────────────────
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

// ── Mode Selection ──────────────────────────────────
function setAdoptionMode(mode) {
    adoptionMode = mode;

    // Update button states
    document.getElementById('mode-generate').classList.toggle('active', mode === 'generate');
    document.getElementById('mode-scan').classList.toggle('active', mode === 'scan');

    // Show/hide card content
    document.getElementById('mode-generate-card').classList.toggle('hidden', mode === 'scan');
    document.getElementById('mode-scan-card').classList.toggle('hidden', mode === 'generate');
}

// ── Copy Params ─────────────────────────────────
function copyParams() {
    if (!createdDevice) return;

    const text = `Open IoT Device Configuration
━━━━━━━━━━━━━━━━━━━━━━━━━━━
Server URL:   ${createdDevice.server_url || window.location.origin}
Device ID:    ${createdDevice.device_id}
Token:        ${createdDevice.adoption_token}
MQTT Host:    ${createdDevice.mqtt_host || 'see .env'}
MQTT Port:    ${createdDevice.mqtt_port || 1883}
MQTT User:    ${createdDevice.mqtt_username}
MQTT Pass:    ${createdDevice.mqtt_password}
━━━━━━━━━━━━━━━━━━━━━━━━━━━`;

    navigator.clipboard.writeText(text).then(() => {
        showToast('Configuration copied to clipboard!', 'success');
    }).catch(() => {
        showToast('Failed to copy', 'error');
    });
}

// ── QR Scanning from ESP ───────────────────────
function handleQrUpload(event) {
    const file = event.target.files[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = function(e) {
        document.getElementById('qr-preview-img').src = e.target.result;
        document.getElementById('qr-preview').classList.remove('hidden');

        // Store for later submission
        scannedQrData = {
            image: e.target.result,
            device_id: document.getElementById('param-device-id').textContent,
            token: document.getElementById('param-token').textContent
        };
    };
    reader.readAsDataURL(file);
}

async function submitScannedQr() {
    if (!scannedQrData) {
        showToast('No QR code scanned yet', 'error');
        return;
    }

    const continueBtn = document.getElementById('continue-btn');
    const originalText = continueBtn.innerHTML;
    continueBtn.innerHTML = '<div class="spinner"></div>';
    continueBtn.disabled = true;

    try {
        // Submit the scanned QR data to adopt the device
        const result = await api('/api/devices/adopt-from-qr', {
            method: 'POST',
            body: JSON.stringify({
                device_id: scannedQrData.device_id,
                token: scannedQrData.token,
                server: createdDevice?.server_url || window.location.origin,
                mqtt_host: createdDevice?.mqtt_host || window.location.hostname,
                mqtt_port: 1883
            })
        });

        if (result.status === 'adopted') {
            document.getElementById('param-device-id').style.color = '#00e5ff';
            showToast('Device adopted successfully! Check dashboard.', 'success');

            setTimeout(() => {
                goStep(3);
            }, 2000);
        } else {
            throw new Error(result.message || 'Failed to adopt device');
        }
    } catch (err) {
        showToast(err.message || 'Failed to adopt device. Please verify the token.', 'error');
        continueBtn.innerHTML = originalText;
        continueBtn.disabled = false;
    }
}
