from __future__ import annotations

import json
from pathlib import Path
import subprocess


class FakeRunner:
    def __init__(
        self,
        config,
        *,
        primary_exit: int = 0,
        primary_error: str | None = None,
        failure_exit: int = 1,
        cancel_exit: int = 130,
        verifier_exit: int = 0,
    ) -> None:
        self.config = config
        self.primary_exit = primary_exit
        self.primary_error = primary_error
        self.failure_exit = failure_exit
        self.cancel_exit = cancel_exit
        self.verifier_exit = verifier_exit
        self.calls: list[tuple[str, ...]] = []

    def __call__(self, argv, **_kwargs) -> subprocess.CompletedProcess[str]:
        command = tuple(argv)
        self.calls.append(command)
        if command[0] == "tart":
            return subprocess.CompletedProcess(
                command,
                0,
                '[{"Name":"maho-e2e-golden","State":"stopped","Running":false}]',
                "",
            )
        if "macos_vm_verify_evidence.py" in command[1]:
            return subprocess.CompletedProcess(
                command,
                self.verifier_exit,
                '{"valid":true}',
                "",
            )
        if "run" not in command:
            stdout = (
                '{"ok":true}'
                if "doctor" in command
                else json.dumps(
                    {
                        "suites": ["interactive-smoke", "ai-mock", "all"],
                        "ai_scenarios": [
                            "ai_panel_renders",
                            "ai_real_turn",
                            "ai_mock_general_turn",
                            "ai_mock_quick_action_context",
                            "ai_mock_server_error",
                            "ai_mock_key_cleared_no_request",
                        ],
                    }
                )
            )
            return subprocess.CompletedProcess(command, 0, stdout, "")
        run_id = command[command.index("--run-id") + 1]
        if run_id.endswith("-primary"):
            exit_code = self.primary_exit
            status = "success" if exit_code == 0 else "transport_failed"
        elif run_id.endswith("-failure"):
            exit_code = self.failure_exit
            status = "suite_failed" if exit_code == 1 else "infra_error"
        elif run_id.endswith("-cancel"):
            exit_code = self.cancel_exit
            status = "cancelled" if exit_code == 130 else "infra_error"
        else:
            exit_code = 0
            status = "success"
        guest: dict[str, object] = {}
        if run_id.endswith("-primary") and exit_code == 3:
            guest["first_error"] = self.primary_error or (
                "required transport 'virtiofs-read-only' "
                "did not pass guest preflight"
            )
        if status == "success":
            guest["transport"] = (
                "runtime-deps-rsync"
                if run_id.endswith("-primary-fallback")
                else "virtiofs-read-only"
            )
        if status == "cancelled":
            guest["guest_ready_at"] = "2026-08-13T00:00:00Z"
        receipt = {
            "schema_version": 1,
            "run_id": run_id,
            "finalized": True,
            "terminal_status": status,
            "guest_facts": guest,
            "host_facts": {
                "cancel_after_guest_ready": run_id.endswith("-cancel"),
            },
            "teardown_result": {"succeeded": True},
        }
        path = self.config.root / run_id / "receipt.json"
        path.parent.mkdir(parents=True)
        path.write_text(json.dumps(receipt), encoding="utf-8")
        isolation_kind = (
            "early_exit"
            if status in {"cancelled", "transport_failed"}
            else "live_run"
        )
        (path.parent / "host-isolation.json").write_text(
            json.dumps(
                {
                    "evidence_kind": isolation_kind,
                    "is_isolated": True,
                    "violation_codes": [],
                }
            ),
            encoding="utf-8",
        )
        return subprocess.CompletedProcess(command, exit_code, "{}", "")
