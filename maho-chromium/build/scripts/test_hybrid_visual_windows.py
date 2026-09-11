#!/usr/bin/env python3
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

SCRIPT_DIR = Path(__file__).resolve().parent
WINDOWS_PS1 = SCRIPT_DIR / "hybrid_visual_windows.ps1"
SCENARIOS = (
    "sentinel", "coordinates", "permission-denied", "native-input", "security",
    "model-loop", "recovery", "fingerprint", "challenge", "misclick", "telemetry",
    "turnstile-online", "model-provider-online",
)
VERIFICATION_FIELDS = re.compile(
    r"(?:_verified|_bounded|_capable|_visible)$|^(?:passed|isTrusted|recovered)$",
    re.IGNORECASE,
)


def script_text():
    return WINDOWS_PS1.read_text(encoding="utf-8")


def powershell_hashtable_assignments(source):
    """Parse field/value pairs inside PowerShell hashtable literals.

    This intentionally tracks balanced @{ ... } regions rather than grepping for
    decorative words. It is sufficient for receipt literals and catches nested
    receipt fields as well.
    """
    assignments = []
    stack = []
    offset = 0
    for line_number, line in enumerate(source.splitlines(), 1):
        code = line.split("#", 1)[0]
        for match in re.finditer(r"@\{|[{}]", code):
            token = match.group(0)
            if token == "@{":
                stack.append("hashtable")
            elif token == "{":
                stack.append("block")
            elif stack:
                stack.pop()
        if "hashtable" in stack:
            assignment = re.match(
                r"\s*(['\"]?)([A-Za-z][A-Za-z0-9_-]*)\1\s*=\s*(.+?)\s*$", code
            )
            if assignment:
                assignments.append(
                    (assignment.group(2), assignment.group(3), line_number, offset)
                )
        offset += len(line) + 1
    return assignments


class HybridVisualWindowsContractTests(unittest.TestCase):
    def test_parameter_surface_and_all_scenarios_are_fixed(self):
        content = script_text()
        for parameter in ("$Scenario", "$Cli", "$App", "$EvidenceDir", "$Output"):
            self.assertIn(parameter, content)
        validate_set = re.search(r"\[ValidateSet\((.*?)\)\]", content, re.DOTALL)
        self.assertIsNotNone(validate_set)
        declared = tuple(re.findall(r"'([^']+)'", validate_set.group(1)))
        self.assertEqual(SCENARIOS, declared)

    def test_mock_bypass_is_completely_absent(self):
        self.assertNotIn("MAHO_STEALTH_MOCK", script_text())

    def test_receipt_verification_fields_are_not_bare_true_literals(self):
        offenders = []
        for field, value, line_number, _ in powershell_hashtable_assignments(script_text()):
            if VERIFICATION_FIELDS.search(field) and re.fullmatch(r"\$true", value, re.I):
                offenders.append(f"line {line_number}: {field} = {value}")
        self.assertEqual([], offenders, "fabricated receipt claims:\n" + "\n".join(offenders))

    def test_preflight_collects_exact_machine_readable_blockers(self):
        content = script_text()
        self.assertIn('"cli_binary_missing:$Cli"', content)
        self.assertIn('"app_binary_missing:$App"', content)
        self.assertIn("'non_interactive_window_station'", content)
        preflight = content.index("# Preflight always executes")
        launch = content.index("Start-BrowserPipeSession", preflight)
        self.assertLess(content.index("Write-FinalSummary 'BLOCKED'", preflight), launch)

    def test_preflight_summary_contract_exits_two_without_receipts(self):
        content = script_text()
        summary_body = re.search(
            r"function Write-FinalSummary.*?\$summary\s*=\s*\[ordered\]@\{(.*?)\n\s*\}",
            content,
            re.DOTALL,
        )
        self.assertIsNotNone(summary_body)
        body = summary_body.group(1)
        self.assertRegex(body, r"status\s*=\s*\$Status")
        self.assertRegex(body, r"blockers\s*=\s*@\(\$Blockers\)")
        self.assertRegex(body, r"browser_executed\s*=\s*\$script:BrowserExecuted")
        self.assertRegex(body, r"receipts\s*=\s*@\(\$script:ObservedReceipts\)")
        self.assertRegex(content, r"Write-FinalSummary 'BLOCKED' @\(\$preflightBlockers\) \$null 2")

    def test_real_session_waits_for_browser_pipe_and_exchanges_jsonl(self):
        content = script_text()
        self.assertIn("MCP: Listening on pipe", content)
        self.assertIn("browser pipe", content)
        self.assertIn("StandardInput.WriteLine($requestJson)", content)
        self.assertIn("StandardOutput.ReadLineAsync()", content)
        self.assertIn("cli_pipe_response_id_mismatch", content)
        self.assertNotIn("Start-Sleep", content)

    def test_fixture_receipt_subscription_precedes_native_dispatch(self):
        content = script_text()
        subscribe = content.index("$receiptPending = $script:Fixture.GetContextAsync()")
        dispatch = content.index("$nativeResponse = Invoke-BrowserRequest", subscribe)
        await_receipt = content.index("Receive-JsonReceipt $receiptPending", dispatch)
        self.assertLess(subscribe, dispatch)
        self.assertLess(dispatch, await_receipt)
        self.assertIn("fixture_timeout:", content)

    def test_pass_is_derived_only_from_rpc_or_observed_receipt(self):
        content = script_text()
        pass_assignments = list(re.finditer(r"\$status\s*=\s*'PASS'", content))
        self.assertEqual(2, len(pass_assignments))
        contexts = [content[max(0, match.start() - 220):match.start()] for match in pass_assignments]
        self.assertTrue(any("rpcCode -eq -32011" in context for context in contexts))
        self.assertTrue(any("delivered.isTrusted -eq $true" in context for context in contexts))
        self.assertNotRegex(content, r"\$scenarioStatus\s*=\s*['\"]PASS")

    def test_unsupported_scenarios_have_exact_blocker_reasons(self):
        content = script_text()
        expected = {
            "sentinel": "windows_handler_not_implemented:sentinel",
            "security": "windows_handler_not_implemented:security",
            "model-loop": "windows_handler_not_implemented:model-loop",
            "recovery": "windows_handler_not_implemented:recovery",
            "fingerprint": "windows_handler_not_implemented:fingerprint",
            "challenge": "windows_handler_not_implemented:challenge",
            "misclick": "windows_handler_not_implemented:misclick",
            "telemetry": "windows_telemetry_receipt_not_implemented",
            "turnstile-online": "windows_turnstile_siteverify_not_implemented",
            "model-provider-online": "windows_model_provider_rpc_not_implemented",
        }
        for scenario, reason in expected.items():
            with self.subTest(scenario=scenario):
                self.assertRegex(
                    content,
                    rf"(?m)^\s*['\"]?{re.escape(scenario)}['\"]?\s*=\s*'{re.escape(reason)}'\s*$",
                )

    def test_existing_artifact_filenames_are_preserved(self):
        content = script_text()
        for filename in (
            "cleanup.json", "transcript.json", "windows-summary.json",
            "fingerprint-audit.json", "turnstile-receipt.json",
            "provider-online-receipt.json",
        ):
            self.assertIn(filename, content)
        self.assertIn('"$Name-receipt.json"', content)

    def test_missing_binary_execution_writes_blocked_summary_when_pwsh_available(self):
        pwsh = shutil.which("pwsh")
        if pwsh is None:
            # The macOS worker does not provision PowerShell. Static contract
            # tests above still run; when pwsh exists this branch cannot hide a
            # failure because the subprocess assertions below are mandatory.
            return
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            evidence = root / "evidence"
            output = root / "summary.json"
            missing_cli = root / "missing-cli.exe"
            missing_app = root / "missing-app.exe"
            completed = subprocess.run(
                [
                    pwsh, "-NoProfile", "-NonInteractive", "-File", str(WINDOWS_PS1),
                    "-Scenario", "sentinel", "-Cli", str(missing_cli),
                    "-App", str(missing_app), "-EvidenceDir", str(evidence),
                    "-Output", str(output),
                ],
                text=True,
                capture_output=True,
                timeout=30,
                check=False,
            )
            self.assertEqual(2, completed.returncode, completed.stderr)
            self.assertTrue(output.is_file(), completed.stderr)
            summary = json.loads(output.read_text(encoding="utf-8-sig"))
            self.assertEqual("BLOCKED", summary["status"])
            self.assertFalse(summary["browser_executed"])
            self.assertEqual([], summary["receipts"])
            self.assertEqual(
                [f"cli_binary_missing:{missing_cli}", f"app_binary_missing:{missing_app}"],
                summary["blockers"],
            )


if __name__ == "__main__":
    unittest.main()
