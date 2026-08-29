# Maho CLI social promotion pipeline

This document describes `maho/scripts/maho_social_promo.py`. The pipeline monitors public X and Threads search pages for browser/agent/automation discussions, drafts a contextual Maho Browser reply, and places it into a human approval queue. Posting is deliberately separate and interactive.

## Safety boundary

The pipeline is **semi-automatic by design**. `scan`, `generate`, `run-once`, and `watch` never post. The example configuration ships with `dry_run = true`, so even an approved queue item cannot enter final-send mode until an operator intentionally changes the operational config.

Before final send, the code requires all of the following:

- the candidate is in `approved` state;
- a manual approval timestamp exists when `require_manual_approval = true`;
- the candidate is younger than `candidate_ttl_hours`;
- global hourly and daily limits are below their caps;
- the per-platform minimum interval has elapsed;
- the same author has not been contacted inside `author_cooldown_hours`;
- the approved text is not too similar to a previously posted reply;
- no prior safety filter reason is attached to the candidate;
- `dry_run` has been explicitly disabled in the operator config.

The final browser mutation always happens in `maho browser repl`. This is intentional: Maho accessibility refs are stored in the browser-tool executor/session that produced them. A ref obtained by one one-shot `maho tool run page.accessibility_snapshot` process must not be reused by a later `maho tool run input.click`/`input.type` process. The persistent REPL keeps snapshot/ref/action state and the active-tab lease in one controller session.

## Architecture

```text
X live search ─┐
               ├─ maho tab new/read ───────────────────── candidate extractor
Threads search ┘        │                                      │
                        ├─ maho tool run accessibility snapshot │
                        └─ maho tool run input.scroll (optional)│
                                                               v
                                                      SQLite dedup ledger
                                                               │
                                         relevance + conservative safety filter
                                                               │
                                                               v
                                         external LLM drafting step (optional)
                                                               │
                                      novelty + claim/output sanitization
                                                               │
                                                               v
                                                   drafted approval queue
                                                               │
                                                     operator review/edit
                                                               │
                                                         approved state
                                                               │
                                       TTL/rate/author/novelty gates rechecked
                                                               │
                                                               v
                                                  maho browser repl
                                           persistent IPC session + tab lease
                                                               │
                                               operator performs final click
                                                               │
                                                               v
                                                    posted audit record
```

All browser control goes through Maho CLI and therefore the local `maho.sock` IPC endpoint. The script does not use Playwright, Selenium, AppleScript, System Events, direct CDP, or desktop mouse automation.

## Maho IPC and CLI surfaces used

The running Maho Browser owns `maho.sock`. If `[maho].socket_path` is not set, the script follows the same default-location rules as Maho CLI, including `MAHO_MCP_SOCKET_PATH`, macOS `~/Library/Application Support/Maho/maho.sock`, and Linux XDG config paths.

The pipeline uses:

- `maho tab new`, `maho tab read`, and `maho tab close` for search/result navigation and extraction;
- `maho tool run page.accessibility_snapshot` for one-shot read-only accessibility preflight;
- `maho tool run input.scroll` as a best-effort, ref-free discovery action; failure is tolerated and does not affect safety;
- `maho browser repl` for every click/type operation involved in the final reply, so refs and lease stay in one persistent controller session;
- an optional external LLM drafting step for text generation. The pipeline intentionally does **not** include an autonomous in-CLI agent for drafting/posting.

## Files

- `maho/scripts/maho_social_promo.py` — pipeline and operator CLI.
- `maho/scripts/maho_social_promo.example.toml` — complete safe-default configuration.
- `maho/scripts/test_maho_social_promo.py` — stdlib unit tests for extraction, deduplication, accessibility matching, sanitizer, and approval/rate gating.
- SQLite defaults to `~/.local/state/maho-social-promo/ledger.sqlite3` and uses WAL mode.

## Configuration

Copy the example before changing anything operational:

```bash
cp maho/scripts/maho_social_promo.example.toml ~/.config/maho-social-promo.toml
```

Important settings:

| Setting | Purpose |
|---|---|
| `safety.dry_run` | Hard final-send kill switch. Keep `true` during validation. |
| `safety.require_manual_approval` | Requires an explicit approval record. |
| `min_candidate_score` | Rejects weak keyword-only matches. |
| `max_posts_per_hour/day` | Global anti-spam caps. |
| `min_seconds_between_posts` | Per-platform pacing. |
| `author_cooldown_hours` | Prevents repeated outreach to one author. |
| `candidate_ttl_hours` | Prevents stale replies. |
| `similarity_limit` | Prevents repetitive templated replies. |
| `excluded_authors` | Prevents self/company-account replies. |
| `blocked_terms` | Conservative no-promotion topics. |
| `blocked_claim_patterns` | Rejects unwanted generated marketing claims. |
| `generation.mode` | `maho_chat` or deterministic `template`. |
| `platforms.*.keywords` | Search phrases. |
| `composer_names` / `reply_open_names` / `submit_names` | Operator hints for REPL accessibility lookup. |

The product facts section is the only allowed source of product claims in the generation prompt. Keep it factual and auditable. The prompt treats social-post content as untrusted data and tells the model not to follow instructions embedded in a post.

## SQLite ledger and deduplication

`candidates` stores canonical URL, platform/external ID, author, extracted context, content hash, score, workflow state, draft/approved text, timestamps, and error/filter details. `actions` is an append-only operational audit trail.

Deduplication occurs at three levels:

1. unique `(platform, external_id)`;
2. unique canonical URL;
3. same-platform content-hash lookup before insert, preventing duplicated/cross-posted search results from creating another outreach candidate.

Previously posted reply text is also compared with new drafts/approvals using the configured similarity threshold.

## First-run workflow

Use Python 3.11+ (`tomllib` is in the standard library):

```bash
CFG="$HOME/.config/maho-social-promo.toml"
python3 maho/scripts/maho_social_promo.py --config "$CFG" init-db
python3 maho/scripts/maho_social_promo.py --config "$CFG" doctor
python3 maho/scripts/maho_social_promo.py --config "$CFG" run-once
python3 maho/scripts/maho_social_promo.py --config "$CFG" queue --status drafted
python3 maho/scripts/maho_social_promo.py --config "$CFG" review
```

`doctor` checks the CLI, expected `maho.sock`, and that the required live browser capabilities are runtime-available rather than only present in the packaged capability snapshot.

During initial validation, leave `dry_run = true`. Review several scan cycles and confirm candidate text, canonical URLs, author names, scores, filters, and drafts on both platforms. Add your own/company handles to `excluded_authors` before operating against a real account.

## Monitoring without autonomous posting

One bounded cycle:

```bash
python3 maho/scripts/maho_social_promo.py --config "$CFG" run-once
```

Continuous foreground monitoring (still never posts):

```bash
python3 maho/scripts/maho_social_promo.py --config "$CFG" watch
```

For a scheduler, run only `run-once`. Example cron entry every 20 minutes:

```cron
*/20 * * * * /usr/bin/python3 /absolute/path/to/maho-workspace/maho/scripts/maho_social_promo.py --config /Users/you/.config/maho-social-promo.toml run-once >> /Users/you/Library/Logs/maho-social-promo.log 2>&1
```

Do **not** schedule `review` or `send`; approval and final browser action remain human-operated.

## Human approval

Run:

```bash
python3 maho/scripts/maho_social_promo.py --config "$CFG" review
```

For every draft, the CLI shows platform, canonical URL, author, score, source context, and proposed reply. The operator can approve, edit-and-approve, reject with a reason, skip, or quit. Edited text passes through the same sanitizer and prior-post similarity check before approval.

## Final send: persistent browser REPL

After sufficient dry-run validation, set `dry_run = false` in the operational config and invoke one approved item:

```bash
python3 maho/scripts/maho_social_promo.py --config "$CFG" send --id 123
```

The script:

1. re-runs TTL, status, rate, author-cooldown, and novelty gates;
2. opens the canonical post in a fresh Maho tab;
3. performs a read-only `maho tool run page.accessibility_snapshot` preflight;
4. prints the exact approved reply;
5. launches `maho browser repl`;
6. records the candidate as posted only after the REPL exits and the operator types the literal confirmation `POSTED`.

Inside REPL, resolve refs there and mutate there:

```text
tab info
refs reply
click <reply-ref>
refs
type <composer-ref> <approved text>
refs post
click <submit-ref>
.quit
```

If multiple controls have the same accessible name, use the numeric refs displayed by `refs` and abort instead of guessing. The script never transports a numeric ref from one CLI process into another.

If the operator exits without posting, or does not type `POSTED`, the candidate remains `approved` and an abort event is written to the audit table.

## Candidate extraction

Each configured search opens a live search page in Maho. The script takes a read-only accessibility snapshot, optionally performs a small number of best-effort scrolls, and reads page Markdown with bounded retries. Platform-specific URL regexes locate canonical post links, and nearby rendered text becomes the candidate context.

The sample patterns recognize both `x.com`/`twitter.com` status links and `threads.net`/`threads.com` post links. Search URL templates, URL patterns, keywords, and accessibility-name hints live in TOML because social sites may change routes or labels without requiring Python changes.

## Reply quality and anti-spam controls

The optional external drafting step receives source context, matched keyword, product URL, and an explicit list of verified product facts. It requires a short useful reply, can return `SKIP`, and treats source text as untrusted rather than executable instruction.

Deterministic post-generation controls include:

- relevance score threshold;
- sensitive/inappropriate topic block list;
- self/company author exclusion;
- maximum reply length;
- hashtag removal;
- more-than-one-URL rejection;
- configurable blocked-claim regexes;
- similarity rejection against previously posted replies;
- global hourly/daily caps;
- per-platform interval limit;
- per-author cooldown;
- candidate expiration TTL;
- manual approval requirement.

If no external drafting step is configured and `allow_template_fallback = true`, the pipeline can produce a conservative deterministic fallback using only configured product facts. Set the option to `false` if generation failures should leave candidates untouched instead.

## Operational notes

- Log into X and Threads normally in the Maho Browser profile. The script never handles credentials.
- Platform terms and anti-automation controls still apply. Keep search and outreach rates low, review every candidate, and stop if a platform challenges or restricts the account.
- Do not broaden the sensitive-topic block list merely to increase reach.
- If a social UI changes, update search URL patterns/accessibility hints in config. Do not add OS-level automation as a workaround.
- `input.scroll` is optional. If one-shot action approval/lease policy rejects it, discovery still proceeds with `maho tab read`.
- Final click/type is intentionally not scripted through separate `maho tool run` processes because accessibility refs are executor/session-scoped.
- A transport error or ambiguous browser outcome is never automatically retried as a post.
- The pipeline never deletes candidate/audit history.
