# Maho Blog SEO Audit + Enhancement Plan

Generated: 2026-07-10
Auditor: Sisyphus-Junior
Scope: `website/src/content/docs/blog/*.mdx` (51 posts) + supporting infra (`astro.config.mjs`, `src/components/StarlightHead.astro`, `src/pages/robots.txt.ts`, `src/content.config.ts`).

## Baseline

- **Total blog posts:** 51 (all with frontmatter parsed successfully)
- **Posts missing hero images:** 10 (all in the "priority queue" targeted by deliverable 2)
- **Site:** `mahobrowser.com` — Astro 6 + Starlight + `starlight-blog` plugin
- **Sitemap:** `@astrojs/sitemap` with per-route priority tuning (blog index 0.9, pillar/glossary 0.9, comparison 0.8, other posts 0.7). Tag archives 0.5, pagination/author 0.4.
- **Robots:** dynamic `robots.txt` route explicitly allows `GPTBot`, `ClaudeBot`, `PerplexityBot`, `Google-Extended`, `CCBot`. Sitemap reference included.
- **Structured data:** BlogPosting JSON-LD emitted per blog post via `StarlightHead.astro` (headline, description, datePublished, author, keywords, publisher). RSS is enabled via `starlight-blog({ rss: true })`.
- **All 51 posts have:** title, description, tags (>=1), excerpt, date, and at least one author.
- **Templates used:** 1 post uses `template: splash` (`agentic-browser-comparison-2026`); the other 50 use the default post template.

## Section 1: Site-wide SEO Issues (with fix instructions)

### 1.1 JSON-LD `image` is a global placeholder, not per-post

**Where:** `src/components/StarlightHead.astro:25-26,67`
**What:** Every `BlogPosting` JSON-LD block emits the same `/og-image.png` for the `image` field, regardless of the post. This weakens rich-result eligibility and means every blog post shares one identical thumbnail in structured data.
**Fix:**
1. Derive a per-post OG image from frontmatter. Recommended convention: if `/assets/blog/<slug>/hero.png` exists, use that; else fall back to `/og-image.png`.
2. Because Astro cannot do FS existence checks at request time in Starlight overrides without a helper, add an optional `heroImage` frontmatter field (or read the slug and infer the URL). Cheapest path: infer URL from `entry.id`, and if a post ships without a hero, keep the fallback.
3. Recommendation: bump the JSON-LD image to a fully-qualified URL that includes the `hero.png` per slug — Google requires >=1200px on the long edge for article rich results; hero PNGs at 1376×768 clear that bar.
**Suggested implementation sketch** (do NOT apply now, planning only):
```ts
const slug = id.replace(/^blog\//, '').replace(/\.mdx?$/, '');
const heroPath = `/assets/blog/${slug}/hero.png`;
const image = data.heroImage
  ? new URL(data.heroImage, Astro.site).href
  : new URL(heroPath, Astro.site).href; // fallback to og-image.png only if the file check fails at build time
```

### 1.2 Blog posts never expose `dateModified` (lastUpdated)

**Where:** `StarlightHead.astro:56-62` — the field is conditional on `data.lastUpdated`, which no post sets.
**What:** Google surfaces "Updated" dates in the SERP when `dateModified` is present. Old-looking posts get demoted; freshness signals are lost.
**Fix:** For any post edited materially after publication, add `lastUpdated: YYYY-MM-DD` to frontmatter. Docs also support `lastUpdated: true` which reads from Git commit time — cheap to enable globally in the Starlight docs collection, but blog collection (`starlight-blog` schema) has to be validated for support.

### 1.3 No canonical override for pagination / tag archives

**Where:** `astro.config.mjs:84-96` — tag pages and pagination pages are indexed with lower priority but no canonical hint back to the source post.
**What:** Tag archive pages (`/blog/tags/<tag>/`) can compete with pillar posts for topical relevance. `starlight-blog` uses its own auto-generated tag pages with no per-post canonical.
**Fix:**
- Confirm `starlight-blog` emits canonical `<link rel="canonical">` on tag archives that references the archive URL itself, not any single post. Then decide whether to `noindex` low-value tag pages (`cli`, `terminal`, `glossary` which each have <=4 posts) via `robots: { noindex: true }` in a custom head hook, or leave them indexed with the lower priority already assigned.
- Prefer noindex + follow for archive pages with fewer than 5 posts (thin listing).

### 1.4 Zero-external-references pattern across 48/51 posts

**Where:** Content bodies.
**What:** Only 3 posts (`ollama-with-maho-step-by-step`, `lm-studio-with-maho`, `verify-maho-locally`) cite an external source. Outbound links to authoritative sources (Anthropic docs, Google docs, Chromium bug tracker, MCP spec, W3C, Mozilla) strengthen topical authority and E-E-A-T signals. Zero-outbound content reads to Google as "unlinked island."
**Fix:** For pillar/glossary posts (`what-is-*`, `byok-architecture-explained`, `agentic-browser-*`, `tool-calling-*`, `mcp-*`), add at least one authoritative outbound link per H2 section (target: >=3 outbound refs per pillar post). Comparison posts should cite the competitor's own docs/pricing page (usually one link per competitor is sufficient).

### 1.5 Internal linking is uneven, mail vs posts are near-orphans

**Where:** The seven `maho-mail-vs-*` posts and `agentic-browser-comparison-2026` all have <=1 internal link. The mail-vs cluster averages 1 internal link each; the browser-vs cluster averages 6-9.
**What:** Low internal link count means the mail cluster receives no PageRank flow from the browser cluster. `agentic-browser-comparison-2026` has literally zero links despite being a `splash`-template landing page — every other post could reasonably link *into* it.
**Fix:**
- Add a "See also" block at the end of every mail-vs post that links to the other 6 mail-vs posts + `/mail/gui` + `/mail/mcp` + `/api/`.
- Add cross-links from browser-vs posts to the pillar `agentic-browser-comparison-2026`.
- Add cross-links from `agentic-browser-comparison-2026` to each of the 6 individual `maho-vs-*` posts referenced in its table (currently the post mentions Dia/Chrome/Edge/Brave/Opera/SigmaOS but does not link to their per-competitor pages).

### 1.6 Tag taxonomy has cannibalization and casing/plural inconsistencies

**Where:** frontmatter `tags:` across all posts.
**Current distribution:**
| Tag | Posts |
|-----|-------|
| browser | 44 |
| comparison | 20 |
| agentic | 14 |
| privacy | 10 |
| power-user | 8 |
| byok | 8 |
| how-to | 7 |
| mail | 7 |
| local-ai | 6 |
| mcp | 5 |
| terminal | 4 |
| glossary | 3 |
| cli | 2 |
| ai | 1 |
| agentic-browser | 1 |

**Problems:**
- `agentic` (14) vs `agentic-browser` (1) — the latter is used only on `agentic-browser-comparison-2026`. Merge into `agentic`.
- `ai` (1) is a leaf-only tag with no siblings. Either promote (retag AI-adjacent posts) or remove; a 1-post tag creates a thin archive page.
- `cli` (2) vs `terminal` (4) vs `power-user` (8) — mail-side uses `cli`, browser-side uses `terminal`. Normalize: use `cli` when the post's primary artifact is a command-line tool, `terminal` when it is a workflow that happens in a terminal. Current usage is inconsistent (`share-tab-to-cli` is tagged `terminal` not `cli`).
- No `mail` posts tagged `agentic` even though the mail MCP server is agentic — under-tagging.

**Fix:** Consolidate tags in three passes:
1. Rename `agentic-browser` -> `agentic` on `agentic-browser-comparison-2026`.
2. Decide on `cli` vs `terminal` unification. Suggested: keep both, define scope in a `docs/blog-tag-policy.md` note.
3. Delete the `ai` tag from `agentic-browser-comparison-2026` (already tagged `agentic-browser` + `comparison`).

### 1.7 Descriptions are heavily concentrated in the 145-165 char range (good), but 6 outliers exist

**Short (<120 chars, missing SERP real estate):** 6 posts, all in the mail-vs cluster (`maho-mail-vs-apple-mail` 119, `maho-mail-vs-himalaya` 110, `maho-mail-vs-shortwave` 110, `maho-mail-vs-spark` 113, `maho-mail-vs-superhuman` 117, `maho-mail-vs-thunderbird` 113).
**Long (>170 chars, will be truncated in SERP):** 2 posts (`agentic-browser-comparison-2026` 179, `verify-maho-locally` 194).

**Fix:** rewrite each flagged description to hit 150-160 chars. See Section 2 table for the exact list.

### 1.8 Title length outliers

- `maho-mail-vs-aerc-mutt-neomutt` — 73 chars, will truncate in SERP. Rewrite to <=60. Suggested: `"Maho Mail vs aerc, mutt, neomutt: TUI email in 2026"` (52).
- `maho-vs-aside-browser` — 21 chars. Missing keyword context. Suggested: `"Maho vs Aside Browser: agentic AI, two philosophies"` (52).

### 1.9 H2/H3 depth is thin on pillar posts

Six pillar posts (all `what-is-*`, `byok-architecture-explained`, several core explainers) have H2 headings but **zero H3**. Google prefers a shallow-but-hierarchical outline for featured-snippet eligibility. Add H3 subsections under 1-2 key H2s per pillar post.

### 1.10 No hreflang, no i18n (correct for now)

The site is English-only. `hreflang` is not needed until locales are added. Flag this as a future item if `mail.mahobrowser.com` or Korean translations ship.

### 1.11 Author archive pages are auto-generated but authors are all "Maho Team"

All 51 posts have `authors: [{ name: Maho Team }]`. `starlight-blog` emits `/blog/authors/maho-team/` which is essentially a duplicate of `/blog/` index. Options:
- `noindex` the author archives (they add no unique value).
- Or, introduce named contributor authors for the technical pillar posts. A named author on `byok-architecture-explained` and `agentic-browser-permission-model` improves E-E-A-T.

### 1.12 RSS feed is enabled but not linked in `<head>` for discovery

Confirm that `starlight-blog` emits `<link rel="alternate" type="application/rss+xml" ...>` in the site head. If not, add it via `StarlightHead.astro`. Feed readers, LLM training pipelines, and some Google features depend on this.

### 1.13 No `og:image` per-post either

Related to 1.1. Confirm Starlight's default `<Head>` currently outputs a per-page `og:image` — audit locally by running `curl https://mahobrowser.com/blog/maho-vs-arc/ | grep og:image`. If the OG image is also the global `og-image.png`, social sharing shows the same graphic for every post. Fix at the same time as 1.1.

### 1.14 No `article:published_time` / `article:modified_time` OG tags

Confirm and add if missing. `starlight-blog` may already handle this; if not, add via `StarlightHead.astro` inside the `isBlogPost` branch.

---

## Section 2: Per-Post Audit Table

Sorted by date (oldest first). Length rules used: title 30-65, description 120-170.

| Slug | Date | Title (len) | Desc (len) | Tags | H2/H3 | Words | Hero | Int/Ext Links | Flags |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `maho-mail-vs-aerc-mutt-neomutt` | 2026-04-12 | Maho Mail vs aerc / mutt / neomutt: Which Email Client Is Better in 2026? (73) | 127 | mail, comparison, cli | 7/9 | 998 | **NO** | 1/0 | title-long, no-hero, low-internal-links, no-external-refs |
| `maho-mail-vs-apple-mail` | 2026-04-12 | Maho Mail vs Apple Mail: Which Email Client Is Better in 2026? (62) | 119 | mail, comparison | 7/9 | 973 | **NO** | 1/0 | desc-short, no-hero, low-internal-links, no-external-refs |
| `maho-mail-vs-himalaya` | 2026-04-12 | Maho Mail vs himalaya: Which Email Client Is Better in 2026? (60) | 110 | mail, comparison, cli | 7/9 | 781 | **NO** | 1/0 | desc-short, no-hero, low-internal-links, no-external-refs, thin-content |
| `maho-mail-vs-shortwave` | 2026-04-12 | Maho Mail vs Shortwave: Which Email Client Is Better in 2026? (61) | 110 | mail, comparison | 7/10 | 1032 | **NO** | 1/0 | desc-short, no-hero, low-internal-links, no-external-refs |
| `maho-mail-vs-spark` | 2026-04-12 | Maho Mail vs Spark: Which Email Client Is Better in 2026? (57) | 113 | mail, comparison | 7/10 | 933 | **NO** | 1/0 | desc-short, no-hero, low-internal-links, no-external-refs |
| `maho-mail-vs-superhuman` | 2026-04-12 | Maho Mail vs Superhuman: Which Email Client Is Better in 2026? (62) | 117 | mail, comparison | 7/10 | 1016 | **NO** | 1/0 | desc-short, no-hero, low-internal-links, no-external-refs |
| `maho-mail-vs-thunderbird` | 2026-04-12 | Maho Mail vs Thunderbird: Which Email Client Is Better in 2026? (63) | 113 | mail, comparison | 7/10 | 983 | **NO** | 1/0 | desc-short, no-hero, low-internal-links, no-external-refs |
| `agentic-browser-comparison-2026` | 2026-04-22 | Agentic Browsers in 2026: How Maho Compares (43) | 179 | browser, comparison, ai, agentic-browser | 10/0 | 2003 | **NO** | 0/0 | desc-long, no-hero, low-internal-links, no-external-refs, tag-cannibal |
| `what-is-an-agentic-browser` | 2026-06-23 | What is an agentic browser? A working definition (48) | 124 | browser, glossary, agentic | 7/0 | 1106 | yes | 6/0 | no-external-refs, no-h3 |
| `maho-vs-arc` | 2026-06-26 | Maho vs Arc: where the agentic browser goes after Arc (53) | 160 | browser, comparison, agentic | 7/0 | 1399 | yes | 6/0 | no-external-refs, no-h3 |
| `maho-vs-aside-browser` | 2026-06-28 | Maho vs Aside Browser (21) | 159 | browser, comparison, agentic | 7/10 | 1132 | **NO** | 3/0 | title-short, no-hero, no-external-refs |
| `verify-maho-locally` | 2026-06-28 | Verify Maho privacy locally: the receipts (41) | 194 | browser, privacy, how-to | 7/0 | 1201 | **NO** | 9/2 | desc-long, no-hero, no-h3 |
| `what-is-mcp-model-context-protocol` | 2026-06-30 | What is MCP? Model Context Protocol explained for browsers (58) | 149 | browser, glossary, mcp | 6/0 | 862 | yes | 6/0 | no-external-refs, no-h3, thin-content |
| `mcp-tools-in-the-browser` | 2026-07-03 | MCP in the browser: what it actually means in 2026 (50) | 148 | browser, agentic, mcp | 8/0 | 1753 | yes | 6/0 | no-external-refs, no-h3 |
| `maho-vs-dia` | 2026-07-07 | Maho vs Dia: two takes on the agentic browser in 2026 (53) | 157 | browser, comparison, agentic | 8/0 | 1437 | yes | 6/0 | no-external-refs, no-h3 |
| `byok-architecture-explained` | 2026-07-10 | BYOK in the browser: the architecture, in detail (48) | 151 | browser, byok, local-ai | 7/0 | 1601 | yes | 6/0 | no-external-refs, no-h3 |
| `what-is-byok-bring-your-own-key` | 2026-07-14 | What is BYOK? Bring Your Own Key, explained (43) | 147 | browser, glossary, byok | 7/0 | 956 | yes | 6/0 | no-external-refs, no-h3 |
| `ollama-with-maho-step-by-step` | 2026-07-17 | BYOK with Ollama: connect a local model to Maho in minutes (61) | 147 | browser, how-to, byok, local-ai | 8/0 | 1211 | yes | 7/2 | — (clean) |
| `maho-vs-brave-leo` | 2026-07-21 | Maho vs Brave Leo: privacy plus AI, two approaches (50) | 163 | browser, comparison, privacy | 7/0 | 1482 | yes | 6/0 | no-external-refs, no-h3 |
| `agentic-browser-permission-model` | 2026-07-24 | Permissions for an agentic browser, in practice (47) | 151 | browser, agentic, privacy | 8/0 | 1667 | yes | 5/0 | no-external-refs, no-h3 |
| `spaces-when-tab-groups-arent-enough` | 2026-07-28 | Spaces in Maho: when tab groups are not enough (46) | 151 | browser, power-user | 6/0 | 1409 | yes | 6/0 | no-external-refs, no-h3 |
| `maho-vs-edge-copilot` | 2026-07-31 | Maho vs Edge with Copilot: the bundle vs the BYOK (49) | 151 | browser, comparison, byok | 6/0 | 1383 | yes | 7/0 | no-external-refs, no-h3 |
| `tool-calling-without-the-mess` | 2026-08-04 | Tool calling, without the mess: how Maho ships browser tools (60) | 150 | browser, agentic, mcp | 7/0 | 1869 | yes | 7/0 | no-external-refs, no-h3 |
| `lm-studio-with-maho` | 2026-08-07 | LM Studio plus Maho: a fully local AI panel on macOS (52) | 152 | browser, how-to, byok, local-ai | 7/0 | 1332 | yes | 6/1 | no-h3 |
| `maho-vs-chrome-gemini` | 2026-08-11 | Maho vs Chrome with Gemini: agentic-first vs AI-bolt-on (57) | 153 | browser, comparison, agentic | 7/0 | 1607 | yes | 7/0 | no-external-refs, no-h3 |
| `page-context-vs-conversation-context` | 2026-08-14 | Page context vs conversation context: two scopes, one panel (59) | 161 | browser, agentic | 6/0 | 1639 | yes | 6/0 | no-external-refs, no-h3 |
| `zero-telemetry-by-default` | 2026-08-18 | Zero telemetry by default: what that actually means in Maho (59) | 146 | browser, privacy | 7/0 | 1567 | yes | 6/0 | no-external-refs, no-h3 |
| `maho-vs-opera-aria` | 2026-08-21 | Maho vs Opera with Aria: AI sidebar, two designs (48) | 147 | browser, comparison, agentic | 7/0 | 1366 | yes | 9/0 | no-external-refs, no-h3 |
| `why-sqlite-for-prompt-history` | 2026-08-25 | Why we put browser AI history in SQLite (39) | 153 | browser, byok, local-ai | 7/0 | 1725 | yes | 7/0 | no-external-refs, no-h3, tag-mismatch (should include `mcp` or `agentic`) |
| `boosts-explained` | 2026-08-28 | Boosts in Maho: per-site customization without an extension (59) | 149 | browser, power-user | 6/0 | 1159 | yes | 6/0 | no-external-refs, no-h3 |
| `maho-vs-sigmaos` | 2026-09-01 | Maho vs SigmaOS: two macOS-native power browsers (48) | 145 | browser, comparison, power-user | 7/0 | 1341 | yes | 6/0 | no-external-refs, no-h3 |
| `agentic-browser-failure-modes` | 2026-09-04 | Agentic browser failure modes (and how Maho handles them) (57) | 148 | browser, agentic, privacy | 7/0 | 1907 | yes | 6/0 | no-external-refs, no-h3 |
| `sync-without-an-account` | 2026-09-08 | Sync without an account: the Maho relay model (45) | 145 | browser, power-user, privacy | 7/0 | 1408 | yes | 6/0 | no-external-refs, no-h3 |
| `maho-vs-safari` | 2026-09-11 | Maho vs Safari: defaults are sticky, agentic is not (60) | 146 | browser, comparison | 7/0 | 1454 | yes | 8/0 | no-external-refs, no-h3, missing-tag (`agentic`) |
| `local-models-cost-vs-cloud` | 2026-09-15 | Local models vs cloud APIs: the real cost comparison (52) | 146 | browser, byok, local-ai | 7/0 | 1583 | yes | 6/0 | no-external-refs, no-h3 |
| `browser-history-encryption-explained` | 2026-09-18 | Browser history encryption: how Maho stores yours (49) | 150 | browser, privacy | 7/0 | 1579 | yes | 7/0 | no-external-refs, no-h3 |
| `maho-vs-firefox` | 2026-09-22 | Maho vs Firefox: two takes on user agency (41) | 150 | browser, comparison, privacy | 7/0 | 1613 | yes | 6/0 | no-external-refs, no-h3 |
| `building-an-agentic-workflow` | 2026-09-25 | Building an agentic workflow: from search to ticket in Maho (51) | 147 | browser, agentic, mcp | 7/0 | 2175 | yes | 6/0 | no-external-refs, no-h3 |
| `byok-vs-managed-ai` | 2026-09-29 | BYOK vs managed AI: what you give up, what you keep (51) | 148 | browser, byok, local-ai | 6/0 | 1293 | yes | 6/0 | no-external-refs, no-h3 |
| `maho-vs-vivaldi` | 2026-10-02 | Maho vs Vivaldi: two power-user browsers, two centuries (55) | 150 | browser, comparison, power-user | 7/0 | 1362 | yes | 7/0 | no-external-refs, no-h3 |
| `side-panel-as-control-surface` | 2026-10-06 | The side panel as a control surface, not a chat box (51) | 151 | browser, agentic | 7/0 | 1504 | yes | 6/0 | no-external-refs, no-h3 |
| `mcp-server-for-your-browser` | 2026-10-09 | Run an MCP server for your browser (34) | 152 | browser, how-to, mcp, terminal | 8/0 | 1445 | yes | 6/0 | no-external-refs, no-h3 |
| `maho-vs-zen-browser` | 2026-10-13 | Maho vs Zen Browser: two ambitious 2026 macOS browsers (54) | 146 | browser, comparison, power-user | 7/0 | 1479 | yes | 7/0 | no-external-refs, no-h3 |
| `keyboard-first-browsing-in-2026` | 2026-10-16 | Keyboard-first browsing in 2026: not an afterthought (52) | 149 | browser, power-user | 6/0 | 1364 | yes | 6/0 | no-external-refs, no-h3 |
| `sync-architecture-end-to-end` | 2026-10-20 | Maho sync architecture, end to end (34) | 150 | browser, privacy | 7/0 | 1767 | yes | 6/0 | no-external-refs, no-h3 |
| `extensions-that-still-work` | 2026-10-23 | Extensions that still work: the Chromium MV3 story (58) | 154 | browser, power-user | 6/0 | 1246 | yes | 7/0 | no-external-refs, no-h3 |
| `share-tab-to-cli` | 2026-10-27 | Share a tab to the terminal (and back) with Maho (48) | 149 | browser, how-to, terminal | 7/0 | 1387 | yes | 6/0 | no-external-refs, no-h3, tag-inconsistent (should also carry `cli`) |
| `agentic-vs-chat` | 2026-10-30 | Agentic vs chat: when the browser should act, not just talk (54) | 155 | browser, agentic | 7/0 | 1768 | yes | 6/0 | no-external-refs, no-h3 |
| `browser-fingerprinting-defense` | 2026-11-03 | Browser fingerprinting defense: what we ship, what we do not (60) | 145 | browser, privacy | 7/0 | 1606 | yes | 7/0 | no-external-refs, no-h3 |
| `headless-browsing-with-maho` | 2026-11-06 | Headless browsing with Maho: scripts that read the web (54) | 136 | browser, how-to, terminal | 8/0 | 1578 | yes | 6/0 | no-external-refs, no-h3 |
| `cli-for-browser-history` | 2026-11-10 | A CLI for your browser history (30) | 149 | browser, how-to, terminal | 10/0 | 1528 | yes | 7/0 | no-external-refs, no-h3 |

**Aggregate issue counts:**
- `no-external-refs`: 48
- `no-h3`: 43
- `no-hero`: 10
- `low-internal-links`: 8
- `desc-short`: 6
- `desc-long`: 2
- `title-long`: 1
- `title-short`: 1
- `thin-content` (<900 words on a pillar/comparison post): 2

---

## Section 3: High-Priority Actions (top 10 fixes)

Ranked by impact-per-hour (SEO lift vs implementation effort).

1. **Ship the 10 missing hero images.** Blocks per-post JSON-LD image once Section 1.1 is fixed, kills the "Article without image" rich-result gap, unblocks better OG previews. See deliverable 2 for prompts. **Effort: medium** (image generation) **Impact: high.**
2. **Fix `StarlightHead.astro` to emit a per-post `image` in JSON-LD** (Section 1.1). Also add `og:image` and `article:published_time` per post if not already emitted. **Effort: low** (~20 lines) **Impact: high** (rich results + social preview).
3. **Rewrite descriptions on 8 outlier posts** (Section 1.7 + 1.8): 6 short mail-vs descs, `agentic-browser-comparison-2026` desc-long, `verify-maho-locally` desc-long. Also rewrite `maho-mail-vs-aerc-mutt-neomutt` title to <=60 chars and `maho-vs-aside-browser` title to include "agentic AI" keyword. **Effort: low** (~1 hour) **Impact: medium-high** (CTR uplift).
4. **Add internal-linking "See also" blocks to 8 low-link posts.** All 7 mail-vs posts should cross-link to each other + `/mail/gui`, `/mail/mcp`, `/api/`. `agentic-browser-comparison-2026` should link to each `maho-vs-*` post it references. **Effort: medium** **Impact: high** (PageRank distribution + user path depth).
5. **Consolidate tag taxonomy** (Section 1.6): rename `agentic-browser` -> `agentic`, drop the singleton `ai` tag, decide `cli` vs `terminal` policy, retag `why-sqlite-for-prompt-history` and `maho-vs-safari`. **Effort: low** **Impact: medium** (avoids thin tag archives being crawled).
6. **Add authoritative outbound refs to pillar posts.** Prioritize the 3 glossary posts (`what-is-*`), `byok-architecture-explained`, `tool-calling-without-the-mess`, `mcp-tools-in-the-browser`, `agentic-browser-permission-model`. Aim for >=3 outbound links each to schema.org, Anthropic docs, Chromium bug tracker, MCP spec, etc. **Effort: medium** **Impact: medium** (E-E-A-T + topical authority).
7. **Add H3 subsections to pillar posts.** 43 posts have zero H3. Target 5-10 pillar posts first: `byok-architecture-explained`, `agentic-browser-permission-model`, `sync-architecture-end-to-end`, `tool-calling-without-the-mess`, `page-context-vs-conversation-context`, `agentic-browser-failure-modes`, `building-an-agentic-workflow`, `local-models-cost-vs-cloud`. **Effort: medium** **Impact: medium** (featured-snippet eligibility, reading depth).
8. **Noindex thin archive pages.** Author archive (`/blog/authors/maho-team/`) and any tag archive with fewer than 5 posts (`glossary`, `cli`, `terminal`, `mcp`) — either bulk up the tag or noindex the archive. **Effort: low** **Impact: medium** (crawl budget cleanup).
9. **Add `dateModified` to top-5 pillar posts after next content refresh.** Fresh dates in SERP boost CTR. Combine with a quarterly "review pillar posts" cadence. **Effort: recurring** **Impact: medium.**
10. **Add RSS `<link rel="alternate">` to head if not already present.** Also verify sitemap includes RSS URL and every blog post URL. **Effort: low** **Impact: low-medium** (feed discovery, LLM ingestion).

---

## Section 4: Content Gaps + Topic Cluster Recommendations

### 4.1 Existing clusters (strong)

| Cluster | Posts | Health |
|---------|-------|--------|
| Agentic browser explainer | 8 posts (`what-is-an-agentic-browser`, `agentic-vs-chat`, `agentic-browser-permission-model`, `agentic-browser-failure-modes`, `agentic-browser-comparison-2026`, `mcp-tools-in-the-browser`, `tool-calling-without-the-mess`, `building-an-agentic-workflow`) | Strong depth. Missing pillar hub post that outlinks to all 8. |
| BYOK / local AI | 7 posts (`what-is-byok-*`, `byok-architecture-explained`, `byok-vs-managed-ai`, `ollama-with-maho-step-by-step`, `lm-studio-with-maho`, `local-models-cost-vs-cloud`, `why-sqlite-for-prompt-history`) | Good. Pillar (`what-is-byok-*`) exists but has zero external refs. |
| Privacy | 8 posts (`browser-fingerprinting-defense`, `zero-telemetry-by-default`, `verify-maho-locally`, `browser-history-encryption-explained`, `sync-without-an-account`, `sync-architecture-end-to-end`, `agentic-browser-failure-modes`, plus 1 crossover) | Strong. Missing a "state of browser tracking in 2026" landscape post. |
| Browser vs (comparison) | 11 posts | Complete for major competitors. Missing linkage into pillar `agentic-browser-comparison-2026`. |
| Mail vs (comparison) | 7 posts | Content exists but is orphaned — see Section 1.5. No pillar equivalent to `agentic-browser-comparison-2026` for mail. |
| Power-user browser | 5 posts (`keyboard-first-browsing-in-2026`, `boosts-explained`, `spaces-when-tab-groups-arent-enough`, `extensions-that-still-work`, `share-tab-to-cli`) | Fine coverage. |
| Terminal / CLI | 4 posts (`cli-for-browser-history`, `headless-browsing-with-maho`, `share-tab-to-cli`, `mcp-server-for-your-browser`) | Small cluster; consider a "Maho for terminal users" pillar tying the four together. |

### 4.2 Missing pillar / gap analysis

- **Mail landscape pillar missing.** `agentic-browser-comparison-2026` exists but there is no `maho-mail-comparison-2026` or `programmable-email-in-2026` post that plays the same role. Recommend commissioning this — it becomes the internal-link anchor for the 7 orphaned mail-vs posts.
- **Security research pillar missing.** Individual posts cover privacy, encryption, and telemetry, but there is no unifying "How we protect your data end-to-end" pillar that would rank for `browser privacy 2026` / `secure browser`.
- **AI failure/prompt-injection deeper post.** `agentic-browser-failure-modes` touches this. A dedicated "Prompt injection defenses in an agentic browser" post would rank for a searched, growing keyword.
- **No integration posts.** How does Maho work with Raycast, Alfred, Zed, Cursor, Claude Desktop? Each is a keyword target and a natural distribution moment.
- **No changelog / release note posts.** Regular changelog posts index well and give crawlers a fresh-content signal.

### 4.3 Cannibalization risks

- `mcp-server-for-your-browser` and `mcp-tools-in-the-browser` are close in intent. Confirm the search intent differs (server-side setup vs conceptual overview). If not, consolidate.
- `byok-architecture-explained` vs `what-is-byok-bring-your-own-key` — pillar + glossary. Keep both but ensure the glossary links to the architecture post explicitly at the "how does this work" moment.
- `agentic-vs-chat` and `what-is-an-agentic-browser` overlap on the "what makes something agentic" thesis. Add a clear internal link between them and differentiate H1s.

### 4.4 Thin-content candidates

- `maho-mail-vs-himalaya` (781 words) — smallest post. Padding it with a "What Himalaya does well" and a "When to use which" H2 would push it past 900 words.
- `what-is-mcp-model-context-protocol` (862 words) — glossary post but under 900. Add an H3 breakdown of transport (stdio vs SSE) and one comparison example (Claude Desktop vs Maho).

---

## Section 5: Technical SEO Checklist

Status legend: ✅ done, ⚠️ partial, ❌ missing, ➖ not applicable.

| Item | Status | Notes |
|---|---|---|
| Sitemap generated | ✅ | `@astrojs/sitemap` with per-route priority in `astro.config.mjs`. |
| Sitemap includes all blog posts | ⚠️ | Need to confirm at build time; TODO in config notes future-dated posts should be filtered. |
| Sitemap includes RSS feed URL | ❌ | RSS feed URL should be added to the sitemap or referenced in `<head>`. |
| Robots.txt served | ✅ | `src/pages/robots.txt.ts` dynamic. Sitemap referenced. |
| Robots.txt allows AI crawlers | ✅ | Explicit `Allow` for `GPTBot`, `ClaudeBot`, `PerplexityBot`, `Google-Extended`, `CCBot`. |
| Canonical tags on all posts | ⚠️ | Starlight's default `<Head>` should output canonical. Verify at build time via `curl | grep canonical` per template. |
| Canonical on tag / author archives points to archive URL | ⚠️ | Auto-generated by `starlight-blog`; verify. |
| BlogPosting JSON-LD emitted | ✅ | `StarlightHead.astro:50-75`. |
| JSON-LD `image` per post | ❌ | Uses global `/og-image.png`. See Section 1.1. |
| JSON-LD `author` per post | ✅ | Correctly normalized (string \| object \| array). |
| JSON-LD `datePublished` | ✅ | Reads frontmatter `date`. |
| JSON-LD `dateModified` | ⚠️ | Present only if `lastUpdated` set. None set today. |
| OG image per post | ❌ | Assumed to fall back to global — verify. |
| `article:published_time` OG tag | ⚠️ | Verify emitted by Starlight default `<Head>`. |
| `<link rel="alternate" type="application/rss+xml">` | ⚠️ | RSS is enabled but head-level discovery link should be verified. |
| Structured data validates in Rich Results Test | ⚠️ | Run against 3-4 posts after 1.1 fix. |
| Mobile-friendly | ✅ | Starlight is responsive by default; no known regressions. |
| Core Web Vitals: LCP | ⚠️ | Depends on hero image size. All hero PNGs range 200KB-900KB — 900KB is large. Convert to WebP (target <200KB) OR export at slightly lower quality. |
| Core Web Vitals: INP | ✅ | Static content, minimal JS. |
| Core Web Vitals: CLS | ✅ | Static Astro layout, no known ad/embed shift. |
| Image lazy loading | ⚠️ | Verify Starlight adds `loading="lazy"` to `![]()` MDX images. If not, add via a remark plugin. |
| `<img>` alt text on all blog images | ⚠️ | Alt text is set by the MDX author; audit a sample and enforce a linter rule (see Section 3). |
| HTTPS everywhere | ✅ | Site is HTTPS-only; site URL uses `https://`. |
| No mixed content | ✅ | No known HTTP embeds. |
| URL structure (readable, hyphens) | ✅ | Slugs are clean. |
| Trailing slash consistency | ⚠️ | Verify — Astro default is trailing slash. |
| 404 page | ⚠️ | Confirm `src/pages/404.astro` exists — not seen in `src/pages/` listing. |
| Broken link check | ⚠️ | Run `linkinator` or similar during CI. Not seen in repo. |
| hreflang | ➖ | English-only today. |
| AMP | ➖ | Not applicable; Astro is fast enough. |
| Analytics wired for search-console | ⚠️ | Confirm site is verified in Google Search Console + Bing Webmaster Tools + IndexNow. |

---

## Appendix A: Suggested rewrites for flagged titles/descriptions

Copy-paste ready. Character counts in parentheses.

**`maho-mail-vs-aerc-mutt-neomutt` (title-long)**
- Current title (73): "Maho Mail vs aerc / mutt / neomutt: Which Email Client Is Better in 2026?"
- Suggested (52): "Maho Mail vs aerc, mutt, neomutt: TUI email in 2026"

**`maho-vs-aside-browser` (title-short)**
- Current title (21): "Maho vs Aside Browser"
- Suggested (55): "Maho vs Aside Browser: agentic AI, two philosophies"

**`agentic-browser-comparison-2026` (desc-long, 179)**
- Suggested (156): "Compare Maho's agentic AI panel against Dia, Chrome + Gemini, Edge + Copilot, Brave Leo, Opera Aria, and SigmaOS. Trade-offs that actually matter."

**`verify-maho-locally` (desc-long, 194)**
- Suggested (154): "Maho is closed source. Verify our privacy claims anyway with Activity Monitor, Little Snitch, Wireshark, and Console.app. No source-reading required."

**`maho-mail-vs-apple-mail` (desc-short, 119)**
- Suggested (155): "Maho Mail vs Apple Mail. The default macOS client vs a programmable email client with CLI, MCP server, and BYOK AI. Which fits your workflow in 2026?"

**`maho-mail-vs-himalaya` (desc-short, 110)**
- Suggested (152): "Maho Mail vs himalaya. Both give you a CLI. Only one adds a desktop GUI, an MCP server for AI agents, and built-in AI features in a single package."

**`maho-mail-vs-shortwave` (desc-short, 110)**
- Suggested (156): "Maho Mail vs Shortwave. AI-wrapper email vs AI-native architecture. Local IMAP with CLI + MCP vs a polished cloud Gmail client with built-in AI chat."

**`maho-mail-vs-spark` (desc-short, 113)**
- Suggested (155): "Maho Mail vs Spark. Team collaboration email vs solo automation. Shared drafts and assignments vs local CLI, MCP server, and BYOK AI in one desktop app."

**`maho-mail-vs-superhuman` (desc-short, 117)**
- Suggested (153): "Maho Mail vs Superhuman. Premium keyboard-first email vs a free programmable client with CLI, MCP server, and AI providers of your choice. Which wins?"

**`maho-mail-vs-thunderbird` (desc-short, 113)**
- Suggested (156): "Maho Mail vs Thunderbird. A modern proprietary email client with CLI, MCP server, and AI, compared to Mozilla's veteran open-source desktop incumbent."

---

## Appendix B: Data collection method

- All 51 MDX files were parsed with a Python script that read frontmatter, computed title/description lengths, counted internal Markdown links (`](/…)`), external links (`](https?://…)`), hero image references (`/assets/blog/<slug>/hero.…`), body image references (`/assets/blog/<slug>/<not-hero>`), H2 count (`^## `), H3 count (`^### `), and total word count.
- Structured data audit performed against `src/components/StarlightHead.astro`.
- Sitemap and robots audit performed against `astro.config.mjs` and `src/pages/robots.txt.ts` respectively.
- Reference images inspected: `public/assets/blog/maho-vs-arc/hero.png`, `public/assets/blog/what-is-byok-bring-your-own-key/hero.png`.
