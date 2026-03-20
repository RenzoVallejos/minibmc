"""
MiniBMC Redfish API
Exposes Redfish-compatible endpoints for power control of the managed host.
Communicates with the minibmc C daemon via Unix domain socket.
"""
import asyncio
import socket
from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from pydantic import BaseModel

SOCKET_PATH     = "/var/run/minibmc.sock"
SOL_SOCKET_PATH = "/var/run/minibmc-sol.sock"

app = FastAPI(
    title="MiniBMC Redfish API",
    description="Redfish-compatible BMC API for the Raspberry Pi 4 MiniBMC",
    version="1.0.0",
)


def send_command(cmd: str) -> str:
    """Send a command to the minibmc daemon and return its response."""
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
            s.settimeout(5.0)
            s.connect(SOCKET_PATH)
            s.sendall((cmd + "\n").encode())
            response = s.recv(256).decode().strip()
            return response
    except FileNotFoundError:
        raise HTTPException(status_code=503, detail="minibmc daemon not running")
    except socket.timeout:
        raise HTTPException(status_code=504, detail="minibmc daemon timed out")
    except OSError as e:
        raise HTTPException(status_code=503, detail=f"IPC error: {e}")


def get_power_state() -> str:
    """Return 'On' or 'Off' based on minibmc state."""
    response = send_command("status")
    state = response.replace("STATE:", "")
    return "On" if state == "ON" else "Off"


# ---------------------------------------------------------------------------
# Redfish endpoints
# ---------------------------------------------------------------------------

@app.get("/redfish/v1/")
def redfish_root():
    return {
        "@odata.type": "#ServiceRoot.v1_5_0.ServiceRoot",
        "@odata.id": "/redfish/v1/",
        "Id": "RootService",
        "Name": "MiniBMC Redfish Service",
        "RedfishVersion": "1.5.0",
        "Systems": {"@odata.id": "/redfish/v1/Systems"},
    }


@app.get("/redfish/v1/Systems")
def systems_collection():
    return {
        "@odata.type": "#ComputerSystemCollection.ComputerSystemCollection",
        "@odata.id": "/redfish/v1/Systems",
        "Name": "Systems Collection",
        "Members@odata.count": 1,
        "Members": [{"@odata.id": "/redfish/v1/Systems/1"}],
    }


@app.get("/redfish/v1/Systems/1")
def get_system():
    power_state = get_power_state()
    return {
        "@odata.type": "#ComputerSystem.v1_13_0.ComputerSystem",
        "@odata.id": "/redfish/v1/Systems/1",
        "Id": "1",
        "Name": "Dell DE051",
        "PowerState": power_state,
        "Status": {
            "State": "Enabled",
            "Health": "OK",
        },
        "Actions": {
            "#ComputerSystem.Reset": {
                "target": "/redfish/v1/Systems/1/Actions/ComputerSystem.Reset",
                "ResetType@Redfish.AllowableValues": [
                    "On",
                    "GracefulShutdown",
                    "ForceOff",
                ],
            }
        },
    }


class ResetAction(BaseModel):
    ResetType: str


@app.post("/redfish/v1/Systems/1/Actions/ComputerSystem.Reset", status_code=204)
def reset_system(action: ResetAction):
    if action.ResetType == "On":
        response = send_command("power on")
    elif action.ResetType in ("GracefulShutdown", "ForceOff"):
        response = send_command("power off")
    else:
        raise HTTPException(
            status_code=400,
            detail=f"Unsupported ResetType: {action.ResetType}. "
                   f"Allowed: On, GracefulShutdown, ForceOff",
        )

    if response.startswith("ERROR"):
        raise HTTPException(status_code=409, detail=response.replace("ERROR:", ""))


# ---------------------------------------------------------------------------
# SOL WebSocket — bidirectional serial console over WebSocket
# ---------------------------------------------------------------------------

@app.websocket("/redfish/v1/Systems/1/SOL")
async def sol_websocket(websocket: WebSocket):
    await websocket.accept()
    try:
        reader, writer = await asyncio.open_unix_connection(SOL_SOCKET_PATH)
    except OSError:
        await websocket.close(code=1011, reason="SOL unavailable")
        return

    async def unix_to_ws():
        try:
            while True:
                data = await reader.read(256)
                if not data:
                    break
                await websocket.send_bytes(data)
        except asyncio.CancelledError:
            raise
        except Exception:
            pass

    async def ws_to_unix():
        try:
            while True:
                data = await websocket.receive_text()
                writer.write(data.encode())
                await writer.drain()
        except asyncio.CancelledError:
            raise
        except (WebSocketDisconnect, Exception):
            pass

    tasks = [asyncio.create_task(unix_to_ws()), asyncio.create_task(ws_to_unix())]
    try:
        await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
    finally:
        for t in tasks:
            t.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        writer.close()
