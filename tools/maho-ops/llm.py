"""LLM analysis for Discord report → structured issue body and release summarization.

Uses Quotio local gateway (OpenAI-compatible endpoint).
"""
import os
import json
from openai import OpenAI

BASE_URL = os.environ.get("QUOTIO_BASE_URL", "http://127.0.0.1:8317/v1")
API_KEY = os.environ.get("QUOTIO_API_KEY", "quotio-local-48ADAAA1")
MODEL = os.environ.get("MAHO_MODEL", "gpt-5.5")

_client = OpenAI(base_url=BASE_URL, api_key=API_KEY)

SYSTEM = """You are the maho-browser triage assistant.

maho is a closed-source Chromium fork positioned as "Better Arc, maintained".
Input: a raw bug report or feature request posted to the Maho Discord community.
Output: a strict JSON object with these keys:
{
  "kind": "bug" | "feature" | "question" | "spam",
  "title": "concise <=80 char summary suitable for a GitHub issue title",
  "severity": "p0" | "p1" | "p2" | "p3",   // p0 = crash/data-loss, p1 = broken core flow, p2 = annoyance, p3 = polish
  "component": "one of: ui, browser-core, ai-features, sidebar, settings, sync, updater, installer, docs, other",
  "repro": "extracted or inferred reproduction steps as a short markdown list; empty string if not a bug",
  "expected": "expected behavior; empty string if N/A",
  "actual": "actual behavior; empty string if N/A",
  "duplicate_hint": "short phrase to grep for possible duplicates, or empty string",
  "reply_to_user": "friendly one-paragraph reply for the Discord thread — thank them, confirm we tracked it, ask for follow-up info if needed. Korean if user wrote Korean, else English."
}

If the message is spam or unusable, set kind='spam' and leave other fields empty."""

RELEASE_SYSTEM = """You are the Maho browser release note summarizer for Discord announcements.

Task:
Summarize the provided release changelog into a friendly, concise Korean Discord announcement.

Guidelines & Constraints:
1. Tone: Friendly, polite, and enthusiastic Korean tone (친근하고 정중한 한국어 톤).
2. Format: Clean Discord markdown with bullet points and bold highlights for key changes.
3. Length: Must be at most 1600 characters.
4. STRICT PROHIBITIONS:
   - NEVER include GitHub issue/PR numbers (e.g. #123, #456).
   - NEVER include git commit hashes or SHAs (e.g. 7a8b9c, a1b2c3d4).
   - NEVER mention competitor names (e.g. Arc, Chrome, Brave, Safari, Edge).
   - NEVER make unverified future roadmap promises or speculative release dates.
5. Focus purely on what is new, improved, and fixed in this specific release for Maho users."""


def analyze(user_text: str, author: str, channel_name: str) -> dict:
    prompt = f"Author: {author}\nChannel: #{channel_name}\n\n---\n\n{user_text}"
    resp = _client.chat.completions.create(
        model=MODEL,
        messages=[
            {"role": "system", "content": SYSTEM},
            {"role": "user", "content": prompt},
        ],
        response_format={"type": "json_object"},
        temperature=0.2,
    )
    content = resp.choices[0].message.content or "{}"
    return json.loads(content)


def summarize_release(tag: str, name: str, body: str) -> str:
    prompt = f"Release Tag: {tag}\nRelease Name: {name}\n\nChangelog:\n{body}"
    resp = _client.chat.completions.create(
        model=MODEL,
        messages=[
            {"role": "system", "content": RELEASE_SYSTEM},
            {"role": "user", "content": prompt},
        ],
        temperature=0.3,
    )
    content = resp.choices[0].message.content or ""
    return content.strip()

