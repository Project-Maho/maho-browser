"""GitHub API wrapper. Uses `gh` CLI for auth (no separate token needed)."""
import subprocess
import json
from typing import Optional


REPO = "Project-Maho/maho-browser"


def _gh(*args: str, input_data: Optional[str] = None) -> dict | list | str:
    """Run gh CLI. Returns parsed JSON if output looks like JSON, else raw str."""
    result = subprocess.run(
        ["gh", *args],
        capture_output=True, text=True, check=True,
        input=input_data,
    )
    out = result.stdout.strip()
    if not out:
        return ""
    try:
        return json.loads(out)
    except json.JSONDecodeError:
        return out


def create_issue(title: str, body: str, labels: list[str]) -> int:
    """Create issue in maho-browser private repo. Returns issue number."""
    label_arg = ",".join(labels)
    out = _gh(
        "issue", "create",
        "--repo", REPO,
        "--title", title,
        "--body", body,
        "--label", label_arg,
    )
    # gh issue create prints the URL on success
    if isinstance(out, str) and "/issues/" in out:
        return int(out.rsplit("/", 1)[-1])
    raise RuntimeError(f"Unexpected gh output: {out!r}")


def get_issue(number: int) -> dict:
    return _gh(
        "issue", "view", str(number),
        "--repo", REPO,
        "--json", "number,title,state,closedAt,labels,milestone,body",
    )


def recent_closed_issues(since_iso: str) -> list[dict]:
    """Issues closed since <since_iso>. Used by release_watcher."""
    return _gh(
        "issue", "list",
        "--repo", REPO,
        "--state", "closed",
        "--search", f"closed:>={since_iso}",
        "--json", "number,title,closedAt,labels",
        "--limit", "100",
    )


def latest_release(repo: str = "Project-Maho/release") -> Optional[dict]:
    """Latest published release from the release-only repo. None if no release yet."""
    try:
        out = _gh(
            "release", "view",
            "--repo", repo,
            "--json", "tagName,publishedAt,name,body",
        )
    except subprocess.CalledProcessError:
        return None  # no release published yet
    return out if isinstance(out, dict) else None
