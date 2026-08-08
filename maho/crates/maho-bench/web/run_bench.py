#!/usr/bin/env python3
"""Minimal Mind2Web-style benchmark runner for the Maho CLI agent.

Runs each task through `maho agent run`, then grades the transcript with an
LLM judge over the same OpenAI-compatible endpoint the browser is configured
for. Results land in results.json next to this file.
"""
import json
import os
import pathlib
import subprocess
import sys
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent
MAHO_ROOT = HERE.parents[2]
REPO = MAHO_ROOT.parent
CLI = MAHO_ROOT / "target" / "debug" / "maho"
PREFS = pathlib.Path.home() / "Library/Application Support/Maho/Default/Preferences"
TASK_TIMEOUT_S = 300


SOCKET = pathlib.Path(os.environ.get(
    "BENCH_MCP_SOCKET",
    pathlib.Path.home() / "Library/Application Support/Maho/maho.sock",
))


def browser_ai_config() -> dict:
    cfg = json.loads(PREFS.read_text())["maho"]["ai"]
    return {"api_key": cfg["api_key"], "base_url": cfg["base_url"], "model": cfg["model"]}


def mcp_call(calls: list, timeout: float = 30.0) -> list:
    """Runs an initialize handshake plus `calls` over the browser's MCP socket."""
    import socket

    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    sock.connect(str(SOCKET))
    try:
        def send(payload):
            sock.sendall((json.dumps(payload) + "\n").encode())

        def recv_one():
            buf = b""
            while b"\n" not in buf:
                chunk = sock.recv(65536)
                if not chunk:
                    break
                buf += chunk
            return json.loads(buf.decode(errors="replace").split("\n")[0])

        send({
            "jsonrpc": "2.0", "id": 0, "method": "initialize",
            "params": {
                "protocolVersion": "2025-03-26", "capabilities": {},
                "clientInfo": {"name": "maho-bench", "version": "1"},
            },
        })
        out = [recv_one()]
        for index, call in enumerate(calls, start=1):
            payload = dict(call, jsonrpc="2.0", id=index)
            send(payload)
            out.append(recv_one())
        return out
    finally:
        sock.close()


def require_live_browser() -> None:
    """Fails loudly when the browser is not actually reachable.

    A stale socket file left by an exited browser makes every browser tool call
    fail, and the agent then answers from narration instead of the page. Grading
    that as a task failure hides the real cause, so the precondition is asserted
    before any task runs.
    """
    if not SOCKET.exists():
        raise SystemExit(f"Maho MCP socket missing at {SOCKET}. Start Maho first.")
    try:
        responses = mcp_call([
            {"method": "tools/call", "params": {"name": "browser_tab_list", "arguments": {}}},
        ])
    except OSError as exc:
        raise SystemExit(
            f"Maho MCP socket at {SOCKET} is not accepting connections ({exc}). "
            "The socket file is stale; start Maho and retry."
        ) from exc
    for response in responses[1:]:
        if "error" in response:
            raise SystemExit(f"Browser precondition failed: {response['error']}")
    print(
        "precondition ok: browser reachable (default-allow navigation; "
        "no domain seeding required)",
        flush=True,
    )


def run_task(task: dict, cfg: dict, db_path: str) -> dict:
    env = dict(os.environ)
    env["MAHO_DB_PATH"] = db_path
    env["OPENAI_API_KEY"] = cfg["api_key"]
    env["MAHO_BYOK_BASE_URL"] = cfg["base_url"]
    cmd = [
        str(CLI), "--socket-path", str(SOCKET), "agent", "run",
        "--yolo", "--model", cfg["model"], task["prompt"],
    ]
    try:
        proc = subprocess.run(
            cmd, cwd=MAHO_ROOT, env=env, capture_output=True,
            text=True, timeout=TASK_TIMEOUT_S,
        )
        return {
            "status": "completed" if proc.returncode == 0 else "error",
            "returncode": proc.returncode,
            "stdout": proc.stdout[-8000:],
            "stderr": proc.stderr[-2000:],
        }
    except subprocess.TimeoutExpired:
        return {"status": "timeout", "returncode": None, "stdout": "", "stderr": ""}


def grade(task: dict, run: dict, cfg: dict) -> dict:
    if run["status"] != "completed":
        return {"passed": False, "reason": f"run {run['status']}"}
    prompt = (
        "You grade a browser agent. Answer strictly as JSON "
        '{"passed": true|false, "reason": "<one sentence>"}.\n\n'
        f"TASK: {task['prompt']}\n"
        f"SUCCESS CRITERION: {task['criterion']}\n\n"
        f"AGENT TRANSCRIPT:\n{run['stdout']}\n"
    )
    for attempt in range(2):
        body = json.dumps({
            "model": cfg["model"],
            "messages": [{"role": "user", "content": prompt}],
        }).encode()
        req = urllib.request.Request(
            cfg["base_url"].rstrip("/") + "/chat/completions",
            data=body,
            headers={
                "Content-Type": "application/json",
                "Authorization": "Bearer " + cfg["api_key"],
            },
        )
        try:
            with urllib.request.urlopen(req, timeout=120) as resp:
                payload = json.loads(resp.read())
            text = payload["choices"][0]["message"]["content"]
            start, end = text.find("{"), text.rfind("}")
            verdict = json.loads(text[start:end + 1])
            return {"passed": bool(verdict.get("passed")), "reason": verdict.get("reason", "")}
        except (json.JSONDecodeError, KeyError, IndexError) as exc:
            if attempt == 0:
                continue
            return {"passed": False, "reason": f"grader failed after retry: {exc}"}
        except Exception as exc:
            return {"passed": False, "reason": f"grader failed: {exc}"}


def main() -> int:
    tasks = json.loads((HERE / "tasks.json").read_text())
    cfg = browser_ai_config()
    require_live_browser()
    db_root = pathlib.Path(os.environ.get("BENCH_DB_PATH", "/tmp/maho-bench/fresh/bench.db"))
    db_root.parent.mkdir(parents=True, exist_ok=True)
    results = []
    for i, task in enumerate(tasks, start=1):
        print(f"[{i}/{len(tasks)}] {task['id']}: {task['prompt'][:70]}", flush=True)
        task_db_path = db_root.with_name(f"{db_root.stem}-{task['id']}{db_root.suffix}")
        for companion in (task_db_path, task_db_path.with_name("maho_storage.key")):
            companion.unlink(missing_ok=True)
        run = run_task(task, cfg, str(task_db_path))
        verdict = grade(task, run, cfg)
        print(f"    -> {'PASS' if verdict['passed'] else 'FAIL'}: {verdict['reason'][:110]}", flush=True)
        results.append({"task": task, "run": run, "verdict": verdict})
    passed = sum(1 for r in results if r["verdict"]["passed"])
    summary = {"total": len(results), "passed": passed, "failed": len(results) - passed}
    (HERE / "results.json").write_text(json.dumps({"summary": summary, "results": results}, indent=2))
    print(f"\n{passed}/{len(results)} passed")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
