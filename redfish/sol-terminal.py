#!/usr/bin/env python3
"""
MiniBMC SOL terminal — connects to the BMC Serial-Over-LAN WebSocket
and provides a proper interactive terminal session.

Usage:
    python3 sol-terminal.py [bmc-ip]

Press Ctrl+] to exit.
"""
import asyncio
import sys
import tty
import termios
import websockets

BMC_HOST = sys.argv[1] if len(sys.argv) > 1 else "10.0.0.136"
SOL_URL  = f"ws://{BMC_HOST}:8000/redfish/v1/Systems/1/SOL"

ESCAPE = b"\x1d"  # Ctrl+] to exit

async def sol_terminal():
    sys.stdout.write(f"[SOL] Connecting to {BMC_HOST} ... (Ctrl+] to exit)\r\n")
    sys.stdout.flush()

    try:
        ws = await websockets.connect(SOL_URL)
    except Exception as e:
        sys.stdout.write(f"[SOL] Connection failed: {e}\r\n")
        return

    fd  = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    try:
        tty.setraw(fd)
        loop = asyncio.get_event_loop()

        async def from_bmc():
            """BMC → local terminal: forward UART output directly."""
            try:
                async for msg in ws:
                    data = msg if isinstance(msg, bytes) else msg.encode("utf-8", errors="replace")
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
            except Exception:
                pass

        async def to_bmc():
            """Local keystrokes → BMC UART: send one byte at a time."""
            try:
                while True:
                    char = await loop.run_in_executor(None, sys.stdin.buffer.read, 1)
                    if char == ESCAPE:
                        return
                    await ws.send(char.decode("latin-1"))
            except Exception:
                pass

        done, pending = await asyncio.wait(
            [asyncio.create_task(from_bmc()), asyncio.create_task(to_bmc())],
            return_when=asyncio.FIRST_COMPLETED,
        )
        for t in pending:
            t.cancel()
        await asyncio.gather(*pending, return_exceptions=True)
        await ws.close()

    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)
        sys.stdout.write("\r\n[SOL] Disconnected.\r\n")
        sys.stdout.flush()

asyncio.run(sol_terminal())
