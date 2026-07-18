"""maho-bot: Discord Forum thread listener.

Watches #bug-report and #feedback Forum channels for new threads.
For each new thread:
1. Wait a few seconds for the OP message to settle
2. LLM analyze → structured issue body + severity + ack_message
3. Create GitHub private issue in Project-Maho/maho-browser
4. Reply in the Discord thread with ack_message (no GitHub link)
5. Persist mapping in mapping.db

Does NOT auto-close, does NOT post GitHub URL, does NOT commit to timeline.
"""
import os
import asyncio
import logging
from pathlib import Path

import discord
from dotenv import load_dotenv

import db
import github_ops
import llm

load_dotenv(Path(__file__).parent / ".env")

GUILD_ID = int(os.environ["MAHO_GUILD_ID"])
BUG_FORUM_ID = int(os.environ["MAHO_BUG_FORUM_ID"])
FEEDBACK_FORUM_ID = int(os.environ["MAHO_FEEDBACK_FORUM_ID"])
MOD_CHANNEL_ID = int(os.environ["MAHO_MOD_CHANNEL_ID"])

LOG_DIR = Path(__file__).parent / "logs"
LOG_DIR.mkdir(exist_ok=True)
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    handlers=[
        logging.FileHandler(LOG_DIR / "bot.log"),
        logging.StreamHandler(),
    ],
)
log = logging.getLogger("maho-bot")

intents = discord.Intents.default()
intents.message_content = True
intents.guilds = True
intents.messages = True

bot = discord.Client(intents=intents)

FORUM_MAP = {
    BUG_FORUM_ID: ("bug", "bug-report"),
    FEEDBACK_FORUM_ID: ("feature", "feedback"),
}


@bot.event
async def on_ready():
    log.info(f"Logged in as {bot.user} (id={bot.user.id})")
    db.init()


@bot.event
async def on_thread_create(thread: discord.Thread):
    if thread.parent_id not in FORUM_MAP:
        return
    kind_hint, forum_name = FORUM_MAP[thread.parent_id]
    log.info(f"New thread in #{forum_name}: {thread.name!r} (id={thread.id}) by {thread.owner}")

    # Wait for the OP message to arrive
    await asyncio.sleep(3)

    try:
        starter = await thread.fetch_message(thread.id)
    except discord.NotFound:
        # Fallback: read first message
        msgs = [m async for m in thread.history(limit=1, oldest_first=True)]
        if not msgs:
            log.warning(f"No starter message for thread {thread.id}")
            return
        starter = msgs[0]

    author = starter.author
    body_text = starter.content or "(no body)"

    db.upsert_thread(
        thread_id=str(thread.id),
        channel_id=str(thread.parent_id),
        channel_name=forum_name,
        kind=kind_hint,
        author_id=str(author.id),
        author_name=str(author),
        title=thread.name,
    )

    # LLM analysis
    try:
        analysis = llm.analyze(
            original_report=body_text,
            author_name=str(author),
            thread_title=thread.name,
            forum_name=forum_name,
        )
    except Exception:
        log.exception("LLM analysis failed")
        await thread.send("접수했습니다. 곧 확인하겠습니다. / Received, will look into it.")
        db.set_replied(str(thread.id))
        return

    kind = analysis.get("kind", kind_hint)
    severity = analysis.get("severity", "p2")
    title = analysis.get("title", thread.name)
    issue_body = analysis.get("body", body_text)
    ack = analysis.get("ack_message", "접수했습니다.")

    labels = ["discord-sourced", severity]
    if kind == "bug":
        labels.append("bug")
    elif kind == "feature":
        labels.append("enhancement")

    footer = (
        f"\n\n---\n"
        f"_Auto-created from Discord Forum by maho-bot._\n"
        f"_Discord thread: {thread.id} (channel #{forum_name})_\n"
        f"_Reporter: {author} ({author.id})_\n"
    )

    try:
        issue_num = github_ops.create_issue(
            title=title,
            body=issue_body + footer,
            labels=labels,
        )
        db.set_issue(str(thread.id), github_ops.REPO, issue_num)
        log.info(f"Created issue #{issue_num} for thread {thread.id}")
    except Exception:
        log.exception("GitHub issue creation failed")
        # Still reply to user, log to mod channel
        mod = bot.get_channel(MOD_CHANNEL_ID)
        if mod:
            await mod.send(
                f"⚠️ Failed to create issue for thread {thread.jump_url}. Manual triage needed."
            )

    try:
        await thread.send(ack)
        db.set_replied(str(thread.id))
    except Exception:
        log.exception("Failed to reply in thread")


def main():
    token = os.environ["DISCORD_BOT_TOKEN"]
    bot.run(token, log_handler=None)  # we already configured logging


if __name__ == "__main__":
    main()
