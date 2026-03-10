"""Open IoT Platform - Configuration."""
import os
from pathlib import Path
from dotenv import load_dotenv

# Load .env file
load_dotenv()

# Base paths
BASE_DIR = Path(__file__).resolve().parent
DATA_DIR = BASE_DIR / "data"
DATA_DIR.mkdir(exist_ok=True)

# Supabase
SUPABASE_URL = os.getenv("SUPABASE_URL", "")
SUPABASE_KEY = os.getenv("SUPABASE_KEY", "")

# Database
DATABASE_URL = os.getenv("DATABASE_URL", "sqlite:///./data/openiot.db")

# JWT Auth
SECRET_KEY = os.getenv("SECRET_KEY", "openiot-dev-secret-change-in-production")
ALGORITHM = "HS256"
ACCESS_TOKEN_EXPIRE_MINUTES = 60 * 24 * 7  # 7 days

# MQTT
MQTT_BROKER_HOST = os.getenv("MQTT_BROKER_HOST", "localhost")
MQTT_BROKER_PORT = int(os.getenv("MQTT_BROKER_PORT", "1883"))
MQTT_USERNAME = os.getenv("MQTT_USERNAME", "openiot")
MQTT_PASSWORD = os.getenv("MQTT_PASSWORD", "openiot123")

# Server
SERVER_HOST = os.getenv("SERVER_HOST", "0.0.0.0")
SERVER_PORT = int(os.getenv("SERVER_PORT", "8000"))
SERVER_URL = os.getenv("SERVER_URL", "http://localhost:8000")

# MQTT Topic patterns
MQTT_TOPIC_STATE = "openiot/{device_id}/state"
MQTT_TOPIC_COMMAND = "openiot/{device_id}/command"
MQTT_TOPIC_AVAILABILITY = "openiot/{device_id}/availability"
