from __future__ import annotations

import json
from pathlib import Path
import re
from typing import TypeAlias, cast

Json: TypeAlias = None | bool | int | float | str | list["Json"] | dict[str, "Json"]
REQUIRED_GTEST = "MahoAiSidePanelLifecycleTest.ShowPanel_EntryIsShowing"
GTESTS = {
    REQUIRED_GTEST,
    "MahoCommandOverlayInteractiveUiTest.FocusPath_UserInitiatedFocusOpensOverlay",
    "MahoSidebarSpacesInteractiveTest.FooterActionOpensSpacesSurface",
}
SCENARIOS = {
    "ai_panel_renders",
    "ai_real_turn",
    "ai_mock_general_turn",
    "ai_mock_quick_action_context",
    "ai_mock_server_error",
    "ai_mock_key_cleared_no_request",
}
BASE_REPOSITORY = "ghcr.io/cirruslabs/macos-tahoe-base"
FORBIDDEN = re.compile(r"(profile|keychain|cookies?|\.env)", re.IGNORECASE)
SECRET = re.compile(
    r"(?:sk-[A-Za-z0-9]{12,}|AIza[\w-]{20,}|BEGIN (?:RSA |EC )?PRIVATE KEY)"
)


def load_object(path: Path, issues: list[str]) -> dict[str, Json]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        issues.append(f"{path}: unreadable JSON: {error}")
        return {}
    if not isinstance(value, dict):
        issues.append(f"{path}: expected JSON object")
        return {}
    return cast(dict[str, Json], value)


def as_object(value: Json) -> dict[str, Json]:
    return value if isinstance(value, dict) else {}


def as_list(value: Json) -> list[Json]:
    return value if isinstance(value, list) else []


def check(condition: bool, issues: list[str], message: str) -> None:
    if not condition:
        issues.append(message)


def suite(receipt: dict[str, Json], name: str) -> dict[str, Json]:
    return next(
        (
            as_object(value)
            for value in as_list(receipt.get("suite_results"))
            if as_object(value).get("name") == name
        ),
        {},
    )


def check_artifact(run_dir: Path, value: Json, issues: list[str]) -> None:
    if not isinstance(value, str):
        issues.append(f"{run_dir.name}: artifact path is not a string")
        return
    path = Path(value)
    path = path if path.is_absolute() else run_dir / path
    try:
        resolved = path.resolve()
        resolved.relative_to(run_dir.resolve())
    except (OSError, ValueError):
        issues.append(f"{run_dir.name}: artifact escapes run directory: {value}")
        return
    check(resolved.exists(), issues, f"{run_dir.name}: missing artifact: {value}")


def check_isolation(run_dir: Path, kind: str, issues: list[str]) -> None:
    evidence = load_object(run_dir / "host-isolation.json", issues)
    check(
        evidence.get("evidence_kind") == kind,
        issues,
        f"{run_dir.name}: wrong isolation kind",
    )
    check(
        evidence.get("is_isolated") is True
        and evidence.get("violation_codes") == [],
        issues,
        f"{run_dir.name}: host isolation failed",
    )


def check_success(
    run_dir: Path,
    receipt: dict[str, Json],
    suites: set[str],
    issues: list[str],
) -> None:
    results = {
        str(as_object(value).get("name")): as_object(value).get("status")
        for value in as_list(receipt.get("suite_results"))
    }
    check(
        receipt.get("primary_status") == receipt.get("terminal_status") == "success",
        issues,
        f"{run_dir.name}: run is not successful",
    )
    check(
        results == {name: "success" for name in suites},
        issues,
        f"{run_dir.name}: unexpected suite results",
    )
    check(
        as_object(receipt.get("teardown_result")).get("succeeded") is True,
        issues,
        f"{run_dir.name}: teardown did not succeed",
    )
    stages = {
        str(as_object(value).get("stage"))
        for value in as_list(receipt.get("isolation_snapshots"))
    }
    check(
        stages == {"baseline", "during", "after"},
        issues,
        f"{run_dir.name}: receipt isolation snapshots are incomplete",
    )
    check_isolation(run_dir, "live_run", issues)


def check_interactive_raw(
    run_dir: Path,
    expected: set[str],
    issues: list[str],
) -> None:
    matches = list((run_dir / "guest-artifacts").rglob("*summary*.json"))
    check(
        len(matches) == 1,
        issues,
        f"{run_dir.name}: expected one interactive summary",
    )
    if not matches:
        return
    summary = load_object(matches[0], issues)
    iterations = as_list(summary.get("per_iteration_data"))
    iteration = as_object(iterations[0]) if len(iterations) == 1 else {}
    statuses: list[Json] = []
    for value in iteration.values():
        runs = as_list(value)
        if len(runs) == 1:
            statuses.append(as_object(runs[0]).get("status"))
    check(
        set(iteration) == expected
        and len(statuses) == len(expected)
        and all(status == "SUCCESS" for status in statuses),
        issues,
        f"{run_dir.name}: raw interactive summary mismatch",
    )


def check_ai_raw(
    run_dir: Path,
    receipt: dict[str, Json],
    issues: list[str],
) -> None:
    logs = sorted(run_dir.glob("ai-mock*.stdout.log"))
    check(len(logs) == 2, issues, f"{run_dir.name}: expected two AI stdout logs")
    texts = [path.read_text(encoding="utf-8") for path in logs]
    lines = [line for text in texts for line in text.splitlines()]
    pass_lines = [line for line in lines if line.startswith("PASS ")]
    passed = {
        line.removeprefix("PASS ").split(" — ", 1)[0] for line in pass_lines
    }
    mocks = [
        line.split("=", 1)[1]
        for line in lines
        if line.startswith("MOCK_LLM base_url=")
    ]
    profiles = [
        line.split("=", 1)[1] for line in lines if line.startswith("PROFILE=")
    ]
    expected_profiles = {
        str(value)
        for value in as_list(as_object(suite(receipt, "ai-mock").get(
            "details"
        )).get("profile_paths"))
    }
    check(
        passed == SCENARIOS and len(pass_lines) == len(SCENARIOS),
        issues,
        f"{run_dir.name}: raw AI PASS lines mismatch",
    )
    check(
        len(mocks) == 2
        and all(value.startswith("http://127.0.0.1:") for value in mocks),
        issues,
        f"{run_dir.name}: MOCK_LLM is not loopback-only",
    )
    check(
        set(profiles) == expected_profiles
        and len(profiles) == len(expected_profiles) == 2,
        issues,
        f"{run_dir.name}: profile paths mismatch",
    )
    check(
        SECRET.search("\n".join(texts)) is None,
        issues,
        f"{run_dir.name}: secret literal in AI stdout",
    )


def check_inventory(path: Path, issues: list[str]) -> None:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        issues.append(f"{path}: unreadable inventory: {error}")
        return
    if not isinstance(value, list) or not all(isinstance(item, dict) for item in value):
        issues.append(f"{path}: inventory must be a JSON object array")
        return
    entries = cast(list[dict[str, object]], value)
    golden = [
        item for item in entries if item.get("Name") == "maho-e2e-golden"
    ]
    oci = [
        item
        for item in entries
        if str(item.get("Name", "")).startswith(f"{BASE_REPOSITORY}@sha256:")
    ]
    unexpected = [
        item for item in entries if item not in golden and item not in oci
    ]
    check(
        len(golden) == 1
        and bool(oci)
        and not unexpected
        and str(golden[0].get("State", "")).lower() == "stopped"
        and golden[0].get("Running") is False,
        issues,
        "inventory is not one stopped golden plus approved OCI digest entries",
    )
