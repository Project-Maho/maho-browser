from __future__ import annotations

import importlib.util
from pathlib import Path
import sys

import pytest

FIXTURE_PATH = Path(__file__).with_name("macos_vm_evidence_test_fixture.py")
FIXTURE_SPEC = importlib.util.spec_from_file_location(
    "macos_vm_evidence_test_fixture",
    FIXTURE_PATH,
)
if FIXTURE_SPEC is None or FIXTURE_SPEC.loader is None:
    raise RuntimeError(f"unable to load {FIXTURE_PATH}")
fixture = importlib.util.module_from_spec(FIXTURE_SPEC)
sys.modules[FIXTURE_SPEC.name] = fixture
FIXTURE_SPEC.loader.exec_module(fixture)
receipt = fixture.receipt
tree = fixture.tree
write_json = fixture.write_json

MODULE_PATH = Path(__file__).with_name("macos_vm_verify_evidence.py")
SPEC = importlib.util.spec_from_file_location("macos_vm_verify_evidence", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"unable to load {MODULE_PATH}")
verifier = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = verifier
SPEC.loader.exec_module(verifier)
GTESTS = verifier.GTESTS
REQUIRED_GTEST = verifier.REQUIRED_GTEST
SCENARIOS = verifier.SCENARIOS
verify = verifier.verify


def _tree(tmp_path: Path) -> tuple[Path, dict[str, str], Path]:
    return tree(tmp_path, REQUIRED_GTEST, GTESTS, SCENARIOS)


def test_accepts_production_shaped_seven_run_tree(tmp_path: Path) -> None:
    root, ids, inventory = _tree(tmp_path)
    assert verify(root, ids, inventory) == {
        "valid": True,
        "run_count": 7,
        "issues": [],
    }


def test_accepts_one_preserved_primary_failure_and_one_fallback(
    tmp_path: Path,
) -> None:
    root, ids, inventory = _tree(tmp_path)
    primary_path = root / ids["primary"] / "receipt.json"
    output_root = tmp_path / "workspace/chromium/src/out/Default"
    primary = receipt(
        ids["primary"],
        (),
        REQUIRED_GTEST,
        GTESTS,
        SCENARIOS,
        output_root,
        terminal="transport_failed",
    )
    primary["host_facts"].update(
        requested_suite="interactive-smoke",
        requested_gtest=REQUIRED_GTEST,
        required_transport="virtiofs-read-only",
    )
    write_json(primary_path, primary)
    write_json(
        root / ids["primary"] / "host-isolation.json",
        {
            "schema_version": 1,
            "evidence_kind": "early_exit",
            "is_isolated": True,
            "violation_codes": [],
        },
    )

    fallback_id = "primary-fallback"
    ids["primary_fallback"] = fallback_id
    fallback_dir = root / fallback_id
    fallback = receipt(
        fallback_id,
        ("interactive-smoke",),
        REQUIRED_GTEST,
        GTESTS,
        SCENARIOS,
        output_root,
        selected=True,
    )
    fallback["host_facts"].pop("required_transport")
    fallback["guest_facts"]["transport"] = "runtime-deps-rsync"
    write_json(fallback_dir / "receipt.json", fallback)
    fixture.build_fixture.build_proof(
        fallback_dir,
        output_root,
        GTESTS,
        fixture.write_json,
    )
    write_json(
        fallback_dir / "host-isolation.json",
        {
            "schema_version": 1,
            "evidence_kind": "live_run",
            "is_isolated": True,
            "violation_codes": [],
        },
    )
    write_json(
        fallback_dir / "guest-artifacts/interactive-test-summary.json",
        {
            "per_iteration_data": [
                {REQUIRED_GTEST: [{"status": "SUCCESS"}]}
            ]
        },
    )

    assert verify(root, ids, inventory)["valid"] is True


@pytest.mark.parametrize(
    ("mutate", "issue"),
    (
        (
            lambda root, ids, _inventory: (
                root
                / ids["primary"]
                / "guest-artifacts/interactive-test-summary.json"
            ).write_text("{}", encoding="utf-8"),
            "raw interactive summary mismatch",
        ),
        (
            lambda root, ids, _inventory: (
                root / ids["ai"] / "ai-mock.stdout.log"
            ).write_text(
                "PROFILE=/tmp/reused\nPASS fake — ok",
                encoding="utf-8",
            ),
            "raw AI PASS lines mismatch",
        ),
        (
            lambda root, ids, _inventory: write_json(
                root / ids["all_b"] / "host-isolation.json",
                {
                    "evidence_kind": "live_run",
                    "is_isolated": False,
                    "violation_codes": ["x"],
                },
            ),
            "host isolation failed",
        ),
        (
            lambda root, ids, _inventory: write_json(
                root / ids["cancel"] / "receipt.json",
                receipt(
                    ids["cancel"],
                    (),
                    REQUIRED_GTEST,
                    GTESTS,
                    SCENARIOS,
                    root.parent / "workspace/chromium/src/out/Default",
                    terminal="cancelled",
                    cancel=False,
                ),
            ),
            "cancel: checkpoint",
        ),
        (
            lambda _root, _ids, inventory: write_json(
                inventory,
                [{"Name": "maho-e2e-leak", "State": "running"}],
            ),
            "inventory is not",
        ),
        (
            lambda _root, _ids, inventory: (
                inventory.parent / "matrix.stderr.log"
            ).write_text(
                "MAHO_E2E_AI_API_KEY=raw-secret",
                encoding="utf-8",
            ),
            "raw sensitive environment assignment",
        ),
    ),
)
def test_rejects_cross_evidence_mismatch(
    tmp_path: Path,
    mutate,
    issue: str,
) -> None:
    root, ids, inventory = _tree(tmp_path)
    mutate(root, ids, inventory)
    result = verify(root, ids, inventory)
    assert result["valid"] is False
    assert any(issue in str(value) for value in result["issues"])
