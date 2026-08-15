from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import sys

import pytest

FIXTURE_PATH = Path(__file__).with_name("macos_vm_evidence_test_fixture.py")
FIXTURE_SPEC = importlib.util.spec_from_file_location(
    "macos_vm_evidence_test_fixture_build",
    FIXTURE_PATH,
)
if FIXTURE_SPEC is None or FIXTURE_SPEC.loader is None:
    raise RuntimeError(f"unable to load {FIXTURE_PATH}")
fixture = importlib.util.module_from_spec(FIXTURE_SPEC)
sys.modules[FIXTURE_SPEC.name] = fixture
FIXTURE_SPEC.loader.exec_module(fixture)

VERIFY_PATH = Path(__file__).with_name("macos_vm_verify_evidence.py")
VERIFY_SPEC = importlib.util.spec_from_file_location(
    "macos_vm_verify_evidence_build",
    VERIFY_PATH,
)
if VERIFY_SPEC is None or VERIFY_SPEC.loader is None:
    raise RuntimeError(f"unable to load {VERIFY_PATH}")
verifier = importlib.util.module_from_spec(VERIFY_SPEC)
sys.modules[VERIFY_SPEC.name] = verifier
VERIFY_SPEC.loader.exec_module(verifier)


def _tree(tmp_path: Path):
    return fixture.tree(
        tmp_path,
        verifier.REQUIRED_GTEST,
        verifier.GTESTS,
        verifier.SCENARIOS,
    )


@pytest.mark.parametrize(
    "argv",
    (
        [
            sys.executable,
            "maho-chromium/build/scripts/build_maho.py",
            "--out-dir",
            "out/VM-E2E",
            "--no-launch",
            "--ninja-target",
            "chrome",
            "--ninja-target",
            "interactive_ui_tests",
        ],
        [
            sys.executable,
            "maho-chromium/build/scripts/build_maho.py",
            "--out-dir",
            "out/Release",
            "--no-launch",
            "--ninja-target",
            "chrome",
            "--ninja-target",
            "interactive_ui_tests",
        ],
        [
            sys.executable,
            "maho-chromium/build/scripts/build_maho.py",
            "--out-dir",
            "out/Narrow",
            "--no-launch",
            "--ninja-target",
            "chrome",
            "--ninja-target",
            "interactive_ui_tests",
        ],
        ["autoninja", "-C", "out/Default", "chrome", "interactive_ui_tests"],
    ),
)
def test_rejects_non_default_canonical_build_argv(
    tmp_path: Path,
    argv: list[str],
) -> None:
    root, ids, inventory = _tree(tmp_path)
    path = root / ids["ai"] / "build-preflight.json"
    proof = json.loads(path.read_text(encoding="utf-8"))
    proof["canonical_build_argv"] = argv
    fixture.write_json(path, proof)
    result = verifier.verify(root, ids, inventory)
    assert result["valid"] is False
    assert any(
        "build preflight evidence mismatch" in str(value)
        for value in result["issues"]
    )


@pytest.mark.parametrize(
    "change",
    (
        lambda proof: proof.update(app_signature_valid=False),
        lambda proof: proof.update(architecture="x86_64"),
        lambda proof: proof.update(listed_tests=[]),
    ),
)
def test_rejects_tampered_build_proof(tmp_path: Path, change) -> None:
    root, ids, inventory = _tree(tmp_path)
    path = root / ids["ai"] / "build-preflight.json"
    proof = json.loads(path.read_text(encoding="utf-8"))
    change(proof)
    fixture.write_json(path, proof)
    result = verifier.verify(root, ids, inventory)
    assert result["valid"] is False
    assert any(
        "build preflight evidence mismatch" in str(value)
        for value in result["issues"]
    )


def test_accepts_evidence_without_external_output_volume_uuid(
    tmp_path: Path,
) -> None:
    root, ids, inventory = _tree(tmp_path)
    for run_id in ids.values():
        path = root / run_id / "build-preflight.json"
        proof = json.loads(path.read_text(encoding="utf-8"))
        proof["output_volume"] = {"VolumeUUID": run_id}
        fixture.write_json(path, proof)
    assert verifier.verify(root, ids, inventory)["valid"] is True


def test_rejects_internally_consistent_alternate_output(tmp_path: Path) -> None:
    root, ids, inventory = _tree(tmp_path)
    alternate = tmp_path / "workspace/chromium/src/out/Release"
    for run_id in ids.values():
        run_dir = root / run_id
        receipt_path = run_dir / "receipt.json"
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
        receipt["build_path"] = str(alternate)
        fixture.write_json(receipt_path, receipt)
        fixture.build_fixture.build_proof(
            run_dir,
            alternate,
            verifier.GTESTS,
            fixture.write_json,
        )
    result = verifier.verify(root, ids, inventory)
    assert result["valid"] is False
    assert any(
        "build preflight evidence mismatch" in str(value)
        for value in result["issues"]
    )


def test_rejects_default_symlink_resolving_elsewhere(tmp_path: Path) -> None:
    root, ids, inventory = _tree(tmp_path)
    output_root = Path(
        json.loads(
            (root / ids["primary"] / "receipt.json").read_text(
                encoding="utf-8"
            )
        )["build_path"]
    )
    alternate = output_root.with_name("Release")
    output_root.rename(alternate)
    output_root.symlink_to(alternate, target_is_directory=True)
    result = verifier.verify(root, ids, inventory)
    assert result["valid"] is False
    assert any(
        "build preflight evidence mismatch" in str(value)
        for value in result["issues"]
    )


def test_rejects_missing_build_artifact(tmp_path: Path) -> None:
    root, ids, inventory = _tree(tmp_path)
    proof = json.loads(
        (root / ids["primary"] / "build-preflight.json").read_text()
    )
    Path(proof["artifacts"]["interactive_binary"]).unlink()
    result = verifier.verify(root, ids, inventory)
    assert result["valid"] is False
    assert any(
        "build preflight evidence mismatch" in str(value)
        for value in result["issues"]
    )
