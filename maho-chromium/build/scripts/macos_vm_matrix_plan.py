from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import sys

REQUIRED_GTEST = "MahoAiSidePanelLifecycleTest.ShowPanel_EntryIsShowing"
SUITES = {"interactive-smoke", "ai-mock", "all"}
SCENARIOS = {
    "ai_panel_renders",
    "ai_real_turn",
    "ai_mock_general_turn",
    "ai_mock_quick_action_context",
    "ai_mock_server_error",
    "ai_mock_key_cleared_no_request",
}
SCRIPT_DIR = Path(__file__).resolve().parent
LIFECYCLE = SCRIPT_DIR / "macos_vm_e2e.py"
VERIFIER = SCRIPT_DIR / "macos_vm_verify_evidence.py"


@dataclass(frozen=True)
class Step:
    name: str
    argv: tuple[str, ...]
    expected_exit: int
    run_id: str | None = None


@dataclass(frozen=True)
class Config:
    workspace: Path
    state_dir: Path
    identity: Path
    golden: str
    tart_home: Path
    prefix: str

    @property
    def root(self) -> Path:
        return self.workspace / "test-results/macos-vm"

    @property
    def matrix_dir(self) -> Path:
        return self.root / f"{self.prefix}-matrix"

    @property
    def inventory(self) -> Path:
        return self.matrix_dir / "final-tart-list.json"


def _base() -> tuple[str, ...]:
    return sys.executable, str(LIFECYCLE)


def _run_argv(config: Config, run_id: str, suite: str) -> tuple[str, ...]:
    return (
        *_base(),
        "run",
        "--suite",
        suite,
        "--run-id",
        run_id,
        "--workspace",
        str(config.workspace),
        "--golden",
        config.golden,
        "--identity",
        str(config.identity),
        "--state-dir",
        str(config.state_dir),
    )


def plan(config: Config) -> tuple[Step, ...]:
    run_id = lambda role: f"{config.prefix}-{role}"
    common = (
        "--golden",
        config.golden,
        "--identity",
        str(config.identity),
        "--state-dir",
        str(config.state_dir),
    )
    return (
        Step("doctor", (*_base(), "doctor", *common), 0),
        Step("list", (*_base(), "list", "--json"), 0),
        Step(
            "primary",
            (
                *_run_argv(config, run_id("primary"), "interactive-smoke"),
                "--gtest",
                REQUIRED_GTEST,
                "--require-transport",
                "virtiofs-read-only",
            ),
            0,
            run_id("primary"),
        ),
        Step(
            "primary-fallback",
            (
                *_run_argv(
                    config,
                    run_id("primary-fallback"),
                    "interactive-smoke",
                ),
                "--gtest",
                REQUIRED_GTEST,
            ),
            0,
            run_id("primary-fallback"),
        ),
        Step(
            "interactive",
            _run_argv(config, run_id("interactive"), "interactive-smoke"),
            0,
            run_id("interactive"),
        ),
        Step("ai", _run_argv(config, run_id("ai"), "ai-mock"), 0, run_id("ai")),
        Step("all-a", _run_argv(config, run_id("all-a"), "all"), 0, run_id("all-a")),
        Step("all-b", _run_argv(config, run_id("all-b"), "all"), 0, run_id("all-b")),
        Step(
            "failure",
            _run_argv(config, run_id("failure"), "intentional-failure-fixture"),
            1,
            run_id("failure"),
        ),
        Step(
            "cancel",
            (
                *_run_argv(config, run_id("cancel"), "all"),
                "--cancel-after-guest-ready",
            ),
            130,
            run_id("cancel"),
        ),
        Step("inventory", ("tart", "list", "--format", "json"), 0),
    )


def verifier_argv(config: Config, fallback: bool) -> tuple[str, ...]:
    roles = ("primary", "interactive", "ai", "all-a", "all-b", "failure", "cancel")
    argv = [
        sys.executable,
        str(VERIFIER),
        "--root",
        str(config.root),
        "--inventory",
        str(config.inventory),
        "--matrix-dir",
        str(config.matrix_dir),
    ]
    for role in roles:
        argv.extend((f"--{role}", f"{config.prefix}-{role}"))
    if fallback:
        argv.extend(("--primary-fallback", f"{config.prefix}-primary-fallback"))
    return tuple(argv)
