"""SQLite mapping: discord_thread_id <-> github_issue_number."""
import sqlite3
from pathlib import Path
from datetime import datetime, timezone

DB_PATH = Path(__file__).parent / "mapping.db"


def _conn():
    conn = sqlite3.connect(DB_PATH)
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA foreign_keys=ON")
    return conn


def init():
    with _conn() as c:
        c.executescript("""
        CREATE TABLE IF NOT EXISTS threads (
            thread_id TEXT PRIMARY KEY,
            channel_id TEXT NOT NULL,
            channel_name TEXT NOT NULL,
            issue_number INTEGER,
            issue_repo TEXT,
            kind TEXT NOT NULL CHECK(kind IN ('bug','feature','unknown')),
            author_id TEXT NOT NULL,
            author_name TEXT NOT NULL,
            title TEXT NOT NULL,
            created_at TEXT NOT NULL,
            replied_at TEXT,
            issue_created_at TEXT,
            closed_notified_at TEXT
        );
        CREATE INDEX IF NOT EXISTS idx_issue ON threads(issue_repo, issue_number);
        CREATE INDEX IF NOT EXISTS idx_kind ON threads(kind);

        CREATE TABLE IF NOT EXISTS releases_seen (
            tag TEXT PRIMARY KEY,
            repo TEXT NOT NULL,
            published_at TEXT NOT NULL,
            processed_at TEXT NOT NULL
        );
        """)


def upsert_thread(thread_id: str, channel_id: str, channel_name: str,
                  kind: str, author_id: str, author_name: str, title: str):
    now = datetime.now(timezone.utc).isoformat()
    with _conn() as c:
        c.execute("""
        INSERT INTO threads(thread_id, channel_id, channel_name, kind,
                             author_id, author_name, title, created_at)
        VALUES(?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(thread_id) DO NOTHING
        """, (thread_id, channel_id, channel_name, kind, author_id, author_name, title, now))


def set_issue(thread_id: str, repo: str, number: int):
    now = datetime.now(timezone.utc).isoformat()
    with _conn() as c:
        c.execute("""UPDATE threads
                     SET issue_number=?, issue_repo=?, issue_created_at=?
                     WHERE thread_id=?""",
                  (number, repo, now, thread_id))


def set_replied(thread_id: str):
    now = datetime.now(timezone.utc).isoformat()
    with _conn() as c:
        c.execute("UPDATE threads SET replied_at=? WHERE thread_id=?", (now, thread_id))


def threads_for_issues(repo: str, numbers: list[int]) -> list[dict]:
    if not numbers:
        return []
    q = "SELECT * FROM threads WHERE issue_repo=? AND issue_number IN ({})".format(
        ",".join("?" * len(numbers)))
    with _conn() as c:
        c.row_factory = sqlite3.Row
        rows = c.execute(q, (repo, *numbers)).fetchall()
        return [dict(r) for r in rows]


def mark_closed_notified(thread_id: str):
    now = datetime.now(timezone.utc).isoformat()
    with _conn() as c:
        c.execute("UPDATE threads SET closed_notified_at=? WHERE thread_id=?",
                  (now, thread_id))


def release_seen(repo: str, tag: str) -> bool:
    with _conn() as c:
        r = c.execute("SELECT 1 FROM releases_seen WHERE repo=? AND tag=?",
                      (repo, tag)).fetchone()
        return r is not None


def mark_release(repo: str, tag: str, published_at: str):
    now = datetime.now(timezone.utc).isoformat()
    with _conn() as c:
        c.execute("""INSERT OR IGNORE INTO releases_seen(tag, repo, published_at, processed_at)
                     VALUES(?, ?, ?, ?)""", (tag, repo, published_at, now))


if __name__ == "__main__":
    init()
    print(f"DB initialized at {DB_PATH}")
