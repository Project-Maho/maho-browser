"""GTM loop orchestrator — the 'brain' step of the maho content loop.

Flow:
  signal (release / topic)
    -> Quotio drafts SEO blurb + per-channel SNS copy (Better-Browser voice)
    -> hard validation against maho's closed-source / trademark rules
    -> structured JSON that the publish legs (Buffer, Git) consume

It does NOT publish. It produces validated copy + a plan. Publishing is a
separate, explicit step (buffer_ops.schedule_post) so nothing goes live by
accident and no post id is ever fabricated.

Positioning rules enforced (from workspace AGENTS.md + USER positioning):
  - maho is a "better everyday browser"; AI is an optional capability,
    never the lead message. Reject AI-first framing.
  - Never claim open source / MIT / "view source". maho is closed-source.
  - Never hard-link a public maho source repo.
  - No competitor trademark as the headline / no attack-style comparison.
"""
from __future__ import annotations

import os
import re
import json
import sys
from openai import OpenAI

BASE_URL = os.environ.get("QUOTIO_BASE_URL", "http://127.0.0.1:8317/v1")
API_KEY = os.environ.get("QUOTIO_API_KEY", "")
MODEL = os.environ.get("MAHO_MODEL", "gpt-5.5")

CHANNELS = ["x", "tiktok", "instagram", "youtube_shorts"]

SYSTEM = """You are the maho-browser GTM copywriter.

maho is a CLOSED-SOURCE Chromium-based desktop browser. Positioning:
- It is a "better everyday browser" first. Clean, calm, fast, organized.
- AI is an OPTIONAL capability, used when you want it — NOT the lead pitch.
- Do NOT position maho as "AI-first" or "agent-native".
- Never claim open source, MIT, auditable source, or link a public source repo.
- Do not put a competitor's trademark in the headline or attack a competitor.

Given a signal, output STRICT JSON:
{
  "seo": {
    "slug": "kebab-case-url-slug",
    "title": "<=60 char SEO/GEO title",
    "meta_description": "<=155 char description",
    "body_md": "250-400 word markdown article body"
  },
  "sns": {
    "x": "<=270 char post, 1-2 hashtags max",
    "tiktok": "caption <=150 char + up to 4 hashtags",
    "instagram": "caption <=200 char + up to 5 hashtags",
    "youtube_shorts": "title <=90 char + 1-line description"
  },
  "primary_message": "one sentence — the everyday-browser benefit being sold"
}
Write in English. Keep it human, specific, no AI-slop filler."""

# Phrases that must never appear in outbound copy.
BANNED = [
    r"\bopen[\s-]?source\b",
    r"\bMIT lic,?ense\b",
    r"\bMIT licensed\b",
    r"\bauditable source\b",
    r"\bview (the )?source\b",
    r"\bagent[\s-]?native\b",
    r"\bAI[\s-]?first\b",
    r"github\.com/[^\s]*maho",
]


def _client() -> OpenAI:
    if not API_KEY:
        raise RuntimeError("QUOTIO_API_KEY not set — cannot reach Quotio gateway")
    return OpenAI(base_url=BASE_URL, api_key=API_KEY)


def _parse_json(text: str | None) -> dict:
    """Tolerant JSON parse: strip markdown fences / prose the gateway may add."""
    t = (text or "{}").strip()
    if t.startswith("```"):
        t = t.split("```", 2)[1] if t.count("```") >= 2 else t.strip("`")
        t = t[4:] if t.lstrip().lower().startswith("json") else t
    # fall back to the outermost {...} span if there's leading/trailing prose
    try:
        return json.loads(t)
    except json.JSONDecodeError:
        i, j = t.find("{"), t.rfind("}")
        if i >= 0 and j > i:
            return json.loads(t[i:j + 1])
        raise


def draft(signal: str) -> dict:
    resp = _client().chat.completions.create(
        model=MODEL,
        messages=[
            {"role": "system", "content": SYSTEM},
            {"role": "user", "content": f"Signal:\n{signal}"},
        ],
        response_format={"type": "json_object"},
        temperature=0.4,
    )
    return _parse_json(resp.choices[0].message.content)


def validate(draft_obj: dict) -> list[str]:
    """Return a list of violations. Empty list == safe to publish."""
    problems: list[str] = []
    # Flatten all copy strings.
    blobs: list[tuple[str, str]] = []
    seo = draft_obj.get("seo", {})
    for k in ("title", "meta_description", "body_md", "slug"):
        if seo.get(k):
            blobs.append((f"seo.{k}", str(seo[k])))
    sns = draft_obj.get("sns", {})
    for ch in CHANNELS:
        if sns.get(ch):
            blobs.append((f"sns.{ch}", str(sns[ch])))

    if not blobs:
        return ["empty draft — model returned no usable copy"]

    for loc, text in blobs:
        for pat in BANNED:
            if re.search(pat, text, re.IGNORECASE):
                problems.append(f"{loc}: banned phrase /{pat}/ -> {text[:80]!r}")

    # length guards (soft platform limits)
    limits = {"x": 280, "tiktok": 170, "instagram": 220, "youtube_shorts": 100}
    for ch, lim in limits.items():
        v = sns.get(ch, "")
        if v and len(v) > lim:
            problems.append(f"sns.{ch}: {len(v)} chars > {lim} limit")
    return problems


def run(signal: str) -> dict:
    d = draft(signal)
    problems = validate(d)
    return {
        "signal": signal,
        "draft": d,
        "violations": problems,
        "safe_to_publish": len(problems) == 0,
    }


if __name__ == "__main__":
    sig = sys.argv[1] if len(sys.argv) > 1 else (
        "maho 1.2 released: faster tab switching, redesigned sidebar spaces, "
        "and a calmer new-tab page."
    )
    out = run(sig)
    print(json.dumps(out, indent=2, ensure_ascii=False))
    # Always exit 0: publish/skip is decided by the safe_to_publish JSON field
    # (the n8n IF gate reads it), not by exit code. A non-zero exit would make
    # n8n's executeCommand node fail the whole run instead of routing to the
    # "false" branch. The CLI caller can still inspect out["safe_to_publish"].
    sys.exit(0)
