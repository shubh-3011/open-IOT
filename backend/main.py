"""Open IoT Platform - FastAPI Application Entry Point."""
import logging
from contextlib import asynccontextmanager
from fastapi import FastAPI, Depends
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
from pathlib import Path

from database import init_db
from auth import get_current_user
from mqtt_client import start_mqtt, stop_mqtt
from routers import auth_router, device_router, data_router, ws_router

# ── Logging ──────────────────────────────────────────────────────────────────
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s │ %(name)-20s │ %(levelname)-7s │ %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger("openiot")

# ── Frontend path ────────────────────────────────────────────────────────────
# Try Docker path first, then fallback to local repo layout
FRONTEND_DIR = Path("/app/frontend")
if not FRONTEND_DIR.exists():
    FRONTEND_DIR = Path(__file__).parent.parent / "frontend"


# ── App lifecycle ────────────────────────────────────────────────────────────
@asynccontextmanager
async def lifespan(app: FastAPI):
    logger.info("Open IoT Platform starting...")
    init_db()
    logger.info("Database initialized")
    start_mqtt()
    yield
    stop_mqtt()
    logger.info("Open IoT Platform stopped")


# ── FastAPI App ──────────────────────────────────────────────────────────────
app = FastAPI(
    title="Open IoT Platform",
    description="Self-hostable IoT device management platform",
    version="0.1.0",
    lifespan=lifespan,
)

# CORS (allow all for dev)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# ── Serve frontend static files ────────────────────────────────────────────
if FRONTEND_DIR.exists():
    print(f"Frontend directory found: {FRONTEND_DIR}")
    app.mount("/css", StaticFiles(directory=FRONTEND_DIR / "css"), name="css")
    app.mount("/js", StaticFiles(directory=FRONTEND_DIR / "js"), name="js")
    app.mount("/assets", StaticFiles(directory=FRONTEND_DIR / "assets"), name="assets")

    @app.get("/", include_in_schema=False)
    async def serve_index():
        print(f"Serving index.html from {FRONTEND_DIR / 'index.html'}")
        return FileResponse(FRONTEND_DIR / "index.html")

    @app.get("/dashboard", include_in_schema=False)
    async def serve_dashboard():
        print(f"Serving dashboard.html from {FRONTEND_DIR / 'dashboard.html'}")
        return FileResponse(FRONTEND_DIR / "dashboard.html")

    @app.get("/add-device", include_in_schema=False)
    async def serve_add_device():
        print(f"Serving add-device.html from {FRONTEND_DIR / 'add-device.html'}")
        return FileResponse(FRONTEND_DIR / "add-device.html")

    @app.get("/device/{device_id}", include_in_schema=False)
    async def serve_device_page(device_id: str):
        print(f"Serving device.html from {FRONTEND_DIR / 'device.html'}")
        return FileResponse(FRONTEND_DIR / "device.html")

else:
    print(f"Frontend directory not found: {FRONTEND_DIR}")

# ── Include API routers ─────────────────────────────────────────────────────
app.include_router(auth_router.router)
app.include_router(device_router.router)
app.include_router(data_router.router)
app.include_router(ws_router.router)


# ── Authenticated user info endpoint ────────────────────────────────────────
@app.get("/api/auth/me", tags=["auth"])
async def get_me(user=Depends(get_current_user)):
    return {
        "id": user.id,
        "username": user.username,
        "email": user.email,
        "display_name": user.display_name,
    }


# ── Run with uvicorn ─────────────────────────────────────────────────────────
if __name__ == "__main__":
    import uvicorn
    from config import SERVER_HOST, SERVER_PORT
    uvicorn.run(
        "main:app",
        host=SERVER_HOST,
        port=SERVER_PORT,
        reload=True,
        log_level="info",
    )
