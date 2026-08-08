"""SNS publish leg — consumes gtm_loop JSON on stdin, schedules to Buffer.

Reads the {stdout: "...gtm json..."} or raw gtm json from stdin, re-checks
safe_to_publish, then schedules each channel's copy to its Buffer channel.

Channel mapping comes from .env:
  BUFFER_CHANNEL_X, BUFFER_CHANNEL_TIKTOK,
  BUFFER_CHANNEL_INSTAGRAM, BUFFER_CHANNEL_YOUTUBE_SHORTS

Honest failure modes (no fabrication):
  - no BUFFER_TOKEN            -> exit 2, prints SKIP
  - copy not safe_to_publish  -> exit 3, publishes nothing
  - a channel id missing       -> that channel skipped, logged, others continue
Every scheduled post's id/url comes straight from Buffer's response.
"""
from __future__ import annotations

import os
import sys
import json

import buffer_ops

ENV_MAP = {
    "x": "BUFFER_CHANNEL_X",
    "tiktok": "BUFFER_CHANNEL_TIKTOK",
    "instagram": "BUFFER_CHANNEL_INSTAGRAM",
    "youtube_shorts": "BUFFER_CHANNEL_YOUTUBE_SHORTS",
}


def _load_stdin() -> dict:
    raw = sys.stdin.read().strip()
    if not raw:
        raise SystemExit("no input on stdin")
    obj = json.loads(raw)
    # gtm_loop.py output may be wrapped by n8n's executeCommand as {stdout: "..."}
    if "stdout" in obj and isinstance(obj["stdout"], str):
        obj = json.loads(obj["stdout"])
    return obj


def main() -> int:
    data = _load_stdin()
    if not data.get("safe_to_publish"):
        print(json.dumps({
            "published": [], "skipped_all": True,
            "reason": "safe_to_publish is false",
            "violations": data.get("violations", []),
        }))
        return 3

    # Honest early skip: no token means nothing can publish, regardless of
    # which channel ids are configured. Fail before touching the network.
    if not os.environ.get("BUFFER_TOKEN", "").strip():
        print(json.dumps({
            "published": [], "skipped_all": True,
            "reason": "BUFFER_TOKEN not set (see publish.buffer.com/settings/api)",
        }))
        return 2

    sns = data.get("draft", {}).get("sns", {})
    results, errors = [], []

    for channel, env_key in ENV_MAP.items():
        text = sns.get(channel, "").strip()
        if not text:
            continue
        chan_id = os.environ.get(env_key, "").strip()
        if not chan_id:
            errors.append(f"{channel}: {env_key} not set in .env")
            continue
        try:
            post = buffer_ops.schedule_post(chan_id, text)
            results.append({"channel": channel, "post": post})
        except buffer_ops.BufferError as e:
            errors.append(f"{channel}: {e}")

    print(json.dumps({"published": results, "errors": errors}, indent=2))
    return 0 if results and not errors else (0 if results else 1)


if __name__ == "__main__":
    raise SystemExit(main())
