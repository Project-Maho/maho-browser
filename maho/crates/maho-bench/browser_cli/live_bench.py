#!/usr/bin/env python3
"""Live V1-vs-V2 browser automation benchmark.

Drives a running Maho browser through identical W1/W2/W5 action sequences:
V1 = full AX snapshot fan-out + ref resolution + input.click/type by ref,
one `maho tool run` process per RPC. V2 = one persistent `maho browser pipe
--jsonl` session with observe(interactive) + locator ops, observe=diff.
Writes maho/crates/maho-bench/browser_cli/live-results/live_compare.json.
"""

import json
import os
import queue
import re
import subprocess
import sys
import threading
import time
from http.server import HTTPServer, SimpleHTTPRequestHandler
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent
FIXTURES = HERE / "fixtures"
OUT = HERE / "live-results"
PORT = 8931
SOCKET = os.environ.get("BENCH_SOCKET")
DEFAULT_MAHO = HERE.parents[2] / "target" / "debug" / "maho"
MAHO_BIN = os.environ.get("BENCH_MAHO", str(DEFAULT_MAHO) if DEFAULT_MAHO.exists() else "maho")
ENV = {**os.environ, "MAHO_BROWSER_PERF_TRACE": "1"}


def cli_args(*cmd: str) -> list[str]:
    return ([MAHO_BIN, "--socket-path", SOCKET] if SOCKET else [MAHO_BIN]) + list(cmd)

Metrics = dict[str, float]
WIRE_RE = re.compile(r"wire_bytes(?:_in|_out)?\D*(\d+)")
AX_ROLES = {"textbox", "button", "checkbox", "combobox", "link"}


def start_server() -> HTTPServer:
    handler = lambda *a, **kw: SimpleHTTPRequestHandler(*a, directory=str(FIXTURES), **kw)
    srv = HTTPServer(("127.0.0.1", PORT), handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


def wire_bytes_of(text: str) -> int:
    return sum(int(m.group(1) or m.group(2)) for m in WIRE_RE.finditer(text))


def tool_run(cap: str, args: dict[str, Any], timeout: float = 90.0):
    t0 = time.perf_counter()
    cmd = cli_args("tool", "run", cap, "--args", json.dumps(args))
    p = subprocess.run(
        cmd,
        capture_output=True, text=True, timeout=timeout, env=ENV,
    )
    wall_ms = (time.perf_counter() - t0) * 1000
    if p.returncode != 0:
        raise RuntimeError(f"{cap} failed rc={p.returncode}: {p.stderr[-500:]}")
    return {"text": p.stdout, "wire": wire_bytes_of(p.stderr) or len(p.stdout.encode()), "wall_ms": wall_ms}


def split_needles(needles: tuple[str, ...]) -> tuple[str, str]:
    """Split a step needle tuple into (accessible-name, ax-role)."""
    name = next(n for n in needles if n not in AX_ROLES)
    role = next((n for n in needles if n in AX_ROLES), "button")
    return name, role


def resolve_ref(snapshot_stdout: str, needles: tuple[str, ...]) -> int | None:
    try:
        idx = snapshot_stdout.index("{")
        d = json.JSONDecoder().raw_decode(snapshot_stdout[idx:])[0]
    except (ValueError, json.JSONDecodeError):
        return None
    lowered = [t.lower() for t in needles]
    hits: list[int] = []

    def walk(node: dict[str, Any]) -> None:
        name = str(node.get("name") or "")
        ref = node.get("ref")
        if ref is not None and all(t in name.lower() for t in lowered):
            hits.append(int(ref))
        for child in node.get("children") or []:
            walk(child)

    walk(d.get("result") or {})
    return hits[0] if hits else None


def cli_json(*cmd: str) -> dict[str, Any]:
    p = subprocess.run(
        cli_args(*cmd, "--json"),
        capture_output=True, text=True, timeout=90.0, env=ENV,
    )
    if p.returncode != 0:
        raise RuntimeError(f"cli {' '.join(cmd)} failed rc={p.returncode}: {p.stderr[-300:]}")
    return json.loads(p.stdout)


def ensure_bench_tab() -> int:
    out = cli_json("tab", "new", "--url", "about:blank")
    return int(out["result"]["tab"]["id"])


def tool_result(reply: dict[str, Any]) -> dict[str, Any]:
    text = reply["text"]
    value = json.JSONDecoder().raw_decode(text[text.index("{"):])[0]
    return value.get("result", value)


def navigation_ready(metrics: Metrics, tab_id: int, since_ms: int) -> None:
    reply = tool_run("navigation.wait", {"tab_id": tab_id, "since_timestamp_ms": since_ms, "timeout_ms": 30000})
    metrics["rpc"] += 1
    metrics["wire"] += reply["wire"]
    if not tool_result(reply).get("navigated"):
        raise RuntimeError("navigation did not complete")


class V1Driver:
    def __init__(self, metrics: Metrics, tab_id: int):
        self.metrics = metrics
        self.snapshot_text = ""
        self.tab_id = tab_id

    def navigate(self, url: str):
        since_ms = int(time.time() * 1000)
        tool_run("navigation.navigate", {"url": url, "tab_id": self.tab_id, "lease": "scoped"})
        self.metrics["rpc"] += 1
        navigation_ready(self.metrics, self.tab_id, since_ms)
        self.wait_for_selector("body")

    def refresh(self) -> None:
        r = tool_run("page.accessibility_snapshot", {"tab_id": self.tab_id})
        self.metrics["rpc"] += 1
        self.metrics["wire"] += r["wire"]
        self.snapshot_text = r["text"]

    def click(self, *needles: str):
        self.refresh()
        name, _ = split_needles(tuple(needles))
        ref = resolve_ref(self.snapshot_text, (name,))
        if ref is None:
            raise RuntimeError(f"V1 ref resolution failed for {needles}")
        r = tool_run("input.click", {"ref": ref, "tab_id": self.tab_id, "lease": "scoped"})
        self.metrics["rpc"] += 1
        self.metrics["wire"] += r["wire"]

    def type(self, text: str, *needles: str):
        self.refresh()
        name, _ = split_needles(tuple(needles))
        ref = resolve_ref(self.snapshot_text, (name,))
        if ref is None:
            raise RuntimeError(f"V1 ref resolution failed for {needles}")
        r = tool_run("input.type", {"ref": ref, "text": text, "tab_id": self.tab_id, "lease": "scoped"})
        self.metrics["rpc"] += 1
        self.metrics["wire"] += r["wire"]

    def select(self, value: str, *needles: str):
        self.refresh()
        name, _ = split_needles(tuple(needles))
        ref = resolve_ref(self.snapshot_text, (name,))
        if ref is None:
            raise RuntimeError(f"V1 ref resolution failed for {needles}")
        reply = tool_run("input.select", {"ref": ref, "value": value, "tab_id": self.tab_id, "lease": "scoped"})
        self.metrics["rpc"] += 1
        self.metrics["wire"] += reply["wire"]

    def wait_for_selector(self, selector: str):
        reply = tool_run("page.wait_for_selector", {"selector": selector, "tab_id": self.tab_id, "timeout_ms": 30000})
        self.metrics["rpc"] += 1
        self.metrics["wire"] += reply["wire"]
        if not tool_result(reply).get("found"):
            raise RuntimeError(f"required selector was not found: {selector}")

    def close(self):
        pass


def locator_of(needles: tuple[str, ...]) -> dict[str, str]:
    name, role = split_needles(needles)
    if name.startswith("#"):
        return {"css": name.lstrip("#") and name}
    return {"role": role, "name": name}


class V2Driver:
    def __init__(self, metrics: Metrics, tab_id: int):
        self.metrics = metrics
        self.tab_id = tab_id
        proc = subprocess.Popen(
            cli_args("browser", "pipe", "--jsonl"),
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, env=ENV, bufsize=1,
        )
        assert proc.stdin is not None and proc.stdout is not None and proc.stderr is not None
        self.proc = proc
        self.stdin = proc.stdin
        self.stdout = proc.stdout
        self.next_id = 0
        self.trace_lines: list[str] = []
        self.responses: queue.Queue = queue.Queue()
        threading.Thread(target=self._read_responses, daemon=True).start()
        threading.Thread(target=self._drain_stderr, args=(proc.stderr,), daemon=True).start()

    def _read_responses(self):
        try:
            for line in self.stdout:
                self.responses.put(line)
        except Exception as error:
            self.responses.put(error)
        finally:
            self.responses.put(None)

    def _drain_stderr(self, stderr):
        for line in stderr:
            self.trace_lines.append(line.rstrip())
            wb = wire_bytes_of(line)
            if wb:
                self.metrics["rpc"] += 1
                self.metrics["wire"] += wb

    def op(self, req: dict[str, Any], timeout: float = 30.0):
        self.next_id += 1
        req.setdefault("tab_id", self.tab_id)
        req = {"op": req.pop("op"), "id": self.next_id, **req}
        t0 = time.perf_counter()
        self.stdin.write(json.dumps(req) + "\n")
        self.stdin.flush()
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"pipe op {req['op']} timed out")
            try:
                line = self.responses.get(timeout=remaining)
            except queue.Empty as error:
                raise TimeoutError(f"pipe op {req['op']} timed out") from error
            if time.monotonic() >= deadline:
                raise TimeoutError(f"pipe op {req['op']} timed out")
            if isinstance(line, Exception):
                raise line
            if line is None:
                raise RuntimeError("pipe closed")
            resp = json.loads(line)
            if resp.get("id") == self.next_id:
                wall = (time.perf_counter() - t0) * 1000
                if not resp.get("ok"):
                    raise RuntimeError(f"pipe op {req['op']} error: {resp.get('error')}")
                return resp.get("result") or {}, wall

    def navigate(self, url: str):
        since_ms = int(time.time() * 1000)
        self.op({"op": "navigate", "url": url})
        self.metrics["rpc"] += 1
        navigation_ready(self.metrics, self.tab_id, since_ms)
        self.wait_for_selector("body")

    def select(self, value: str, *needles: str):
        # The pipe currently has no select operation. Use the supported ref
        # capability and charge its snapshot/process cost to V2's metrics.
        V1Driver(self.metrics, self.tab_id).select(value, *needles)

    def wait_for_selector(self, selector: str):
        result, _ = self.op({"op": "wait", "selector": selector, "timeout_ms": 30000})
        if not result.get("found"):
            raise RuntimeError(f"required selector was not found: {selector}")

    def observe(self, mode: str = "interactive"):
        res, w = self.op({"op": "observe", "mode": mode})
        self.metrics["observe_ms"] = self.metrics.get("observe_ms", 0) + w
        return res

    def click(self, *needles: str):
        res, w = self.op({"op": "click", "locator": locator_of(needles), "observe": "diff"})
        self.metrics["click_ms"] = self.metrics.get("click_ms", 0) + w
        return res

    def type(self, text: str, *needles: str):
        res, w = self.op({"op": "type", "locator": locator_of(needles), "text": text, "observe": "diff"})
        self.metrics["type_ms"] = self.metrics.get("type_ms", 0) + w
        return res

    def close(self):
        try:
            self.stdin.close()
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()


WORKLOAD_STEPS: dict[str, list[tuple[str, tuple[Any, ...]]]] = {
    "w1": [
        ("type", ("Indo Yoon", "Full Name", "textbox")),
        ("type", ("indo@mahobrowser.com", "Email Address", "textbox")),
        ("type", ("555-0199", "Phone Number", "textbox")),
        ("select", ("developer", "Primary Role", "combobox")),
        ("click", ("I accept the terms of service", "checkbox")),
        ("click", ("Submit Application",)),
        ("wait_for_selector", ("#validation-message.success",)),
    ],
    "w2": [
        ("click", ("Open Dialog",)),
        ("click", ("Prepend Banner & Update",)),
    ],
    "w5": [],
}

WORKLOAD_FIXTURES = {
    "w1": "w1_static_form.html",
    "w2": "w2_spa_modal.html",
    "w5": "w5_large_ax_tree.html",
}


def run_workload(driver, wid: str, url: str):
    driver.navigate(url)
    for verb, args in WORKLOAD_STEPS[wid]:
        getattr(driver, verb)(*args)
    if wid == "w5":
        if isinstance(driver, V2Driver):
            driver.observe("interactive")
        else:
            driver.refresh()


def new_metrics() -> Metrics:
    return {"wall_ms": 0.0, "rpc": 0.0, "wire": 0.0}


def avg(xs: list[float]) -> float:
    return sum(xs) / len(xs)


def summarize(samples: list[dict]) -> dict:
    def a(side: str, key: str) -> float:
        return avg([float(s[side][key]) for s in samples])

    v1w, v2w = a("v1", "wall_ms"), a("v2", "wall_ms")
    v1r, v2r = max(a("v1", "rpc"), 1e-9), max(a("v2", "rpc"), 1e-9)
    v1b, v2b = max(a("v1", "wire"), 1e-9), max(a("v2", "wire"), 1e-9)
    return {
        "v1_wall_ms": v1w, "v2_wall_ms": v2w,
        "v1_rpc": v1r, "v2_rpc": v2r,
        "v1_wire": v1b, "v2_wire": v2b,
        "wall_delta_pct": (v2w / max(v1w, 1e-9) - 1) * 100,
        "rpc_delta_pct": (v2r / v1r - 1) * 100,
        "wire_delta_pct": (v2b / v1b - 1) * 100,
    }


def main(runs: int = 3) -> int:
    OUT.mkdir(exist_ok=True)
    srv = start_server()
    results: dict[str, Any] = {}
    bench_tab = ensure_bench_tab()
    print(f"bench tab: {bench_tab}", flush=True)
    try:
        for wid in ("w1", "w2", "w5"):
            url = f"http://127.0.0.1:{PORT}/{WORKLOAD_FIXTURES[wid]}"
            samples = []
            for i in range(runs):
                m1, m2 = new_metrics(), new_metrics()
                d1, d2 = V1Driver(m1, bench_tab), V2Driver(m2, bench_tab)
                t0 = time.perf_counter()
                run_workload(d1, wid, url)
                m1["wall_ms"] = (time.perf_counter() - t0) * 1000
                t0 = time.perf_counter()
                run_workload(d2, wid, url)
                m2["wall_ms"] = (time.perf_counter() - t0) * 1000
                d1.close()
                d2.close()
                samples.append({"run": i, "v1": dict(m1), "v2": dict(m2)})
                print(f"[{wid}] run{i} v1={m1['wall_ms']:.0f}ms/{m1['rpc']:.0f}rpc "
                      f"v2={m2['wall_ms']:.0f}ms/{m2['rpc']:.0f}rpc", flush=True)
            results[wid] = {"runs": samples, "summary": summarize(samples)}
    finally:
        srv.shutdown()
        # NOTE: do not auto-close the bench tab here. Closing a tab from a
        # third-party-classified CLI pops a blocking approval dialog in the
        # browser and stalls every queued mutation. Clean up stray bench tabs
        # manually with the trusted bundle-helper CLI instead.
    artifact = OUT / "live_compare.json"
    artifact.write_text(json.dumps(results, indent=2))
    print(f"\nartifact: {artifact}")
    for wid, r in results.items():
        s = r["summary"]
        print(f"{wid}: v1 {s['v1_wall_ms']:.0f}ms/{s['v1_rpc']:.0f}rpc -> "
              f"v2 {s['v2_wall_ms']:.0f}ms/{s['v2_rpc']:.0f}rpc "
              f"(wall {s['wall_delta_pct']:+.0f}%, rpc {s['rpc_delta_pct']:+.0f}%, wire {s['wire_delta_pct']:+.0f}%)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
