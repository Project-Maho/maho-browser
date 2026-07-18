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

load_dotenv(Path(__file__).parent / ".env")

ANN_CHANNEL_ID = int(os.environ["MAHO_ANNOUNCEMENTS_CHANNEL_ID"])
ISSUE_RE = re.compile(r"#(\d+)")

log = logging.getLogger("release-watcher")


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
            ann = client.get_channel(ANN_CHANNEL_ID)
            if ann:
                lines = [f"🚀 **maho {tag} released**"]
                if rel.get("name"):
                    lines.append(f"_{rel['name']}_")
                if rel.get("body"):
                    # Strip GitHub-flavored issue refs since we don't expose those
                    clean = ISSUE_RE.sub("", rel["body"])[:1500]
                    lines.append("")
                    lines.append(clean)
                lines.append("")
                lines.append("Download: https://mahobrowser.com/download")
                await ann.send("\n".join(lines))

            db.mark_release("Project-Maho/release", tag, rel["publishedAt"])
        finally:
            await client.close()

    await client.start(os.environ["DISCORD_BOT_TOKEN"])


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s: %(message)s")
    db.init()
    asyncio.run(run())
