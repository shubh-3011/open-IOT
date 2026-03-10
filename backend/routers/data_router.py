"""Open IoT Platform - Data Router for sensor data history."""
from datetime import datetime, timedelta, timezone
from fastapi import APIRouter, Depends, HTTPException, Query
from sqlalchemy.orm import Session
from sqlalchemy import func
from database import get_db, Device, SensorData, User
from auth import get_current_user

router = APIRouter(prefix="/api/data", tags=["data"])


@router.get("/{device_id}/history")
async def get_sensor_history(
    device_id: str,
    sensor_type: str = Query(None, description="Filter by sensor type"),
    hours: int = Query(24, description="Hours of history to return"),
    limit: int = Query(500, description="Max number of records"),
    user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Get sensor data history for a device."""
    device = db.query(Device).filter(
        Device.device_id == device_id,
        Device.owner_id == user.id,
    ).first()
    if not device:
        raise HTTPException(404, "Device not found")

    since = datetime.now(timezone.utc) - timedelta(hours=hours)
    query = db.query(SensorData).filter(
        SensorData.device_id == device_id,
        SensorData.timestamp >= since,
    )

    if sensor_type:
        query = query.filter(SensorData.sensor_type == sensor_type)

    records = query.order_by(SensorData.timestamp.desc()).limit(limit).all()

    return [
        {
            "sensor_type": r.sensor_type,
            "value": r.value,
            "unit": r.unit,
            "timestamp": r.timestamp.isoformat(),
        }
        for r in records
    ]


@router.get("/{device_id}/latest")
async def get_latest_data(
    device_id: str,
    user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Get the latest sensor reading for each sensor type on a device."""
    device = db.query(Device).filter(
        Device.device_id == device_id,
        Device.owner_id == user.id,
    ).first()
    if not device:
        raise HTTPException(404, "Device not found")

    # Get latest of each sensor type using subquery
    subq = db.query(
        SensorData.sensor_type,
        func.max(SensorData.id).label("max_id"),
    ).filter(
        SensorData.device_id == device_id
    ).group_by(SensorData.sensor_type).subquery()

    latest = db.query(SensorData).join(
        subq, SensorData.id == subq.c.max_id
    ).all()

    return {
        r.sensor_type: {
            "value": r.value,
            "unit": r.unit,
            "timestamp": r.timestamp.isoformat(),
        }
        for r in latest
    }


@router.get("/dashboard/stats")
async def get_dashboard_stats(
    user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Get dashboard summary statistics."""
    from mqtt_client import device_states

    total_devices = db.query(Device).filter(Device.owner_id == user.id).count()
    adopted_devices = db.query(Device).filter(
        Device.owner_id == user.id,
        Device.is_adopted == True,
    ).count()

    # Count online devices from MQTT state
    user_devices = db.query(Device).filter(Device.owner_id == user.id).all()
    online_count = sum(
        1 for d in user_devices
        if device_states.get(d.device_id, {}).get("_online", False)
    )

    # Count sensor readings in last 24h
    since = datetime.now(timezone.utc) - timedelta(hours=24)
    device_ids = [d.device_id for d in user_devices]
    readings_24h = db.query(SensorData).filter(
        SensorData.device_id.in_(device_ids),
        SensorData.timestamp >= since,
    ).count() if device_ids else 0

    return {
        "total_devices": total_devices,
        "adopted_devices": adopted_devices,
        "online_devices": online_count,
        "readings_24h": readings_24h,
    }
