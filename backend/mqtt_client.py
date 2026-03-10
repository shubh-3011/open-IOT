"""Open IoT Platform - MQTT Client for device communication."""
import asyncio
import json
import logging
from datetime import datetime, timezone
from typing import Any, Callable

import paho.mqtt.client as mqtt
from config import MQTT_BROKER_HOST, MQTT_BROKER_PORT, MQTT_USERNAME, MQTT_PASSWORD

logger = logging.getLogger("openiot.mqtt")

# Global state for connected WebSocket clients
ws_clients: list[Any] = []
# Callback registry: device_id -> list of callbacks
_data_callbacks: dict[str, list[Callable]] = {}
# Store for latest device states
device_states: dict[str, dict] = {}


def _on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        logger.info("✅ Connected to MQTT broker")
        client.subscribe("openiot/+/state")
        client.subscribe("openiot/+/availability")
    else:
        logger.error(f"❌ MQTT connection failed with code {rc}")


def _on_message(client, userdata, msg):
    """Handle incoming MQTT messages from devices."""
    try:
        topic_parts = msg.topic.split("/")
        if len(topic_parts) < 3:
            return

        device_id = topic_parts[1]
        msg_type = topic_parts[2]  # "state" or "availability"
        payload = msg.payload.decode("utf-8")

        if msg_type == "state":
            try:
                data = json.loads(payload)
            except json.JSONDecodeError:
                data = {"raw": payload}

            data["_received_at"] = datetime.now(timezone.utc).isoformat()
            device_states[device_id] = data

            # Broadcast to WebSocket clients
            asyncio.get_event_loop().call_soon_threadsafe(
                _broadcast_ws, device_id, data
            )

            logger.debug(f"📡 Device {device_id}: {data}")

        elif msg_type == "availability":
            is_online = payload.lower() in ("online", "1", "true")
            device_states.setdefault(device_id, {})["_online"] = is_online
            logger.info(f"📡 Device {device_id} availability: {payload}")

    except Exception as e:
        logger.error(f"Error processing MQTT message: {e}")


def _broadcast_ws(device_id: str, data: dict):
    """Send device update to all connected WebSocket clients."""
    message = json.dumps({
        "type": "device_update",
        "device_id": device_id,
        "data": data,
    })
    disconnected = []
    for ws in ws_clients:
        try:
            asyncio.ensure_future(ws.send_text(message))
        except Exception:
            disconnected.append(ws)
    for ws in disconnected:
        ws_clients.remove(ws)


# ──────────────────────────────────────────────────────────────────────────────
# MQTT Client singleton
# ──────────────────────────────────────────────────────────────────────────────

_mqtt_client: mqtt.Client | None = None


def get_mqtt_client() -> mqtt.Client:
    global _mqtt_client
    if _mqtt_client is None:
        _mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        _mqtt_client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
        _mqtt_client.on_connect = _on_connect
        _mqtt_client.on_message = _on_message
    return _mqtt_client


def start_mqtt():
    """Start the MQTT client in a background thread."""
    client = get_mqtt_client()
    try:
        client.connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT, 60)
        client.loop_start()
        logger.info(f"🚀 MQTT connecting to {MQTT_BROKER_HOST}:{MQTT_BROKER_PORT}")
    except Exception as e:
        logger.warning(f"⚠️  MQTT broker not available: {e}. Running without MQTT.")


def stop_mqtt():
    """Stop the MQTT client."""
    if _mqtt_client:
        _mqtt_client.loop_stop()
        _mqtt_client.disconnect()
        logger.info("MQTT disconnected")


def publish_command(device_id: str, command: dict):
    """Send a command to a device via MQTT."""
    client = get_mqtt_client()
    topic = f"openiot/{device_id}/command"
    payload = json.dumps(command)
    client.publish(topic, payload, qos=1)
    logger.info(f"📤 Command to {device_id}: {command}")
