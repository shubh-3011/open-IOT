# 🔌 Open IoT Platform

> **Self-hostable, vendor-free IoT device management platform.** Connect your ESP32/ESP8266 devices, visualize sensor data in real-time, and control everything from a modern cyberpunk dashboard.

![Python](https://img.shields.io/badge/Python-3.11+-3776AB?style=flat-square&logo=python&logoColor=white)
![FastAPI](https://img.shields.io/badge/FastAPI-0.115-009688?style=flat-square&logo=fastapi&logoColor=white)
![PostgreSQL](https://img.shields.io/badge/PostgreSQL-Supabase-336791?style=flat-square&logo=postgresql&logoColor=white)
![MQTT](https://img.shields.io/badge/MQTT-Paho-660066?style=flat-square)
![License](https://img.shields.io/badge/License-MIT-green?style=flat-square)

---

## ✨ Features

- 🔐 **User Authentication** — JWT-based signup/login with bcrypt password hashing
- 📡 **Device Management** — Create, adopt, monitor, and control IoT devices
- 📱 **QR Code Onboarding** — Generate QR codes for one-scan device setup
- 📊 **Real-time Dashboard** — Live sensor data via WebSocket + MQTT bridge
- 🎛️ **Device Commands** — Send commands (ping, restart, LED control) from dashboard
- 🌐 **MQTT Integration** — Devices communicate via standard MQTT protocol
- 🔧 **ESP Firmware Included** — Ready-to-flash Arduino sketch for ESP32/ESP8266
- 🐳 **Docker Ready** — Deploy with `docker-compose up`
- 🎨 **Cyberpunk UI** — Dark theme with grid overlays, scan lines, and micro-animations

---

## 🏗️ Architecture

```
┌──────────────┐     MQTT      ┌──────────────┐     HTTP/WS     ┌──────────────┐
│  ESP Device  │ ◄───────────► │  MQTT Broker  │ ◄─────────────► │   FastAPI    │
│  (Firmware)  │               │  (Mosquitto)  │                 │   Backend    │
└──────────────┘               └──────────────┘                 └──────┬───────┘
                                                                       │
                                                                       │ REST API
                                                                       │ WebSocket
                                                                       │
                                                                ┌──────▼───────┐
                                                                │   Frontend   │
                                                                │  (Dashboard) │
                                                                └──────────────┘
```

### How It Works

#### Complete Onboarding Flow

**Step 1: Create Device on Dashboard**
- User goes to `/add-device` page
- Fills in device name and type (ESP32, ESP8266, etc.)
- Backend generates:
  - Unique `device_id` (e.g., `dev_abc123`)
  - MQTT credentials (username/password)
  - Single-use `adoption_token`
  - QR code containing device_id, token, server URL, MQTT settings

**Step 2: Choose Capture Mode**

**Mode A: Backend-Generated QR (ESP scans QR)**
- User shows QR code to ESP32-CAM device
- ESP's camera scans the QR automatically
- ESP extracts credentials and begins self-configuration

**Mode B: ESP-Generated QR (User scans QR with phone)**
- User flashes ESP with firmware
- ESP creates `OpenIoT-Setup` WiFi AP
- User connects phone to ESP's WiFi, opens captive portal
- User selects "QR Generation" mode
- ESP displays QR code containing device_id, token
- User captures QR with phone camera
- User goes back to dashboard, selects "Scan QR from ESP"
- User uploads QR screenshot or manually enters data
- Backend validates token and adopts device

**Mode C: Manual Entry**
- User flashes ESP with firmware
- ESP creates `OpenIoT-Setup` WiFi AP
- User connects, enters WiFi credentials
- User selects "Manual Entry" mode
- User manually types in: Server URL, Device ID, Token, MQTT settings
- ESP saves configuration and adopts itself

**Step 3: Device Adoption**
- ESP connects to WiFi
- ESP calls `POST /api/devices/adopt` with adoption token
- Backend validates token (checks expiry, single-use)
- Device marked as `is_adopted = True`
- ESP receives MQTT credentials
- ESP connects to MQTT broker

**Step 4: Live Monitoring**
- ESP publishes sensor data to `openiot/{device_id}/state` every 10 seconds
- Backend receives MQTT data, stores in database
- WebSocket broadcasts updates to dashboard
- Dashboard displays real-time sensor values, charts, activity feed

**Step 5: Device Control**
- User sends commands from dashboard (ping, restart, LED control)
- Commands published to `openiot/{device_id}/command`
- ESP receives commands, executes, responds
- Dashboard updates in real-time

---

## 🚀 Quick Start

### Prerequisites

- Python 3.11+
- PostgreSQL database (or [Supabase](https://supabase.com) free tier)
- MQTT broker (Mosquitto included in Docker setup)

### Local Development

```bash
# Clone the repo
git clone https://github.com/shubh-3011/open-iot.git
cd open-iot

# Create virtual environment
python -m venv .venv
source .venv/bin/activate  # Linux/macOS
# .venv\Scripts\Activate.ps1  # Windows PowerShell

# Install dependencies
pip install -r backend/requirements.txt

# Configure database (edit backend/config.py)
# Set your DATABASE_URL, SUPABASE_URL, SUPABASE_KEY

# Start the server
cd backend
python main.py
```

Open **http://localhost:8000** in your browser.

### Docker Deployment

```bash
docker-compose up -d
```

This starts:
- **FastAPI backend** on port `8000`
- **Mosquitto MQTT broker** on port `1883`

---

## 📁 Project Structure

```
open-iot/
├── backend/
│   ├── main.py              # FastAPI app entry point
│   ├── config.py             # Configuration (DB, MQTT, JWT)
│   ├── database.py           # SQLAlchemy models & DB setup
│   ├── auth.py               # JWT + bcrypt authentication
│   ├── mqtt_client.py        # MQTT client (subscribe, publish, WS bridge)
│   ├── qr_generator.py       # QR code generation for device adoption
│   ├── requirements.txt      # Python dependencies
│   └── routers/
│       ├── auth_router.py    # /api/auth/* (register, login)
│       ├── device_router.py  # /api/devices/* (CRUD, adopt, commands)
│       ├── data_router.py    # /api/data/* (sensor history, stats)
│       └── ws_router.py      # /ws (WebSocket for live updates)
│
├── frontend/
│   ├── index.html            # Login / Register page
│   ├── dashboard.html        # Main dashboard
│   ├── add-device.html       # Device onboarding (3-step wizard)
│   ├── device.html           # Individual device detail page
│   ├── css/style.css         # Design system (cyberpunk dark theme)
│   └── js/
│       ├── api.js            # API client, auth, WebSocket, utilities
│       ├── auth.js           # Login/register form handlers
│       ├── dashboard.js      # Dashboard rendering + live updates
│       ├── add-device.js     # Device creation + QR generation
│       └── device.js         # Device detail page logic
│
├── esp_firmware/
│   └── open_iot_esp/
│       └── open_iot_esp.ino  # Arduino sketch for ESP32/ESP8266
│
├── mosquitto/
│   └── mosquitto.conf        # MQTT broker configuration
│
├── docker-compose.yml        # Docker deployment
└── README.md
```

---

## 📡 ESP Device Setup

### Required Arduino Libraries

- [WiFiManager](https://github.com/tzapu/WiFiManager) — Captive portal for WiFi setup
- [PubSubClient](https://github.com/knolleary/pubsubclient) — MQTT client
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson) — JSON parsing
- HTTPClient (built-in)
- WebServer (built-in for ESP32/ESP8266)

### Dual-Mode Captive Portal

The ESP firmware supports **two setup modes**:

#### Mode 1: Manual Entry
1. ESP creates `OpenIoT-Setup` WiFi AP
2. User connects phone to AP, opens captive portal
3. User selects "Manual Entry" mode
4. User enters:
   - Server URL (e.g., `http://your-server:8000`)
   - Device ID (e.g., `dev_abc123`)
   - Adoption Token (from web dashboard)
   - MQTT Host, Port, Username, Password
5. ESP saves settings and adopts itself automatically

#### Mode 2: QR Code Generation
1. ESP creates `OpenIoT-Setup` WiFi AP
2. User connects, selects "QR Generation" mode
3. ESP displays a QR code containing:
   - Server URL
   - Device ID
   - Adoption Token
   - MQTT Host + Port
4. User captures QR code (screenshot/phone camera)
5. User goes to web dashboard, selects "Scan QR from ESP"
6. User uploads QR screenshot or manually enters data
7. Backend validates and completes adoption

**This flow enables:**
- **ESP32-CAM devices** to scan QR codes directly with their camera
- **Standard ESP32/ESP8266** to display QR codes for user to capture
- **Flexible onboarding** for different hardware capabilities

### Customizing Sensors

Edit the `publishState()` function in the firmware to read your actual sensors:

```cpp
void publishState() {
  JsonDocument doc;

  // Replace with your real sensors:
  doc["temperature"] = dht.readTemperature();
  doc["humidity"]    = dht.readHumidity();
  doc["soil"]        = analogRead(A0);

  String payload;
  serializeJson(doc, payload);
  String topic = "openiot/" + DEVICE_ID + "/state";
  mqttClient.publish(topic.c_str(), payload.c_str());
}
```

The dashboard **automatically renders** whatever JSON keys your device sends — no frontend changes needed.

### Supported Commands

The firmware responds to these MQTT commands from the dashboard:

| Command   | Action                        |
|-----------|-------------------------------|
| `ping`    | Device responds with state    |
| `restart` | Reboots the ESP              |
| `led_on`  | Turns on built-in LED        |
| `led_off` | Turns off built-in LED       |

---

## 🔌 API Reference

Full interactive API docs available at `http://localhost:8000/docs` (Swagger UI).

### Auth
| Method | Endpoint             | Description            |
|--------|----------------------|------------------------|
| POST   | `/api/auth/register` | Register new user      |
| POST   | `/api/auth/login`    | Login (OAuth2 form)    |
| GET    | `/api/auth/me`       | Get current user info  |

### Devices
| Method | Endpoint                        | Description               |
|--------|---------------------------------|---------------------------|
| GET    | `/api/devices/`                 | List all user devices     |
| POST   | `/api/devices/create`           | Create a new device       |
| POST   | `/api/devices/adopt`            | Adopt device (ESP calls)  |
| POST   | `/api/devices/{id}/command`     | Send command to device    |
| GET    | `/api/devices/{id}`             | Get device details        |
| DELETE | `/api/devices/{id}`             | Delete a device           |

### Data
| Method | Endpoint                       | Description                |
|--------|--------------------------------|----------------------------|
| GET    | `/api/data/dashboard/stats`    | Dashboard statistics       |
| GET    | `/api/data/{device_id}/history`| Sensor data history        |
| GET    | `/api/data/{device_id}/latest` | Latest sensor readings     |

---

## 🛡️ Security

- Passwords hashed with **bcrypt** (12 rounds)
- Authentication via **JWT** tokens (7-day expiry)
- MQTT credentials are **per-device** (generated on creation)
- Adoption tokens are **single-use** with expiration

---

## 🧰 Tech Stack

| Component     | Technology                          |
|---------------|-------------------------------------|
| Backend       | Python, FastAPI, SQLAlchemy         |
| Database      | PostgreSQL (Supabase)               |
| Auth          | JWT (python-jose), bcrypt           |
| MQTT          | Paho MQTT, Mosquitto broker         |
| Frontend      | Vanilla HTML/CSS/JS                 |
| Real-time     | WebSocket + MQTT bridge             |
| QR Codes      | qrcode + Pillow                     |
| Firmware      | Arduino (ESP32/ESP8266)             |
| Deployment    | Docker, docker-compose              |

---

## 📄 License

MIT License — use it however you want.

---

## 🤝 Contributing

Contributions welcome! Open an issue or submit a pull request.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/amazing`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to branch (`git push origin feature/amazing`)
5. Open a Pull Request
