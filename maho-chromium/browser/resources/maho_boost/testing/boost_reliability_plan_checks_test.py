from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
from typing import Final


WORKSPACE_ROOT: Final = Path(__file__).resolve().parents[5]
DESIGN_PATH: Final = WORKSPACE_ROOT / "maho-chromium/browser/resources/maho_boost/DESIGN.md"
CHECKER_PATH: Final = Path(__file__).with_name("boost_reliability_plan_checks.py")
REQUIRED_CONTRACT_TOKENS: Final[tuple[str, ...]] = (
    "selectedBoostId",
    "activeBoostId",
    "ASCII case-insensitive name, then ID",
    "cancel unsent debounce work",
    "await sent mutations",
    "colorBoostEnabled=true",
    "magicTheme=false",
    "pointer-capture owner",
    "@icons/lucide",
    "Sparkles + Auto",
    "maho-zap-unhide",
    "25 ms",
    "one drain request in flight",
    "184x582",
    "452x582",
    "No new Mojo schema/API",
    "No migration changes existing saved disabled-color Boosts",
    "No GN expansion",
    "Todo 2",
    "Todo 9",
)
CHARACTERIZATION_TOKENS: Final[tuple[str, ...]] = (
    "184x582",
    "452x582",
    "standalone content-script boundary",
    "Mojo schema",
    "This surface has no mobile, tablet, desktop, or fluid responsive mode",
)


@dataclass(frozen=True, slots=True)
class EvidenceFixture:
    root: Path
    plan: Path
    source_root: Path


def test_design_characterization_when_current_contract_is_read() -> None:
    design = DESIGN_PATH.read_text(encoding="utf-8")
    missing = tuple(token for token in CHARACTERIZATION_TOKENS if token not in design)
    assert missing == (), f"missing characterization tokens: {', '.join(missing)}"


def run_checker(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ("python3", str(CHECKER_PATH), *arguments),
        check=False,
        capture_output=True,
        text=True,
    )


def combined_output(result: subprocess.CompletedProcess[str]) -> str:
    return result.stdout + result.stderr


def write_text(path: Path, content: str, mtime_ns: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    os.utime(path, ns=(mtime_ns, mtime_ns))


def create_evidence_fixture() -> tempfile.TemporaryDirectory[str]:
    temporary_directory = tempfile.TemporaryDirectory[str]()
    root = Path(temporary_directory.name) / "evidence"
    source_root = Path(temporary_directory.name) / "source"
    plan = Path(temporary_directory.name) / "plan.md"
    root.mkdir()
    source_root.mkdir()
    write_text(plan, "Boost reliability plan\n", 1_000_000_000)
    write_text(source_root / "source.txt", "source\n", 2_000_000_000)
    write_text(source_root / "resource.txt", "resource\n", 3_000_000_000)
    write_text(root / "build.log", "build\n", 4_000_000_000)
    write_text(root / "task.log", "log\n", 4_500_000_000)
    write_text(root / "result.json", "{}\n", 4_500_000_000)
    write_text(root / "tree.ax.json", "{}\n", 4_500_000_000)
    write_text(root / "screen.png", "png\n", 5_000_000_000)
    write_text(root / "task-1-contract.md", "VERDICT: APPROVE\n", 5_000_000_000)
    write_text(
        root / "results.xml",
        '<testsuite tests="1" failures="0" errors="0" skipped="0">'
        '<testcase classname="BoostTest" name="Ready"/>'
        "</testsuite>\n",
        5_000_000_000,
    )
    return temporary_directory


def fixture_paths(temporary_directory: str) -> EvidenceFixture:
    temporary_root = Path(temporary_directory)
    return EvidenceFixture(
        root=temporary_root / "evidence",
        plan=temporary_root / "plan.md",
        source_root=temporary_root / "source",
    )


def write_manifest(fixture: EvidenceFixture, entries: tuple[str, ...]) -> None:
    plan_sha256 = hashlib.sha256(fixture.plan.read_bytes()).hexdigest()
    content = "\n".join((f"plan-sha256={plan_sha256}", *entries, ""))
    (fixture.root / "evidence-manifest.txt").write_text(content, encoding="utf-8")


def complete_manifest_entries() -> tuple[str, ...]:
    return (
        "xml=results.xml",
        "testcase=BoostTest.Ready",
        "log=task.log",
        "json=result.json",
        "ax=tree.ax.json",
        "screenshot=screen.png",
        "verdict=task-1-contract.md",
        "source=source.txt",
        "build=build.log",
        "resource=resource.txt",
    )


XML_FAILURE_CASES: Final[tuple[tuple[str, str], ...]] = (
    ("<testsuite", "malformed-xml: results.xml"),
    ("<testsuite/>\n", "xml-case-missing: BoostTest.Ready"),
    (
        "<testsuite><testcase classname=\"BoostTest\" name=\"Ready\"/>"
        "<testcase classname=\"BoostTest\" name=\"Ready\"/></testsuite>\n",
        "xml-case-duplicate: BoostTest.Ready",
    ),
    (
        "<testsuite skipped=\"1\"><testcase classname=\"BoostTest\" name=\"Ready\">"
        "<skipped/></testcase></testsuite>\n",
        "xml-case-skipped: BoostTest.Ready",
    ),
    (
        "<testsuite failures=\"1\"><testcase classname=\"BoostTest\" name=\"Ready\">"
        "<failure/></testcase></testsuite>\n",
        "xml-case-failed: BoostTest.Ready",
    ),
)


def evidence_arguments(fixture: EvidenceFixture, *phase_arguments: str) -> tuple[str, ...]:
    return (
        "evidence",
        "--root",
        str(fixture.root),
        "--plan",
        str(fixture.plan),
        "--source-root",
        str(fixture.source_root),
        *phase_arguments,
    )


def assert_failure_includes(result: subprocess.CompletedProcess[str], diagnostic: str) -> None:
    assert result.returncode != 0, "checker unexpectedly succeeded"
    assert diagnostic in combined_output(result), combined_output(result)


def test_contract_when_each_required_token_is_missing_reports_every_token() -> None:
    with tempfile.TemporaryDirectory[str]() as temporary_directory:
        design = Path(temporary_directory) / "DESIGN.md"
        original = "\n".join(REQUIRED_CONTRACT_TOKENS)
        for token in REQUIRED_CONTRACT_TOKENS:
            design.write_text(original.replace(token, ""), encoding="utf-8")
            result = run_checker("contract", "--design", str(design))
            assert_failure_includes(result, f"missing-contract-token: {token}")


def test_evidence_when_integration_artifact_is_missing_reports_the_category() -> None:
    with create_evidence_fixture() as temporary_directory:
        fixture = fixture_paths(temporary_directory)
        entries = tuple(entry for entry in complete_manifest_entries() if not entry.startswith("log="))
        write_manifest(fixture, entries)
        assert_failure_includes(
            run_checker(*evidence_arguments(fixture, "--phase", "integration")),
            "missing-log-entry",
        )


def test_evidence_when_integration_manifest_is_complete_succeeds() -> None:
    with create_evidence_fixture() as temporary_directory:
        fixture = fixture_paths(temporary_directory)
        write_manifest(fixture, complete_manifest_entries())
        result = run_checker(*evidence_arguments(fixture, "--phase", "integration"))
        assert result.returncode == 0, combined_output(result)


def test_evidence_when_manifest_line_is_malformed_reports_it() -> None:
    with create_evidence_fixture() as temporary_directory:
        fixture = fixture_paths(temporary_directory)
        write_text(fixture.root / "evidence-manifest.txt", "malformed\n", 5_000_000_000)
        assert_failure_includes(
            run_checker(*evidence_arguments(fixture, "--phase", "integration")),
            "malformed-manifest-line: 1",
        )


def test_evidence_when_xml_is_invalid_reports_each_named_discrepancy() -> None:
    for xml, diagnostic in XML_FAILURE_CASES:
        with create_evidence_fixture() as temporary_directory:
            fixture = fixture_paths(temporary_directory)
            write_manifest(fixture, complete_manifest_entries())
            write_text(fixture.root / "results.xml", xml, 5_000_000_000)
            assert_failure_includes(
                run_checker(*evidence_arguments(fixture, "--phase", "integration")), diagnostic
            )


def test_evidence_when_screenshot_predates_build_reports_stale_state() -> None:
    with create_evidence_fixture() as temporary_directory:
        fixture = fixture_paths(temporary_directory)
        write_manifest(fixture, complete_manifest_entries())
        os.utime(fixture.root / "screen.png", ns=(3_000_000_000, 3_000_000_000))
        assert_failure_includes(
            run_checker(*evidence_arguments(fixture, "--phase", "integration")),
            "screenshot-stale-than-build: screen.png",
        )


def test_evidence_when_source_is_newer_than_build_reports_stale_build() -> None:
    with create_evidence_fixture() as temporary_directory:
        fixture = fixture_paths(temporary_directory)
        write_manifest(fixture, complete_manifest_entries())
        os.utime(fixture.source_root / "source.txt", ns=(5_000_000_000, 5_000_000_000))
        assert_failure_includes(
            run_checker(*evidence_arguments(fixture, "--phase", "integration")),
            "source-mtime-newer-than-build: source.txt",
        )


def write_final_receipts(fixture: EvidenceFixture, approved: tuple[str, ...]) -> None:
    receipt_names = (
        ("F1", "f1-plan-compliance.md"),
        ("F2", "f2-code-quality.md"),
        ("F3", "f3-real-app-qa.md"),
        ("F4", "f4-scope-fidelity.md"),
    )
    for review, receipt_name in receipt_names:
        content = "VERDICT: APPROVE\n" if review in approved else "VERDICT: PENDING\n"
        write_text(fixture.root / f"final/{receipt_name}", content, 6_000_000_000)


def test_evidence_when_current_review_is_f1_requires_f2_through_f4() -> None:
    with create_evidence_fixture() as temporary_directory:
        fixture = fixture_paths(temporary_directory)
        write_manifest(fixture, complete_manifest_entries())
        write_final_receipts(fixture, ())
        result = run_checker(*evidence_arguments(fixture, "--phase", "final", "--current-review", "F1"))
        for review in ("F2", "F3", "F4"):
            assert_failure_includes(result, f"missing-approve-receipt: {review}")
        assert "missing-approve-receipt: F1" not in combined_output(result)


def test_evidence_when_current_review_f1_has_other_approvals_succeeds() -> None:
    with create_evidence_fixture() as temporary_directory:
        fixture = fixture_paths(temporary_directory)
        write_manifest(fixture, complete_manifest_entries())
        write_final_receipts(fixture, ("F2", "F3", "F4"))
        result = run_checker(*evidence_arguments(fixture, "--phase", "final", "--current-review", "F1"))
        assert result.returncode == 0, combined_output(result)


def test_evidence_when_final_receipt_f1_is_not_approved_reports_it() -> None:
    with create_evidence_fixture() as temporary_directory:
        fixture = fixture_paths(temporary_directory)
        write_manifest(fixture, complete_manifest_entries())
        write_final_receipts(fixture, ("F2", "F3", "F4"))
        assert_failure_includes(
            run_checker(*evidence_arguments(fixture, "--phase", "final")),
            "missing-approve-receipt: F1",
        )


def run_test(test: Callable[[], None]) -> None:
    test()
    print(f"PASS {test.__name__}")


def main() -> int:
    tests: Final[tuple[Callable[[], None], ...]] = (
        test_design_characterization_when_current_contract_is_read,
        test_contract_when_each_required_token_is_missing_reports_every_token,
        test_evidence_when_integration_artifact_is_missing_reports_the_category,
        test_evidence_when_integration_manifest_is_complete_succeeds,
        test_evidence_when_manifest_line_is_malformed_reports_it,
        test_evidence_when_xml_is_invalid_reports_each_named_discrepancy,
        test_evidence_when_screenshot_predates_build_reports_stale_state,
        test_evidence_when_source_is_newer_than_build_reports_stale_build,
        test_evidence_when_current_review_is_f1_requires_f2_through_f4,
        test_evidence_when_current_review_f1_has_other_approvals_succeeds,
        test_evidence_when_final_receipt_f1_is_not_approved_reports_it,
    )
    for test in tests:
        run_test(test)
    print(f"PASS {len(tests)} tests")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
