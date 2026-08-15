#!/usr/bin/env python3

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import re
import sys
from typing import TypeAlias

LOADER_PATH = Path(__file__).with_name("macos_vm_module_loader.py")
LOADER_SPEC = importlib.util.spec_from_file_location(
    "macos_vm_module_loader",
    LOADER_PATH,
)
if LOADER_SPEC is None or LOADER_SPEC.loader is None:
    raise RuntimeError(f"unable to load {LOADER_PATH}")
loader = importlib.util.module_from_spec(LOADER_SPEC)
sys.modules[LOADER_SPEC.name] = loader
LOADER_SPEC.loader.exec_module(loader)

checks = loader.load_sibling(__file__, "macos_vm_evidence_checks")
bundle_checks = loader.load_sibling(__file__, "macos_vm_evidence_bundle_checks")
privacy = loader.load_sibling(__file__, "macos_vm_evidence_privacy")
build_checks = loader.load_sibling(__file__, "macos_vm_evidence_build")

Json: TypeAlias = None | bool | int | float | str | list["Json"] | dict[str, "Json"]
FORBIDDEN, GTESTS = checks.FORBIDDEN, checks.GTESTS
REQUIRED_GTEST, SCENARIOS = checks.REQUIRED_GTEST, checks.SCENARIOS
as_list = checks.as_list
as_object = checks.as_object
check = checks.check
check_ai_raw = checks.check_ai_raw
check_artifact = checks.check_artifact
check_interactive_raw = checks.check_interactive_raw
check_inventory = checks.check_inventory
check_isolation = checks.check_isolation
check_success = checks.check_success
load_object = checks.load_object
suite = checks.suite
RUN_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")


def _load_receipts(
    root: Path,
    ids: dict[str, str],
    issues: list[str],
) -> dict[str, dict[str, Json]]:
    receipts: dict[str, dict[str, Json]] = {}
    for role, run_id in ids.items():
        if not RUN_ID.fullmatch(run_id) or run_id in {".", ".."}:
            issues.append(f"{role}: invalid run ID {run_id!r}")
            receipts[role] = {}
            continue
        run_dir = root / run_id
        receipt = load_object(run_dir / "receipt.json", issues)
        receipts[role] = receipt
        check(
            receipt.get("schema_version") == 1
            and receipt.get("finalized") is True
            and receipt.get("run_id") == run_id,
            issues,
            f"{run_id}: receipt identity/finalization mismatch",
        )
        for value in as_list(receipt.get("artifact_paths")):
            check_artifact(run_dir, value, issues)
        for suite_value in as_list(receipt.get("suite_results")):
            for value in as_list(as_object(suite_value).get("artifact_paths")):
                check_artifact(run_dir, value, issues)
        guest_artifacts = run_dir / "guest-artifacts"
        if guest_artifacts.exists():
            for path in guest_artifacts.rglob("*"):
                check(
                    not FORBIDDEN.search(path.name),
                    issues,
                    f"{run_id}: forbidden artifact name: {path.name}",
                )
    return receipts


def _check_successes(
    root: Path,
    ids: dict[str, str],
    receipts: dict[str, dict[str, Json]],
    issues: list[str],
) -> None:
    primary = receipts["primary"]
    host = as_object(primary.get("host_facts"))
    guest = as_object(primary.get("guest_facts"))
    primary_succeeded = primary.get("terminal_status") == "success"
    if primary_succeeded:
        check_success(root / ids["primary"], primary, {"interactive-smoke"}, issues)
        check(
            "primary_fallback" not in receipts
            and host.get("requested_suite") == "interactive-smoke"
            and host.get("requested_gtest") == REQUIRED_GTEST
            and host.get("required_transport") == guest.get("transport")
            == "virtiofs-read-only",
            issues,
            "primary: suite, gtest, transport, or fallback mismatch",
        )
        selected_receipt = primary
        selected_dir = root / ids["primary"]
    else:
        fallback = receipts.get("primary_fallback", {})
        fallback_host = as_object(fallback.get("host_facts"))
        fallback_guest = as_object(fallback.get("guest_facts"))
        check(
            primary.get("primary_status") == primary.get("terminal_status")
            == "transport_failed"
            and host.get("required_transport") == "virtiofs-read-only"
            and not any(
                as_object(value).get("status") == "success"
                for value in as_list(primary.get("suite_results"))
            )
            and as_object(primary.get("teardown_result")).get("succeeded") is True,
            issues,
            "primary: required VirtioFS failure was not preserved",
        )
        check_isolation(root / ids["primary"], "early_exit", issues)
        check_success(
            root / ids.get("primary_fallback", ""),
            fallback,
            {"interactive-smoke"},
            issues,
        )
        check(
            fallback_host.get("requested_gtest") == REQUIRED_GTEST
            and fallback_host.get("required_transport") is None
            and fallback_guest.get("transport") == "runtime-deps-rsync",
            issues,
            "primary fallback: gtest or transport mismatch",
        )
        selected_receipt = fallback
        selected_dir = root / ids.get("primary_fallback", "")
    check(
        {
            str(value)
            for value in as_list(as_object(suite(
                selected_receipt, "interactive-smoke"
            ).get("details")).get("expected"))
        }
        == {REQUIRED_GTEST},
        issues,
        "primary: receipt expected test mismatch",
    )
    check_interactive_raw(selected_dir, {REQUIRED_GTEST}, issues)

    for role, suites in (
        ("interactive", {"interactive-smoke"}),
        ("ai", {"ai-mock"}),
        ("all_a", {"interactive-smoke", "ai-mock"}),
        ("all_b", {"interactive-smoke", "ai-mock"}),
    ):
        check_success(root / ids[role], receipts[role], suites, issues)
    check_interactive_raw(root / ids["interactive"], GTESTS, issues)
    check_ai_raw(root / ids["ai"], receipts["ai"], issues)
    check(
        {
            str(value)
            for value in as_list(as_object(suite(
                receipts["interactive"], "interactive-smoke"
            ).get("details")).get("expected"))
        }
        == GTESTS,
        issues,
        "interactive: receipt expected tests mismatch",
    )
    check(
        {
            str(value)
            for value in as_list(as_object(suite(
                receipts["ai"], "ai-mock"
            ).get("details")).get("scenarios"))
        }
        == SCENARIOS,
        issues,
        "ai: receipt scenarios mismatch",
    )
    for role in ("all_a", "all_b"):
        check(
            {
                str(value)
                for value in as_list(as_object(suite(
                    receipts[role], "interactive-smoke"
                ).get("details")).get("expected"))
            }
            == GTESTS
            and {
                str(value)
                for value in as_list(as_object(suite(
                    receipts[role], "ai-mock"
                ).get("details")).get("scenarios"))
            }
            == SCENARIOS,
            issues,
            f"{role}: receipt suite details mismatch",
        )
        check_interactive_raw(root / ids[role], GTESTS, issues)
        check_ai_raw(root / ids[role], receipts[role], issues)


def verify(
    root: Path,
    ids: dict[str, str],
    inventory: Path,
    matrix_dir: Path | None = None,
) -> dict[str, Json]:
    issues: list[str] = []
    receipts = _load_receipts(root, ids, issues)
    _check_successes(root, ids, receipts, issues)
    bundle_checks.check_identities(ids, receipts, issues)
    bundle_checks.check_non_successes(root, ids, receipts, issues)
    identities = {
        (
            json.dumps(receipt.get("source_image_identity"), sort_keys=True),
            str(receipt.get("build_path")),
            str(as_object(receipt.get("host_facts")).get("source_revision")),
        )
        for receipt in receipts.values()
    }
    check(len(identities) == 1, issues, "source image, revision, or build differs")
    build_checks.check_build_evidence(
        root,
        ids,
        receipts,
        GTESTS,
        load_object,
        issues,
    )
    check_inventory(inventory, issues)
    privacy.scan_evidence(
        root,
        tuple(ids.values()),
        matrix_dir or inventory.parent,
        issues,
    )
    issue_values: list[Json] = list(issues)
    return {"valid": not issues, "run_count": len(receipts), "issues": issue_values}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--matrix-dir", type=Path, required=True)
    roles = ("primary", "interactive", "ai", "all-a", "all-b", "failure", "cancel")
    for role in roles:
        parser.add_argument(f"--{role}", required=True)
    parser.add_argument("--primary-fallback")
    args = parser.parse_args(argv)
    ids = {
        role: getattr(args, role)
        for role in ("primary", "interactive", "ai", "failure", "cancel")
    }
    ids.update(all_a=args.all_a, all_b=args.all_b)
    if args.primary_fallback is not None:
        ids["primary_fallback"] = args.primary_fallback
    result = verify(args.root, ids, args.inventory, args.matrix_dir)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
