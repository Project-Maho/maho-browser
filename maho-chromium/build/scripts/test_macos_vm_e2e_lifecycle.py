#!/usr/bin/env python3
"""Regression tests for the adapter-driven macOS VM lifecycle."""

from __future__ import annotations

import importlib.util
from importlib.machinery import ModuleSpec
import io
import json
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace
from typing import TYPE_CHECKING
from unittest import mock

import pytest

if TYPE_CHECKING:
    from macos_vm_runtime import SuiteResult


MODULE_PATH = Path(__file__).with_name("macos_vm_e2e.py")
_loaded_spec = importlib.util.spec_from_file_location("macos_vm_e2e", MODULE_PATH)
if _loaded_spec is None or _loaded_spec.loader is None:
    raise RuntimeError(f"unable to load {MODULE_PATH}")
SPEC: ModuleSpec = _loaded_spec
LOADER = _loaded_spec.loader
vm = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = vm
LOADER.exec_module(vm)


def test_help_exposes_complete_lifecycle() -> None:
    result = subprocess.run(
        [sys.executable, str(MODULE_PATH), "--help"],
        text=True,
        capture_output=True,
        check=False,
    )
    assert result.returncode == 0
    for command in ("doctor", "prepare", "list", "run"):
        assert command in result.stdout


def test_run_parser_uses_manifest_suites_and_failure_fixture() -> None:
    parser = vm.build_parser()
    for suite in (
        "interactive-smoke",
        "ai-mock",
        "all",
        "intentional-failure-fixture",
    ):
        assert parser.parse_args(["run", "--suite", suite]).suite == suite
    with pytest.raises(SystemExit):
        parser.parse_args(["run", "--suite", "smoke"])


def test_run_parser_exposes_exact_gtest_and_primary_transport_selectors_only_for_run() -> None:
    parser = vm.build_parser()
    args = parser.parse_args(
        [
            "run",
            "--suite",
            "interactive-smoke",
            "--gtest",
            "MahoAiSidePanelLifecycleTest.ShowPanel_EntryIsShowing",
            "--require-transport",
            "virtiofs-read-only",
        ]
    )
    assert args.gtest == "MahoAiSidePanelLifecycleTest.ShowPanel_EntryIsShowing"
    assert args.require_transport == "virtiofs-read-only"
    with pytest.raises(SystemExit):
        parser.parse_args(["list", "--gtest", "Suite.Test"])


@pytest.mark.parametrize(
    ("suite", "gtest", "message"),
    (
        ("interactive-smoke", "Maho*", "exact gtest"),
        ("interactive-smoke", "MissingSuite.MissingTest", "not a member"),
        ("ai-mock", "MahoAiSidePanelLifecycleTest.ShowPanel_EntryIsShowing", "gtest suite"),
        ("all", "MahoAiSidePanelLifecycleTest.ShowPanel_EntryIsShowing", "gtest suite"),
        (
            "intentional-failure-fixture",
            "MahoAiSidePanelLifecycleTest.ShowPanel_EntryIsShowing",
            "gtest suite",
        ),
    ),
)
def test_invalid_gtest_selection_is_rejected_before_build_or_clone(
    suite, gtest, message
) -> None:
    args = SimpleNamespace(
        manifest=vm._DEFAULT_MANIFEST_PATH,
        suite=suite,
        gtest=gtest,
        require_transport=None,
    )
    with mock.patch.object(vm, "_run_build_and_preflight") as build:
        with mock.patch.object(vm, "_new_clone") as clone:
            with pytest.raises(vm.LifecycleError, match=message):
                vm.run(args)
    build.assert_not_called()
    clone.assert_not_called()


def test_bad_suite_is_rejected_before_build_or_clone() -> None:
    args = SimpleNamespace(manifest=vm._DEFAULT_MANIFEST_PATH, suite="bogus")
    with mock.patch.object(vm, "_run_build_and_preflight") as build:
        with mock.patch.object(vm, "_new_clone") as clone:
            with pytest.raises(vm.LifecycleError, match="unknown suite"):
                vm.run(args)
    build.assert_not_called()
    clone.assert_not_called()


def test_clone_names_are_unique_and_ownership_receipt_is_atomic(tmp_path) -> None:
    services = mock.Mock()
    services.runner.deadline_after.return_value = 100.0
    services.runner.run.return_value = SimpleNamespace(
        status=vm._CommandStatus.EXITED,
        succeeded=True,
        returncode=0,
        stdout="",
        stderr="",
        error=None,
    )
    cancellation = vm._CancellationToken()
    first = vm._new_clone("golden", tmp_path, services, cancellation)
    second = vm._new_clone("golden", tmp_path, services, cancellation)
    assert first.name != second.name
    assert json.loads(first.receipt.read_text())["token"] == first.token
    assert not list(tmp_path.glob("**/.ownership.json.*"))


def command_result(
    vm_module,
    *,
    succeeded: bool,
    returncode: int = 0,
    stdout: str = "",
    stderr: str = "",
):
    result = SimpleNamespace(
        status=vm_module._CommandStatus.EXITED,
        succeeded=succeeded,
        returncode=returncode,
        stdout=stdout,
        stderr=stderr,
        error=None,
    )
    result.to_dict = lambda: {
        "status": result.status.value,
        "returncode": result.returncode,
    }
    return result


def _default_build_fixture(tmp_path):
    manifest = vm.load_manifest()
    output_path = tmp_path / "chromium/src/out/Default"
    output_path.mkdir(parents=True)
    deps = output_path / "interactive_ui_tests.runtime_deps"
    deps.write_text("interactive_ui_tests\n", encoding="utf-8")
    binary = output_path / "interactive_ui_tests"
    binary.write_text("binary", encoding="utf-8")
    app = output_path / "Maho.app"
    app.mkdir()
    required = vm._required_gtests(manifest)
    suites: dict[str, list[str]] = {}
    for test in required:
        suite, name = test.split(".", 1)
        suites.setdefault(f"{suite}.", []).append(name)
    listed = "\n".join(
        (suite + "\n" + "\n".join(f"  {name}" for name in names))
        for suite, names in suites.items()
    )
    services = vm.LifecycleServices()
    services.runner = mock.Mock()
    services.runner.deadline_after.return_value = 100.0
    services.runner.run.side_effect = (
        command_result(vm, succeeded=True, stdout="Waiting for build lock...\n"),
        command_result(vm, succeeded=True, stdout="Mach-O 64-bit arm64"),
        command_result(vm, succeeded=True),
        command_result(vm, succeeded=True),
        command_result(vm, succeeded=True, stdout=listed),
    )
    run_directory = tmp_path / "run"
    run_directory.mkdir()
    return manifest, output_path, binary, required, services, run_directory


def test_waiting_for_build_lock_completes_default_build_and_writes_exact_proof(
    tmp_path,
) -> None:
    manifest, output_path, binary, required, services, run_directory = (
        _default_build_fixture(tmp_path)
    )

    dependencies = vm._run_build_and_preflight(
        tmp_path,
        manifest,
        services,
        vm._CancellationToken(),
        run_directory,
    )

    proof = json.loads(
        (run_directory / "build-preflight.json").read_text(encoding="utf-8")
    )
    assert dependencies == (binary.resolve(),)
    assert proof["canonical_build_argv"] == list(
        vm._canonical_build_argv(tmp_path)
    )
    assert proof["output_root"] == str(output_path.resolve())
    assert "output_volume" not in proof
    assert proof["architecture"] == "Mach-O 64-bit arm64"
    assert set(proof["required_tests"]) == set(required)
    assert set(proof["listed_tests"]) == set(required)
@pytest.mark.parametrize(
    ("status", "terminal_status", "message"),
    (
        (vm._CommandStatus.EXITED, vm._TerminalStatus.BUILD_FAILED, "build stopped"),
        (vm._CommandStatus.CANCELLED, vm._TerminalStatus.CANCELLED, "received signal 2"),
    ),
)
def test_failed_or_cancelled_default_build_never_tries_an_alternate_output(
    tmp_path, status, terminal_status, message
) -> None:
    manifest = vm.load_manifest()
    services = vm.LifecycleServices()
    services.runner = mock.Mock()
    services.runner.deadline_after.return_value = 100.0
    result = command_result(vm, succeeded=False, returncode=1, stderr=message)
    result.status = status
    result.error = message if status is vm._CommandStatus.CANCELLED else None
    services.runner.run.return_value = result
    run_directory = tmp_path / "run"
    run_directory.mkdir()

    with pytest.raises(vm._StageFailure) as caught:
        vm._run_build_and_preflight(
            tmp_path,
            manifest,
            services,
            vm._CancellationToken(),
            run_directory,
        )

    assert caught.value.status is terminal_status
    assert message in str(caught.value)
    services.runner.run.assert_called_once()
    argv = services.runner.run.call_args.args[0]
    assert argv == vm._canonical_build_argv(tmp_path)
    assert argv[argv.index("--out-dir") + 1] == "out/Default"


@pytest.mark.parametrize(
    ("build_status", "exit_code"),
    (
        (vm._TerminalStatus.BUILD_FAILED, 3),
        (vm._TerminalStatus.CANCELLED, 130),
    ),
)
def test_run_build_failure_or_cancellation_starts_no_tart_or_vm(
    tmp_path, build_status, exit_code
) -> None:
    manifest = vm.load_manifest()
    identity = tmp_path / "id_ed25519"
    identity.write_text("private", encoding="utf-8")
    services = vm.LifecycleServices()
    services.runner = mock.Mock()
    args = SimpleNamespace(
        manifest=vm._DEFAULT_MANIFEST_PATH,
        suite="interactive-smoke",
        workspace=tmp_path,
        state_dir=tmp_path / "state",
        identity=identity,
        golden=None,
        run_id=f"build-{build_status.value}",
    )

    with mock.patch.object(vm, "_source_revision", return_value="abc123"), \
         mock.patch.object(
             vm,
             "_run_build_and_preflight",
             side_effect=vm._StageFailure(build_status, "build stopped"),
         ) as build, \
         mock.patch.object(vm, "_new_clone") as clone, \
         mock.patch.object(vm, "_start_clone") as start, \
         mock.patch.object(vm, "_teardown_clone", return_value=vm._TeardownResult(True, True)), \
         mock.patch.object(vm, "_required_snapshot") as snapshot:
        assert vm.run(args, services=services) == exit_code

    build.assert_called_once()
    services.runner.run.assert_not_called()
    snapshot.assert_not_called()
    clone.assert_not_called()
    start.assert_not_called()


def test_cleanup_refuses_unowned_clone(tmp_path) -> None:
    services = mock.Mock()
    clone = vm.OwnedClone("foreign", tmp_path / "foreign", "golden", "ours")
    clone.directory.mkdir()
    clone.receipt.write_text(
        json.dumps({"name": "foreign", "token": "theirs"}), encoding="utf-8"
    )
    result = vm._teardown_clone(clone, None, services)
    assert not result.succeeded
    assert "unowned" in (result.error or "")
    services.runner.run.assert_not_called()
    assert clone.directory.exists()


def test_teardown_records_second_wait_timeout_and_continues_cleanup(tmp_path) -> None:
    process = mock.Mock()
    process.pid = 8112
    process.poll.return_value = None
    process.wait.side_effect = [subprocess.TimeoutExpired("tart", 10), subprocess.TimeoutExpired("tart", 10)]
    running = vm._RunningVm(process=process, log_handle=io.StringIO())
    clone = vm.OwnedClone("owned", tmp_path / "owned", "golden", "token")
    clone.directory.mkdir()
    clone.receipt.write_text(
        json.dumps({"name": clone.name, "token": clone.token}),
        encoding="utf-8",
    )
    services = mock.Mock()
    services.runner.deadline_after.return_value = 100.0
    services.runner.run.return_value = command_result(vm, succeeded=True)

    with mock.patch.object(vm._os, "killpg"):
        result = vm._teardown_clone(clone, running, services)

    assert not result.succeeded
    assert "SIGKILL" in (result.error or "")
    assert services.runner.run.call_count == 2


def test_ssh_contract_is_strictly_key_only(tmp_path) -> None:
    manifest = vm.load_manifest()
    identity = tmp_path / "id"
    identity.write_text("private", encoding="utf-8")
    args = SimpleNamespace(identity=identity, state_dir=tmp_path, golden=None)
    config = vm._tart_config(manifest, args)
    base = vm._strict_ssh_base(config, "192.0.2.1")
    for option in (
        "BatchMode=yes",
        "PasswordAuthentication=no",
        "KbdInteractiveAuthentication=no",
        "IdentitiesOnly=yes",
        "StrictHostKeyChecking=accept-new",
    ):
        assert option in base
    assert "StrictHostKeyChecking=no" not in base


def test_guest_ip_wait_uses_installed_tart_argv_order(tmp_path) -> None:
    clone = vm.OwnedClone("maho-e2e-abc123", tmp_path / "clone", "golden", "token")
    config = SimpleNamespace(guest_user="admin", private_key=tmp_path / "id")
    services = mock.Mock()
    services.runner.deadline_after.return_value = 100.0
    services.command_results = []
    services.runner.run.return_value = command_result(
        vm, succeeded=True, stdout="192.0.2.9\n"
    )

    ip_address, _ = vm._wait_for_guest(
        clone, config, services, vm._CancellationToken()
    )

    assert ip_address == "192.0.2.9"
    assert services.runner.run.call_args_list[0].args[0] == (
        "tart",
        "ip",
        clone.name,
        "--wait",
        "600",
    )


def test_regression_forbidden_prior_lifecycle_patterns_are_absent() -> None:
    source = MODULE_PATH.read_text(encoding="utf-8")
    assert "_time.sleep(" not in source
    assert '"smoke": ("python3", "-m", "pytest"' not in source
    assert '"rsync", "-az", "--delete"' not in source
    assert 'destination="~/maho-workspace"' not in source
    assert "_canonical_build_argv(workspace)" in source
    assert "_build_interactive_command(" in source
    assert "_build_ai_mock_commands(" in source
    assert "_compare_run_snapshots(" in source
    assert "_AtomicReceiptWriter(workspace, run_id)" in source
    assert "_BuildOutputWatcher" not in source
    assert "_validate_build_output_volume" not in source
    assert '"/usr/sbin/diskutil"' not in source
    assert '"output_volume"' not in source


def test_read_only_transport_is_primary_and_fallback_is_allowlisted() -> None:
    source = MODULE_PATH.read_text(encoding="utf-8")
    assert "_read_only_mount_contract(workspace, manifest.guest_workspace)" in source
    assert "_plan_rsync_fallback(" in source
    assert "dependencies=dependencies" in source
    assert "--files-from" in source


def test_transport_falls_back_after_mounted_execution_preflight_fails(tmp_path) -> None:
    manifest = vm.load_manifest()
    workspace = tmp_path / "workspace"
    workspace.mkdir()
    dependency = workspace / "chromium/src/out/Default/interactive_ui_tests"
    dependency.parent.mkdir(parents=True)
    dependency.write_text("binary", encoding="utf-8")
    for relative in (
        "maho-chromium/build/scripts/e2e_chromium_ai.js",
        "maho-chromium/build/scripts/e2e_mock_llm_server.js",
        "maho-chromium/build/scripts/macos_vm_guest_suites.py",
    ):
        path = workspace / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("fixture", encoding="utf-8")
    config = SimpleNamespace(guest_user="admin", private_key=tmp_path / "id")
    responses = iter(
        (
            command_result(vm, succeeded=True),
            command_result(vm, succeeded=True),
            command_result(vm, succeeded=False, returncode=1),
            command_result(vm, succeeded=False, returncode=126),
            command_result(vm, succeeded=True),
            command_result(vm, succeeded=True),
            command_result(vm, succeeded=True),
            command_result(vm, succeeded=True),
        )
    )
    services = mock.Mock()
    services.runner.deadline_after.return_value = 100.0
    services.runner.run.side_effect = lambda *args, **kwargs: next(responses)
    services.command_results = []
    (tmp_path / "run").mkdir()

    transport = vm._configure_transport(
        workspace,
        manifest,
        (dependency,),
        config,
        "192.0.2.1",
        ("ssh", "admin@192.0.2.1"),
        services,
        vm._CancellationToken(),
        tmp_path / "run",
    )

    assert transport == "runtime-deps-rsync"
    invocations = [call.args[0] for call in services.runner.run.call_args_list]
    assert any("umount" in " ".join(argv) for argv in invocations)
    assert any("install -d -o admin -g staff" in " ".join(argv) for argv in invocations)
    assert any(argv[0] == "rsync" for argv in invocations)


def test_required_primary_transport_fails_before_fallback_planning_or_writes(
    tmp_path,
) -> None:
    manifest = vm.load_manifest()
    workspace = tmp_path / "workspace"
    workspace.mkdir()
    dependency = workspace / "chromium/src/out/Default/interactive_ui_tests"
    dependency.parent.mkdir(parents=True)
    dependency.write_text("binary", encoding="utf-8")
    config = SimpleNamespace(guest_user="admin", private_key=tmp_path / "id")
    responses = iter(
        (
            command_result(vm, succeeded=True),
            command_result(vm, succeeded=True),
            command_result(vm, succeeded=False, returncode=1),
            command_result(vm, succeeded=False, returncode=126),
            command_result(vm, succeeded=True),
        )
    )
    services = mock.Mock()
    services.runner.deadline_after.return_value = 100.0
    services.runner.run.side_effect = lambda *args, **kwargs: next(responses)
    services.command_results = []
    run_directory = tmp_path / "run"
    run_directory.mkdir()

    with mock.patch.object(vm, "_plan_rsync_fallback") as fallback:
        with pytest.raises(vm._StageFailure) as raised:
            vm._configure_transport(
                workspace,
                manifest,
                (dependency,),
                config,
                "192.0.2.1",
                ("ssh", "admin@192.0.2.1"),
                services,
                vm._CancellationToken(),
                run_directory,
                required_transport="virtiofs-read-only",
            )

    assert raised.value.status is vm._TerminalStatus.TRANSPORT_FAILED
    fallback.assert_not_called()
    assert not list(run_directory.iterdir())
    invocations = [call.args[0] for call in services.runner.run.call_args_list]
    assert not any("install -d -o admin -g staff" in " ".join(argv) for argv in invocations)
    assert not any(argv[0] == "rsync" for argv in invocations)


def test_artifact_copy_uses_artifact_failure_taxonomy(tmp_path) -> None:
    services = mock.Mock()
    services.runner.deadline_after.return_value = 100.0
    services.runner.run.return_value = command_result(
        vm, succeeded=False, returncode=1, stderr="copy failed"
    )
    services.command_results = []
    config = SimpleNamespace(guest_user="admin", private_key=tmp_path / "id")

    with pytest.raises(vm._StageFailure) as raised:
        vm._copy_guest_file(
            config,
            "192.0.2.1",
            "/remote/result",
            tmp_path / "result",
            services,
            vm._CancellationToken(),
            failure_status=vm._TerminalStatus.ARTIFACT_FAILED,
        )

    assert raised.value.status is vm._TerminalStatus.ARTIFACT_FAILED


def test_aggregate_suite_ledger_records_failed_and_not_run_members(tmp_path) -> None:
    manifest = vm.load_manifest()
    snapshots: list[tuple[SuiteResult, ...]] = []
    failed_runtime = command_result(
        vm, succeeded=False, returncode=1, stderr="gtest failed"
    )
    command = vm._build_interactive_command(
        str(manifest.guest_workspace),
        str(vm._GUEST_RUN_ROOT / "run"),
        manifest.suite("interactive-smoke").gtest_filter,
    )
    captured = vm._CapturedCommandResult(command, 1, "", "gtest failed")

    with mock.patch.object(vm, "_run_command", return_value=command_result(vm, succeeded=True)):
        with mock.patch.object(vm, "_execute_guest_command", return_value=(captured, failed_runtime)):
            with mock.patch.object(vm, "_copy_guest_file"):
                summary = tmp_path / "interactive-test-summary.json"
                summary.write_text("{}", encoding="utf-8")
                with mock.patch.object(vm, "_write_command_logs", return_value=("stdout", "stderr")):
                    with mock.patch.object(Path, "read_text", return_value="{}"):
                        with pytest.raises(vm._StageFailure):
                            vm._run_selected_suites(
                                manifest,
                                "all",
                                vm._GUEST_RUN_ROOT / "run",
                                SimpleNamespace(guest_user="admin", private_key=tmp_path / "id"),
                                "192.0.2.1",
                                ("ssh", "admin@192.0.2.1"),
                                mock.Mock(),
                                vm._CancellationToken(),
                                tmp_path,
                                snapshots.append,
                            )

    assert [result.status for result in snapshots[-1]] == ["failed", "not_run"]
    assert [result.name for result in snapshots[-1]] == ["interactive-smoke", "ai-mock"]


def test_selected_interactive_gtest_builds_one_exact_command_and_one_suite_result(
    tmp_path,
) -> None:
    manifest = vm.load_manifest()
    selected = "MahoAiSidePanelLifecycleTest.ShowPanel_EntryIsShowing"
    captured_commands = []

    def execute(command, *args, **kwargs):
        captured_commands.append(command)
        captured = vm._CapturedCommandResult(command, 0, "", "")
        return captured, command_result(vm, succeeded=True)

    summary = {
        "per_iteration_data": [{selected: [{"status": "SUCCESS"}]}]
    }
    with mock.patch.object(vm, "_run_command", return_value=command_result(vm, succeeded=True)), \
         mock.patch.object(vm, "_execute_guest_command", side_effect=execute), \
         mock.patch.object(vm, "_copy_guest_file"), \
         mock.patch.object(vm, "_write_command_logs", return_value=("stdout", "stderr")), \
         mock.patch.object(Path, "read_text", return_value=json.dumps(summary)):
        results = vm._run_selected_suites(
            manifest,
            "interactive-smoke",
            vm._GUEST_RUN_ROOT / "run",
            SimpleNamespace(guest_user="admin", private_key=tmp_path / "id"),
            "192.0.2.1",
            ("ssh", "admin@192.0.2.1"),
            mock.Mock(),
            vm._CancellationToken(),
            tmp_path,
            selected_gtest=selected,
        )

    assert len(captured_commands) == 1
    assert captured_commands[0].expected_results == (selected,)
    assert f"--gtest_filter={selected}" in captured_commands[0].argv
    assert len(results) == 1
    assert results[0].name == "interactive-smoke"
    assert results[0].details["expected"] == [selected]


def test_full_interactive_suite_remains_all_three_manifest_tests() -> None:
    manifest = vm.load_manifest()
    suite = manifest.suite("interactive-smoke")
    command = vm._build_interactive_command(
        str(manifest.guest_workspace),
        str(vm._GUEST_RUN_ROOT / "run"),
        suite.gtest_filter,
    )

    assert len(command.expected_results) == 3
    assert command.expected_results == suite.gtest_filter
    assert "--gtest_filter=" + ":".join(suite.gtest_filter) in command.argv


def test_source_revision_uses_git_head(tmp_path) -> None:
    completed = SimpleNamespace(returncode=0, stdout="abc123\n", stderr="")
    with mock.patch.object(vm._subprocess, "run", return_value=completed) as run:
        assert vm._source_revision(tmp_path) == "abc123"
    run.assert_called_once_with(
        ("git", "rev-parse", "HEAD"),
        cwd=tmp_path,
        check=False,
        capture_output=True,
        text=True,
    )


def test_run_checkpoints_guest_ready_before_transport_and_preserves_facts(
    tmp_path,
) -> None:
    manifest = vm.load_manifest()
    identity = tmp_path / "id_ed25519"
    identity.write_text("private", encoding="utf-8")
    events: list[str] = []
    snapshots = [mock.Mock(), mock.Mock(), mock.Mock()]
    for snapshot in snapshots:
        snapshot.to_json_dict.return_value = {}
    services = vm.LifecycleServices()
    services.runner = mock.Mock()
    services.runner.deadline_after.return_value = 100.0
    services.runner.run.return_value = command_result(vm, succeeded=True)
    services.snapshot = mock.Mock(side_effect=snapshots)
    args = SimpleNamespace(
        manifest=vm._DEFAULT_MANIFEST_PATH,
        suite="interactive-smoke",
        gtest="MahoAiSidePanelLifecycleTest.ShowPanel_EntryIsShowing",
        require_transport="virtiofs-read-only",
        workspace=tmp_path,
        state_dir=tmp_path / "state",
        identity=identity,
        golden=None,
        run_id="guest-ready-order",
    )
    clone = vm.OwnedClone(
        "maho-e2e-owned", tmp_path / "clone", manifest.golden_image, "token"
    )

    def wait_for_guest(*args, **kwargs):
        events.append("guest_ready")
        return "192.0.2.9", ("ssh", "admin@192.0.2.9")

    def configure_transport(*args, **kwargs):
        receipt_path = (
            tmp_path
            / "test-results/macos-vm/guest-ready-order/receipt.json"
        )
        facts = json.loads(receipt_path.read_text(encoding="utf-8"))["guest_facts"]
        assert facts["clone"] == clone.name
        assert facts["ip_address"] == "192.0.2.9"
        assert facts["guest_workspace"] == str(manifest.guest_workspace)
        assert facts["guest_run_dir"] == str(
            vm._GUEST_RUN_ROOT / "guest-ready-order"
        )
        assert facts["guest_ready_at"].endswith("Z")
        events.append("transport")
        return "virtiofs-read-only"

    def run_suites(*args, **kwargs):
        kwargs["on_process_started"](7001)
        return (
            vm._ReceiptSuiteResult(
                name="interactive-smoke",
                status="success",
                started_at="2026-08-13T00:00:00.000Z",
                finished_at="2026-08-13T00:00:01.000Z",
            ),
        )

    with mock.patch.object(vm, "_source_revision", return_value="abc123"), \
         mock.patch.object(vm, "_run_build_and_preflight", return_value=()), \
         mock.patch.object(vm, "_new_clone", return_value=clone), \
         mock.patch.object(vm, "_start_clone", return_value=mock.Mock()), \
         mock.patch.object(vm, "_wait_for_guest", side_effect=wait_for_guest), \
         mock.patch.object(vm, "_configure_transport", side_effect=configure_transport), \
         mock.patch.object(vm, "_run_selected_suites", side_effect=run_suites), \
         mock.patch.object(vm, "_collect_guest_artifacts", return_value=tmp_path / "artifacts"), \
         mock.patch.object(vm, "_teardown_clone", return_value=vm._TeardownResult(True, True)), \
         mock.patch.object(vm, "_compare_run_snapshots") as compare:
        compare.return_value.is_isolated = True
        compare.return_value.violation_codes = ()
        compare.return_value.during.violation_codes = ()
        compare.return_value.after.violation_codes = ()
        compare.return_value.to_json_dict.return_value = {}
        assert vm.run(args, services=services) == 0

    receipt = json.loads(
        (tmp_path / "test-results/macos-vm/guest-ready-order/receipt.json").read_text()
    )
    assert events == ["guest_ready", "transport"]
    assert receipt["guest_facts"]["guest_ready_at"].endswith("Z")
    assert receipt["guest_facts"]["transport"] == "virtiofs-read-only"
    assert receipt["host_facts"]["requested_gtest"] == (
        "MahoAiSidePanelLifecycleTest.ShowPanel_EntryIsShowing"
    )
    assert receipt["host_facts"]["required_transport"] == "virtiofs-read-only"
    assert [result["name"] for result in receipt["suite_results"]] == [
        "interactive-smoke"
    ]
    assert [item["stage"] for item in receipt["isolation_snapshots"]].count(
        "during"
    ) == 1


def test_suite_snapshot_failure_cancels_active_process_and_surfaces_isolation() -> None:
    events: list[str] = []
    process = mock.Mock()
    process.pid = 7001
    process.returncode = -9

    def communicate(timeout=None):
        events.append("process_exit")
        return "", ""

    process.communicate.side_effect = communicate

    def kill_process_group(pid, sig):
        events.append("cancel")

    runner = vm._SubprocessArgvRunner(
        popen_factory=lambda *args, **kwargs: process,
        kill_process_group=kill_process_group,
    )
    services = vm.LifecycleServices(runner=runner)
    cancellation = vm._CancellationToken()
    command = vm._GuestCommand(
        suite="interactive-smoke",
        argv=("guest-suite",),
        environment=(),
        stdout_path="stdout.log",
        stderr_path="stderr.log",
        expected_results=(),
    )

    def fail_snapshot(pid):
        events.append("snapshot")
        raise vm._StageFailure(vm._TerminalStatus.ISOLATION_FAILED, "probe failed")

    with pytest.raises(vm._StageFailure) as raised:
        vm._execute_guest_command(
            command,
            (),
            services,
            cancellation,
            on_process_started=fail_snapshot,
        )

    assert raised.value.status is vm._TerminalStatus.ISOLATION_FAILED
    assert cancellation.is_cancelled
    assert events == ["snapshot", "cancel", "process_exit"]


def test_run_snapshot_failure_attempts_artifacts_and_teardown(tmp_path) -> None:
    manifest = vm.load_manifest()
    identity = tmp_path / "id_ed25519"
    identity.write_text("private", encoding="utf-8")
    baseline = vm._HostIsolationSnapshot.from_json_dict(
        {
            "schema_version": 1,
            "windows": [],
            "maho_processes": [],
            "tart_run_processes": [],
            "listeners": [],
        }
    )
    after = vm._HostIsolationSnapshot.from_json_dict(
        {
            "schema_version": 1,
            "windows": [],
            "maho_processes": [],
            "tart_run_processes": [],
            "listeners": [],
        }
    )
    services = vm.LifecycleServices()
    services.runner = mock.Mock()
    services.runner.deadline_after.return_value = 100.0
    services.runner.run.return_value = command_result(vm, succeeded=True)
    services.snapshot = mock.Mock(
        side_effect=(baseline, RuntimeError("probe failed"), after)
    )
    args = SimpleNamespace(
        manifest=vm._DEFAULT_MANIFEST_PATH,
        suite="interactive-smoke",
        workspace=tmp_path,
        state_dir=tmp_path / "state",
        identity=identity,
        golden=None,
        run_id="snapshot-failure-cleanup",
    )
    clone = vm.OwnedClone(
        "maho-e2e-owned", tmp_path / "clone", manifest.golden_image, "token"
    )

    def run_suites(*args, **kwargs):
        kwargs["on_process_started"](7001)
        raise AssertionError("snapshot failure must stop suite execution")

    with mock.patch.object(vm, "_source_revision", return_value="abc123"), \
         mock.patch.object(vm, "_run_build_and_preflight", return_value=()), \
         mock.patch.object(vm, "_new_clone", return_value=clone), \
         mock.patch.object(vm, "_start_clone", return_value=mock.Mock()), \
         mock.patch.object(
             vm,
             "_wait_for_guest",
             return_value=("192.0.2.9", ("ssh", "admin@192.0.2.9")),
         ), \
         mock.patch.object(vm, "_configure_transport", return_value="virtiofs-read-only"), \
         mock.patch.object(vm, "_run_selected_suites", side_effect=run_suites), \
         mock.patch.object(
             vm,
             "_collect_guest_artifacts",
             return_value=tmp_path / "artifacts",
         ) as collect, \
         mock.patch.object(
             vm,
             "_teardown_clone",
             return_value=vm._TeardownResult(True, True),
         ) as teardown:
        assert vm.run(args, services=services) == 3

    receipt = json.loads(
        (
            tmp_path
            / "test-results/macos-vm/snapshot-failure-cleanup/receipt.json"
        ).read_text(encoding="utf-8")
    )
    assert receipt["terminal_status"] == "isolation_failed"
    assert "host isolation snapshot failed: probe failed" in receipt["guest_facts"][
        "first_error"
    ]
    collect.assert_called_once()
    teardown.assert_called_once()


def test_guest_ready_cancellation_writes_post_teardown_isolation_evidence(
    tmp_path,
) -> None:
    manifest = vm.load_manifest()
    identity = tmp_path / "id_ed25519"
    identity.write_text("private", encoding="utf-8")
    empty_snapshot = vm._HostIsolationSnapshot.from_json_dict(
        {
            "schema_version": 1,
            "windows": [],
            "maho_processes": [],
            "tart_run_processes": [],
            "listeners": [],
        }
    )
    services = vm.LifecycleServices()
    services.runner = mock.Mock()
    services.runner.deadline_after.return_value = 100.0
    services.runner.run.return_value = command_result(vm, succeeded=True)
    services.snapshot = mock.Mock(side_effect=(empty_snapshot, empty_snapshot))
    args = SimpleNamespace(
        manifest=vm._DEFAULT_MANIFEST_PATH,
        suite="interactive-smoke",
        workspace=tmp_path,
        state_dir=tmp_path / "state",
        identity=identity,
        golden=None,
        run_id="guest-ready-cancelled",
        cancel_after_guest_ready=True,
    )
    clone = vm.OwnedClone(
        "maho-e2e-owned", tmp_path / "clone", manifest.golden_image, "token"
    )

    with mock.patch.object(vm, "_source_revision", return_value="abc123"), \
         mock.patch.object(vm, "_run_build_and_preflight", return_value=()), \
         mock.patch.object(vm, "_new_clone", return_value=clone), \
         mock.patch.object(vm, "_start_clone", return_value=mock.Mock()), \
         mock.patch.object(
             vm,
             "_wait_for_guest",
             return_value=("192.0.2.9", ("ssh", "admin@192.0.2.9")),
         ), \
         mock.patch.object(vm, "_configure_transport") as configure_transport, \
         mock.patch.object(vm, "_run_selected_suites") as run_suites, \
         mock.patch.object(
             vm,
             "_collect_guest_artifacts",
             return_value=tmp_path / "artifacts",
         ) as collect, \
         mock.patch.object(
             vm,
             "_teardown_clone",
             return_value=vm._TeardownResult(True, True),
         ) as teardown:
        assert vm.run(args, services=services) == 130

    run_directory = tmp_path / "test-results/macos-vm/guest-ready-cancelled"
    receipt = json.loads((run_directory / "receipt.json").read_text())
    isolation = json.loads((run_directory / "host-isolation.json").read_text())
    assert receipt["primary_status"] == "cancelled"
    assert receipt["terminal_status"] == "cancelled"
    assert receipt["host_facts"]["cancel_after_guest_ready"] is True
    assert receipt["guest_facts"]["guest_ready_at"].endswith("Z")
    assert receipt["guest_facts"]["guest_run_dir"] == str(
        vm._GUEST_RUN_ROOT / "guest-ready-cancelled"
    )
    assert receipt["teardown_result"]["succeeded"] is True
    assert [item["stage"] for item in receipt["isolation_snapshots"]] == [
        "baseline",
        "post_teardown_after_early_exit",
    ]
    assert str(run_directory / "host-isolation.json") in receipt["artifact_paths"]
    assert isolation["evidence_kind"] == "early_exit"
    cleanup = isolation["post_teardown_after_early_exit"]
    assert cleanup["phase"] == "post_teardown_after_early_exit"
    assert cleanup["is_isolated"] is True
    assert cleanup["new_windows"] == []
    assert cleanup["new_maho_processes"] == []
    assert cleanup["new_tart_run_processes"] == []
    assert cleanup["new_listeners"] == []
    run_suites.assert_not_called()
    configure_transport.assert_not_called()
    collect.assert_called_once()
    teardown.assert_called_once()


def test_success_without_event_bound_during_snapshot_fails_isolation(tmp_path) -> None:
    manifest = vm.load_manifest()
    identity = tmp_path / "id_ed25519"
    identity.write_text("private", encoding="utf-8")
    empty_snapshot = vm._HostIsolationSnapshot.from_json_dict(
        {
            "schema_version": 1,
            "windows": [],
            "maho_processes": [],
            "tart_run_processes": [],
            "listeners": [],
        }
    )
    services = vm.LifecycleServices()
    services.runner = mock.Mock()
    services.runner.deadline_after.return_value = 100.0
    services.runner.run.return_value = command_result(vm, succeeded=True)
    services.snapshot = mock.Mock(side_effect=(empty_snapshot, empty_snapshot))
    args = SimpleNamespace(
        manifest=vm._DEFAULT_MANIFEST_PATH,
        suite="interactive-smoke",
        workspace=tmp_path,
        state_dir=tmp_path / "state",
        identity=identity,
        golden=None,
        run_id="missing-during",
    )
    clone = vm.OwnedClone(
        "maho-e2e-owned", tmp_path / "clone", manifest.golden_image, "token"
    )

    with mock.patch.object(vm, "_source_revision", return_value="abc123"), \
         mock.patch.object(vm, "_run_build_and_preflight", return_value=()), \
         mock.patch.object(vm, "_new_clone", return_value=clone), \
         mock.patch.object(vm, "_start_clone", return_value=mock.Mock()), \
         mock.patch.object(
             vm,
             "_wait_for_guest",
             return_value=("192.0.2.9", ("ssh", "admin@192.0.2.9")),
         ), \
         mock.patch.object(vm, "_configure_transport", return_value="virtiofs-read-only"), \
         mock.patch.object(vm, "_run_selected_suites", return_value=()), \
         mock.patch.object(
             vm,
             "_collect_guest_artifacts",
             return_value=tmp_path / "artifacts",
         ), \
         mock.patch.object(
             vm,
             "_teardown_clone",
             return_value=vm._TeardownResult(True, True),
         ):
        assert vm.run(args, services=services) == 3

    receipt = json.loads(
        (
            tmp_path / "test-results/macos-vm/missing-during/receipt.json"
        ).read_text()
    )
    assert receipt["terminal_status"] == "isolation_failed"
    assert receipt["guest_facts"]["first_error"] == (
        "normal run did not capture event-bound during isolation snapshot"
    )
    assert not (
        tmp_path / "test-results/macos-vm/missing-during/host-isolation.json"
    ).exists()
