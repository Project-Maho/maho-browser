"""Buffer publishing adapter for the maho GTM loop.

Uses Buffer's public GraphQL API (https://developers.buffer.com).
Auth: Bearer token from BUFFER_TOKEN env (get one at
https://publish.buffer.com/settings/api).

This module is intentionally strict: if BUFFER_TOKEN is missing it raises
immediately rather than pretending to publish. Nothing here fabricates a
post id / url — every returned handle comes from Buffer's response.
"""
from __future__ import annotations

import os
import json
import urllib.request
import urllib.error

API = "https://graphql.buffer.com"


class BufferError(RuntimeError):
    pass


def _token() -> str:
    tok = os.environ.get("BUFFER_TOKEN", "").strip()
    if not tok:
        raise BufferError(
            "BUFFER_TOKEN not set. Create an API key at "
            "https://publish.buffer.com/settings/api and add it to .env"
        )
    return tok


def _gql(query: str, variables: dict | None = None) -> dict:
    payload = json.dumps({"query": query, "variables": variables or {}}).encode()
    req = urllib.request.Request(
        API,
        data=payload,
        headers={
            "Authorization": f"Bearer {_token()}",
            "Content-Type": "application/json",
            "User-Agent": "maho-ops/1.0",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            body = json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        raise BufferError(f"Buffer HTTP {e.code}: {e.read().decode()[:500]}") from e
    if body.get("errors"):
        raise BufferError(f"Buffer GraphQL errors: {body['errors']}")
    return body["data"]


def list_channels() -> list[dict]:
    """All connected channels for the account's first organization."""
    data = _gql("query { account { organizations { id name } } }")
    orgs = data["account"]["organizations"]
    if not orgs:
        raise BufferError("No Buffer organizations on this account")
    org_id = orgs[0]["id"]
    data = _gql(
        "query($input: ChannelsInput!) { channels(input: $input) "
        "{ id name service } }",
        {"input": {"organizationId": org_id}},
    )
    return data["channels"]


def schedule_post(channel_id: str, text: str, due_at_iso: str | None = None) -> dict:
    """Create a post. If due_at_iso is None, add to next queue slot.

    Returns {id, dueAt, text} from Buffer — never fabricated.
    """
    if due_at_iso:
        mutation = """
        mutation($input: CreatePostInput!) {
          createPost(input: $input) {
            ... on PostActionSuccess { post { id text dueAt } }
            ... on MutationError { message }
          }
        }"""
        variables = {"input": {
            "text": text, "channelId": channel_id,
            "schedulingType": "automatic", "mode": "customScheduled",
            "dueAt": due_at_iso,
        }}
    else:
        mutation = """
        mutation($input: CreatePostInput!) {
          createPost(input: $input) {
            ... on PostActionSuccess { post { id text dueAt } }
            ... on MutationError { message }
          }
        }"""
        variables = {"input": {
            "text": text, "channelId": channel_id,
            "schedulingType": "automatic", "mode": "addToQueue",
        }}
    data = _gql(mutation, variables)
    result = data["createPost"]
    if "message" in result and result.get("message"):
        raise BufferError(f"Buffer rejected post: {result['message']}")
    return result["post"]


if __name__ == "__main__":
    # Live smoke test — only runs if BUFFER_TOKEN is present.
    import sys
    try:
        chans = list_channels()
    except BufferError as e:
        print(f"SKIP (not configured): {e}", file=sys.stderr)
        sys.exit(2)
    print(json.dumps({"channels": chans}, indent=2))
