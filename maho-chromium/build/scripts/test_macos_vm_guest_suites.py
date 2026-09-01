#!/usr/bin/env python3

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import macos_vm_guest_suites as guest_suites


WORKSPACE = "/Users/Shared/maho-workspace"
RUN_DIR = "/private/tmp/maho-vm-run-123"
GTESTS = (
    "MahoAiSidePanelLifecycleTest.ShowPanel_EntryIsShowing",
    "MahoSidebarInteractiveTest.ToggleSidebar",
)


def successful_summary(test_names=GTESTS):
    return {
        "per_iteration_data": [
            {
                name: [{"status": "SUCCESS"}]
                for name in test_names
            }
        ]
    }


class InteractiveCommandTest(unittest.TestCase):
    def test_builds_exact_zero_retry_argv(self):
        command = guest_suites.build_interactive_command(WORKSPACE, RUN_DIR, GTESTS)

        summary_path = f"{RUN_DIR}/interactive-test-summary.json"
        self.assertEqual(
            command.argv[0],
            f"{WORKSPACE}/chromium/src/out/Default/interactive_ui_tests",
        )
        self.assertEqual(command.argv[1], f"--gtest_filter={':'.join(GTESTS)}")
        self.assertIn("--gtest_fail_if_no_test_selected", command.argv)
        self.assertIn("--test-launcher-jobs=1", command.argv)
        self.assertIn("--test-launcher-retry-limit=0", command.argv)
        self.assertIn(f"--test-launcher-summary-output={summary_path}", command.argv)
        self.assertEqual(command.summary_path, summary_path)
        self.assertEqual(command.expected_results, GTESTS)
        self.assertEqual(command.environment, ())
        self.assertNotIn("out/VM-E2E", "\n".join(command.argv))
        self.assertNotIn("/Volumes/maho-build", "\n".join(command.argv))

    def test_builds_exact_argv_for_custom_binary_and_suite(self):
        command = guest_suites.build_interactive_command(
            WORKSPACE,
            RUN_DIR,
            GTESTS,
            suite_name="password-fill",
            binary_name="maho_password_fill_browsertest",
        )

        self.assertEqual(command.suite, "password-fill")
        self.assertEqual(
            command.argv[0],
            (
                f"{WORKSPACE}/chromium/src/out/Default/"
                "maho_password_fill_browsertest"
            ),
        )
        self.assertEqual(command.argv[1], f"--gtest_filter={':'.join(GTESTS)}")
        self.assertEqual(
            command.stdout_path, f"{RUN_DIR}/password-fill.stdout.log"
        )
        self.assertEqual(
            command.stderr_path, f"{RUN_DIR}/password-fill.stderr.log"
        )
        self.assertEqual(command.summary_path, f"{RUN_DIR}/interactive-test-summary.json")

    def test_rejects_unsafe_custom_binary_and_suite_names(self):
        for binary_name in ("../escape", "a/b", ".", "..", "x.runtime_deps"):
            with self.assertRaises(guest_suites.SuiteContractError):
                guest_suites.build_interactive_command(
                    WORKSPACE, RUN_DIR, GTESTS, binary_name=binary_name
                )
        with self.assertRaises(guest_suites.SuiteContractError):
            guest_suites.build_interactive_command(
                WORKSPACE, RUN_DIR, GTESTS, suite_name="../escape"
            )

    def test_rejects_empty_duplicate_and_broad_filters(self):
        invalid_filters = (
            (),
            (GTESTS[0], GTESTS[0]),
            ("Maho*",),
            ("Suite.Test:Other.Test",),
            ("Suite.*-Suite.Flaky",),
        )
        for filters in invalid_filters:
            with self.subTest(filters=filters):
                with self.assertRaises(guest_suites.SuiteContractError):
                    guest_suites.build_interactive_command(WORKSPACE, RUN_DIR, filters)


class InteractiveResultTest(unittest.TestCase):
    def setUp(self):
        self.command = guest_suites.build_interactive_command(WORKSPACE, RUN_DIR, GTESTS)

    def captured(self, returncode=0):
        return guest_suites.CapturedCommandResult(
            command=self.command,
            returncode=returncode,
            stdout="interactive stdout",
            stderr="interactive stderr",
        )

    def test_accepts_exact_single_successful_runs(self):
        validation = guest_suites.validate_interactive_result(
            self.captured(), successful_summary()
        )

        self.assertEqual(validation.expected, GTESTS)
        self.assertEqual(validation.passed, GTESTS)
        self.assertEqual(validation.returncodes, (0,))
        self.assertEqual(validation.to_dict()["status"], "SUCCESS")
        self.assertNotIn("profile_paths", validation.to_dict())

    def test_recovers_exact_pre_result_when_launcher_summary_overwrites_it(self):
        pre_test = "RestartSuite.PRE_PersistsCredential"
        post_test = "RestartSuite.PersistsCredential"
        command = guest_suites.build_interactive_command(
            WORKSPACE, RUN_DIR, (pre_test, post_test)
        )
        captured = guest_suites.CapturedCommandResult(
            command=command,
            returncode=0,
            stdout=(
                f"[       OK ] {pre_test} (17017 ms)\n"
                f"[       OK ] {post_test} (19001 ms)\n"
            ),
            stderr="",
        )
        overwritten_summary = successful_summary((post_test,))

        merged = guest_suites.merge_gtest_summary_with_stdout(
            overwritten_summary, command.expected_results, captured.stdout
        )
        validation = guest_suites.validate_interactive_result(captured, merged)

        self.assertEqual(
            tuple(merged["per_iteration_data"][0]), (post_test, pre_test)
        )
        self.assertEqual(validation.passed, (pre_test, post_test))

    def test_rejects_nonzero_process_even_with_green_summary(self):
        with self.assertRaisesRegex(
            guest_suites.SuiteValidationError, "process exited with 9"
        ):
            guest_suites.validate_interactive_result(
                self.captured(returncode=9), successful_summary()
            )

    def test_rejects_missing_extra_zero_retry_and_failed_results(self):
        fixtures = {
            "missing": successful_summary(GTESTS[:1]),
            "extra": successful_summary((*GTESTS, "UnexpectedSuite.UnexpectedTest")),
            "zero runs": {
                "per_iteration_data": [
                    {GTESTS[0]: [], GTESTS[1]: [{"status": "SUCCESS"}]}
                ]
            },
            "retry": {
                "per_iteration_data": [
                    {
                        GTESTS[0]: [
                            {"status": "FAILURE"},
                            {"status": "SUCCESS"},
                        ],
                        GTESTS[1]: [{"status": "SUCCESS"}],
                    }
                ]
            },
            "failed status": {
                "per_iteration_data": [
                    {
                        GTESTS[0]: [{"status": "FAILURE"}],
                        GTESTS[1]: [{"status": "SUCCESS"}],
                    }
                ]
            },
            "multiple iterations": {
                "per_iteration_data": [
                    successful_summary()["per_iteration_data"][0],
                    successful_summary()["per_iteration_data"][0],
                ]
            },
            "zero tests": {"per_iteration_data": [{}]},
        }
        for name, summary in fixtures.items():
            with self.subTest(name=name):
                with self.assertRaises(guest_suites.SuiteValidationError):
                    guest_suites.validate_interactive_result(self.captured(), summary)


class AiMockCommandTest(unittest.TestCase):
    def test_builds_explicit_node_24_commands_for_all_six_scenarios(self):
        node = "/opt/homebrew/opt/node@24/bin/node"
        commands = guest_suites.build_ai_mock_commands(
            WORKSPACE,
            RUN_DIR,
            guest_suites.AI_MOCK_SCENARIOS,
            node_binary=node,
            cdp_port=19422,
        )

        self.assertEqual(len(commands), 2)
        keyed, no_key = commands
        keyed_scenarios = guest_suites.AI_MOCK_SCENARIOS[:-1]
        self.assertEqual(
            keyed.argv,
            (
                node,
                f"{WORKSPACE}/maho-chromium/build/scripts/e2e_chromium_ai.js",
                "--mock",
                *(argument for name in keyed_scenarios for argument in ("--scenario", name)),
            ),
        )
        self.assertEqual(
            no_key.argv,
            (
                node,
                f"{WORKSPACE}/maho-chromium/build/scripts/e2e_chromium_ai.js",
                "--mock",
                "--scenario",
                "ai_mock_key_cleared_no_request",
            ),
        )
        self.assertEqual(keyed.required_node_major, 24)
        self.assertEqual(no_key.required_node_major, 24)
        self.assertEqual(keyed.cdp_host, "127.0.0.1")
        self.assertEqual(no_key.cdp_host, "127.0.0.1")

        keyed_env = keyed.environment_dict()
        no_key_env = no_key.environment_dict()
        self.assertEqual(keyed_env["MAHO_E2E_CDP_PORT"], "19422")
        self.assertEqual(keyed_env["MAHO_E2E_AI_API_KEY"], "test-key-mock")
        self.assertEqual(no_key_env["MAHO_E2E_AI_API_KEY"], "")
        self.assertEqual(
            keyed_env["MAHO_APP_PATH"],
            f"{WORKSPACE}/chromium/src/out/Default/Maho.app",
        )
        self.assertEqual(
            keyed_env["MAHO_E2E_SOURCE_PROFILE"],
            f"{RUN_DIR}/empty-source-profile",
        )
        self.assertNotIn("MAHO_E2E_KEEP_PROFILE", keyed_env)
        self.assertEqual(keyed.stdout_path, f"{RUN_DIR}/ai-mock.stdout.log")
        self.assertEqual(no_key.stderr_path, f"{RUN_DIR}/ai-mock-no-key.stderr.log")
        generated = "\n".join(
            (*keyed.argv, *keyed_env.values(), *no_key.argv, *no_key_env.values())
        )
        self.assertNotIn("out/VM-E2E", generated)

    def test_rejects_nonloopback_cdp(self):
        for host in ("0.0.0.0", "localhost", "::1", "192.168.64.2"):
            with self.subTest(host=host):
                with self.assertRaisesRegex(
                    guest_suites.SuiteContractError, "must be exactly 127.0.0.1"
                ):
                    guest_suites.build_ai_mock_commands(
                        WORKSPACE,
                        RUN_DIR,
                        guest_suites.AI_MOCK_SCENARIOS,
                        cdp_host=host,
                    )

    def test_rejects_any_scenario_inventory_other_than_exact_six(self):
        fixtures = (
            guest_suites.AI_MOCK_SCENARIOS[:-1],
            (*guest_suites.AI_MOCK_SCENARIOS, "unknown"),
            (*guest_suites.AI_MOCK_SCENARIOS[:-1], guest_suites.AI_MOCK_SCENARIOS[0]),
        )
        for scenarios in fixtures:
            with self.subTest(scenarios=scenarios):
                with self.assertRaises(guest_suites.SuiteContractError):
                    guest_suites.build_ai_mock_commands(WORKSPACE, RUN_DIR, scenarios)


class AiMockResultTest(unittest.TestCase):
    def setUp(self):
        self.commands = guest_suites.build_ai_mock_commands(
            WORKSPACE, RUN_DIR, guest_suites.AI_MOCK_SCENARIOS
        )

    @staticmethod
    def successful_result(command, port):
        pass_lines = "\n".join(
            f"PASS {scenario} — validated" for scenario in command.expected_results
        )
        profile_path = (
            f"{command.environment_dict()['TMPDIR']}/"
            f"maho-ai-e2e-profile-{port}"
        )
        return guest_suites.CapturedCommandResult(
            command=command,
            returncode=0,
            stdout=(
                f"MOCK_LLM base_url=http://127.0.0.1:{port}\n"
                f"PROFILE={profile_path}\n"
                f"{pass_lines}\n"
            ),
            stderr="",
        )

    def successful_results(self):
        return tuple(
            self.successful_result(command, 18000 + index)
            for index, command in enumerate(self.commands)
        )

    def test_accepts_exactly_one_pass_for_all_six_scenarios(self):
        validation = guest_suites.validate_ai_mock_results(self.successful_results())

        self.assertEqual(validation.expected, guest_suites.AI_MOCK_SCENARIOS)
        self.assertEqual(validation.passed, guest_suites.AI_MOCK_SCENARIOS)
        self.assertEqual(validation.returncodes, (0, 0))
        self.assertEqual(
            validation.profile_paths,
            (
                f"{RUN_DIR}/ai-mock-tmp/maho-ai-e2e-profile-18000",
                f"{RUN_DIR}/ai-mock-no-key-tmp/maho-ai-e2e-profile-18001",
            ),
        )
        self.assertEqual(
            validation.to_dict()["profile_paths"],
            list(validation.profile_paths),
        )

    def test_rejects_missing_duplicate_out_of_tmpdir_and_reused_profiles(self):
        keyed, no_key = self.successful_results()
        keyed_profile = next(
            line for line in keyed.stdout.splitlines() if line.startswith("PROFILE=")
        )
        cases = {
            "missing profile": (
                replace(keyed, stdout=keyed.stdout.replace(keyed_profile + "\n", "")),
                no_key,
            ),
            "duplicate profile": (
                replace(keyed, stdout=keyed.stdout + keyed_profile + "\n"),
                no_key,
            ),
            "out of TMPDIR": (
                replace(
                    keyed,
                    stdout=keyed.stdout.replace(
                        keyed_profile,
                        "PROFILE=/private/tmp/maho-ai-e2e-outside",
                    ),
                ),
                no_key,
            ),
            "wrong basename prefix": (
                replace(
                    keyed,
                    stdout=keyed.stdout.replace(
                        keyed_profile,
                        f"PROFILE={RUN_DIR}/ai-mock-tmp/chromium-profile",
                    ),
                ),
                no_key,
            ),
            "reused profile": (
                keyed,
                replace(
                    no_key,
                    command=replace(
                        no_key.command,
                        environment=tuple(
                            (name, f"{RUN_DIR}/ai-mock-tmp" if name == "TMPDIR" else value)
                            for name, value in no_key.command.environment
                        ),
                    ),
                    stdout=no_key.stdout.replace(
                        next(
                            line
                            for line in no_key.stdout.splitlines()
                            if line.startswith("PROFILE=")
                        ),
                        keyed_profile,
                    ),
                ),
            ),
        }
        for name, results in cases.items():
            with self.subTest(name=name):
                with self.assertRaises(guest_suites.SuiteValidationError):
                    guest_suites.validate_ai_mock_results(results)

    def test_rejects_missing_duplicate_failed_and_nonloopback_results(self):
        successful = self.successful_results()
        keyed, no_key = successful
        cases = {
            "missing pass": (
                replace(keyed, stdout=keyed.stdout.replace("PASS ai_panel_renders — validated\n", "")),
                no_key,
            ),
            "duplicate pass": (
                replace(
                    keyed,
                    stdout=keyed.stdout + "PASS ai_panel_renders — duplicate\n",
                ),
                no_key,
            ),
            "failure line": (
                replace(
                    keyed,
                    returncode=1,
                    stdout=keyed.stdout.replace(
                        "PASS ai_mock_server_error — validated",
                        "FAIL ai_mock_server_error — broken",
                    ),
                ),
                no_key,
            ),
            "nonloopback mock": (
                replace(
                    keyed,
                    stdout=keyed.stdout.replace("127.0.0.1", "0.0.0.0"),
                ),
                no_key,
            ),
            "nonloopback cdp command": (
                replace(
                    keyed,
                    command=replace(keyed.command, cdp_host="0.0.0.0"),
                ),
                no_key,
            ),
        }
        for name, results in cases.items():
            with self.subTest(name=name):
                with self.assertRaises(guest_suites.SuiteValidationError):
                    guest_suites.validate_ai_mock_results(results)


class IntentionalFailureFixtureTest(unittest.TestCase):
    def test_builds_exact_fixture_argv_without_retry_flags(self):
        command = guest_suites.build_intentional_failure_command(
            WORKSPACE, RUN_DIR, python_binary="/usr/bin/python3"
        )

        self.assertEqual(
            command.argv,
            (
                "/usr/bin/python3",
                f"{WORKSPACE}/maho-chromium/build/scripts/macos_vm_guest_suites.py",
                "intentional-failure-fixture",
                "--artifact-dir",
                f"{RUN_DIR}/intentional-failure-fixture",
            ),
        )
        self.assertFalse(any("retry" in argument for argument in command.argv))

    def test_fixture_exits_nonzero_and_preserves_diagnostics(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            artifact_dir = Path(temp_dir) / "fixture-artifacts"
            result = subprocess.run(
                (
                    sys.executable,
                    str(SCRIPT_DIR / "macos_vm_guest_suites.py"),
                    "intentional-failure-fixture",
                    "--artifact-dir",
                    str(artifact_dir),
                ),
                capture_output=True,
                text=True,
                check=False,
            )

            self.assertEqual(result.returncode, guest_suites.INTENTIONAL_FAILURE_EXIT_CODE)
            self.assertIn(guest_suites.INTENTIONAL_FAILURE_MARKER, result.stdout)
            self.assertIn(guest_suites.INTENTIONAL_FAILURE_MARKER, result.stderr)
            diagnostic_path = artifact_dir / "intentional-failure.json"
            self.assertTrue(diagnostic_path.is_file())
            diagnostic = json.loads(diagnostic_path.read_text(encoding="utf-8"))
            self.assertEqual(
                diagnostic,
                {
                    "exit_code": guest_suites.INTENTIONAL_FAILURE_EXIT_CODE,
                    "fixture": "intentional-failure-fixture",
                    "intentional": True,
                },
            )


if __name__ == "__main__":
    unittest.main()
