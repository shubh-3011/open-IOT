"""Open IoT Platform - Device Router."""
import secrets
import uuid
from datetime import datetime, timedelta, timezone
from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel
from sqlalchemy.orm import Session
from database import get_db, Device, AdoptionToken, User
from auth import get_current_user
from qr_generator import generate_adoption_qr
from mqtt_client import device_states, publish_command

router = APIRouter(prefix="/api/devices", tags=["devices"])


# ── Schemas ──────────────────────────────────────────────────────────────────

class CreateDeviceRequest(BaseModel):
    name: str = "New Device"
    device_type: str = "esp32"


class AdoptRequest(BaseModel):
    token: str
    device_id: str
    firmware_version: str | None = None
    ip_address: str | None = None
    mac_address: str | None = None
    chip_model: str | None = None


class CommandRequest(BaseModel):
    command: str
    params: dict = {}


# ── Endpoints ────────────────────────────────────────────────────────────────

@router.get("/")
async def list_devices(
    user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """List all devices for the current user."""
    devices = db.query(Device).filter(Device.owner_id == user.id).all()
    result = []
    for d in devices:
        state = device_states.get(d.device_id, {})
        result.append({
            "id": d.id,
            "device_id": d.device_id,
            "name": d.name,
            "device_type": d.device_type,
            "is_online": state.get("_online", d.is_online),
            "is_adopted": d.is_adopted,
            "last_state": state or d.last_state,
            "last_seen": d.last_seen.isoformat() if d.last_seen else None,
            "firmware_version": d.firmware_version,
            "ip_address": d.ip_address,
            "mac_address": d.mac_address,
            "chip_model": d.chip_model,
            "created_at": d.created_at.isoformat() if d.created_at else None,
        })
    return result


@router.post("/create")
async def create_device(
    req: CreateDeviceRequest,
    user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Create a new device and generate adoption credentials."""
    device_id = f"dev_{uuid.uuid4().hex[:12]}"
    mqtt_username = f"device_{device_id}"
    mqtt_password = secrets.token_urlsafe(16)
    adoption_token = secrets.token_urlsafe(32)

    # Create the device record
    device = Device(
        device_id=device_id,
        name=req.name,
        device_type=req.device_type,
        owner_id=user.id,
        mqtt_username=mqtt_username,
        mqtt_password=mqtt_password,
        is_adopted=False,
    )
    db.add(device)

    # Create adoption token (valid for 24 hours)
    token = AdoptionToken(
        token=adoption_token,
        device_id=device_id,
        owner_id=user.id,
        expires_at=datetime.now(timezone.utc) + timedelta(hours=24),
    )
    db.add(token)
    db.commit()
    db.refresh(device)

    # Generate QR code
    qr_image = generate_adoption_qr(
        device_id=device_id,
        adoption_token=adoption_token,
        mqtt_username=mqtt_username,
        mqtt_password=mqtt_password,
    )

    return {
        "device_id": device_id,
        "name": req.name,
        "device_type": req.device_type,
        "adoption_token": adoption_token,
        "mqtt_username": mqtt_username,
        "mqtt_password": mqtt_password,
        "qr_code": qr_image,
        "manual_params": {
            "device_id": device_id,
            "token": adoption_token,
            "mqtt_user": mqtt_username,
            "mqtt_pass": mqtt_password,
        },
    }


@router.post("/adopt")
async def adopt_device(
    req: AdoptRequest,
    db: Session = Depends(get_db),
):
    """
    Called by the ESP device to complete adoption.
    No auth required — uses the adoption token instead.
    """
    token = db.query(AdoptionToken).filter(
        AdoptionToken.token == req.token,
        AdoptionToken.device_id == req.device_id,
        AdoptionToken.is_used == False,
    ).first()

    if not token:
        raise HTTPException(400, "Invalid or expired adoption token")

    if token.expires_at < datetime.now(timezone.utc):
        raise HTTPException(400, "Adoption token has expired")

    # Mark token as used
    token.is_used = True

    # Activate the device
    device = db.query(Device).filter(Device.device_id == req.device_id).first()
    if not device:
        raise HTTPException(404, "Device not found")

    device.is_adopted = True
    device.firmware_version = req.firmware_version
    device.ip_address = req.ip_address
    device.mac_address = req.mac_address
    device.chip_model = req.chip_model
    device.last_seen = datetime.now(timezone.utc)

    db.commit()

    return {
        "status": "adopted",
        "device_id": device.device_id,
        "mqtt_username": device.mqtt_username,
        "mqtt_password": device.mqtt_password,
        "mqtt_topics": {
            "state": f"openiot/{device.device_id}/state",
            "command": f"openiot/{device.device_id}/command",
            "availability": f"openiot/{device.device_id}/availability",
        },
    }


@router.get("/{device_id}")
async def get_device(
    device_id: str,
    user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Get a single device's details."""
    device = db.query(Device).filter(
        Device.device_id == device_id,
        Device.owner_id == user.id,
    ).first()
    if not device:
        raise HTTPException(404, "Device not found")

    state = device_states.get(device_id, {})
    return {
        "id": device.id,
        "device_id": device.device_id,
        "name": device.name,
        "device_type": device.device_type,
        "is_online": state.get("_online", device.is_online),
        "is_adopted": device.is_adopted,
        "last_state": state or device.last_state,
        "last_seen": device.last_seen.isoformat() if device.last_seen else None,
        "firmware_version": device.firmware_version,
        "ip_address": device.ip_address,
        "mac_address": device.mac_address,
        "chip_model": device.chip_model,
        "created_at": device.created_at.isoformat() if device.created_at else None,
    }


@router.post("/{device_id}/command")
async def send_command(
    device_id: str,
    req: CommandRequest,
    user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Send a command to a device via MQTT."""
    device = db.query(Device).filter(
        Device.device_id == device_id,
        Device.owner_id == user.id,
    ).first()
    if not device:
        raise HTTPException(404, "Device not found")

    publish_command(device_id, {"command": req.command, "params": req.params})
    return {"status": "sent", "device_id": device_id, "command": req.command}


@router.delete("/{device_id}")
async def delete_device(
    device_id: str,
    user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Delete a device."""
    device = db.query(Device).filter(
        Device.device_id == device_id,
        Device.owner_id == user.id,
    ).first()
    if not device:
        raise HTTPException(404, "Device not found")

    # Delete related tokens
    db.query(AdoptionToken).filter(AdoptionToken.device_id == device_id).delete()
    db.delete(device)
    db.commit()
    return {"status": "deleted", "device_id": device_id}


@router.put("/{device_id}")
async def update_device(
    device_id: str,
    name: str = None,
    user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Update device properties."""
    device = db.query(Device).filter(
        Device.device_id == device_id,
        Device.owner_id == user.id,
    ).first()
    if not device:
        raise HTTPException(404, "Device not found")

    if name:
        device.name = name
    db.commit()
    return {"status": "updated", "device_id": device_id, "name": device.name}
