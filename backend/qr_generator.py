"""Open IoT Platform - QR Code Generator for device adoption."""
import io
import json
import base64
import qrcode
from config import SERVER_URL, MQTT_BROKER_HOST, MQTT_BROKER_PORT


def generate_adoption_qr(
    device_id: str,
    adoption_token: str,
    mqtt_username: str,
    mqtt_password: str,
) -> str:
    """
    Generate a QR code image (base64-encoded PNG) containing
    all the parameters an ESP device needs to connect.

    The QR payload is a JSON object:
    {
        "server": "http://host:8000",
        "mqtt_host": "broker-host",
        "mqtt_port": 1883,
        "device_id": "abc123",
        "token": "adoption-token",
        "mqtt_user": "device_abc123",
        "mqtt_pass": "generated-pass"
    }
    """
    payload = json.dumps({
        "server": SERVER_URL,
        "mqtt_host": MQTT_BROKER_HOST,
        "mqtt_port": MQTT_BROKER_PORT,
        "device_id": device_id,
        "token": adoption_token,
        "mqtt_user": mqtt_username,
        "mqtt_pass": mqtt_password,
    }, separators=(",", ":"))

    qr = qrcode.QRCode(
        version=None,  # auto-size
        error_correction=qrcode.constants.ERROR_CORRECT_M,
        box_size=8,
        border=2,
    )
    qr.add_data(payload)
    qr.make(fit=True)

    img = qr.make_image(fill_color="#00e5ff", back_color="#0d1117")
    buffer = io.BytesIO()
    img.save(buffer, format="PNG")
    buffer.seek(0)

    b64 = base64.b64encode(buffer.getvalue()).decode("utf-8")
    return f"data:image/png;base64,{b64}"
