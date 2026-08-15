from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import sys

import pytest

MODULE_PATH = Path(__file__).with_name("macos_vm_evidence_privacy.py")
SPEC = importlib.util.spec_from_file_location("macos_vm_evidence_privacy", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"unable to load {MODULE_PATH}")
privacy = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = privacy
SPEC.loader.exec_module(privacy)


def _scan(tmp_path: Path) -> list[str]:
    run_dir = tmp_path / "run"
    matrix_dir = tmp_path / "matrix"
    run_dir.mkdir(exist_ok=True)
    matrix_dir.mkdir(exist_ok=True)
    issues: list[str] = []
    privacy.scan_evidence(tmp_path, ("run",), matrix_dir, issues)
    return issues


@pytest.mark.parametrize(
    ("relative", "content", "issue"),
    (
        ("browser-profile.json", "{}", "forbidden evidence path"),
        ("build.stderr.log", "sk-abcdefghijklmnop", "credential-shaped value"),
        (
            "receipt.json",
            '{"client_secret":"not-redacted"}',
            "unredacted secret field",
        ),
        (
            "commands.log",
            json.dumps({"environment": {"MAHO_E2E_AI_API_KEY": "raw-value"}}),
            "unredacted secret field",
        ),
        (
            "command.txt",
            "MAHO_E2E_AI_API_KEY=raw-value",
            "raw sensitive environment assignment",
        ),
    ),
)
def test_rejects_private_evidence(
    tmp_path: Path,
    relative: str,
    content: str,
    issue: str,
) -> None:
    path = tmp_path / "run" / relative
    path.parent.mkdir(parents=True)
    path.write_text(content, encoding="utf-8")
    assert any(issue in value for value in _scan(tmp_path))


def test_accepts_redacted_text_and_binary_screenshot(tmp_path: Path) -> None:
    run_dir = tmp_path / "run"
    run_dir.mkdir()
    (run_dir / "commands.log").write_text(
        json.dumps(
            {"environment": {"MAHO_E2E_AI_API_KEY": "<redacted>"}}
        ),
        encoding="utf-8",
    )
    (run_dir / "window.png").write_bytes(b"\x89PNG\r\n")
    assert _scan(tmp_path) == []


def test_rejects_symlink_evidence(tmp_path: Path) -> None:
    run_dir = tmp_path / "run"
    run_dir.mkdir()
    outside = tmp_path / "outside.txt"
    outside.write_text("not secret", encoding="utf-8")
    (run_dir / "linked.txt").symlink_to(outside)
    assert any("symlink evidence is forbidden" in value for value in _scan(tmp_path))
