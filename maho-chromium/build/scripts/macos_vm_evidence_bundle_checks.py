from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
from typing import TypeAlias

CHECKS_PATH = Path(__file__).with_name("macos_vm_evidence_checks.py")
SPEC = importlib.util.spec_from_file_location("macos_vm_evidence_checks", CHECKS_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"unable to load {CHECKS_PATH}")
checks = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = checks
SPEC.loader.exec_module(checks)

Json: TypeAlias = None | bool | int | float | str | list["Json"] | dict[str, "Json"]


def check_identities(
    ids: dict[str, str],
    receipts: dict[str, dict[str, Json]],
    issues: list[str],
) -> None:
    checks.check(
        len(set(ids.values())) == len(ids),
        issues,
        "run IDs are not distinct",
    )
    for key in ("clone", "guest_run_dir"):
        values = {
            str(checks.as_object(receipt.get("guest_facts")).get(key, ""))
            for receipt in receipts.values()
        }
        checks.check(
            "" not in values and len(values) == len(receipts),
            issues,
            f"{key} values differ",
        )
    all_profiles: list[tuple[str, ...]] = []
    for role in ("ai", "all_a", "all_b"):
        guest_run = str(
            checks.as_object(receipts[role].get("guest_facts")).get(
                "guest_run_dir"
            )
        )
        profiles = tuple(
            str(value)
            for value in checks.as_list(checks.as_object(checks.suite(
                receipts[role], "ai-mock"
            ).get("details")).get("profile_paths"))
        )
        expected_parents = {
            f"{guest_run}/ai-mock-tmp",
            f"{guest_run}/ai-mock-no-key-tmp",
        }
        checks.check(
            len(profiles) == 2
            and {str(Path(value).parent) for value in profiles} == expected_parents
            and all(Path(value).name.startswith("maho-ai-e2e-") for value in profiles),
            issues,
            f"{role}: profile paths escape declared guest TMPDIRs",
        )
        if role != "ai":
            all_profiles.append(profiles)
    checks.check(
        len(all_profiles) == 2 and all_profiles[0] != all_profiles[1],
        issues,
        "all-run profile paths are not distinct",
    )


def check_non_successes(
    root: Path,
    ids: dict[str, str],
    receipts: dict[str, dict[str, Json]],
    issues: list[str],
) -> None:
    failure = receipts["failure"]
    failure_dir = root / ids["failure"]
    statuses = {
        str(checks.as_object(value).get("name")): checks.as_object(value).get(
            "status"
        )
        for value in checks.as_list(failure.get("suite_results"))
    }
    diagnostic = checks.load_object(
        failure_dir / "guest-artifacts/intentional-failure.json",
        issues,
    )
    checks.check(
        failure.get("primary_status") == failure.get("terminal_status")
        == "suite_failed"
        and statuses == {"intentional-failure-fixture": "failed"}
        and diagnostic.get("fixture") == "intentional-failure-fixture"
        and diagnostic.get("intentional") is True
        and diagnostic.get("exit_code") == 86
        and bool(checks.as_list(failure.get("artifact_paths")))
        and (failure_dir / "guest-artifacts").is_dir()
        and checks.as_object(failure.get("teardown_result")).get("succeeded")
        is True,
        issues,
        "failure: status, diagnostic, or teardown mismatch",
    )
    checks.check_isolation(failure_dir, "live_run", issues)

    cancel = receipts["cancel"]
    host = checks.as_object(cancel.get("host_facts"))
    guest = checks.as_object(cancel.get("guest_facts"))
    checks.check(
        cancel.get("primary_status") == cancel.get("terminal_status") == "cancelled"
        and host.get("cancel_after_guest_ready") is True
        and bool(guest.get("guest_ready_at"))
        and bool(guest.get("guest_run_dir"))
        and not any(
            checks.as_object(value).get("status") == "success"
            for value in checks.as_list(cancel.get("suite_results"))
        )
        and bool(checks.as_list(cancel.get("artifact_paths")))
        and (root / ids["cancel"] / "guest-artifacts").is_dir()
        and checks.as_object(cancel.get("teardown_result")).get("succeeded") is True,
        issues,
        "cancel: checkpoint, status, or teardown mismatch",
    )
    checks.check_isolation(root / ids["cancel"], "early_exit", issues)
