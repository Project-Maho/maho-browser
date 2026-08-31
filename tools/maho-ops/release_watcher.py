"""Release watcher.

Called periodically by Hermes cron (or `make release-check`).

1. Check Project-Maho/release for a new tag not yet seen in mapping.db
2. Parse release body for issue references (`#123`, `Fixes #456`, etc.)
3. For each referenced issue that we have a mapping for, post a comment
   in the linked Discord thread saying "Fixed in vX.Y.Z" (no GitHub link)
4. Post a release announcement in #announcements
"""
import os
import re
import asyncio
import logging
from pathlib import Path

import discord
from dotenv import load_dotenv

import db
import github_ops
import llm

load_dotenv(Path(__file__).parent / ".env")

ANN_CHANNEL_ID = int(os.environ.get("MAHO_ANNOUNCEMENTS_CHANNEL_ID", "0"))
ISSUE_RE = re.compile(r"#(\d+)")

log = logging.getLogger("release-watcher")


def build_announcement(tag: str, name: str | None, summary: str | None) -> str:
    lines = [f"🚀 **maho {tag} released**"]
    if name:
        lines.append(f"_{name}_")
    if summary:
        lines.append("")
        lines.append(summary)
    lines.append("")
    lines.append("Download: https://mahobrowser.com/download")
    return "\n".join(lines)


async def run():
    rel = github_ops.latest_release()
    if not rel:
        log.info("No release found.")
        return

    tag = rel["tagName"]
    if db.release_seen("Project-Maho/release", tag):
        log.info(f"Release {tag} already processed.")
        return

    log.info(f"New release {tag}, processing…")
    body = rel.get("body") or ""
    issue_numbers = list({int(m) for m in ISSUE_RE.findall(body)})
    threads = db.threads_for_issues(github_ops.REPO, issue_numbers)

    summary = ""
    if body:
        try:
            summary = await asyncio.to_thread(
                llm.summarize_release, tag, rel.get("name") or "", body
            )
        except Exception:
            log.exception("LLM release summarization failed; falling back to raw body")
            summary = ISSUE_RE.sub("", body)[:1500]
        if not summary:
            summary = ISSUE_RE.sub("", body)[:1500]

    payload = build_announcement(tag, rel.get("name"), summary)

    ann_channel_id = int(os.environ.get("MAHO_ANNOUNCEMENTS_CHANNEL_ID", ANN_CHANNEL_ID or "0"))

    dry_run = os.environ.get("MAHO_DRY_RUN") == "1"
    if dry_run:
        log.info(f"[DRY RUN] Would notify {len(threads)} threads for issues: {issue_numbers}")
        log.info(f"[DRY RUN] Final payload for #{ann_channel_id}:\n{payload}")
        return

    token = os.environ.get("DISCORD_BOT_TOKEN")
    if not token:
        log.error("DISCORD_BOT_TOKEN is not set.")
        return

    intents = discord.Intents.default()
    client = discord.Client(intents=intents)

    @client.event
    async def on_ready():
        try:
            # 1. Notify each linked thread
            for t in threads:
                if t.get("closed_notified_at"):
                    continue
                try:
                    ch = client.get_channel(int(t["thread_id"])) or await client.fetch_channel(int(t["thread_id"]))
                    if ch:
                        await ch.send(
                            f"이 문제는 최근 릴리즈 **{tag}** 에서 반영됐습니다. "
                            f"업데이트 후 확인 부탁드리며, 여전히 재현되면 댓글 남겨주세요."
                        )
                        db.mark_closed_notified(t["thread_id"])
                        log.info(f"Notified thread {t['thread_id']} for issue #{t['issue_number']}")
                except Exception:
                    log.exception(f"Failed to notify thread {t['thread_id']}")

            # 2. Post release announcement
            ann = client.get_channel(ann_channel_id)
            if ann:
                await ann.send(payload)

            db.mark_release("Project-Maho/release", tag, rel.get("publishedAt", ""))
        finally:
            await client.close()

    await client.start(token)


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s: %(message)s")
    db.init()
    asyncio.run(run())
