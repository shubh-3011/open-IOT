"""Open IoT Platform - Database setup and models."""
from datetime import datetime, timezone
from sqlalchemy import (
    create_engine, Column, Integer, String, Float, Boolean,
    DateTime, Text, ForeignKey, JSON
)
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy.orm import sessionmaker, relationship
from config import DATABASE_URL

engine = create_engine(DATABASE_URL, pool_pre_ping=True)
SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)
Base = declarative_base()


def utcnow():
    return datetime.now(timezone.utc)


# ──────────────────────────────────────────────────────────────────────────────
# MODELS
# ──────────────────────────────────────────────────────────────────────────────

class User(Base):
    __tablename__ = "users"

    id = Column(Integer, primary_key=True, index=True)
    username = Column(String(50), unique=True, index=True, nullable=False)
    email = Column(String(255), unique=True, index=True, nullable=False)
    hashed_password = Column(String(255), nullable=False)
    display_name = Column(String(100), nullable=True)
    is_active = Column(Boolean, default=True)
    created_at = Column(DateTime, default=utcnow)

    devices = relationship("Device", back_populates="owner")


class Device(Base):
    __tablename__ = "devices"

    id = Column(Integer, primary_key=True, index=True)
    device_id = Column(String(64), unique=True, index=True, nullable=False)
    name = Column(String(100), nullable=False, default="New Device")
    device_type = Column(String(50), default="generic")  # esp32, esp8266, generic
    owner_id = Column(Integer, ForeignKey("users.id"), nullable=False)
    is_online = Column(Boolean, default=False)
    is_adopted = Column(Boolean, default=False)

    # MQTT credentials for this device
    mqtt_username = Column(String(100), nullable=True)
    mqtt_password = Column(String(100), nullable=True)

    # Device metadata
    firmware_version = Column(String(50), nullable=True)
    ip_address = Column(String(45), nullable=True)
    mac_address = Column(String(17), nullable=True)
    chip_model = Column(String(50), nullable=True)

    # Last known state (JSON blob)
    last_state = Column(JSON, nullable=True)
    last_seen = Column(DateTime, nullable=True)
    created_at = Column(DateTime, default=utcnow)

    owner = relationship("User", back_populates="devices")
    sensor_data = relationship("SensorData", back_populates="device",
                               order_by="SensorData.timestamp.desc()")


class SensorData(Base):
    __tablename__ = "sensor_data"

    id = Column(Integer, primary_key=True, index=True)
    device_id = Column(String(64), ForeignKey("devices.device_id"), nullable=False)
    sensor_type = Column(String(50), nullable=False)  # temperature, humidity, etc.
    value = Column(Float, nullable=False)
    unit = Column(String(20), nullable=True)
    timestamp = Column(DateTime, default=utcnow, index=True)

    device = relationship("Device", back_populates="sensor_data")


class AdoptionToken(Base):
    __tablename__ = "adoption_tokens"

    id = Column(Integer, primary_key=True, index=True)
    token = Column(String(64), unique=True, index=True, nullable=False)
    device_id = Column(String(64), ForeignKey("devices.device_id"), nullable=False)
    owner_id = Column(Integer, ForeignKey("users.id"), nullable=False)
    is_used = Column(Boolean, default=False)
    created_at = Column(DateTime, default=utcnow)
    expires_at = Column(DateTime, nullable=False)


# ──────────────────────────────────────────────────────────────────────────────
# DB INIT
# ──────────────────────────────────────────────────────────────────────────────

def init_db():
    """Create all tables."""
    try:
        Base.metadata.create_all(bind=engine)
    except Exception as e:
        import logging
        logging.getLogger("openiot").error(f"❌ Database init failed: {e}")
        raise


def get_db():
    """Dependency: yield a DB session."""
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()
