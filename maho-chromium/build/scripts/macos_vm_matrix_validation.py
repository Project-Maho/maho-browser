from __future__ import annotations

import json
from pathlib import Path


def receipt(root: Path, run_id: str) -> dict[str, object]:
    path = root / run_id / "receipt.json"
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise RuntimeError(f"{run_id}: receipt is not an object")
    if (
        value.get("run_id") != run_id
        or value.get("schema_version") != 1
        or value.get("finalized") is not True
    ):
        raise RuntimeError(f"{run_id}: receipt identity/finalization mismatch")
    return value


def clean_isolation(root: Path, run_id: str, kind: str) -> bool:
    path = root / run_id / "host-isolation.json"
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False
    return (
        isinstance(value, dict)
        and value.get("evidence_kind") == kind
        and value.get("is_isolated") is True
        and value.get("violation_codes") == []
    )


def terminal_receipt(
    root: Path,
    step_name: str,
    run_id: str,
    actual_exit: int,
) -> dict[str, object]:
    value = receipt(root, run_id)
    expected_status = {
        0: "success",
        1: "suite_failed",
        130: "cancelled",
    }.get(actual_exit)
    if expected_status is not None and value.get("terminal_status") != expected_status:
        raise RuntimeError(
            f"{step_name}: exit {actual_exit} disagrees with terminal receipt"
        )
    return value


def step_contract(
    root: Path,
    step_name: str,
    run_id: str,
    value: dict[str, object],
) -> None:
    guest = value.get("guest_facts")
    guest_facts = guest if isinstance(guest, dict) else {}
    teardown = value.get("teardown_result")
    teardown_result = teardown if isinstance(teardown, dict) else {}
    if step_name == "primary":
        if guest_facts.get("transport") != "virtiofs-read-only":
            raise RuntimeError("primary: receipt transport is not VirtioFS read-only")
    elif step_name == "primary-fallback":
        if guest_facts.get("transport") != "runtime-deps-rsync":
            raise RuntimeError("primary-fallback: receipt transport is not rsync")
    elif step_name == "failure":
        if value.get("terminal_status") != "suite_failed":
            raise RuntimeError("failure: receipt does not preserve suite failure")
    elif step_name == "cancel":
        host = value.get("host_facts")
        host_facts = host if isinstance(host, dict) else {}
        if (
            value.get("terminal_status") != "cancelled"
            or host_facts.get("cancel_after_guest_ready") is not True
            or not guest_facts.get("guest_ready_at")
        ):
            raise RuntimeError("cancel: receipt lacks deterministic checkpoint")
    isolation_kind = "early_exit" if step_name == "cancel" else "live_run"
    if (
        teardown_result.get("succeeded") is not True
        or not clean_isolation(root, run_id, isolation_kind)
    ):
        raise RuntimeError(f"{step_name}: teardown or isolation evidence failed")


def eligible_primary_failure(root: Path, run_id: str) -> bool:
    value = receipt(root, run_id)
    guest = value.get("guest_facts")
    guest_facts = guest if isinstance(guest, dict) else {}
    teardown = value.get("teardown_result")
    teardown_result = teardown if isinstance(teardown, dict) else {}
    return (
        value.get("terminal_status") == "transport_failed"
        and guest_facts.get("first_error")
        == "required transport 'virtiofs-read-only' did not pass guest preflight"
        and teardown_result.get("succeeded") is True
        and clean_isolation(root, run_id, "early_exit")
    )
