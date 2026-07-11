# Maho Website — Launch Prep Plan (v2, refreshed 2026-07-08)

> **Status**: READY for delegation. Day 1 ops complete. All Phase 0 decisions locked.
> **Scope**: Website codebase ONLY. Phase 1 (domain correction) + Phase 3 (download gate + waitlist delete) + Phase 5 (regression).
> **Related**: `.hermes/plans/2026-07-08_relay-workers-rs-port.md` — Phase 2 (relay backend Workers port) runs as a **separate delegation**.
> **Supersedes**: `.hermes/plans/2026-07-08_200407-maho-website-repositioning.md` (rejected in 5-agent review).
> **Companion**: `.omo/plans/maho-website-improvement-plan.md` (design-language adoption, orthogonal).

---

## TL;DR

Correct the fictional `maho.app` string across the `website/` codebase to the real domain `mahobrowser.com`. Add a build-time gate on `/download.astro` for zero-day release. Delete the `/waitlist` route per "no waitlist" constraint. Run regression against 2026-06-28 shipped work.

**Every code task in this plan is agy-delegatable.** Independent tasks run in parallel. Sequential tasks flagged explicitly.

**Total wall time estimate**: ~45 minutes for a delegated session working end-to-end.

---

## Delegation Contract — READ FIRST

This plan is designed for a **separate opencode/Sisyphus session** to execute end-to-end without prior conversation context. The delegated session:

1. Reads this plan file as sole source of truth.
2. Executes each `[agy]` task via `agy-task <workspace-root> "<prompt>"` bash command.
3. After every agy task, runs `lsp_diagnostics` on changed files; if diagnostics fail, respawns agy-task with error appended.
4. Marks each task complete after diagnostics pass + verify commands pass.
5. Commits to **`main` branch directly**. No feature branch, no PR.
6. **Does NOT push** — operator pushes manually after review.
7. Does NOT need Cloudflare API access (Day 1 ops already covered CF-side setup).

### Repo state expected at delegation start

```bash
cd /Users/indo/code/project/maho-workspace
git status website/ | head -3
# expect: nothing to commit, or only expected in-progress edits
```

If `website/` is not clean, delegated session pauses and asks operator to stash or clean.

### Tag before start (delegated session's first action)

```bash
cd /Users/indo/code/project/maho-workspace
git tag pre-website-domain-migration
```

This is the rollback anchor.

### Batching guidance

- **Batch A (parallel, independent files)**: Task 1.2 + 1.3 + 1.4a + 1.4b + 1.5a + 1.6a + 1.6b + 1.6e + 1.6f + 1.6g + 1.6h — 11 agy calls in ONE bash tool message (opencode runs parallel bash calls concurrently).
- **Batch B (parallel per shared file, sequential within file)**:
  - `terms.astro`: 1.5c → 1.6c (sequential)
  - `privacy.astro`: 1.5b → 1.6d (sequential)
- **Batch C (parallel, Phase 3)**: Task 3.1 + 3.2a + 3.2b + 3.2d — 4 agy calls in parallel.
- **Sisyphus-only tasks**: preflight, task 1.7 grep, task 1.8 build, task 3.2c file rm, Phase 5 regression suite.

---

## Locked Decisions (Phase 0)

| ID | Decision | Rationale |
|---|---|---|
| **D0.1** | Canonical domain: `mahobrowser.com` | Existing `maho.app` in codebase is a fictional placeholder (owned by third party, resolves to unrelated Mastodon). |
| **D0.2** | Backend relay migrates to **Cloudflare Workers** with Durable Objects | Operator subscribed to Workers Paid 2026-07-08. Separate Plan B handles the port. This plan (A) does NOT touch relay code. |
| **D0.3** | Email: `hello@mahobrowser.com` + `security@mahobrowser.com` | MX/SPF/DKIM/DMARC live on CF as of 2026-07-08. Forwarding destination: `freedomzero91@gmail.com`. |
| **D0.4** | DELETE `/waitlist` route + 301 to `/` | Purest "no waitlist" semantic. Relay backend endpoint stays alive for legacy row preservation (not exposed via UI). |
| **D0.5** | Release repo: `Project-Maho/release` (already hardcoded in `download.astro`) | No code change; operator locks org (verify + branch protection + 2FA) as separate ops task. |
| **D0.6** | D-Day date: **TBD** | Phase 3 code ships with `PUBLIC_RELEASE_LIVE=false` default. Gate flip is a manual op deferred. |
| **D0.7** | Mastodon: N/A | `maho.app` never operator-owned. No sunset burden. |
| **D0.8** | Analytics: **CF Web Analytics** (free, dashboard-enabled) | Zero code work — operator activates via CF Dashboard → Web Analytics → Add Site. Automatic beacon injection since site is CF-proxied. No plan tasks required. |
| **D0.9** | Discord invite: `https://discord.gg/Ty4J6pMdqg` | Used in `download.astro` (Task 3.1) as CTA both pre-launch (coming soon) and post-launch (fallback link). |

---

## Day 1 Ops — COMPLETED 2026-07-08

Verified live via CF API + public DNS at 2026-07-08. Delegated session should NOT re-do any of these.

### Domain + SSL
- ✅ Zone `mahobrowser.com` active (id `af1d3367cb526a8eb008b58078cd9185`)
- ✅ Pages custom domains bound to `maho-website` project: `mahobrowser.com` + `www.mahobrowser.com` (status `active`)
- ✅ Universal SSL cert issued (Google CA)
- ✅ CNAME apex + www → `maho-website.pages.dev` (Proxied)
- ✅ SSL mode `Full (strict)`, Always Use HTTPS + Auto HTTPS Rewrites + TLS 1.3 ON, Min TLS 1.2
- ✅ DNSSEC activated
- ✅ CAA: `pki.goog`, `letsencrypt.org`, `digicert.com`, `iodef mailto:hello@mahobrowser.com`

### Email
- ✅ CF Email Routing enabled (MX/SPF/DKIM auto)
- ✅ Destination `freedomzero91@gmail.com` verified
- ✅ Rules: `hello@mahobrowser.com` + `security@mahobrowser.com` → `freedomzero91@gmail.com`
- ✅ Resend account + `mahobrowser.com` domain added
- ✅ Resend DNS live: `MX send.mahobrowser.com`, `TXT send. SPF`, `TXT resend._domainkey DKIM`

### Policy
- ✅ DMARC: `v=DMARC1; p=none; rua=mailto:hello@mahobrowser.com; ruf=mailto:hello@mahobrowser.com; fo=1; adkim=r; aspf=r; pct=100` (monitoring; upgrade to `p=quarantine` after 2 clean weeks)

### External accounts
- ✅ Discord: `https://discord.gg/Ty4J6pMdqg`
- ✅ CF account: `b1c1bfc44b1313b6f51553095ccb2eb4` (upgraded to Workers Paid $5/mo 2026-07-08)
- ✅ Resend account (operator email)

---

## Prior Shipped Work — DO NOT RE-DO

Delegated session must PRESERVE this via Phase 5 regression suite.

Reference commit `c01ec345` (2026-06-28) via `.omo/plans/maho-website-improvement-plan.md`:

| Area | Delivered | Verify command |
|---|---|---|
| Pricing consistency | Homepage + `/pricing` at $0/$5/$20 + PAYG | `grep -c '\$5\|\$20' website/src/pages/pricing.astro website/src/components/LandingPage.tsx` → ≥ 6 |
| Terms proprietary | Individual-developer voice, OSS claims removed | `grep -iE 'open source\|GPL\|MIT\|auditable' website/src/pages/terms.astro` → 0 hits |
| Closed-source FAQ | "No. Maho is not open source." | `grep -c 'not open source' website/src/content/docs/browser/faq.mdx` → ≥ 1 |
| Hero H1 | "Your AI. Your data. Your browser." | `grep -c 'Your AI\.' website/src/components/LandingPage.tsx` → ≥ 1 |
| Anti-agent `/why` | "hand-on-wheel" phrase | `grep -c 'hand-on-wheel' website/src/pages/why.astro` → ≥ 1 |
| Nav/Footer clean | GitHub links removed | `grep -c 'github.com' website/src/components/Nav.astro website/src/components/Footer.astro` → 0 |

Any regression = revert the phase that caused it.

---

## Corrections From 5-Agent Review

These facts were wrong in the prior draft. This plan encodes them correctly:

1. **`LandingPage.tsx` is a single file** — subcomponents live in `src/components/landing/` (lowercase). There is no `LandingPage/` directory.
2. **`astro.config.mjs` has TWO hardcoded `maho.app` sites** — line 9 (SITE_URL default) AND line 128 (sitemap serialize matcher).
3. **`index.astro:95` has hardcoded `'https://maho.app/?q={search_term_string}'`** — must convert to `absoluteUrl(...)`.
4. **`.env.example` has ONLY `SITE_URL`** — no `PUBLIC_RELAY_URL`, `PUBLIC_RELEASE_LIVE`, `PUBLIC_RELEASE_REPO_URL`. These are new additions.
5. **`hello@maho.app` occurs in 8 files**: `Footer.astro`, `about.astro`, `terms.astro`, `privacy.astro`, `security.astro`, `why.astro`, `roadmap.astro`, `verify-maho-locally.mdx`.
6. **`compare/index.astro` is Maho-Mail-specific** — not browser-comparison. Ignore for this plan.
7. **`why.astro` is the anti-agent manifesto** — keep, do not delete.
8. **Grep must use word boundaries**: `\b(Arc|Zen|Dia|Comet|Atlas)\b` — NOT `arc|zen|dia`.
9. **Do not attempt CF Rules 301 from `maho.app`** — operator does not own that domain.

---

## Files Changed Inventory

Domain refactor (7 files, some overlap with email):
- `website/astro.config.mjs`
- `website/.env.example`
- `website/src/lib/site.ts`
- `website/src/components/JSONLD.astro`
- `website/src/pages/index.astro`
- `website/src/pages/privacy.astro` (shared with email 1.6d)
- `website/src/pages/terms.astro` (shared with email 1.6c)

Email migration (8 files, some overlap with above):
- `website/src/components/Footer.astro` (also touched in Phase 3.2a)
- `website/src/pages/about.astro`
- `website/src/pages/terms.astro`
- `website/src/pages/privacy.astro`
- `website/src/pages/security.astro`
- `website/src/pages/why.astro`
- `website/src/pages/roadmap.astro`
- `website/src/content/docs/blog/verify-maho-locally.mdx`

Phase 3 (4 files + 1 delete):
- `website/src/pages/download.astro` (rewrite with gate)
- `website/src/components/Footer.astro` (remove /waitlist link — combine with 1.6a in one commit)
- `website/src/components/Nav.astro` (remove /waitlist link)
- `website/src/pages/waitlist.astro` (**DELETE**)
- `website/public/_redirects` (new file)

**Total distinct files touched**: 13
**Total commits**: 3

Explicitly OUT of scope (do not touch):
- Blog post rewrites / draft-hiding
- `why.astro` deletion (keep — anti-agent manifesto)
- `pricing.astro` stubbing (keep — production pricing)
- Solo-dev → "team" identity flip
- `compare/aside.astro` v3 (keep)
- `compare/maho-mail-vs-*.astro` (Maho Mail scope)
- `maho-vs-*.mdx` tone edits (deferred to separate plan)
- Any file under `maho/`, `maho-chromium/`, `mail/`, `.github/workflows/` (relay Workers port scope — Plan B)

---

## agy-task Delegation Rules

Every `[agy]` task below is a self-contained brief. Invocation:

```bash
agy-task /Users/indo/code/project/maho-workspace "<full prompt from this file>"
```

Delegated session's per-task workflow:
1. Copy the `agy-task prompt` block verbatim (bounded by `─── begin ───` / `─── end ───`)
2. Pass to agy-task
3. Parse agy's `### CHANGED_FILES` output
4. Run `lsp_diagnostics <path>` on each changed file
5. If diagnostics fail, respawn agy-task with the error text appended to prompt
6. If diagnostics pass, run the `VERIFY` commands from the prompt
7. If verify fails, respawn agy-task with the specific failing check
8. Once clean, stage the file(s) but do not commit yet — batch commit per section

Independent agy tasks (different files) → launch multiple `bash` tool calls in ONE assistant message so opencode runs them in parallel.

---

# Phase 1 — Domain Correction

## Preflight [Sisyphus]

```bash
cd /Users/indo/code/project/maho-workspace
git tag pre-website-domain-migration    # rollback anchor

# Audit reference
grep -rn --include='*.astro' --include='*.ts' --include='*.tsx' \
     --include='*.mjs' --include='*.md' --include='*.mdx' \
     'maho\.app' website/ \
     | grep -v 'node_modules' | grep -v '\.astro/' \
     > /tmp/maho-app-audit.txt

wc -l /tmp/maho-app-audit.txt
# Expected: ~55 hits.
# - 5 runtime hardcoded (fixed by 1.2, 1.5a, 1.5b, 1.5c)
# - 9 JSDoc examples (fixed by 1.4a, 1.4b)
# - 13 email addresses (fixed by 1.6a-h)
# - ~28 backend infra hostnames in blog docs (out of scope for Plan A; see Plan B Task 2J)
```

---

## Task 1.2 [agy] — `astro.config.mjs` domain replacement

**Files touched**: `website/astro.config.mjs` only.

**agy-task prompt**:

─── begin ───
```
CONTEXT: Astro 6 marketing site for Maho browser at /Users/indo/code/project/maho-workspace/website/. The file astro.config.mjs has TWO hardcoded uses of the fictional string "maho.app" that must be replaced with the real domain "mahobrowser.com". Line 9 is a fallback default; line ~128 is a home-URL matcher inside the sitemap serialize function.

TASK: Edit /Users/indo/code/project/maho-workspace/website/astro.config.mjs.

Change 1 — Line 9. Original:
    const SITE_URL = process.env.SITE_URL || 'https://maho.app';
Replace with:
    const SITE_URL = process.env.SITE_URL || 'https://mahobrowser.com';

Change 2 — Around line 128, inside the `sitemap({ serialize(item) { ... } })` block. Original:
        if (url === 'https://maho.app/' || url === 'https://maho.app') {
Replace with:
        if (url === `${SITE_URL}/` || url === SITE_URL) {

Note the switch to template literals. SITE_URL is already declared at the top of the file (line 9 after your Change 1) so it's in scope inside the serialize function's closure.

DO NOT TOUCH:
- Any other file
- Any other line in astro.config.mjs
- Comments
- Any of the other conditions in the serialize function (blog, tags, marketing pages, docs, etc.)
- The `site: SITE_URL` binding
- Indentation of unrelated lines

VERIFY (each command must produce the exact expected output):
- `grep -c 'maho\.app' /Users/indo/code/project/maho-workspace/website/astro.config.mjs` → 0
- `grep -c 'mahobrowser\.com' /Users/indo/code/project/maho-workspace/website/astro.config.mjs` → 1
- `grep -c 'SITE_URL' /Users/indo/code/project/maho-workspace/website/astro.config.mjs` → at least 4
- File must parse as valid JavaScript (no syntax errors)
```
─── end ───

**Post-agy**:
- `lsp_diagnostics /Users/indo/code/project/maho-workspace/website/astro.config.mjs`

---

## Task 1.3 [agy] — `.env.example` build-time env vars

**agy-task prompt**:

─── begin ───
```
CONTEXT: Astro 6 site deployed to Cloudflare Pages via Direct Upload. Env vars must be present in the operator shell at build time (Cloudflare Dashboard env vars are ignored for Direct Upload). Current .env.example has only SITE_URL. Add PUBLIC_RELAY_URL, PUBLIC_RELEASE_LIVE, PUBLIC_RELEASE_REPO_URL.

TASK: Replace the entire contents of /Users/indo/code/project/maho-workspace/website/.env.example with:

# Canonical site URL. Used for sitemap, OG tags, JSON-LD, canonical links.
# Override per environment (staging, preview).
SITE_URL=https://mahobrowser.com

# Relay backend origin for waitlist form (POST /waitlist) and other relay APIs.
# Falls back to http://localhost:8080 if unset. Production: relay.mahobrowser.com (CF Worker).
PUBLIC_RELAY_URL=https://relay.mahobrowser.com

# Gates /download page: false = "coming soon" state, true = live release links.
# Cloudflare Pages Direct Upload IGNORES dashboard vars — must be in operator shell at build time.
PUBLIC_RELEASE_LIVE=false

# GitHub release-only repository for download links.
PUBLIC_RELEASE_REPO_URL=https://github.com/Project-Maho/release

DO NOT TOUCH:
- Any other file
- Existing .env (gitignored) if present
- Do not add trailing whitespace, tabs, or CRLF

VERIFY:
- `grep -c 'mahobrowser\.com' /Users/indo/code/project/maho-workspace/website/.env.example` → 3
- `grep -c 'PUBLIC_RELEASE_LIVE' /Users/indo/code/project/maho-workspace/website/.env.example` → 1
- `grep -c 'PUBLIC_RELAY_URL' /Users/indo/code/project/maho-workspace/website/.env.example` → 1
- `grep -c 'SITE_URL=' /Users/indo/code/project/maho-workspace/website/.env.example` → 1
- File is UTF-8, LF line endings
```
─── end ───

---

## Task 1.4a [agy] — `src/lib/site.ts` JSDoc update

**agy-task prompt**:

─── begin ───
```
CONTEXT: JSDoc-only string change. The runtime code uses Astro.site (no hardcoded domain). The JSDoc example URLs still reference the fictional "https://maho.app" — update to "https://mahobrowser.com". Per website/AGENTS.md, hardcoded domain is allowed in JSDoc examples of exactly two files: src/lib/site.ts and src/components/JSONLD.astro. This task only touches the first.

TASK: In /Users/indo/code/project/maho-workspace/website/src/lib/site.ts, replace every occurrence of "https://maho.app" with "https://mahobrowser.com". There are exactly 3 occurrences, all inside the /** ... */ JSDoc block at the top of the file:
- Line 5: "* with a default of `https://maho.app`."
- Line 11: "*   const homepage = siteUrl;                  // "https://maho.app""
- Line 12: "*   const aboutUrl = absoluteUrl("/about");    // "https://maho.app/about""

Change to:
- "* with a default of `https://mahobrowser.com`."
- "*   const homepage = siteUrl;                  // "https://mahobrowser.com""
- "*   const aboutUrl = absoluteUrl("/about");    // "https://mahobrowser.com/about""

DO NOT TOUCH:
- Any other file
- Any code outside the JSDoc block (the getSiteUrl function body must be unchanged)
- The JSDoc block structure or ordering
- Indentation, spacing, or the trailing `//` inline comments

VERIFY:
- `grep -c 'maho\.app' /Users/indo/code/project/maho-workspace/website/src/lib/site.ts` → 0
- `grep -c 'mahobrowser\.com' /Users/indo/code/project/maho-workspace/website/src/lib/site.ts` → 3
- File compiles (TypeScript valid)
```
─── end ───

**Post-agy**: `lsp_diagnostics /Users/indo/code/project/maho-workspace/website/src/lib/site.ts`

---

## Task 1.4b [agy] — `src/components/JSONLD.astro` JSDoc update

**agy-task prompt**:

─── begin ───
```
CONTEXT: JSDoc-only string change. This Astro component's runtime uses props — no hardcoded domain in the template. Only the JSDoc examples in the /** ... */ comment block reference the fictional "https://maho.app". Update to "https://mahobrowser.com". Per website/AGENTS.md, hardcoded domain is allowed in JSDoc examples of exactly two files: src/lib/site.ts and src/components/JSONLD.astro. This task only touches the second.

TASK: In /Users/indo/code/project/maho-workspace/website/src/components/JSONLD.astro, replace every occurrence of "https://maho.app" with "https://mahobrowser.com". Occurrences (all in the JSDoc block near lines 1-60):
- Line 10: `*   "url": "https://maho.app",`
- Line 11: `*   "logo": "https://maho.app/logo.svg"`
- Line 29: `*   "url": "https://maho.app"`
- Line 38: `*   "url": "https://maho.app/waitlist"` ← this JSDoc example should also change /waitlist to /download since /waitlist is being deleted (Task 3.2). Change this specific line to: `*   "url": "https://mahobrowser.com/download"`
- Line 53: `*     { "@type": "Organization", "name": "Maho", "url": "https://maho.app" },`
- Line 54: `*     { "@type": "WebSite", "name": "Maho", "url": "https://maho.app" }`

DO NOT TOUCH:
- Any other file
- Any code below the JSDoc block (the runtime template body)
- Any other JSDoc example URL paths (only the domain and the /waitlist → /download example)

VERIFY:
- `grep -c 'maho\.app' /Users/indo/code/project/maho-workspace/website/src/components/JSONLD.astro` → 0
- `grep -c 'mahobrowser\.com' /Users/indo/code/project/maho-workspace/website/src/components/JSONLD.astro` → 6
- `grep -c '/waitlist' /Users/indo/code/project/maho-workspace/website/src/components/JSONLD.astro` → 0
- `grep -c '/download' /Users/indo/code/project/maho-workspace/website/src/components/JSONLD.astro` → 1
```
─── end ───

**Post-agy**: `lsp_diagnostics /Users/indo/code/project/maho-workspace/website/src/components/JSONLD.astro`

---

## Task 1.5a [agy] — `index.astro:95` SearchAction template

**agy-task prompt**:

─── begin ───
```
CONTEXT: The landing page's WebSite JSON-LD has a hardcoded "https://maho.app/?q=..." in its SearchAction potentialAction.urlTemplate field. This is runtime code, not JSDoc. Must be dynamic via the getSiteUrl helper.

TASK: Edit /Users/indo/code/project/maho-workspace/website/src/pages/index.astro.

Line ~95, inside the `potentialAction` block in the `ld` object. Original:
          urlTemplate: 'https://maho.app/?q={search_term_string}',

Replace with:
          urlTemplate: absoluteUrl('/?q={search_term_string}'),

Note: `absoluteUrl` is already imported at the top of the file — it's destructured from `getSiteUrl(Astro)` on line 20. Confirm before editing:
    const { siteUrl, absoluteUrl } = getSiteUrl(Astro);

If for some reason absoluteUrl is NOT already destructured, add it to the destructure. Otherwise leave the destructure untouched.

DO NOT TOUCH:
- Any other file
- Any other line in index.astro
- The `siteUrl` variable
- The Organization JSON-LD block (line 76-84)
- The SoftwareApplication JSON-LD block
- The template body (all JSX below `---`)

VERIFY:
- `grep -c 'https://maho\.app' /Users/indo/code/project/maho-workspace/website/src/pages/index.astro` → 0
- `grep -c "absoluteUrl('/?q=" /Users/indo/code/project/maho-workspace/website/src/pages/index.astro` → 1
- The file must still be valid Astro (frontmatter + template well-formed)
```
─── end ───

**Post-agy**: `lsp_diagnostics /Users/indo/code/project/maho-workspace/website/src/pages/index.astro`

---

## Task 1.5b [agy, sequential-before-1.6d] — `privacy.astro:80` JSON-LD @id

**agy-task prompt**:

─── begin ───
```
CONTEXT: The privacy page's WebPage JSON-LD has a hardcoded "https://maho.app/#website" in its isPartOf.@id field. Must be dynamic via getSiteUrl. This task must run BEFORE Task 1.6d (which edits emails in the same file); sequence matters to avoid overlapping edits.

TASK: Edit /Users/indo/code/project/maho-workspace/website/src/pages/privacy.astro.

At line ~80, in the `ld` object frontmatter. Original:
    "isPartOf": { "@type": "WebSite", "@id": "https://maho.app/#website" },

Replace with:
    "isPartOf": { "@type": "WebSite", "@id": `${siteUrl}/#website` },

Note the switch from single-quoted string to a template literal (backticks). This requires that `siteUrl` is already destructured from `getSiteUrl(Astro)` in the file's frontmatter. Verify at the top of the frontmatter:
    const { siteUrl, absoluteUrl } = getSiteUrl(Astro);

If siteUrl is not destructured, add it. If getSiteUrl is not imported at the top, add:
    import { getSiteUrl } from '../lib/site';

DO NOT TOUCH:
- Any other file
- Any other line in privacy.astro (email addresses on lines 53, 54, 69 are handled by Task 1.6d — leave them alone HERE)
- The rest of the ld object structure
- Any body content

VERIFY:
- `grep -c '"https://maho\.app/#website"' /Users/indo/code/project/maho-workspace/website/src/pages/privacy.astro` → 0
- `grep -c 'siteUrl' /Users/indo/code/project/maho-workspace/website/src/pages/privacy.astro` → at least 1
- Note: `hello@maho.app` still remains in this file — that's Task 1.6d's job
- The file must still be valid Astro
```
─── end ───

**Post-agy**: `lsp_diagnostics /Users/indo/code/project/maho-workspace/website/src/pages/privacy.astro`

---

## Task 1.5c [agy, sequential-before-1.6c] — `terms.astro:98` JSON-LD @id

**agy-task prompt**:

─── begin ───
```
CONTEXT: The terms page's JSON-LD has a hardcoded "https://maho.app/#website" in its isPartOf.@id field. Must be dynamic via getSiteUrl. This task must run BEFORE Task 1.6c (which edits emails in the same file).

TASK: Edit /Users/indo/code/project/maho-workspace/website/src/pages/terms.astro.

At line ~98, in the `ld` object frontmatter. Original:
    "isPartOf": { "@type": "WebSite", "@id": "https://maho.app/#website" },

Replace with:
    "isPartOf": { "@type": "WebSite", "@id": `${siteUrl}/#website` },

Ensure `siteUrl` is destructured from `getSiteUrl(Astro)` at the top of frontmatter. If not, add the destructure and the import:
    import { getSiteUrl } from '../lib/site';
    ...
    const { siteUrl } = getSiteUrl(Astro);

DO NOT TOUCH:
- Any other file
- Any other line in terms.astro (email address on line 87 is handled by Task 1.6c)
- The rest of the ld object structure
- Any body content

VERIFY:
- `grep -c '"https://maho\.app/#website"' /Users/indo/code/project/maho-workspace/website/src/pages/terms.astro` → 0
- `grep -c 'siteUrl' /Users/indo/code/project/maho-workspace/website/src/pages/terms.astro` → at least 1
- Note: `hello@maho.app` still remains in this file — that's Task 1.6c's job
- Valid Astro
```
─── end ───

**Post-agy**: `lsp_diagnostics /Users/indo/code/project/maho-workspace/website/src/pages/terms.astro`

---

## Commit checkpoint (after Tasks 1.2 – 1.5)

Stage everything and commit as one "domain replacement" logical unit:

```bash
git add website/astro.config.mjs \
        website/.env.example \
        website/src/lib/site.ts \
        website/src/components/JSONLD.astro \
        website/src/pages/index.astro \
        website/src/pages/privacy.astro \
        website/src/pages/terms.astro

git commit -m "chore(website): correct fictional maho.app to real mahobrowser.com

Domain string correction across canonical URLs, JSON-LD, sitemap
matcher, env var docs, and JSDoc examples. Runtime paths now derive
URLs from getSiteUrl(Astro); JSDoc examples updated to the new domain
per website/AGENTS.md convention.

Also adds PUBLIC_RELAY_URL / PUBLIC_RELEASE_LIVE / PUBLIC_RELEASE_REPO_URL
to .env.example (build-time vars for the Direct Upload deploy pattern)."
```

---

## Task 1.6a [agy, parallel-safe] — `Footer.astro` email

**Coordination note**: `Footer.astro` is also touched by Task 3.2a (remove `/waitlist` link). Run 1.6a first (email), then 3.2a later in Phase 3.

**agy-task prompt**:

─── begin ───
```
CONTEXT: Replace the mailto contact address in the site footer. The email domain migrates from the fictional maho.app to the real mahobrowser.com (MX records live 2026-07-08).

TASK: In /Users/indo/code/project/maho-workspace/website/src/components/Footer.astro, replace every occurrence of "hello@maho.app" with "hello@mahobrowser.com". Exactly 1 occurrence expected (line ~83, inside a mailto href and link text).

Both the `mailto:hello@maho.app` in href AND the visible text `hello@maho.app` (if the link's inner text is the same email) must change.

DO NOT TOUCH:
- Any other file
- Any other line — this task is EMAIL ONLY, do not remove /waitlist links (Task 3.2a handles that)
- Any component structure or styling

VERIFY:
- `grep -c 'hello@maho\.app' /Users/indo/code/project/maho-workspace/website/src/components/Footer.astro` → 0
- `grep -c 'hello@mahobrowser\.com' /Users/indo/code/project/maho-workspace/website/src/components/Footer.astro` → ≥1
```
─── end ───

**Post-agy**: `lsp_diagnostics /Users/indo/code/project/maho-workspace/website/src/components/Footer.astro`

---

## Task 1.6b [agy, parallel-safe] — `about.astro` email

**agy-task prompt**:

─── begin ───
```
CONTEXT: About page has 2 email references (line 24 JSON-LD email field, line 94 mailto link + visible text).

TASK: In /Users/indo/code/project/maho-workspace/website/src/pages/about.astro, replace every "hello@maho.app" with "hello@mahobrowser.com". Exactly 2 occurrences expected: JSON-LD frontmatter (email: 'hello@maho.app'), and body mailto anchor.

DO NOT TOUCH:
- Any other file
- Any other content, ScrollReveal structure, headings, or paragraph text
- The solo-developer positioning (do not swap to "The maho team" — that positioning stays)

VERIFY:
- `grep -c 'hello@maho\.app' /Users/indo/code/project/maho-workspace/website/src/pages/about.astro` → 0
- `grep -c 'hello@mahobrowser\.com' /Users/indo/code/project/maho-workspace/website/src/pages/about.astro` → ≥2
```
─── end ───

**Post-agy**: `lsp_diagnostics /Users/indo/code/project/maho-workspace/website/src/pages/about.astro`

---

## Task 1.6c [agy, sequential-after-1.5c] — `terms.astro` email

**agy-task prompt**:

─── begin ───
```
CONTEXT: Terms page has 1 email reference (line 87, contact directive). This task must run AFTER Task 1.5c completed the JSON-LD @id change in the same file.

TASK: In /Users/indo/code/project/maho-workspace/website/src/pages/terms.astro, replace every "hello@maho.app" with "hello@mahobrowser.com". Exactly 1 occurrence.

DO NOT TOUCH:
- Any other file
- Any other line — Task 1.5c already handled the JSON-LD @id on line ~98; this task is only about the email address
- ScrollReveal / JSX structure

VERIFY:
- `grep -c 'hello@maho\.app' /Users/indo/code/project/maho-workspace/website/src/pages/terms.astro` → 0
- `grep -c 'hello@mahobrowser\.com' /Users/indo/code/project/maho-workspace/website/src/pages/terms.astro` → 1
```
─── end ───

**Post-agy**: `lsp_diagnostics /Users/indo/code/project/maho-workspace/website/src/pages/terms.astro`

---

## Task 1.6d [agy, sequential-after-1.5b] — `privacy.astro` email

**agy-task prompt**:

─── begin ───
```
CONTEXT: Privacy page has 3 email references on lines 53, 54, 69. This task must run AFTER Task 1.5b completed the JSON-LD @id change in the same file.

TASK: In /Users/indo/code/project/maho-workspace/website/src/pages/privacy.astro, replace every "hello@maho.app" with "hello@mahobrowser.com". Exactly 3 occurrences.

DO NOT TOUCH:
- Any other file
- The line 80 JSON-LD @id (Task 1.5b handled that)
- Any other content

VERIFY:
- `grep -c 'hello@maho\.app' /Users/indo/code/project/maho-workspace/website/src/pages/privacy.astro` → 0
- `grep -c 'hello@mahobrowser\.com' /Users/indo/code/project/maho-workspace/website/src/pages/privacy.astro` → 3
```
─── end ───

**Post-agy**: `lsp_diagnostics /Users/indo/code/project/maho-workspace/website/src/pages/privacy.astro`

---

## Task 1.6e [agy, parallel-safe] — `security.astro` email

**agy-task prompt**:

─── begin ───
```
CONTEXT: Security page has 3 email references: hello@ on lines 118 and 144, and security@ on line 144. Both addresses migrate.

TASK: In /Users/indo/code/project/maho-workspace/website/src/pages/security.astro, replace:
- Every "hello@maho.app" → "hello@mahobrowser.com" (expect 2 occurrences)
- Every "security@maho.app" → "security@mahobrowser.com" (expect 1 occurrence)

DO NOT TOUCH:
- Any other file
- Any body content beyond the email addresses
- The 4-layer trust fallback content

VERIFY:
- `grep -Ec '(hello|security)@maho\.app' /Users/indo/code/project/maho-workspace/website/src/pages/security.astro` → 0
- `grep -c 'hello@mahobrowser\.com' /Users/indo/code/project/maho-workspace/website/src/pages/security.astro` → 2
- `grep -c 'security@mahobrowser\.com' /Users/indo/code/project/maho-workspace/website/src/pages/security.astro` → 1
```
─── end ───

**Post-agy**: `lsp_diagnostics /Users/indo/code/project/maho-workspace/website/src/pages/security.astro`

---

## Task 1.6f [agy, parallel-safe] — `why.astro` email

**agy-task prompt**:

─── begin ───
```
CONTEXT: Why page has 1 email reference near line 111 (footer contact). This page is the anti-agent manifesto — do not modify positioning content.

TASK: In /Users/indo/code/project/maho-workspace/website/src/pages/why.astro, replace every "hello@maho.app" with "hello@mahobrowser.com". Exactly 1 occurrence (mailto anchor + inline text).

DO NOT TOUCH:
- Any other file
- The three-commitments content
- Anti-agent-native positioning language ("hand-on-wheel", "Other AI browsers chase autonomous agents", etc.)

VERIFY:
- `grep -c 'hello@maho\.app' /Users/indo/code/project/maho-workspace/website/src/pages/why.astro` → 0
- `grep -c 'hello@mahobrowser\.com' /Users/indo/code/project/maho-workspace/website/src/pages/why.astro` → 1
- `grep -c 'hand-on-wheel' /Users/indo/code/project/maho-workspace/website/src/pages/why.astro` → ≥1 (preserved)
```
─── end ───

**Post-agy**: `lsp_diagnostics /Users/indo/code/project/maho-workspace/website/src/pages/why.astro`

---

## Task 1.6g [agy, parallel-safe] — `roadmap.astro` email

**agy-task prompt**:

─── begin ───
```
CONTEXT: Roadmap page has 1 email reference near line 137 (feedback CTA).

TASK: In /Users/indo/code/project/maho-workspace/website/src/pages/roadmap.astro, replace every "hello@maho.app" with "hello@mahobrowser.com". Exactly 1 occurrence.

DO NOT TOUCH:
- Any other file
- Roadmap item lists (Shipped / Building / Considering)
- Any other content

VERIFY:
- `grep -c 'hello@maho\.app' /Users/indo/code/project/maho-workspace/website/src/pages/roadmap.astro` → 0
- `grep -c 'hello@mahobrowser\.com' /Users/indo/code/project/maho-workspace/website/src/pages/roadmap.astro` → 1
```
─── end ───

**Post-agy**: `lsp_diagnostics /Users/indo/code/project/maho-workspace/website/src/pages/roadmap.astro`

---

## Task 1.6h [agy, parallel-safe] — `verify-maho-locally.mdx` email

**agy-task prompt**:

─── begin ───
```
CONTEXT: Blog post documents user-verifiable network endpoints. Two email references on lines ~46 and ~107 direct users to file bug reports. Only the email address changes in this task. The *.maho.app hostname references (update.maho.app, sync.maho.app, proxy.maho.app) are OUT OF SCOPE — they will be corrected in a separate plan (Plan B: relay Workers port).

TASK: In /Users/indo/code/project/maho-workspace/website/src/content/docs/blog/verify-maho-locally.mdx, replace every "hello@maho.app" with "hello@mahobrowser.com". Exactly 2 occurrences.

DO NOT TOUCH:
- Any other file
- Any *.maho.app hostname references (update.maho.app, sync.maho.app, proxy.maho.app, updates.maho.app, wss://sync.maho.app) — these are handled by a separate plan
- Blog post frontmatter (title, description, date, tags)
- MDX components and structure

VERIFY:
- `grep -c 'hello@maho\.app' /Users/indo/code/project/maho-workspace/website/src/content/docs/blog/verify-maho-locally.mdx` → 0
- `grep -c 'hello@mahobrowser\.com' /Users/indo/code/project/maho-workspace/website/src/content/docs/blog/verify-maho-locally.mdx` → 2
- Backend infra hostnames (sync.maho.app, update.maho.app, proxy.maho.app) unchanged from baseline
```
─── end ───

**Post-agy**: MDX — LSP may be limited; skip if diagnostics unavailable.

---

## Commit checkpoint (after Tasks 1.6a-h)

```bash
git add website/src/components/Footer.astro \
        website/src/pages/about.astro \
        website/src/pages/terms.astro \
        website/src/pages/privacy.astro \
        website/src/pages/security.astro \
        website/src/pages/why.astro \
        website/src/pages/roadmap.astro \
        website/src/content/docs/blog/verify-maho-locally.mdx

git commit -m "feat(website): migrate contact email to mahobrowser.com

hello@ and security@ move from the fictional maho.app to the real
mahobrowser.com domain. MX + SPF + DKIM + DMARC live on CF as of
2026-07-08; freedomzero91@gmail.com is the forwarding destination.

Backend infra hostnames (update/sync/proxy.maho.app) in blog docs
are separately corrected in Plan B alongside the CF Workers relay port."
```

---

## Task 1.7 [Sisyphus] — Static asset verification

Not agy — read-only inspection.

```bash
cd /Users/indo/code/project/maho-workspace
grep -rn 'maho\.app' website/public/ website/og.config.mjs website/src/pages/robots.txt.ts 2>&1
```

Expected: 0 hits. If any hit → address ad-hoc, then include in Phase 3 commit.

---

## Task 1.8 [Sisyphus] — Full build + dist grep verification

```bash
cd /Users/indo/code/project/maho-workspace/website

SITE_URL=https://mahobrowser.com \
PUBLIC_RELAY_URL=https://relay.mahobrowser.com \
PUBLIC_RELEASE_LIVE=false \
PUBLIC_RELEASE_REPO_URL=https://github.com/Project-Maho/release \
bun run build

# Post-build audit
grep -rln 'maho\.app' dist/ | tee /tmp/dist-post-phase1.txt

# Any dist hits should be in blog posts documenting backend hostnames.
# Marketing pages (dist/index.html, dist/about/, dist/download/, etc.) should be clean.

grep -c 'mahobrowser\.com' dist/index.html                        # expect ≥ 3
grep -l 'mahobrowser\.com' dist/sitemap-*.xml                     # expect ≥ 1
grep -c 'hello@mahobrowser\.com' dist/about/index.html            # expect ≥ 1
grep -c 'hello@maho\.app' dist/about/index.html                   # expect 0
```

Phase 1 exit criteria:
- All Task 1.2 – 1.6 commits landed
- Build succeeds (~119 pages)
- Marketing page dist grep clean
- LSP diagnostics clean on all changed files

---

# Phase 3 — Zero-Day Download Gate + Waitlist Delete

## Task 3.1 [agy] — `download.astro` release gate

**agy-task prompt**:

─── begin ───
```
CONTEXT: Currently /download.astro unconditionally redirects to https://github.com/Project-Maho/release/releases via meta-refresh + inline JS setTimeout. This must be gated behind a build-time env var PUBLIC_RELEASE_LIVE so the pre-launch state is a "coming soon" page pointing at Discord instead of a broken bounce to GitHub.

TASK: Rewrite /Users/indo/code/project/maho-workspace/website/src/pages/download.astro with the following structure. Preserve existing imports (BaseLayout, Nav, Footer, CtaButton, SquircleBox, ScrollReveal, JSONLD) and the installSteps array structure.

The new frontmatter must include:

    const RELEASE_REPO = import.meta.env.PUBLIC_RELEASE_REPO_URL || 'https://github.com/Project-Maho/release';
    const RELEASE_LIVE = import.meta.env.PUBLIC_RELEASE_LIVE === 'true';
    const productParam = Astro.url.searchParams.get('product');
    const productLabel = productParam === 'mail' ? 'Maho Mail' : 'Maho';
    const target = `${RELEASE_REPO}/releases`;
    const discordInvite = 'https://discord.gg/Ty4J6pMdqg';

Body renders conditionally based on RELEASE_LIVE.

STATE 1 — RELEASE_LIVE === false (pre-launch, default):
- Do NOT emit meta http-equiv="refresh"
- Do NOT emit the inline JS setTimeout redirect
- Do NOT emit the SoftwareApplication JSON-LD (search engines will cache the downloadUrl — leak avoidance)
- Do NOT emit the install-steps grid
- Body content (adapt existing Tailwind classes for consistency):
    <h1 class="heading-serif text-4xl sm:text-5xl mb-6">Maho launches soon.</h1>
    <p class="text-muted-foreground leading-relaxed text-lg mb-8">The macOS build ships when it ships. No waitlist. Join our Discord to hear when.</p>
    <a href={discordInvite} class="inline-flex items-center justify-center rounded-full bg-foreground text-background px-6 py-3 text-sm font-medium hover:opacity-90 transition">Join Discord</a>

STATE 2 — RELEASE_LIVE === true (D-Day and after):
- Emit meta http-equiv="refresh" content={`0; url=${target}`} slot="head"
- Emit the inline JS setTimeout redirect (same as current file)
- Emit the JSONLD data block (existing `ld` object structure)
- Emit the installSteps grid (unchanged from current)
- Body content matches the current file's main structure:
    <h1>Your download is starting.</h1>
    <p>Redirecting you to the latest {productLabel} release. If nothing happens within a few seconds:</p>
    <CtaButton client:load href={target} variant="primary">Download now</CtaButton>
    ... existing install-steps grid ...
    <p class="text-xs text-muted-foreground mt-16 leading-relaxed">
        macOS 12 or later. iPhone and Android are in development — <a href={discordInvite}>join our Discord</a> for updates.
    </p>

Note the FINAL <p> in STATE 2 references Discord instead of /waitlist. This aligns with D0.4 (waitlist deleted).

DO NOT TOUCH:
- Any other file (waitlist.astro deletion is Task 3.2c)
- The `mailReleaseUrl` and `browserDownloadUrl` variables in LandingPage.tsx
- BaseLayout.astro, Nav.astro, Footer.astro
- The `ld` object schema (SoftwareApplication) — reuse but only emit conditionally

VERIFY (default env with PUBLIC_RELEASE_LIVE unset):
- `grep -c 'RELEASE_LIVE' /Users/indo/code/project/maho-workspace/website/src/pages/download.astro` → ≥ 3
- `grep -c 'discordInvite\|discord\.gg' /Users/indo/code/project/maho-workspace/website/src/pages/download.astro` → ≥ 2
- `grep -c 'Ty4J6pMdqg' /Users/indo/code/project/maho-workspace/website/src/pages/download.astro` → ≥ 1

Build test with PUBLIC_RELEASE_LIVE=false:
    cd /Users/indo/code/project/maho-workspace/website
    PUBLIC_RELEASE_LIVE=false SITE_URL=https://mahobrowser.com PUBLIC_RELAY_URL=https://relay.mahobrowser.com PUBLIC_RELEASE_REPO_URL=https://github.com/Project-Maho/release bun run build
    grep -c 'meta http-equiv="refresh"' dist/download/index.html   # expect 0
    grep -c 'launches soon\|Maho launches' dist/download/index.html   # expect ≥ 1
    grep -c 'Ty4J6pMdqg' dist/download/index.html   # expect ≥ 1

Build test with PUBLIC_RELEASE_LIVE=true:
    PUBLIC_RELEASE_LIVE=true (rest same) bun run build
    grep -c 'meta http-equiv="refresh"' dist/download/index.html   # expect 1
    grep -c 'Project-Maho/release' dist/download/index.html   # expect ≥ 2
    grep -c 'downloadUrl' dist/download/index.html   # expect ≥ 1
```
─── end ───

**Post-agy**: `lsp_diagnostics /Users/indo/code/project/maho-workspace/website/src/pages/download.astro`

---

## Task 3.2 [multi] — DELETE `/waitlist`

### Task 3.2a [agy] — Remove `/waitlist` link from `Footer.astro`

**agy-task prompt**:

─── begin ───
```
CONTEXT: Footer may contain a link to /waitlist. That route is being deleted (D0.4 zero-day-no-waitlist). Remove the link.

TASK: In /Users/indo/code/project/maho-workspace/website/src/components/Footer.astro, remove any block that contains href="/waitlist" (typically a link with visible text like "Join waitlist"). Search for the "/waitlist" string and remove the enclosing <a> element and its surrounding <li> or nav-item wrapper. Preserve the surrounding structure.

If there is no "/waitlist" reference in Footer.astro at all, this task is a no-op (still exit clean; report "no /waitlist reference found").

DO NOT TOUCH:
- Any other file
- The mailto link to hello@mahobrowser.com (email already migrated in Task 1.6a)
- The overall Footer structure

VERIFY:
- `grep -c '/waitlist' /Users/indo/code/project/maho-workspace/website/src/components/Footer.astro` → 0
```
─── end ───

### Task 3.2b [agy] — Remove `/waitlist` link from `Nav.astro`

**agy-task prompt**:

─── begin ───
```
CONTEXT: Nav may contain a link to /waitlist. Same as 3.2a — remove.

TASK: In /Users/indo/code/project/maho-workspace/website/src/components/Nav.astro, remove any anchor/link/CTA pointing at "/waitlist". Search for the "/waitlist" string; delete the enclosing element. Preserve the rest of the nav.

If no /waitlist reference exists, this is a no-op.

DO NOT TOUCH:
- Any other file
- Other nav items
- The Nav's mobile/desktop responsive structure

VERIFY:
- `grep -c '/waitlist' /Users/indo/code/project/maho-workspace/website/src/components/Nav.astro` → 0
```
─── end ───

### Task 3.2c [Sisyphus] — Delete `waitlist.astro`

Not agy — file deletion:

```bash
cd /Users/indo/code/project/maho-workspace
git rm website/src/pages/waitlist.astro
```

### Task 3.2d [agy] — Create `_redirects` file

**agy-task prompt**:

─── begin ───
```
CONTEXT: Cloudflare Pages honors a public/_redirects file for same-origin path redirects. Add a rule so /waitlist 301s to /.

TASK: Create the file /Users/indo/code/project/maho-workspace/website/public/_redirects with the following exact content (no additional lines beyond what's shown, no trailing whitespace, LF line endings, UTF-8):

# Cloudflare Pages redirects — evaluated in order, first match wins.

# D0.4: /waitlist route deleted, preserve inbound URLs via 301 to home.
/waitlist  /  301
/waitlist/ /  301

If the file already exists (unlikely but check), APPEND the two rules above at the end (idempotent — do not duplicate if already present).

DO NOT TOUCH:
- Any other file
- Any other rule that may exist in _redirects

VERIFY:
- `test -f /Users/indo/code/project/maho-workspace/website/public/_redirects` → success
- `grep -c '/waitlist' /Users/indo/code/project/maho-workspace/website/public/_redirects` → 2
- File is valid CF _redirects format (whitespace-separated columns)
```
─── end ───

### Task 3.2 verify build [Sisyphus]

```bash
cd /Users/indo/code/project/maho-workspace/website
bun run build
test ! -f dist/waitlist/index.html   # expect success (file gone)
test -f dist/_redirects              # expect success (copied through)
grep -c '/waitlist' dist/_redirects  # expect 2
grep -rn 'waitlist' dist/ | grep -v sitemap | grep -v _redirects  # expect 0
```

---

## Commit checkpoint (after Tasks 3.1 + 3.2)

```bash
git add -A website/src/components/ website/src/pages/ website/public/_redirects
git rm website/src/pages/waitlist.astro   # if not already staged
git commit -m "feat(website): zero-day download gate + delete /waitlist

Task 3.1 — download.astro:
Gated behind PUBLIC_RELEASE_LIVE build-time env var. Pre-launch state
(false, default): 'Maho launches soon' + Discord CTA, no meta-refresh,
no JSON-LD SoftwareApplication (prevents search-engine caching of a
broken downloadUrl). D-Day state (true): existing meta-refresh + JS
redirect to Project-Maho/release + install-steps grid + full JSON-LD.

Task 3.2 — /waitlist deletion:
Per D0.4 zero-day constraint. Delete page, remove Nav/Footer links,
redirect /waitlist → / 301 via public/_redirects. Relay POST /waitlist
backend endpoint stays live for legacy signup row preservation but is
no longer exposed via any UI surface after this."
```

---

# Phase 5 — Regression Verification

Run **after** every Phase 1 + Phase 3 change, and **before merging any commits to remote**.

## Task 5.1 [Sisyphus] — Prior-work regression suite

```bash
cd /Users/indo/code/project/maho-workspace/website

echo "-- Pricing consistency (from 2026-06-28 commit c01ec345) --"
test $(grep -c '\$5\|\$20' src/pages/pricing.astro src/components/LandingPage.tsx) -ge 6 && echo PASS || echo FAIL

echo "-- Terms proprietary --"
test $(grep -iEc 'open source|GPL|MIT|auditable' src/pages/terms.astro) -eq 0 && echo PASS || echo FAIL

echo "-- Live FAQ closed-source claim --"
test $(grep -c 'not open source' src/content/docs/browser/faq.mdx) -ge 1 && echo PASS || echo FAIL

echo "-- Hero H1 preserved --"
test $(grep -c 'Your AI\.' src/components/LandingPage.tsx) -ge 1 && echo PASS || echo FAIL

echo "-- /why anti-agent stance --"
test $(grep -c 'hand-on-wheel' src/pages/why.astro) -ge 1 && echo PASS || echo FAIL

echo "-- No open-source claim leaks in marketing pages --"
test $(grep -riE 'open[- ]source|View on GitHub for source' src/pages/ src/components/ | grep -v 'not open source' | wc -l) -eq 0 && echo PASS || echo FAIL

echo "-- No agent-native/AI-native language on landing --"
test $(grep -riE 'agent-native|AI-native|agent-first|agentic browser' src/pages/index.astro src/components/LandingPage.tsx src/components/landing/ | wc -l) -eq 0 && echo PASS || echo FAIL

echo "-- All Phase 1 email addresses migrated --"
test $(grep -rc 'hello@maho\.app' src/pages/ src/components/ | grep -v ':0$' | wc -l) -eq 0 && echo PASS || echo FAIL
```

Every line must print `PASS`.

## Task 5.2 [Sisyphus] — Build health

```bash
cd /Users/indo/code/project/maho-workspace/website
bun run typecheck    # exit 0
bun run build        # exit 0, ~119 pages generated
```

## Task 5.3 [Sisyphus] — Dead link scan

```bash
cd /Users/indo/code/project/maho-workspace/website
bun run build
bunx --package linkinator@6 linkinator dist/ --recurse \
     --skip 'external|localhost|github\.com|discord\.gg'
# Expect: 0 broken internal links
```

`github.com` skipped (pre-launch may 404). `discord.gg` skipped (invite links resolve dynamically).

## Task 5.4 [Sisyphus] — Trademark word-boundary audit

```bash
cd /Users/indo/code/project/maho-workspace/website

grep -rEn '\b(Arc|Zen|Dia|Comet|Atlas)\b' \
  src/pages/index.astro \
  src/pages/download.astro \
  src/pages/pricing.astro \
  src/pages/about.astro \
  src/pages/why.astro \
  src/components/LandingPage.tsx \
  src/components/landing/
```

Expected: 0 hits in commercial-copy contexts (headlines, CTA labels, section titles).

If any hit appears in a hero/CTA — flag to operator, halt commit. Editorial context in blog posts is fair use, outside this scan.

---

# Exit Criteria (Delegated Session Complete)

Delegated session may exit and hand back to operator when ALL of these are true:

- [ ] `git tag pre-website-domain-migration` exists (rollback anchor placed)
- [ ] 3 commits on `main` (domain replacement, email migration, zero-day + waitlist)
- [ ] Task 5.1 — all regression checks PASS
- [ ] Task 5.2 — `bun run typecheck` clean, `bun run build` succeeds
- [ ] Task 5.3 — linkinator 0 broken links
- [ ] Task 5.4 — trademark audit clean
- [ ] `git status website/` — clean (no unstaged changes)
- [ ] `git log --oneline main -5` shows the 3 new commits
- [ ] Operator NOT called to intervene mid-run (all decisions in this plan are pre-locked)

Delegated session output at exit:
```
=== Website Launch Prep — Complete ===
Branch:  main (3 new commits)
Tag:     pre-website-domain-migration (rollback anchor)
Commits:
  <sha1>  chore(website): correct fictional maho.app to real mahobrowser.com
  <sha2>  feat(website): migrate contact email to mahobrowser.com
  <sha3>  feat(website): zero-day download gate + delete /waitlist

Regression:  PASS (5.1) / typecheck+build PASS (5.2) / linkinator clean (5.3) / trademark clean (5.4)
Files touched: 13
Not pushed — operator to `git push origin main` after review.

Phase 2 (relay Workers port) runs as separate delegation — see:
  .hermes/plans/2026-07-08_relay-workers-rs-port.md
```

Operator picks up from here for `git push` and any subsequent work.

---

# Rollback

Before Phase 1 starts, delegated session tags: `git tag pre-website-domain-migration`

If any phase produces unrecoverable state before commit:
```bash
git checkout .   # discard unstaged
git reset --hard pre-website-domain-migration
```

If commits landed but need revert BEFORE push (still local):
```bash
git reset --hard pre-website-domain-migration
```

If commits already pushed (operator's job): use CF Pages Dashboard → Deployments → "Rollback to previous deployment" for instant site rollback; then decide whether to `git revert` the offending commits.

---

# Non-Code Followups (Operator, Not Delegated)

These are captured for operator's post-Phase-1 backlog:

1. **CF Web Analytics activation** (D0.8): Dashboard → Analytics → Web Analytics → Add Site → `mahobrowser.com`. Enable "Automatic Setup" (uses CF proxy to inject beacon). Zero code.
2. **DMARC upgrade** (calendar reminder, T+14d from 2026-07-08 = 2026-07-22): patch DMARC record `p=none` → `p=quarantine` via CF API.
3. **DMG signing pipeline**: Apple Developer Program ($99/yr), Developer ID Application cert, `codesign` + `notarytool` workflow. Blocks D-Day.
4. **`Project-Maho/release` repo prep**: verify org ownership, enable branch protection on `main`, enforce 2FA for members, ensure release binaries + SHA256 checksums in place before D-Day.
5. **D0.6 date decision**: fill in D-Day date. Runbook activation.
6. **Resend API key**: Dashboard → API Keys → Create. Stored in `.env` when relay code (Plan B) needs it.
7. **Gmail "Send Mail As" hello@mahobrowser.com** (optional): via Gmail Settings → Accounts → Send Mail As. Uses Resend SMTP with API key.

None of these block the delegated session's Phase 1 + 3 + 5 execution.

---

# Sign-off

**Author**: Sisyphus (2026-07-08)
**Status**: **READY for delegation**. All Phase 0 decisions locked. Day 1 ops verified live.
**Wall time**: ~45 minutes for delegated session.
**Scope**: Website (`website/`) codebase only. Zero touch to `maho/`, `maho-chromium/`, `mail/`, `.github/workflows/`.

**Next action** (delegated session):
```bash
cd /Users/indo/code/project/maho-workspace
git tag pre-website-domain-migration
# Then fire Batch A parallel agy tasks per "Batching guidance" section.
```

**Phase 2 (relay Workers port)** — see companion plan:
- `.hermes/plans/2026-07-08_relay-workers-rs-port.md`
- Delegate **separately** after this Plan A completes.
- Requires operator to have completed CF Workers Paid upgrade (done 2026-07-08).

**Companion strategic plan (orthogonal, in progress by another track)**:
- `.omo/plans/maho-website-improvement-plan.md` (design-language adoption W0-W7)
