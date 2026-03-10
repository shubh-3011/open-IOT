"""Open IoT Platform - WebSocket Router for real-time updates."""
import json
import logging
from fastapi import APIRouter, WebSocket, WebSocketDisconnect, Query
from mqtt_client import ws_clients, device_states
from auth import decode_access_token

router = APIRouter(tags=["websocket"])
logger = logging.getLogger("openiot.ws")


@router.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket, token: str = Query(None)):
    """
    WebSocket endpoint for real-time device updates.
    Connect with: ws://host:8000/ws?token=<jwt_token>

    Messages sent to client:
    - { "type": "device_update", "device_id": "...", "data": {...} }
    - { "type": "connected", "message": "..." }

    Messages received from client:
    - { "type": "command", "device_id": "...", "command": "...", "params": {...} }
    """
    # Validate token
    if token:
        payload = decode_access_token(token)
        if not payload:
            await websocket.close(code=4001, reason="Invalid token")
            return
    # Allow unauthenticated WS for dev mode (can be restricted in production)

    await websocket.accept()
    ws_clients.append(websocket)
    logger.info(f"🔌 WebSocket client connected. Total: {len(ws_clients)}")

    # Send current state of all devices
    await websocket.send_text(json.dumps({
        "type": "connected",
        "message": "Connected to Open IoT real-time feed",
        "device_states": device_states,
    }))

    try:
        while True:
            data = await websocket.receive_text()
            try:
                msg = json.loads(data)
                if msg.get("type") == "command":
                    from mqtt_client import publish_command
                    publish_command(msg["device_id"], {
                        "command": msg.get("command"),
                        "params": msg.get("params", {}),
                    })
            except json.JSONDecodeError:
                pass
    except WebSocketDisconnect:
        ws_clients.remove(websocket)
        logger.info(f"🔌 WebSocket client disconnected. Total: {len(ws_clients)}")
