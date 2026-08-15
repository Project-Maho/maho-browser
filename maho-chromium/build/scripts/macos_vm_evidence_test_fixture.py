from __future__ import annotations

import json
import importlib.util
from pathlib import Path
import sys

BUILD_FIXTURE_PATH = Path(__file__).with_name("macos_vm_evidence_build_fixture.py")
BUILD_FIXTURE_SPEC = importlib.util.spec_from_file_location(
    "macos_vm_evidence_build_fixture", BUILD_FIXTURE_PATH
)
if BUILD_FIXTURE_SPEC is None or BUILD_FIXTURE_SPEC.loader is None:
    raise RuntimeError(f"unable to load {BUILD_FIXTURE_PATH}")
build_fixture = importlib.util.module_from_spec(BUILD_FIXTURE_SPEC)
sys.modules[BUILD_FIXTURE_SPEC.name] = build_fixture
BUILD_FIXTURE_SPEC.loader.exec_module(build_fixture)


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value), encoding="utf-8")


def _isolation(kind: str) -> dict[str, object]:
    return {
        "schema_version": 1,
        "evidence_kind": kind,
        "is_isolated": True,
        "violation_codes": [],
    }


def _summary(tests: set[str]) -> dict[str, object]:
    return {
        "per_iteration_data": [
            {name: [{"status": "SUCCESS"}] for name in sorted(tests)}
        ]
    }


def _ai_logs(
    run_dir: Path,
    profiles: tuple[str, str],
    scenarios: set[str],
) -> None:
    keyed = sorted(scenarios - {"ai_mock_key_cleared_no_request"})
    lines = [
        f"PROFILE={profiles[0]}",
        "MOCK_LLM base_url=http://127.0.0.1:12345",
        *(f"PASS {name} — ok" for name in keyed),
    ]
    no_key = [
        f"PROFILE={profiles[1]}",
        "MOCK_LLM base_url=http://127.0.0.1:12346",
        "PASS ai_mock_key_cleared_no_request — ok",
    ]
    (run_dir / "ai-mock.stdout.log").write_text("\n".join(lines), encoding="utf-8")
    (run_dir / "ai-mock-no-key.stdout.log").write_text(
        "\n".join(no_key),
        encoding="utf-8",
    )



def receipt(
    run_id: str,
    suites: tuple[str, ...],
    required_gtest: str,
    gtests: set[str],
    scenarios: set[str],
    build_path: Path,
    *,
    terminal: str = "success",
    selected: bool = False,
    cancel: bool = False,
) -> dict[str, object]:
    guest_run = f"/Users/admin/maho-e2e-runs/{run_id}"
    profiles = (
        f"{guest_run}/ai-mock-tmp/maho-ai-e2e-a",
        f"{guest_run}/ai-mock-no-key-tmp/maho-ai-e2e-b",
    )
    results = []
    for name in suites:
        details: dict[str, object] = {}
        if name == "interactive-smoke":
            details["expected"] = [required_gtest] if selected else sorted(gtests)
        elif name == "ai-mock":
            details = {
                "scenarios": sorted(scenarios),
                "profile_paths": list(profiles),
            }
        results.append(
            {
                "name": name,
                "status": "failed" if terminal == "suite_failed" else "success",
                "details": details,
            }
        )
    return {
        "schema_version": 1,
        "run_id": run_id,
        "finalized": True,
        "primary_status": terminal,
        "terminal_status": terminal,
        "host_facts": {
            "source_revision": "abc123",
            "workspace": str(build_path.parents[3]),
            "requested_suite": suites[0] if suites else "all",
            **({"requested_gtest": required_gtest} if selected else {}),
            **({"required_transport": "virtiofs-read-only"} if selected else {}),
            "cancel_after_guest_ready": cancel,
        },
        "guest_facts": {
            "clone": f"maho-e2e-{run_id}",
            "guest_run_dir": guest_run,
            "guest_ready_at": "2026-08-13T00:00:00Z",
            "transport": "virtiofs-read-only",
        },
        "source_image_identity": {
            "source_identity": "base@sha256:abc",
            "golden_image": "maho-e2e-golden",
        },
        "build_path": str(build_path),
        "suite_results": results,
        "artifact_paths": [],
        "isolation_snapshots": [
            {"stage": "baseline"},
            {"stage": "during"},
            {"stage": "after"},
        ],
        "teardown_result": {"attempted": True, "succeeded": True},
    }


def tree(
    tmp_path: Path,
    required_gtest: str,
    gtests: set[str],
    scenarios: set[str],
) -> tuple[Path, dict[str, str], Path]:
    root = tmp_path / "runs"
    output_root = tmp_path / "workspace/chromium/src/out/Default"
    ids = {
        role: role.replace("_", "-")
        for role in (
            "primary",
            "interactive",
            "ai",
            "all_a",
            "all_b",
            "failure",
            "cancel",
        )
    }
    specs = {
        "primary": (("interactive-smoke",), True),
        "interactive": (("interactive-smoke",), False),
        "ai": (("ai-mock",), False),
        "all_a": (("interactive-smoke", "ai-mock"), False),
        "all_b": (("interactive-smoke", "ai-mock"), False),
    }
    for role, (suites, selected) in specs.items():
        run_dir = root / ids[role]
        value = receipt(
            ids[role],
            suites,
            required_gtest,
            gtests,
            scenarios,
            output_root,
            selected=selected,
        )
        write_json(run_dir / "receipt.json", value)
        build_fixture.build_proof(run_dir, output_root, gtests, write_json)
        write_json(
            run_dir / "host-isolation.json",
            _isolation("live_run"),
        )
        if "interactive-smoke" in suites:
            expected = {required_gtest} if selected else gtests
            write_json(
                run_dir / "guest-artifacts/interactive-test-summary.json",
                _summary(expected),
            )
        if "ai-mock" in suites:
            guest_run = f"/Users/admin/maho-e2e-runs/{ids[role]}"
            _ai_logs(
                run_dir,
                (
                    f"{guest_run}/ai-mock-tmp/maho-ai-e2e-a",
                    f"{guest_run}/ai-mock-no-key-tmp/maho-ai-e2e-b",
                ),
                scenarios,
            )

    failure_dir = root / ids["failure"]
    write_json(
        failure_dir / "receipt.json",
        receipt(
            ids["failure"],
            ("intentional-failure-fixture",),
            required_gtest,
            gtests,
            scenarios,
            output_root,
            terminal="suite_failed",
        ),
    )
    build_fixture.build_proof(failure_dir, output_root, gtests, write_json)
    write_json(
        failure_dir / "guest-artifacts/intentional-failure.json",
        {
            "fixture": "intentional-failure-fixture",
            "intentional": True,
            "exit_code": 86,
        },
    )
    write_json(
        failure_dir / "host-isolation.json",
        _isolation("live_run"),
    )
    failure_receipt_path = failure_dir / "receipt.json"
    failure_receipt = json.loads(failure_receipt_path.read_text(encoding="utf-8"))
    failure_receipt["artifact_paths"] = [str(failure_dir / "guest-artifacts")]
    write_json(failure_receipt_path, failure_receipt)

    cancel_dir = root / ids["cancel"]
    write_json(
        cancel_dir / "receipt.json",
        receipt(
            ids["cancel"],
            (),
            required_gtest,
            gtests,
            scenarios,
            output_root,
            terminal="cancelled",
            cancel=True,
        ),
    )
    build_fixture.build_proof(cancel_dir, output_root, gtests, write_json)
    write_json(
        cancel_dir / "host-isolation.json",
        _isolation("early_exit"),
    )
    (cancel_dir / "guest-artifacts").mkdir(parents=True)
    cancel_receipt_path = cancel_dir / "receipt.json"
    cancel_receipt = json.loads(cancel_receipt_path.read_text(encoding="utf-8"))
    cancel_receipt["artifact_paths"] = [str(cancel_dir / "guest-artifacts")]
    write_json(cancel_receipt_path, cancel_receipt)
    inventory = tmp_path / "inventory.json"
    write_json(
        inventory,
        [
            {
                "Name": "maho-e2e-golden",
                "State": "stopped",
                "Running": False,
            },
            {
                "Name": "ghcr.io/cirruslabs/macos-tahoe-base@sha256:abc",
                "State": "stopped",
                "Running": False,
            },
        ],
    )
    return root, ids, inventory
