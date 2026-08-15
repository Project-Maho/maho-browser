from __future__ import annotations

import json
from pathlib import Path
import re
from typing import TypeAlias

Json: TypeAlias = None | bool | int | float | str | list["Json"] | dict[str, "Json"]
FORBIDDEN_PATH = re.compile(
    r"(browser[-_ ]?profile|keychain|cookies?|source[-_ ]?preferences|"
    r"raw[-_ ]?env|environment[-_ ]?dump|\.env(?:\.|$))",
    re.IGNORECASE,
)
SECRET_VALUE = re.compile(
    r"(?:sk-[A-Za-z0-9]{12,}|AIza[\w-]{20,}|"
    r"BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY|"
    r"gh[opusr]_[A-Za-z0-9]{20,})"
)
SECRET_NAME = re.compile(
    r"(?:^|[_-])(?:api[_-]?key|password|authorization|client[_-]?secret|"
    r"private[_-]?key|credential)(?:$|[_-])",
    re.IGNORECASE,
)
SENSITIVE_ASSIGNMENT = re.compile(
    r"(?:OPENAI_API_KEY|ANTHROPIC_API_KEY|GOOGLE_API_KEY|"
    r"MAHO_E2E_AI_API_KEY|GH_TOKEN)="
    r"(?!<redacted>(?:\s|$)|(?:\"|')?<redacted>(?:\"|')?)\S+"
)
TEXT_SUFFIXES = {".json", ".jsonl", ".log", ".txt"}


def _check_json(
    value: Json,
    path: Path,
    issues: list[str],
    parent_key: str | None = None,
) -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            _check_json(child, path, issues, key)
        return
    if isinstance(value, list):
        for child in value:
            _check_json(child, path, issues, parent_key)
        return
    if (
        parent_key is not None
        and SECRET_NAME.search(parent_key)
        and value not in (None, "", "<redacted>")
    ):
        issues.append(f"{path}: unredacted secret field {parent_key!r}")


def _scan_text(path: Path, issues: list[str]) -> None:
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        issues.append(f"{path}: unreadable evidence text: {error}")
        return
    if SECRET_VALUE.search(text):
        issues.append(f"{path}: credential-shaped value")
    if SENSITIVE_ASSIGNMENT.search(text):
        issues.append(f"{path}: raw sensitive environment assignment")
    if path.suffix == ".json":
        try:
            value = json.loads(text)
        except json.JSONDecodeError as error:
            issues.append(f"{path}: invalid JSON evidence: {error}")
            return
        _check_json(value, path, issues)
    elif path.name == "commands.log":
        for line_number, line in enumerate(text.splitlines(), 1):
            try:
                value = json.loads(line)
            except json.JSONDecodeError as error:
                issues.append(
                    f"{path}:{line_number}: invalid JSON command evidence: {error}"
                )
                continue
            _check_json(value, path, issues)


def scan_evidence(
    root: Path,
    run_ids: tuple[str, ...],
    matrix_dir: Path,
    issues: list[str],
) -> None:
    directories = tuple(root / run_id for run_id in run_ids) + (matrix_dir,)
    seen: set[Path] = set()
    for directory in directories:
        if not directory.is_dir():
            issues.append(f"{directory}: evidence directory missing")
            continue
        for path in directory.rglob("*"):
            if path in seen:
                continue
            seen.add(path)
            relative = path.relative_to(directory)
            if path.is_symlink():
                issues.append(f"{path}: symlink evidence is forbidden")
                continue
            if FORBIDDEN_PATH.search(str(relative)):
                issues.append(f"{path}: forbidden evidence path")
            if path.is_file() and (
                path.suffix in TEXT_SUFFIXES or path.name == "commands.log"
            ):
                _scan_text(path, issues)
