"""Hermetic unit tests for release summarization and release watcher.

All OpenAI, GitHub, Discord, and database operations are hermetically mocked.
"""
import os
import unittest
from unittest.mock import MagicMock, AsyncMock, patch

import llm
import release_watcher


class TestLLMReleaseSummary(unittest.TestCase):
    """Tests for llm.py release summarization and system prompt constraints."""

    def test_system_prompt_constraints(self):
        prompt = llm.RELEASE_SYSTEM
        # Korean friendly tone requirement
        self.assertTrue("Korean" in prompt or "한국어" in prompt)
        # Length cap instruction (at most 1600 chars)
        self.assertIn("1600", prompt)
        # Prohibitions: issue numbers, SHAs, competitor names, roadmap promises
        self.assertIn("#123", prompt)
        self.assertTrue("commit" in prompt.lower() or "sha" in prompt.lower())
        self.assertTrue("competitor" in prompt.lower() or "Arc" in prompt)
        self.assertTrue("roadmap" in prompt.lower() or "promises" in prompt.lower())
        # Discord markdown requirement
        self.assertIn("Discord markdown", prompt)

    @patch.object(llm, "_client")
    def test_summarize_release_happy_path(self, mock_client):
        mock_response = MagicMock()
        mock_response.choices = [
            MagicMock(message=MagicMock(content="🎉 **새로운 마호 릴리즈**\n- 기능 개선 및 버그 수정이 포함되었습니다."))
        ]
        mock_client.chat.completions.create.return_value = mock_response

        tag = "v1.2.3"
        name = "Spring Refresh"
        body = "Fixed memory leak in tabs. Added dark mode toggle."

        summary = llm.summarize_release(tag, name, body)

        mock_client.chat.completions.create.assert_called_once()
        _, kwargs = mock_client.chat.completions.create.call_args
        self.assertEqual(kwargs["model"], llm.MODEL)
        self.assertEqual(kwargs["messages"][0]["role"], "system")
        self.assertEqual(kwargs["messages"][0]["content"], llm.RELEASE_SYSTEM)
        self.assertIn(tag, kwargs["messages"][1]["content"])
        self.assertIn(name, kwargs["messages"][1]["content"])
        self.assertIn(body, kwargs["messages"][1]["content"])
        self.assertEqual(summary, "🎉 **새로운 마호 릴리즈**\n- 기능 개선 및 버그 수정이 포함되었습니다.")


class TestReleaseWatcher(unittest.IsolatedAsyncioTestCase):
    """Tests for release_watcher.py announcement generation, dry-run, and fallbacks."""

    def test_build_announcement(self):
        payload = release_watcher.build_announcement(
            tag="v1.0.0",
            name="Initial Launch",
            summary="주요 변경 사항 요약입니다.",
        )
        self.assertIn("🚀 **maho v1.0.0 released**", payload)
        self.assertIn("_Initial Launch_", payload)
        self.assertIn("주요 변경 사항 요약입니다.", payload)
        self.assertIn("Download: https://mahobrowser.com/download", payload)

    @patch("release_watcher.github_ops.latest_release")
    @patch("release_watcher.db.release_seen", return_value=False)
    @patch("release_watcher.db.threads_for_issues", return_value=[])
    @patch("release_watcher.llm.summarize_release")
    async def test_happy_summary_dry_run(self, mock_summarize, mock_threads, mock_seen, mock_release):
        mock_release.return_value = {
            "tagName": "v2.0.0",
            "name": "Major Update",
            "body": "Added AI assistant. Fixes #101.",
            "publishedAt": "2026-08-31T00:00:00Z",
        }
        mock_summarize.return_value = "✨ **주요 업데이트 안내**\n- AI 어시스턴트 기능이 추가되었습니다."

        with patch.dict(os.environ, {"MAHO_DRY_RUN": "1", "MAHO_ANNOUNCEMENTS_CHANNEL_ID": "12345"}):
            with self.assertLogs("release-watcher", level="INFO") as cm:
                await release_watcher.run()

        mock_summarize.assert_called_once_with("v2.0.0", "Major Update", "Added AI assistant. Fixes #101.")
        logs = "\n".join(cm.output)
        self.assertIn("[DRY RUN] Final payload for #12345:", logs)
        self.assertIn("🚀 **maho v2.0.0 released**", logs)
        self.assertIn("_Major Update_", logs)
        self.assertIn("✨ **주요 업데이트 안내**", logs)
        self.assertIn("Download: https://mahobrowser.com/download", logs)

    @patch("release_watcher.github_ops.latest_release")
    @patch("release_watcher.db.release_seen", return_value=False)
    @patch("release_watcher.db.threads_for_issues", return_value=[])
    @patch("release_watcher.llm.summarize_release", side_effect=RuntimeError("Quotio gateway timeout"))
    async def test_fallback_on_exception(self, mock_summarize, mock_threads, mock_seen, mock_release):
        raw_body = "Performance patch. Fixes #42 and Closes #999 for good."
        mock_release.return_value = {
            "tagName": "v2.0.1",
            "name": "Bugfix 2.0.1",
            "body": raw_body,
            "publishedAt": "2026-08-31T01:00:00Z",
        }

        with patch.dict(os.environ, {"MAHO_DRY_RUN": "1", "MAHO_ANNOUNCEMENTS_CHANNEL_ID": "12345"}):
            with self.assertLogs("release-watcher", level="INFO") as cm:
                await release_watcher.run()

        logs = "\n".join(cm.output)
        # Should log fallback / error
        self.assertIn("[DRY RUN] Final payload for #12345:", logs)
        # Issue numbers #42 and #999 must be stripped from raw body fallback
        self.assertNotIn("#42", logs)
        self.assertNotIn("#999", logs)
        self.assertIn("Performance patch. Fixes  and Closes  for good.", logs)

    @patch("release_watcher.github_ops.latest_release")
    @patch("release_watcher.db.release_seen", return_value=False)
    @patch("release_watcher.db.threads_for_issues", return_value=[])
    @patch("release_watcher.llm.summarize_release", side_effect=Exception("LLM Down"))
    async def test_length_cap_and_issue_stripping_on_fallback(
        self, mock_summarize, mock_threads, mock_seen, mock_release
    ):
        long_body = ("Feature A #100 details. " * 100)  # ~2400 characters with repeated #100
        mock_release.return_value = {
            "tagName": "v2.1.0",
            "name": "Long Changelog",
            "body": long_body,
            "publishedAt": "2026-08-31T02:00:00Z",
        }

        with patch.dict(os.environ, {"MAHO_DRY_RUN": "1", "MAHO_ANNOUNCEMENTS_CHANNEL_ID": "12345"}):
            with self.assertLogs("release-watcher", level="INFO") as cm:
                await release_watcher.run()

        logs = "\n".join(cm.output)
        # Strip issue number check
        self.assertNotIn("#100", logs)
        # Sanitize length check: the raw body fallback part must be <= 1500 chars
        clean_fallback = release_watcher.ISSUE_RE.sub("", long_body)[:1500]
        self.assertEqual(len(clean_fallback), 1500)
        self.assertIn(clean_fallback, logs)

    @patch("release_watcher.github_ops.latest_release")
    @patch("release_watcher.db.release_seen", return_value=True)
    async def test_release_already_processed(self, mock_seen, mock_release):
        mock_release.return_value = {
            "tagName": "v1.0.0",
            "name": "Already Seen",
            "body": "Notes",
        }
        with patch.dict(os.environ, {"MAHO_DRY_RUN": "1"}):
            with self.assertLogs("release-watcher", level="INFO") as cm:
                await release_watcher.run()

        logs = "\n".join(cm.output)
        self.assertIn("Release v1.0.0 already processed.", logs)

    @patch("release_watcher.github_ops.latest_release")
    @patch("release_watcher.db.release_seen", return_value=False)
    @patch("release_watcher.db.threads_for_issues", return_value=[])
    @patch("release_watcher.llm.summarize_release", return_value="")
    async def test_empty_summary_falls_back_to_raw_body(
        self, mock_summarize, mock_threads, mock_seen, mock_release
    ):
        mock_release.return_value = {
            "tagName": "v2.0.2",
            "name": "Empty Summary Case",
            "body": "Fixes #555 - important security fix.",
            "publishedAt": "2026-08-31T03:00:00Z",
        }
        with patch.dict(os.environ, {"MAHO_DRY_RUN": "1", "MAHO_ANNOUNCEMENTS_CHANNEL_ID": "12345"}):
            with self.assertLogs("release-watcher", level="INFO") as cm:
                await release_watcher.run()

        logs = "\n".join(cm.output)
        self.assertNotIn("#555", logs)
        self.assertIn("Fixes  - important security fix.", logs)

    @patch("release_watcher.discord.Client")
    @patch("release_watcher.db.mark_release")
    @patch("release_watcher.db.mark_closed_notified")
    @patch("release_watcher.db.threads_for_issues")
    @patch("release_watcher.db.release_seen", return_value=False)
    @patch("release_watcher.github_ops.latest_release")
    @patch("release_watcher.llm.summarize_release")
    async def test_live_discord_send_flow(
        self,
        mock_summarize,
        mock_release,
        mock_seen,
        mock_threads,
        mock_mark_notified,
        mock_mark_release,
        mock_discord_client_cls,
    ):
        mock_release.return_value = {
            "tagName": "v3.0.0",
            "name": "Major v3",
            "body": "Great features! Fixes #777",
            "publishedAt": "2026-08-31T04:00:00Z",
        }
        mock_summarize.return_value = "✨ 새로운 기능들이 추가되었습니다!"
        mock_threads.return_value = [{"thread_id": "9999", "issue_number": 777, "closed_notified_at": None}]

        mock_client = MagicMock()
        mock_discord_client_cls.return_value = mock_client

        mock_thread_ch = AsyncMock()
        mock_ann_ch = AsyncMock()

        def get_channel_side_effect(ch_id):
            if ch_id == 9999:
                return mock_thread_ch
            elif ch_id == 12345:
                return mock_ann_ch
            return None

        mock_client.get_channel.side_effect = get_channel_side_effect
        mock_client.fetch_channel = AsyncMock(return_value=None)

        event_handlers = {}

        def fake_event(coro):
            event_handlers[coro.__name__] = coro
            return coro

        mock_client.event = fake_event

        async def fake_start(token):
            self.assertEqual(token, "fake-bot-token")
            # Invoke on_ready
            await event_handlers["on_ready"]()

        mock_client.start = AsyncMock(side_effect=fake_start)
        mock_client.close = AsyncMock()

        with patch.dict(os.environ, {
            "MAHO_DRY_RUN": "0",
            "DISCORD_BOT_TOKEN": "fake-bot-token",
            "MAHO_ANNOUNCEMENTS_CHANNEL_ID": "12345",
        }):
            await release_watcher.run()

        mock_thread_ch.send.assert_awaited_once()
        mock_mark_notified.assert_called_once_with("9999")
        mock_ann_ch.send.assert_awaited_once()
        sent_payload = mock_ann_ch.send.call_args[0][0]
        self.assertIn("🚀 **maho v3.0.0 released**", sent_payload)
        self.assertIn("✨ 새로운 기능들이 추가되었습니다!", sent_payload)
        mock_mark_release.assert_called_once_with("Project-Maho/release", "v3.0.0", "2026-08-31T04:00:00Z")
        mock_client.close.assert_awaited_once()

    @patch("release_watcher.discord.Client")
    @patch("release_watcher.db.mark_release")
    @patch("release_watcher.db.threads_for_issues", return_value=[])
    @patch("release_watcher.db.release_seen", return_value=False)
    @patch("release_watcher.github_ops.latest_release")
    @patch("release_watcher.llm.summarize_release")
    async def test_llm_success_output_is_sanitized_and_capped(
        self,
        mock_summarize,
        mock_release,
        mock_seen,
        mock_threads,
        mock_mark_release,
        mock_discord_client_cls,
    ):
        """Successful LLM output must still pass sanitization + length cap."""
        hostile_summary = "Fixes #123 and #456! " + "x" * 2000
        mock_release.return_value = {
            "tagName": "v3.1.0",
            "name": "v3.1",
            "body": "body text",
            "publishedAt": "2026-08-31T05:00:00Z",
        }
        mock_summarize.return_value = hostile_summary
        mock_threads.return_value = []

        mock_client = MagicMock()
        mock_discord_client_cls.return_value = mock_client
        mock_ann_ch = AsyncMock()

        event_handlers = {}

        def fake_event(func):
            event_handlers[func.__name__] = func
            return func

        mock_client.event = fake_event
        mock_client.start = AsyncMock()
        mock_client.close = AsyncMock()

        with patch.dict(os.environ, {
            "MAHO_DRY_RUN": "1",
            "MAHO_ANNOUNCEMENTS_CHANNEL_ID": "12345",
        }):
            with self.assertLogs("release-watcher", level="INFO") as cm:
                await release_watcher.run()

        dry_logs = "\n".join(cm.output)
        self.assertIn("Fixes  and !", dry_logs)
        self.assertNotIn("Fixes #123", dry_logs)
        self.assertNotIn("and #456", dry_logs)
        # Summary portion is capped at 1500 chars; whole payload = header + summary + download line
        self.assertLessEqual(len(dry_logs), len(hostile_summary))

    @patch("release_watcher.github_ops.latest_release", return_value=None)
    async def test_no_release_found(self, mock_release):
        with self.assertLogs("release-watcher", level="INFO") as cm:
            await release_watcher.run()

        logs = "\n".join(cm.output)
        self.assertIn("No release found.", logs)


if __name__ == "__main__":
    unittest.main()
