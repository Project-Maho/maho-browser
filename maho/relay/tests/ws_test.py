import asyncio
import json
import os
import sys
import time
import websockets


def usage_and_exit() -> None:
    print("Usage: BASE_WS=wss://relay-staging.mahobrowser.com/ws TOKEN=<access_token> ROOM_KEY=<32-hex> python tests/ws_test.py", file=sys.stderr)
    print("       run integration.sh first to obtain TOKEN + device room_id via /auth/signup + /auth/me", file=sys.stderr)
    sys.exit(2)


async def scenario_auth_and_ack(base_ws: str, token: str, key: str) -> None:
    print(f"--- scenario: auth + send + ack against {base_ws} key={key[:8]}... ---")
    async with websockets.connect(f"{base_ws}?key={key}") as ws:
        await ws.send(json.dumps({"type": "auth", "key": key, "token": token, "since_seq": 0}))
        auth_reply = json.loads(await asyncio.wait_for(ws.recv(), timeout=5.0))
        assert auth_reply.get("type") == "auth_ok", f"expected auth_ok, got {auth_reply}"
        print(f"  auth_ok peer_id={auth_reply.get('peer_id')}")

        while True:
            msg = json.loads(await asyncio.wait_for(ws.recv(), timeout=5.0))
            if msg.get("type") == "catchup_done":
                break
            assert msg.get("type") in ("catchup",), f"unexpected during catchup: {msg}"
        print("  catchup_done")

        await ws.send("hello from ws_test.py")
        ack = json.loads(await asyncio.wait_for(ws.recv(), timeout=5.0))
        assert ack.get("type") == "ack" and int(ack.get("seq", -1)) >= 0, f"expected ack seq>=0, got {ack}"
        print(f"  ack seq={ack['seq']}")


async def scenario_broadcast(base_ws: str, token: str, key: str) -> None:
    print(f"--- scenario: broadcast to peer ---")
    async with websockets.connect(f"{base_ws}?key={key}") as ws_a, \
               websockets.connect(f"{base_ws}?key={key}") as ws_b:
        for label, ws in (("A", ws_a), ("B", ws_b)):
            await ws.send(json.dumps({"type": "auth", "key": key, "token": token, "since_seq": 0}))
            reply = json.loads(await asyncio.wait_for(ws.recv(), timeout=5.0))
            assert reply.get("type") == "auth_ok", f"peer {label} auth_ok expected, got {reply}"
            while json.loads(await asyncio.wait_for(ws.recv(), timeout=5.0)).get("type") != "catchup_done":
                pass
            print(f"  peer {label} authenticated")

        marker = f"broadcast-marker-{int(time.time())}"
        await ws_a.send(marker)
        try:
            received = await asyncio.wait_for(ws_b.recv(), timeout=5.0)
            received_str = received if isinstance(received, str) else received.decode("utf-8", errors="replace")
            print(f"  peer B received: {received_str[:80]}")
            assert marker in received_str, f"peer B did not receive marker (got {received_str[:80]})"
        except asyncio.TimeoutError:
            print("  FAIL peer B did not receive broadcast within 5s")
            sys.exit(1)

        try:
            ack = json.loads(await asyncio.wait_for(ws_a.recv(), timeout=5.0))
            assert ack.get("type") == "ack", f"peer A ack expected, got {ack}"
            print(f"  peer A ack seq={ack.get('seq')}")
        except asyncio.TimeoutError:
            print("  FAIL peer A did not receive ack within 5s")
            sys.exit(1)


async def scenario_invalid_token(base_ws: str, key: str) -> None:
    print(f"--- scenario: invalid token rejected ---")
    async with websockets.connect(f"{base_ws}?key={key}") as ws:
        await ws.send(json.dumps({"type": "auth", "key": key, "token": "not-a-real-token", "since_seq": 0}))
        try:
            reply = json.loads(await asyncio.wait_for(ws.recv(), timeout=5.0))
            assert reply.get("type") == "error", f"expected error, got {reply}"
            print(f"  error msg={reply.get('msg')}")
        except asyncio.TimeoutError:
            print("  FAIL server did not respond to invalid token within 5s")
            sys.exit(1)


async def main() -> None:
    base_ws_opt = os.environ.get("BASE_WS")
    token_opt = os.environ.get("TOKEN")
    key_opt = os.environ.get("ROOM_KEY")
    if not (base_ws_opt and token_opt and key_opt):
        usage_and_exit()
        return
    base_ws: str = base_ws_opt
    token: str = token_opt
    key: str = key_opt

    await scenario_auth_and_ack(base_ws, token, key)
    await scenario_broadcast(base_ws, token, key)
    await scenario_invalid_token(base_ws, key)
    print("ALL PASS")


if __name__ == "__main__":
    asyncio.run(main())
