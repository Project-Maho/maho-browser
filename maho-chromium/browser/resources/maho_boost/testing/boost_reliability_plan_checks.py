from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
from pathlib import Path
from typing import Final, Sequence
import xml.etree.ElementTree as ElementTree


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
REQUIRED_FILE_KINDS: Final[tuple[str, ...]] = (
    "xml",
    "log",
    "json",
    "ax",
    "screenshot",
    "verdict",
    "source",
    "build",
    "resource",
)
RECEIPT_PATHS: Final[tuple[tuple[str, str], ...]] = (
    ("F1", "final/f1-plan-compliance.md"),
    ("F2", "final/f2-code-quality.md"),
    ("F3", "final/f3-real-app-qa.md"),
    ("F4", "final/f4-scope-fidelity.md"),
)


@dataclass(frozen=True, slots=True)
class ManifestEntry:
    key: str
    value: str


@dataclass(frozen=True, slots=True)
class EvidenceManifest:
    plan_sha256: str
    entries: tuple[ManifestEntry, ...]


@dataclass(frozen=True, slots=True)
class EvidenceFiles:
    groups: tuple[tuple[str, tuple[Path, ...]], ...]


def values_for(manifest: EvidenceManifest, key: str) -> tuple[str, ...]:
    return tuple(entry.value for entry in manifest.entries if entry.key == key)


def paths_for(files: EvidenceFiles, key: str) -> tuple[Path, ...]:
    for group_key, paths in files.groups:
        if group_key == key:
            return paths
    return ()


def resolve_relative(root: Path, value: str) -> Path | None:
    candidate = Path(value)
    if not value or candidate.is_absolute() or ".." in candidate.parts:
        return None
    resolved_root = root.resolve()
    resolved_candidate = (root / candidate).resolve(strict=False)
    try:
        resolved_candidate.relative_to(resolved_root)
    except ValueError:
        return None
    return resolved_candidate


def read_manifest(root: Path) -> tuple[EvidenceManifest | None, tuple[str, ...]]:
    manifest_path = root / "evidence-manifest.txt"
    if not manifest_path.is_file():
        return None, ("missing-evidence-manifest: evidence-manifest.txt",)
    plan_values: list[str] = []
    entries: list[ManifestEntry] = []
    discrepancies: list[str] = []
    known_keys = frozenset(("plan-sha256", "testcase", *REQUIRED_FILE_KINDS))
    for line_number, raw_line in enumerate(manifest_path.read_text(encoding="utf-8").splitlines(), 1):
        if not raw_line:
            continue
        key, separator, value = raw_line.partition("=")
        if not separator or not key or not value:
            discrepancies.append(f"malformed-manifest-line: {line_number}")
        elif key not in known_keys:
            discrepancies.append(f"unknown-manifest-key: {key}")
        elif key == "plan-sha256":
            plan_values.append(value)
        else:
            entries.append(ManifestEntry(key, value))
    if not plan_values:
        discrepancies.append("missing-plan-sha256")
    if len(plan_values) > 1:
        discrepancies.append("duplicate-plan-sha256")
    plan_sha256 = plan_values[0] if plan_values else ""
    return EvidenceManifest(plan_sha256, tuple(entries)), tuple(discrepancies)


def collect_files(root: Path, source_root: Path, manifest: EvidenceManifest) -> tuple[EvidenceFiles, tuple[str, ...]]:
    groups: list[tuple[str, tuple[Path, ...]]] = []
    discrepancies: list[str] = []
    source_kinds = frozenset(("source", "resource"))
    for key in REQUIRED_FILE_KINDS:
        values = values_for(manifest, key)
        if not values:
            discrepancies.append(f"missing-{key}-entry")
        base = source_root if key in source_kinds else root
        paths: list[Path] = []
        for value in values:
            path = resolve_relative(base, value)
            if path is None:
                discrepancies.append(f"unsafe-{key}-path: {value}")
            elif not path.is_file():
                discrepancies.append(f"missing-{key}-file: {value}")
            else:
                paths.append(path)
        groups.append((key, tuple(paths)))
    return EvidenceFiles(tuple(groups)), tuple(discrepancies)


def testcase_name(case: ElementTree.Element) -> str:
    name = case.get("name", "")
    classname = case.get("classname", "")
    return f"{classname}.{name}" if classname else name


def xml_discrepancies(xml_path: Path, expected_cases: tuple[str, ...]) -> tuple[str, ...]:
    try:
        root = ElementTree.parse(xml_path).getroot()
    except ElementTree.ParseError:
        return (f"malformed-xml: {xml_path.name}",)
    discrepancies: list[str] = []
    cases = tuple(root.iter("testcase"))
    observed = tuple(testcase_name(case) for case in cases)
    for expected in expected_cases:
        count = observed.count(expected)
        if count == 0:
            discrepancies.append(f"xml-case-missing: {expected}")
        if count > 1:
            discrepancies.append(f"xml-case-duplicate: {expected}")
    for name in observed:
        if name not in expected_cases:
            discrepancies.append(f"xml-case-unexpected: {name}")
    for case in cases:
        name = testcase_name(case)
        if case.find("failure") is not None or case.find("error") is not None:
            discrepancies.append(f"xml-case-failed: {name}")
        if case.find("skipped") is not None or case.get("status") == "notrun":
            discrepancies.append(f"xml-case-skipped: {name}")
    for attribute in ("failures", "errors", "skipped"):
        if root.get(attribute, "0") != "0":
            discrepancies.append(f"xml-suite-{attribute}: {xml_path.name}")
    return tuple(discrepancies)


def freshness_discrepancies(files: EvidenceFiles) -> tuple[str, ...]:
    builds = paths_for(files, "build")
    if not builds:
        return ()
    build_mtime = max(path.stat().st_mtime_ns for path in builds)
    discrepancies: list[str] = []
    for key in ("source", "resource"):
        for path in paths_for(files, key):
            if path.stat().st_mtime_ns > build_mtime:
                discrepancies.append(f"{key}-mtime-newer-than-build: {path.name}")
    for path in paths_for(files, "screenshot"):
        if path.stat().st_mtime_ns < build_mtime:
            discrepancies.append(f"screenshot-stale-than-build: {path.name}")
    return tuple(discrepancies)


def final_discrepancies(root: Path, current_review: str | None) -> tuple[str, ...]:
    discrepancies: list[str] = []
    required_reviews = ("F2", "F3", "F4") if current_review == "F1" else ("F1", "F2", "F3", "F4")
    for review, relative_path in RECEIPT_PATHS:
        receipt = root / relative_path
        if not receipt.is_file():
            discrepancies.append(f"missing-final-receipt: {review}")
        elif review in required_reviews and "VERDICT: APPROVE" not in receipt.read_text(encoding="utf-8"):
            discrepancies.append(f"missing-approve-receipt: {review}")
    return tuple(discrepancies)


def validate_evidence(root: Path, plan: Path, source_root: Path, phase: str, current_review: str | None) -> tuple[str, ...]:
    manifest, manifest_discrepancies = read_manifest(root)
    if manifest is None:
        return manifest_discrepancies
    discrepancies = list(manifest_discrepancies)
    if not plan.is_file():
        discrepancies.append(f"missing-plan-file: {plan}")
    elif hashlib.sha256(plan.read_bytes()).hexdigest() != manifest.plan_sha256:
        discrepancies.append("plan-sha256-mismatch")
    if not source_root.is_dir():
        discrepancies.append(f"missing-source-root: {source_root}")
    files, file_discrepancies = collect_files(root, source_root, manifest)
    discrepancies.extend(file_discrepancies)
    expected_cases = values_for(manifest, "testcase")
    if not expected_cases:
        discrepancies.append("missing-testcase-entry")
    for xml_path in paths_for(files, "xml"):
        discrepancies.extend(xml_discrepancies(xml_path, expected_cases))
    discrepancies.extend(freshness_discrepancies(files))
    if phase == "final":
        discrepancies.extend(final_discrepancies(root, current_review))
    if phase == "integration" and current_review is not None:
        discrepancies.append("current-review-not-allowed-for-integration")
    return tuple(discrepancies)


def print_discrepancies(discrepancies: tuple[str, ...]) -> int:
    for discrepancy in discrepancies:
        print(discrepancy)
    return 1 if discrepancies else 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="boost_reliability_plan_checks.py")
    commands = parser.add_subparsers(dest="command", required=True)
    contract = commands.add_parser("contract")
    contract.add_argument("--design", type=Path, required=True)
    evidence = commands.add_parser("evidence")
    evidence.add_argument("--root", type=Path, required=True)
    evidence.add_argument("--plan", type=Path, required=True)
    evidence.add_argument("--source-root", type=Path, required=True)
    evidence.add_argument("--phase", choices=("integration", "final"), required=True)
    evidence.add_argument("--current-review", choices=("F1",))
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = build_parser().parse_args(argv)
    if arguments.command == "contract":
        design = arguments.design
        if not design.is_file():
            return print_discrepancies((f"missing-design-file: {design}",))
        content = design.read_text(encoding="utf-8")
        return print_discrepancies(tuple(f"missing-contract-token: {token}" for token in REQUIRED_CONTRACT_TOKENS if token not in content))
    return print_discrepancies(
        validate_evidence(arguments.root, arguments.plan, arguments.source_root, arguments.phase, arguments.current_review)
    )


if __name__ == "__main__":
    raise SystemExit(main())
