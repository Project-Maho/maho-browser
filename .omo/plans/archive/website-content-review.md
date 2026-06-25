# Maho Website — Full Content Review & Improvement

## TL;DR

> **Quick Summary**: Comprehensive content audit, rewrite, and structural cleanup of the Maho Website (`website/` — Astro 6 + Starlight + React 19, 39 pages total). Resolves live-broken assets, three site-wide content contradictions, weak SEO, accessibility gaps, performance issues, and thin docs — culminating in a clean English-only pre-release marketing site.
>
> **Deliverables**:
> - All P0 broken items fixed (OG image, sitemap, /waitlist replaces /download, dead doc link, Bug Bounty stub removed)
> - Three site-wide truth contradictions resolved: closed-source positioning, free-forever pricing, Project-Maho GitHub org
> - Pre-release CTA conversion: every "Download" CTA migrated to "Join Waitlist"
> - SEO infrastructure: sitemap, JSON-LD on main pages, complete OG/Twitter tags, per-page OG image override
> - Accessibility fixes: alt-text titles, React keys, heading hierarchy validated
> - Performance: astro:assets migration, font preload, hydration directives optimized
> - Content rewrites: all 14 marketing pages + 24 docs pages + 1 blog post passing brand-voice rubric
> - Component cleanup: unused .astro components consolidated or marked TODO
>
> **Estimated Effort**: XL (50+ tasks across 9 waves)
> **Parallel Execution**: YES — 9 waves with peak concurrency 7
> **Critical Path**: Wave 0 (truth lock) → Wave 1 (P0 broken) → Wave 3 (LandingPage) → Wave Final (verification)

---

## Context

### Original Request
"프론트엔드 웹사이트 내용 전체 리뷰 및 개선 계획 수립" — Full content review and improvement plan for the frontend website.

### Interview Summary

**Confirmed scope**: ALL surfaces of `website/`
- Marketing/landing pages (14): index, mail, pricing, security, privacy, terms, compare hub + 7 compare pages
- Starlight docs (24): docs root + 7 browser + 2 mail + 7 cli + 7 api
- Blog (1): single existing post
- SEO/meta/structured data
- Accessibility · performance
- i18n consistency check (English-only operation; remove any Korean residue, verify lang attributes)

**Confirmed goals (all equal priority)**: message clarity & brand voice, conversion optimization, SEO, accuracy/currency, completeness/consistency, tech doc quality.

**Audience**: General users (browser users); docs section retains dev-friendly tone.

**Language**: English only — no bilingual build, just consistency audit.

**Output depth**: FULL — issues + concrete rewrites + structural/component refactor.

**Source-of-truth resolutions** (user-confirmed, all content rewrites depend on these):
1. **Open-source status**: PROPRIETARY (closed source). `browser/faq.mdx:99` is canonical. Remove "open-source" claims from `LandingPage.tsx`.
2. **Pricing**: Free Forever, no paid tier. `pricing.astro` is canonical. Remove "$5/mo AI Pro / Managed Pro" from `LandingPage.tsx`.
3. **Product availability**: PRE-RELEASE — waitlist collection. Replace `/download` with `/waitlist`. ALL "Download" CTAs become "Join Waitlist".
4. **GitHub org**: `https://github.com/Project-Maho/maho-browser`. Mail repo defaults to `https://github.com/Project-Maho/maho-mail` (verify during execution).
5. **Bug Bounty section**: REMOVE entirely from `security.astro:78–84`.

### Research Findings (concrete)

**P0 — live broken**:
- `/og-image.png` referenced in `BaseLayout.astro:26,30` but file does not exist → all social cards broken
- `robots.txt:3` points to `sitemap-index.xml`; `@astrojs/sitemap` is not installed
- `pricing.astro:32` `<a href="/download">` — no `download.astro` exists
- `pricing.astro:83` `/docs/onboarding/local-ai-with-opencode` — doc does not exist
- `security.astro:80` Bug Bounty section is just `Coming soon.`

**P1 — content integrity**:
- "Open-source" vs "proprietary" contradiction (LandingPage vs faq.mdx)
- "$5/mo AI Pro" tier vs "free forever" contradiction (LandingPage vs pricing.astro)
- GitHub org inconsistency: `nicepkg/maho` (browser pages) vs `aspect-apps/maho-mail` (mail/legal). Canonical now: `Project-Maho`.
- MCP tool name mismatch: `mail/mcp.mdx` lists 4 tools (`search_emails`, `get_thread`, `list_folders`, `draft_reply`); `api/*` references list 6 different tools (`list_accounts`, `list_folders`, `list_emails`, `get_email`, `search_emails`, `list_thread`)
- `mail.astro` title vs description tone mismatch
- `LandingPage.tsx:170` heroImage marked `// Fallback placeholder`

**P2 — SEO/metadata gaps**:
- No JSON-LD on index/mail/pricing/security/privacy/terms (only on compare pages)
- Missing `og:image:width`, `og:image:height`, `og:image:alt`, `twitter:site`, `og:locale`
- No per-page OG image override mechanism
- Sitemap not generated
- No analytics wired (privacy.astro has aspirational copy)
- `docs/index.mdx` description: "API and CLI reference for Maho Mail" — incorrect
- Blog post has no `description` frontmatter

**P2 — accessibility (LSP-flagged)**:
- `LandingPage.tsx:283,349,520,561` empty alt text titles
- `LandingPage.tsx:328,395` index used as React key

**P2 — performance**:
- `astro:assets` `<Image>` never used; bare `<img>` tags throughout
- Hero PNG loads as raw PNG — no WebP/AVIF, no lazy
- Google Fonts render-blocking; no font preload
- `LandingPage` (601 lines) hydrates `client:load`

**P3 — structure**:
- 4 unused standalone components: `Hero.astro`, `WhySection.astro`, `Features.astro`, `FinalCTA.astro` (LandingPage re-implements all)
- `global.css:55–68` single-post-specific CSS hack
- Footer "Changelog" → external GitHub releases (no on-site changelog)
- `terms.astro:29` and `privacy.astro:41` bare `github.com/...` text (not hyperlinked)

**P3 — content depth**:
- `mail/gui.mdx` 42 lines, `mail/mcp.mdx` 49 lines — thin
- `docs/index.mdx` 6 lines — placeholder
- Single blog post

### Metis Review (gaps addressed)

**Strategic positioning gaps surfaced**: 5 source-of-truth questions all resolved (see Interview Summary).

**Guardrails added** (Metis):
- NO visual redesign — bug-fix CSS only
- NO URL changes (existing slugs preserved; only `/download` → `/waitlist` is added)
- NO new npm dependencies except `@astrojs/sitemap` (pre-approved)
- NO Starlight sidebar / nav restructure
- NO new pages except `/waitlist`
- NO removing existing pages — expand thin pages, don't delete
- NO new blog posts (out of scope)
- NO substantive changes to legal pages — link/format fixes only
- Per-page rewrite bounded to ±30% word count
- Compare pages: facts updated, primary keyword in title/H1 PRESERVED (existing rankings)
- Docs: thin pages expanded to min 150, max 800 words
- Brand voice rubric derived from `pricing.astro` + `security.astro` BEFORE rewrites
- Wave 0 baseline capture mandatory (truth dependencies tagged on every content task)

**Edge cases surfaced** (Metis):
- Starlight pages do NOT use `BaseLayout` — they use Starlight's own `<head>` injection (frontmatter `head:` array). Two parallel SEO-fix tracks needed.
- Blog post uses `template: splash` — verify rendering with starlight-blog
- RSS feed status / 404 page / favicon / manifest.json — verify presence
- Existing search rankings: do not change compare page H1/title primary keywords

---

## Work Objectives

### Core Objective
Deliver a clean, consistent, English-only **pre-release** marketing site for Maho Browser + Mail with zero broken links, resolved content contradictions, complete SEO metadata, accessibility-clean copy, and a documented brand voice — verifiable end-to-end by automated agent QA.

### Concrete Deliverables
- `public/og-image.png` (1200×630) created
- `@astrojs/sitemap` integrated; `sitemap-index.xml` builds successfully
- `src/pages/waitlist.astro` created (replaces `/download`)
- All "Download" CTAs site-wide migrated to "Join Waitlist"
- All `nicepkg/maho` and `aspect-apps/maho-mail` references replaced with `Project-Maho` URLs
- `LandingPage.tsx` rewritten: open-source claims removed, $5/mo tiers removed, alt-text fixed, React keys fixed, hydration optimized
- `BaseLayout.astro` enriched: complete OG/Twitter tags, per-page OG image override prop
- JSON-LD added to index/mail/pricing/security via reusable component
- All 7 compare pages: facts refreshed, keyword-stable titles
- All thin docs pages expanded (min 150 / max 800 words)
- `mail/mcp.mdx` tool list reconciled with `api/*` references
- Bug Bounty section removed from `security.astro`
- Bare github.com text in `terms.astro`/`privacy.astro` hyperlinked
- `global.css` per-post CSS hack removed
- Brand voice rubric checked into `.omo/refs/brand-voice-rubric.md`
- Baseline + post-build word-count diff report saved to `.omo/evidence/`

### Definition of Done
- [ ] `cd website && bun run build` exits 0
- [ ] `dist/sitemap-index.xml` exists and lists all routes
- [ ] `curl -I https://localhost:5555/og-image.png` returns 200 (preview server) — also asserted via `ls public/og-image.png`
- [ ] Zero internal links return 404 (link-check script on `dist/`)
- [ ] No occurrences of `nicepkg/maho` or `aspect-apps/maho-mail` in `src/` or `dist/`
- [ ] No occurrences of `open-source` referring to Maho's own code (Chromium-engine references allowed)
- [ ] No occurrences of `$5/mo`, `AI Pro`, `Managed Pro` in `dist/`
- [ ] No occurrences of `Download for macOS`, `Download Maho` (replaced by waitlist verbiage) — exception: `dist/docs/**` developer build instructions if present
- [ ] No `Coming soon`, `TODO`, `FIXME`, `Fallback placeholder`, `placeholder` in `dist/`
- [ ] All 39 pages have non-empty `<title>` and `<meta name="description">`
- [ ] All marketing pages have complete OG tags (image, image:width, image:height, image:alt, locale)
- [ ] JSON-LD on index/mail/pricing/security validates via schema.org parser
- [ ] All `<img>` in `dist/` have non-empty `alt`
- [ ] Lighthouse SEO ≥ 95, A11y ≥ 90, Perf ≥ 85 on index/mail/pricing/security/compare-hub
- [ ] No page exceeds 2500 words; no marketing page below 300 words; no doc page below 150 words

### Must Have
- All 5 source-of-truth resolutions reflected consistently across every page
- All P0 live-broken items fixed
- Sitemap generation working (`@astrojs/sitemap` integrated)
- `/waitlist` page exists and is reachable from every "Download" CTA
- Project-Maho org references verified by HTTP probe (200 OK)
- Brand voice rubric document committed
- Wave 0 baseline build log captured before any code changes
- Metis guardrails enforced (no URL changes, no Starlight nav restructure, no new deps beyond sitemap)
- Truth dependencies tagged on every content task

### Must NOT Have (Guardrails)
- NO visual redesign (no color/layout/typography reinvention; bug-fix CSS only)
- NO URL changes to any existing page (canonical paths preserved; `/waitlist` is the lone new route)
- NO new npm dependencies other than `@astrojs/sitemap`
- NO Starlight `astro.config.mjs` changes beyond adding sitemap integration
- NO sidebar / nav restructure
- NO new blog posts
- NO deletion of any file in this pass — unused components are TODO-marked or wired
- NO substantive changes to legal language in `privacy.astro` / `terms.astro` (only links/formatting)
- NO compare-page title/H1 primary keyword changes (preserve existing search rankings)
- NO marketing page word count change exceeding ±30% from baseline
- NO analytics implementation (copy may mention privacy commitment; zero tracking code)
- NO redesign of `Nav.astro` / `ProductSwitcher.tsx` / `Footer.astro` structure (link target updates only)
- NO domain / DNS / hosting changes
- NO Korean content added (English-only operation)

### Spec Framework Integration

No SDD framework (`openspec/`, `.specify/`) detected in this repo. Section omitted.

---

## Verification Strategy (MANDATORY)

> **ZERO HUMAN INTERVENTION** — All verification is agent-executed.

### Test Decision
- **Infrastructure exists**: NO (Astro project; no test framework wired)
- **Automated tests**: NONE for content (subjective). Verification is build + Lighthouse + grep + Playwright + curl.
- **Framework**: N/A
- **TDD**: N/A — content review is not test-driven

### QA Policy
Every task includes agent-executed QA scenarios. Evidence saved to `.omo/evidence/task-{N}-{scenario-slug}.{ext}`.

- **Frontend/UI**: Playwright (playwright skill) — navigate dev server (`bun run dev` → :5555), assert DOM text/selectors, screenshot
- **Static asset checks**: Bash (`ls`, `curl -I`, `file`)
- **Content/grep audits**: Bash (`rg`, `wc -w`)
- **Build verification**: Bash (`bun run build`, `bun run typecheck`)
- **Schema validation**: Bash + Node (`node -e 'JSON.parse(...)'` on extracted JSON-LD blocks)
- **Lighthouse**: Bash (`bunx lighthouse http://localhost:4321/{path} --output=json --quiet --chrome-flags='--headless=new'`) on `astro preview`
- **Link check**: Bash — extract all `href`/`src` from `dist/`, HEAD-request internals against built routes

---

## Execution Strategy

### Parallel Execution Waves

```
Wave 0 (Setup + Truth Lock — must finish before Wave 1):
├── 1. Capture baseline build log + word-count snapshot
├── 2. Internal link inventory snapshot
├── 3. Truth table reference document
├── 4. Brand voice rubric extraction (pricing.astro + security.astro)
└── 5. Verify Project-Maho GitHub URLs (browser + mail) via curl

Wave 1 (P0 broken — parallel, file-independent):
├── 6.  Generate /og-image.png (1200×630)
├── 7.  Install + integrate @astrojs/sitemap
├── 8.  Create /waitlist page (replaces /download)
├── 9.  Remove Bug Bounty section from security.astro
├── 10. Fix dead /docs/onboarding link in pricing.astro
└── 11. Verify favicon, 404 page, RSS, manifest status

Wave 2 (SEO infrastructure — parallel):
├── 12. Enrich BaseLayout.astro meta (og:image:width/height/alt, twitter:site, og:locale, per-page OG override prop)
├── 13. Create reusable JSONLD.astro component + types
└── 14. Update robots.txt to match generated sitemap path

Wave 3 (LandingPage.tsx — central rewrite, single file but multi-concern):
├── 15. Rewrite browser content record (proprietary + free-forever + waitlist CTAs)
├── 16. Rewrite mail content record (same direction)
├── 17. Fix accessibility issues in LandingPage (alt titles, React keys)
├── 18. Migrate <img> to optimized loading (still <img> due to React; lazy + width/height + srcset where possible)
└── 19. Hydration directive optimization (client:visible for below-fold sub-components)

Wave 4 (Marketing pages — parallel rewrites, non-LandingPage):
├── 20. Rewrite pricing.astro (waitlist CTA, /docs/onboarding link fix or removal, FAQ refresh)
├── 21. Rewrite security.astro (post-Bug-Bounty-removal body refresh, GitHub org swap)
├── 22. Touch up privacy.astro (hyperlink github.com, fix "may use analytics" copy, GitHub org swap)
├── 23. Touch up terms.astro (hyperlink github.com, GitHub org swap)
├── 24. Reconcile mail.astro title/description tone
└── 25. Add JSON-LD to index, mail, pricing, security via JSONLD component

Wave 5 (Compare pages — 7 parallel, keyword-stable):
├── 26. Compare hub (compare/index.astro) refresh + JSON-LD audit
├── 27. compare/maho-mail-vs-superhuman.astro refresh
├── 28. compare/maho-mail-vs-thunderbird.astro refresh
├── 29. compare/maho-mail-vs-apple-mail.astro refresh
├── 30. compare/maho-mail-vs-spark.astro refresh
├── 31. compare/maho-mail-vs-shortwave.astro refresh
├── 32. compare/maho-mail-vs-himalaya.astro refresh
└── 33. compare/maho-mail-vs-aerc-mutt-neomutt.astro refresh

Wave 6 (Docs rewrites — parallel by section):
├── 34. Expand docs/index.mdx (orientation page, fix description)
├── 35. Browser docs section pass (7 docs: index + spaces + ai + sync + boosts + extensions + faq)
├── 36. Mail docs expansion + tool reconciliation (gui.mdx + mcp.mdx)
├── 37. CLI ref consistency pass (7 docs: index + 6 commands)
├── 38. API ref consistency pass (7 docs: index + 6 tools)
└── 39. Blog post metadata fix (description frontmatter, splash template verification)

Wave 7 (Site-wide swaps + a11y/perf — parallel):
├── 40. Site-wide GitHub org swap (Nav, Footer, all pages → Project-Maho)
├── 41. Site-wide Download CTA swap (any remaining outside LandingPage)
├── 42. Font preload + self-hostable evaluation in BaseLayout
└── 43. Heading hierarchy audit + fix script

Wave 8 (Cleanup):
├── 44. Unused component decision (Hero/WhySection/Features/FinalCTA — TODO-mark or wire)
├── 45. Remove single-post CSS hack from global.css
└── 46. Footer link audit (Changelog target decision)

Wave FINAL (4 parallel reviewers — ALL APPROVE before user okay):
├── F1. Plan Compliance Audit (oracle)
├── F2. Code Quality Review (unspecified-high)
├── F3. Real Manual QA (unspecified-high + playwright)
└── F4. Scope Fidelity Check (deep)
→ Present consolidated results → Wait for user okay

Critical Path: 1 → 5 → 8 → 15 → 25 → F1-F4 → user okay
Parallel Speedup: ~70% vs sequential
Max Concurrent: 7 (Wave 5 — compare pages all independent)
```

### Dependency Matrix (abbreviated — all 46 tasks present in TODOs)

- **1 (baseline)**: blocks all subsequent
- **3 (truth table)**: blocks 8, 15-39 (all content tasks reference it)
- **4 (brand rubric)**: blocks 15-39 (all content rewrites)
- **5 (Project-Maho verify)**: blocks 8, 15, 16, 20-23, 40
- **7 (sitemap)**: blocks 14
- **12 (BaseLayout meta)**: blocks 13, 25
- **13 (JSONLD component)**: blocks 25
- **15-16 (LandingPage rewrites)**: block 17, 18, 19
- **17, 18, 19 (LandingPage internals)**: block 44 (component cleanup)
- **20-25 (marketing rewrites)**: parallel within Wave 4
- **26-33 (compare pages)**: parallel within Wave 5
- **34-39 (docs)**: parallel within Wave 6
- **40 (org swap)**: depends on 5, parallel with 41-43
- **All Waves 1-8 complete**: blocks Final Wave

### Agent Dispatch Summary

- **Wave 0 (5 tasks)**: T1-T2 → `quick`, T3 → `quick`, T4 → `writing`, T5 → `quick`
- **Wave 1 (6 tasks)**: T6 → `visual-engineering`, T7 → `quick`, T8 → `unspecified-high` (waitlist page = page + form), T9 → `quick`, T10 → `quick`, T11 → `quick`
- **Wave 2 (3 tasks)**: T12 → `unspecified-high`, T13 → `unspecified-high`, T14 → `quick`
- **Wave 3 (5 tasks)**: T15-T16 → `writing` (copy-heavy), T17 → `quick`, T18 → `visual-engineering`, T19 → `unspecified-high`
- **Wave 4 (6 tasks)**: T20 → `writing`, T21 → `writing`, T22 → `quick`, T23 → `quick`, T24 → `writing`, T25 → `unspecified-high`
- **Wave 5 (8 tasks)**: T26 → `unspecified-high`, T27-T33 → `writing`
- **Wave 6 (6 tasks)**: T34 → `writing`, T35 → `writing`, T36 → `writing`, T37 → `writing`, T38 → `writing`, T39 → `quick`
- **Wave 7 (4 tasks)**: T40 → `quick`, T41 → `quick`, T42 → `unspecified-high`, T43 → `unspecified-high`
- **Wave 8 (3 tasks)**: T44 → `unspecified-high`, T45 → `quick`, T46 → `quick`
- **Final (4 tasks)**: F1 → `oracle`, F2 → `unspecified-high`, F3 → `unspecified-high` (+ playwright), F4 → `deep`

---

## TODOs

> Implementation + Test = ONE Task. EVERY task MUST have: Recommended Agent Profile + Parallelization info + QA Scenarios.

### Wave 0 — Setup & Truth Lock

- [x] 1. `.omo/evidence/baseline-build.log` + `baseline-wordcounts.json`: capture baseline build output and per-page word counts — establishes regression detection baseline

  **What to do**:
  - `cd website && bun run typecheck && bun run build 2>&1 | tee ../.omo/evidence/baseline-build.log`
  - For every `.astro` and `.mdx` in `src/pages/` and `src/content/docs/`, extract visible text content and `wc -w` it. Write JSON map of `path -> word_count` to `.omo/evidence/baseline-wordcounts.json`. Use a small Node/bun script saved to `.omo/scripts/wordcount.ts` (and reference it from later tasks).
  - Save inventory of all `<a href>` and image `src` paths to `.omo/evidence/baseline-links.json`.

  **Must NOT do**: Modify any source file. Read-only.

  **Recommended Agent Profile**:
  - **Category**: `quick` — pure data capture, no judgment
  - **Skills**: none required
  - **Reason**: Mechanical script + read-only build run

  **Parallelization**:
  - **Can Run In Parallel**: NO (must complete first)
  - **Blocks**: ALL subsequent tasks
  - **Blocked By**: None (entry point)

  **References**:
  - `website/package.json:scripts.build` — `astro check && astro build` is canonical build
  - `website/src/pages/`, `website/src/content/docs/` — content roots to enumerate

  **Acceptance Criteria**:
  - [ ] `.omo/evidence/baseline-build.log` exists, ends with success line
  - [ ] `.omo/evidence/baseline-wordcounts.json` lists ≥ 39 entries
  - [ ] `.omo/evidence/baseline-links.json` lists every internal href

  **QA Scenarios**:
  ```
  Scenario: Baseline build captured cleanly
    Tool: Bash
    Steps:
      1. cat .omo/evidence/baseline-build.log | tail -5
      2. Assert last 5 lines contain "[build] Server built" or equivalent astro success marker
      3. jq 'length' .omo/evidence/baseline-wordcounts.json
      4. Assert >= 39
    Expected Result: Build log shows success; wordcount JSON has all pages
    Failure Indicators: Build log ends with "ERROR" or wordcount JSON missing keys
    Evidence: .omo/evidence/baseline-build.log + baseline-wordcounts.json (the artifacts themselves)

  Scenario: Read-only invariant preserved
    Tool: Bash
    Steps:
      1. git status website/src
      2. Assert "nothing to commit, working tree clean" for src/
    Expected Result: No source-tree modifications
    Evidence: .omo/evidence/task-1-readonly-check.txt
  ```

  **Commit**: YES (groups with chore(website): wave 0 baseline + truth lock)
  - Files: `.omo/evidence/baseline-*.{log,json}`, `.omo/scripts/wordcount.ts`
  - Pre-commit: `cd website && bun run typecheck`

- [x] 2. `.omo/evidence/baseline-screenshots/`: Playwright snapshot of every marketing page at desktop + mobile viewport — visual baseline for regression diffs

  **What to do**:
  - Start `cd website && bun run dev` (port 5555). Wait for ready signal.
  - Use Playwright skill to navigate every marketing route: `/`, `/mail`, `/pricing`, `/security`, `/privacy`, `/terms`, `/compare`, and each `/compare/maho-mail-vs-*`
  - At each, capture full-page screenshot at desktop (1440×900) and mobile (390×844), save to `.omo/evidence/baseline-screenshots/{route-slug}-{desktop|mobile}.png`
  - Stop dev server.

  **Must NOT do**: Modify any source file.

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: `playwright`
  - **Reason**: Pure capture flow

  **Parallelization**:
  - **Can Run In Parallel**: YES (with task 1 — both read-only on different artifacts)
  - **Blocks**: F3 (final QA needs visual diff baseline)
  - **Blocked By**: None

  **References**:
  - `website/package.json:scripts.dev` — `astro dev --port 5555`
  - List of marketing routes: see Wave 0 todo description

  **Acceptance Criteria**:
  - [ ] `.omo/evidence/baseline-screenshots/` contains ≥ 14 × 2 = 28 PNGs
  - [ ] Each PNG > 0 bytes

  **QA Scenarios**:
  ```
  Scenario: All baseline screenshots captured
    Tool: Bash
    Steps:
      1. ls .omo/evidence/baseline-screenshots/*.png | wc -l
      2. Assert >= 28
      3. find .omo/evidence/baseline-screenshots/ -size 0 | wc -l
      4. Assert == 0
    Expected Result: 28+ non-empty PNGs
    Evidence: .omo/evidence/baseline-screenshots/ directory
  ```

  **Commit**: YES (with task 1)
  - Files: `.omo/evidence/baseline-screenshots/`
  - Pre-commit: file count check

- [x] 3. `.omo/refs/truth-table.md`: write the canonical truth table referenced by every content task — single source for "what's true about Maho"

  **What to do**:
  - Create `.omo/refs/truth-table.md` with sections:
    - **Open-source status**: PROPRIETARY (closed source). Maho's core engine and platform shells are proprietary. The Chromium engine underneath remains open-source upstream — copy may say "built on open-source Chromium" but MUST NOT call Maho itself open-source.
    - **Pricing**: Free Forever, no paid tier. AI uses BYOK. No subscription, no payment processor. If $5/mo / "AI Pro" / "Managed Pro" appears anywhere, it must be removed.
    - **Product availability**: PRE-RELEASE. No public download. CTAs collect waitlist signups. Use verbs: "Join the waitlist", "Get early access", "Get notified". MUST NOT use "Download for macOS" / "Download Maho" / "Download now" anywhere visible to users.
    - **GitHub canonical**: `https://github.com/Project-Maho/maho-browser` for browser; `https://github.com/Project-Maho/maho-mail` for mail (verified by task 5; falls back to user override on 404). MUST NOT use `nicepkg/maho` or `aspect-apps/maho-mail`.
    - **Bug Bounty**: Section is REMOVED from `security.astro`. Do not add a placeholder.
    - **Brand voice anchors**: `pricing.astro` and `security.astro` are the canonical voice references (see task 4).

  **Must NOT do**: Add any unstated assumptions to the truth table.

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: none
  - **Reason**: Mechanical record of decisions

  **Parallelization**:
  - **Can Run In Parallel**: YES (with tasks 1, 2, 4, 5)
  - **Blocks**: All content tasks (15-39, 40, 41)
  - **Blocked By**: None

  **References**:
  - Source-of-truth resolutions in this plan's Interview Summary
  - `website/src/content/docs/browser/faq.mdx:99` — proprietary statement
  - `website/src/pages/pricing.astro` — free-forever statement

  **Acceptance Criteria**:
  - [ ] `.omo/refs/truth-table.md` exists with all 6 sections
  - [ ] Each section has a one-sentence directive AND a "MUST NOT" clause

  **QA Scenarios**:
  ```
  Scenario: Truth table covers every resolved decision
    Tool: Bash
    Steps:
      1. test -f .omo/refs/truth-table.md
      2. for keyword in "PROPRIETARY" "Free Forever" "PRE-RELEASE" "Project-Maho/maho-browser" "Bug Bounty" "Brand voice"; do grep -q "$keyword" .omo/refs/truth-table.md || echo "MISSING: $keyword"; done
      3. Assert no MISSING output
    Expected Result: All 6 keywords found
    Evidence: .omo/evidence/task-3-truth-table-coverage.txt (output of grep run)
  ```

  **Commit**: YES (with task 1)
  - Files: `.omo/refs/truth-table.md`

- [x] 4. `.omo/refs/brand-voice-rubric.md`: extract 5 voice rules from `pricing.astro` + `security.astro` — the rubric every rewrite is checked against

  **What to do**:
  - Read `website/src/pages/pricing.astro` and `website/src/pages/security.astro` end-to-end.
  - Distill 5 rules covering: (a) sentence-length pattern, (b) tone markers (confident? understated? technical?), (c) pronoun usage (we / you / our), (d) CTA verb style (imperative? promise?), (e) heading style (noun phrases? questions? statements?). Each rule must include 1 verbatim quote from these pages as exemplar AND 1 anti-pattern of what to avoid.
  - Add a 6th meta-rule: "When in doubt, prefer the phrasing of `pricing.astro` over `LandingPage.tsx`."
  - Write to `.omo/refs/brand-voice-rubric.md`.

  **Must NOT do**: Invent new rules outside what the two anchor pages demonstrate.

  **Recommended Agent Profile**:
  - **Category**: `writing` — voice/style analysis is editorial work
  - **Skills**: `copy-editing`
  - **Reason**: Distilling style requires editorial judgment, not coding

  **Parallelization**:
  - **Can Run In Parallel**: YES (with tasks 1, 2, 3, 5)
  - **Blocks**: All content rewrites (15, 16, 20-39)
  - **Blocked By**: None

  **References**:
  - `website/src/pages/pricing.astro` (read in full)
  - `website/src/pages/security.astro` (read in full)

  **Acceptance Criteria**:
  - [ ] `.omo/refs/brand-voice-rubric.md` exists
  - [ ] Contains exactly 6 numbered rules
  - [ ] Every rule has both an exemplar quote (with file:line citation) and an anti-pattern

  **QA Scenarios**:
  ```
  Scenario: Rubric is structurally complete
    Tool: Bash
    Steps:
      1. test -f .omo/refs/brand-voice-rubric.md
      2. grep -c "^[0-9]\." .omo/refs/brand-voice-rubric.md
      3. Assert >= 6
      4. grep -c "Exemplar:" .omo/refs/brand-voice-rubric.md
      5. Assert >= 6
      6. grep -c "Anti-pattern:" .omo/refs/brand-voice-rubric.md
      7. Assert >= 6
    Expected Result: 6 rules, each with exemplar and anti-pattern
    Evidence: .omo/evidence/task-4-rubric-structure.txt
  ```

  **Commit**: YES (with task 1)
  - Files: `.omo/refs/brand-voice-rubric.md`

- [x] 5. Verify Project-Maho GitHub URLs and write `.omo/refs/repo-urls.json`: confirm canonical org URLs return 200 OR record user override

  **What to do**:
  - `curl -sI -o /dev/null -w "%{http_code}\n" https://github.com/Project-Maho/maho-browser`
  - `curl -sI -o /dev/null -w "%{http_code}\n" https://github.com/Project-Maho/maho-mail`
  - If both return 200 (or 301 + final 200), write `.omo/refs/repo-urls.json` with `{ browser: "...", mail: "...", website: "...", verified: "<date>" }`. Default `website` URL to `https://github.com/Project-Maho/maho-website` and verify the same way.
  - If any URL returns 404, record `{ browser: "...", mail: "...", website: "...", verified_status: { ...all status codes... }, action_required: "user must confirm canonical mail/website URL" }` and STOP — flag this back to user, do not proceed without correction.

  **Must NOT do**: Guess at alternative URLs. Record exact status codes.

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: none
  - **Reason**: Pure HTTP probe + JSON write

  **Parallelization**:
  - **Can Run In Parallel**: YES (with tasks 1-4)
  - **Blocks**: 8 (waitlist page references browser repo), 15, 16, 20-23, 40 (all GitHub link sites)
  - **Blocked By**: None

  **References**:
  - User answer to source-of-truth question 4: `https://github.com/Project-Maho/maho-browser`

  **Acceptance Criteria**:
  - [ ] `.omo/refs/repo-urls.json` exists
  - [ ] All probed URLs have explicit status code recorded
  - [ ] If any URL is 404, plan execution PAUSES for user override

  **QA Scenarios**:
  ```
  Scenario: All canonical URLs verified live
    Tool: Bash
    Steps:
      1. cat .omo/refs/repo-urls.json
      2. Assert browser/mail/website keys present
      3. For each URL value, run: curl -sI -o /dev/null -w "%{http_code}" "$url"
      4. Assert each returns 200 (or 301 with final 200)
    Expected Result: 3 verified URLs
    Failure Indicators: Any 404 → execution must halt with explicit user-override request
    Evidence: .omo/evidence/task-5-repo-url-checks.txt (curl output capture)

  Scenario: 404 path triggers explicit halt
    Tool: Bash
    Steps:
      1. If verified_status field exists in JSON with any 404, executor must NOT proceed to task 8.
      2. Confirm by checking task 8 evidence does not exist when this task records a 404.
    Expected Result: Halt is enforced; downstream tasks skipped until override provided
    Evidence: .omo/refs/repo-urls.json (the action_required flag)
  ```

  **Commit**: YES (with task 1)
  - Files: `.omo/refs/repo-urls.json`

---

### Wave 1 — P0 Broken Items

- [x] 6. `website/public/og-image.png`: generate 1200×630 OG image — fixes broken social cards on every marketing page

  **What to do**:
  - Generate a 1200×630 PNG OG card. Either:
    - **Option A** (preferred, no design tool): Write an Astro endpoint at `src/pages/og-image.png.ts` that uses `satori` + `@resvg/resvg-js` to render an SVG to PNG with the Maho logo, headline "Maho — Browser and Mail for modern work", and a subtle dark gradient. Pre-render at build time and copy result to `public/og-image.png`.
    - **Option B** (fallback): Hand-export a static PNG matching brand colors (`#0a0a0a` bg, white logo + heading text) and place at `public/og-image.png`.
  - Verify final asset is exactly 1200×630, < 300 KB, no transparency.

  **Must NOT do**: Use a placeholder image. Use external image hosting. Add `satori`/`resvg` as dependencies if Option B is chosen.

  **Recommended Agent Profile**:
  - **Category**: `visual-engineering` — image generation, brand-aware
  - **Skills**: none required (no UI changes)
  - **Reason**: Visual asset creation; designer-mind needed even though output is single PNG

  **Parallelization**:
  - **Can Run In Parallel**: YES (with all Wave 1 tasks)
  - **Blocks**: F1 compliance (Must Have)
  - **Blocked By**: 1 (baseline)

  **References**:
  - `website/src/layouts/BaseLayout.astro:26,30` — the references that 404 today
  - `website/src/assets/logo.svg` — logo source
  - `website/src/styles/global.css` — brand color tokens (`#0a0a0a` bg)

  **Acceptance Criteria**:
  - [ ] `website/public/og-image.png` exists
  - [ ] Dimensions exactly 1200×630
  - [ ] File size < 300 KB
  - [ ] After `bun run build`, `dist/og-image.png` exists

  **QA Scenarios**:
  ```
  Scenario: OG image asset is correct
    Tool: Bash
    Steps:
      1. file website/public/og-image.png
      2. Assert output contains "1200 x 630"
      3. stat -f%z website/public/og-image.png  (or stat -c%s on Linux)
      4. Assert < 307200 (300 KB)
      5. cd website && bun run build
      6. ls website/dist/og-image.png
    Expected Result: PNG passes dimension + size; copied to dist after build
    Evidence: .omo/evidence/task-6-og-image-meta.txt

  Scenario: OG card renders in Twitter card validator simulation
    Tool: Playwright
    Preconditions: dev server running (bun run dev)
    Steps:
      1. Navigate to http://localhost:5555/ in headless browser
      2. Read <meta property="og:image"> content attribute
      3. HEAD-request that URL relative to localhost:5555
      4. Assert HTTP 200, content-type image/png
      5. Take screenshot of OG card visible at the rendered URL
    Expected Result: og:image URL returns 200 + valid PNG
    Evidence: .omo/evidence/task-6-og-image-render.png + http-headers.txt
  ```

  **Commit**: YES (groups with fix(website): repair P0 broken assets and links)
  - Files: `website/public/og-image.png` (+ optional `src/pages/og-image.png.ts` if Option A)
  - Pre-commit: dimension check

- [x] 7. `astro.config.mjs`: add `@astrojs/sitemap` integration — fixes 404'ing sitemap referenced by robots.txt

  **What to do**:
  - `cd website && bun add @astrojs/sitemap`
  - Edit `astro.config.mjs`: import `sitemap` from `@astrojs/sitemap`, add to `integrations` array. Configure with `{ filter: (page) => !page.includes('/draft') }` (precaution).
  - Run `cd website && bun run build`
  - Verify `dist/sitemap-index.xml` and `dist/sitemap-0.xml` (or whatever astro/sitemap emits) exist and list every public route.

  **Must NOT do**: Add any other dependency. Modify Starlight config beyond integration array. Add custom sitemap routing.

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: none
  - **Reason**: Mechanical 3-line config edit

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Blocks**: 14 (robots.txt update needs sitemap path), F1
  - **Blocked By**: 1

  **References**:
  - `website/astro.config.mjs:1–34` — current integrations array (insert after `react()`)
  - `website/public/robots.txt:3` — currently expected sitemap path: `https://maho.app/sitemap-index.xml`
  - Astro sitemap docs: https://docs.astro.build/en/guides/integrations-guide/sitemap/

  **Acceptance Criteria**:
  - [ ] `package.json` has `@astrojs/sitemap` dep
  - [ ] `astro.config.mjs` imports + uses `sitemap()`
  - [ ] `cd website && bun run build` succeeds
  - [ ] `dist/sitemap-index.xml` exists and references at least one sitemap-N.xml
  - [ ] Combined sitemap entries cover ≥ 39 routes

  **QA Scenarios**:
  ```
  Scenario: Sitemap generates and lists every page
    Tool: Bash
    Steps:
      1. cd website && bun run build 2>&1 | tail -20
      2. Assert no errors
      3. ls website/dist/sitemap-index.xml
      4. cat website/dist/sitemap-0.xml | grep -c "<url>"
      5. Assert >= 39
    Expected Result: Sitemap has all routes
    Evidence: .omo/evidence/task-7-sitemap-build.txt + dist/sitemap-*.xml

  Scenario: Sitemap matches robots.txt promise
    Tool: Bash
    Steps:
      1. grep "Sitemap:" website/public/robots.txt
      2. Note the URL path
      3. Assert dist/<that-path-fragment> exists
    Expected Result: robots.txt sitemap URL resolves to a real file
    Evidence: .omo/evidence/task-7-robots-sitemap-match.txt
  ```

  **Commit**: YES (with P0 group)
  - Files: `website/astro.config.mjs`, `website/package.json`, `website/bun.lock`

- [x] 8. `website/src/pages/waitlist.astro`: create waitlist page replacing the dead `/download` link — pre-release primary conversion target

  **What to do**:
  - Create `src/pages/waitlist.astro` using `BaseLayout`. Title: `"Join the waitlist — Maho"`, description: `"Get early access to Maho — the proprietary, privacy-first browser and email client. We'll email you when builds are ready for your platform."`
  - Body sections: hero (`<h1>Get early access.</h1>` + 1-paragraph rationale + form) + below-the-fold "What you'll get" (3-bullet checklist) + "What you won't get" (no spam, no resale, ≤ 1 email/month).
  - Form: bare HTML `<form>` with `method="POST"` and a `TODO: action` comment marking where the user must wire backend (Buttondown/ConvertKit/Loops/Formspree). Inputs: email (required, type=email), platform (radio: macOS / Linux / Windows / iOS / Android — disabled-with-tooltip for unbuilt platforms), referrer (optional text).
  - The form must work as a static HTML form — no JS dependency.
  - Add `noindex` meta? **NO** — page should be indexable for SEO (Metis confirmed).
  - Add JSON-LD `WebPage` schema (skeleton only; details filled in task 25 if not done here).

  **Must NOT do**: Add a JS framework dep for form handling. Use a real backend URL (leave as TODO comment). Use `Download` verbiage anywhere on this page.

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high` — page creation + structured copy + schema markup
  - **Skills**: `copywriting`
  - **Reason**: Net-new page combining structured copy and form HTML; needs care

  **Parallelization**:
  - **Can Run In Parallel**: YES (no other Wave 1 task touches this file)
  - **Blocks**: 15, 16, 20 (other pages CTAs link here), F1
  - **Blocked By**: 3 (truth table), 4 (rubric), 5 (repo URLs for any GitHub link in page body)

  **References**:
  - `website/src/pages/pricing.astro` — voice anchor + BaseLayout usage pattern
  - `website/src/components/Nav.astro` — current nav structure (do not modify; just ensure waitlist linkable)
  - `.omo/refs/truth-table.md` — for waitlist verbiage and pre-release framing
  - `.omo/refs/brand-voice-rubric.md` — copy must pass rubric

  **Acceptance Criteria**:
  - [ ] `website/src/pages/waitlist.astro` exists
  - [ ] Page renders at `/waitlist` in dev server
  - [ ] Page has `<title>`, `<meta description>`, JSON-LD WebPage block
  - [ ] Form has email input with `required` and `type="email"`
  - [ ] Form action is a `TODO:` placeholder (commented), not a real URL
  - [ ] No occurrence of "Download" in page text (case-insensitive)
  - [ ] Word count between 200 and 600

  **QA Scenarios**:
  ```
  Scenario: Waitlist page renders and structurally correct
    Tool: Playwright
    Preconditions: cd website && bun run dev
    Steps:
      1. Navigate to http://localhost:5555/waitlist
      2. Assert document.title contains "Join the waitlist"
      3. Assert <h1> visible with text matching /early access|waitlist/i
      4. Assert <form> exists with action containing "TODO" OR commented placeholder
      5. Assert <input type="email" required> exists
      6. Take screenshot
    Expected Result: Page renders, all required elements present
    Evidence: .omo/evidence/task-8-waitlist-render.png + dom-assertions.txt

  Scenario: Form submits with empty email blocked by browser validation
    Tool: Playwright
    Steps:
      1. Click submit without entering email
      2. Assert form did not navigate (URL unchanged)
      3. Read validity state of email input — assert .validity.valueMissing === true
    Expected Result: Native HTML validation prevents empty submit
    Evidence: .omo/evidence/task-8-form-validation.png

  Scenario: No "Download" verbiage leaked
    Tool: Bash
    Steps:
      1. grep -ni "download" website/src/pages/waitlist.astro
      2. Assert no matches OR matches are inside HTML comments only
    Expected Result: Zero user-visible "download" strings
    Evidence: .omo/evidence/task-8-download-grep.txt
  ```

  **Commit**: YES (with P0 group)
  - Files: `website/src/pages/waitlist.astro`

- [x] 9. `website/src/pages/security.astro`: remove Bug Bounty section (lines ~78-84) — eliminates "Coming soon" placeholder

  **What to do**:
  - Read `website/src/pages/security.astro` end-to-end.
  - Identify the Bug Bounty `<section>` (currently containing only `Coming soon.`). Remove it cleanly: opening tag, body, closing tag, and any leading/trailing blank lines so adjacent sections stay properly spaced.
  - Verify TOC / anchor links elsewhere on the page do not reference the removed section. If they do, remove those too.
  - Verify `<title>`/`<meta description>` text does not advertise bug bounty.

  **Must NOT do**: Replace with another placeholder. Touch unrelated sections. Leave dangling links.

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: none
  - **Reason**: Surgical deletion

  **Parallelization**:
  - **Can Run In Parallel**: YES (different file from other Wave 1 tasks)
  - **Blocks**: 21 (Wave 4 security.astro full pass)
  - **Blocked By**: 1

  **References**:
  - `website/src/pages/security.astro:78–84` (or current line range — verify with read)

  **Acceptance Criteria**:
  - [ ] No `Coming soon` text in `security.astro`
  - [ ] No `Bug Bounty` heading remains
  - [ ] `cd website && bun run build` succeeds
  - [ ] Visual diff vs baseline screenshot shows only that section removed (no other layout shift)

  **QA Scenarios**:
  ```
  Scenario: Bug Bounty fully removed
    Tool: Bash
    Steps:
      1. grep -i "bug bounty" website/src/pages/security.astro
      2. Assert no matches
      3. grep -i "coming soon" website/src/pages/security.astro
      4. Assert no matches
    Expected Result: Both greps empty
    Evidence: .omo/evidence/task-9-bug-bounty-grep.txt

  Scenario: Build still succeeds and no orphan anchors
    Tool: Bash
    Steps:
      1. cd website && bun run build 2>&1 | tail -10
      2. Assert no errors
      3. grep -i 'href="#bug-bounty"' website/src/pages/security.astro
      4. Assert no matches
    Expected Result: Clean build, no dangling anchor references
    Evidence: .omo/evidence/task-9-build.txt

  Scenario: Visual diff confirms only intended removal
    Tool: Playwright
    Preconditions: dev server running
    Steps:
      1. Navigate to /security
      2. Screenshot full page
      3. Diff against .omo/evidence/baseline-screenshots/security-desktop.png
      4. Assert diff is constrained to the Bug Bounty region (other regions match)
    Evidence: .omo/evidence/task-9-security-diff.png
  ```

  **Commit**: YES (with P0 group)
  - Files: `website/src/pages/security.astro`

- [x] 10. `website/src/pages/pricing.astro`: fix dead `/docs/onboarding/local-ai-with-opencode` link — eliminates 404 in pricing FAQ

  **What to do**:
  - Read `pricing.astro` to find the FAQ entry referencing `/docs/onboarding/local-ai-with-opencode` (~line 83).
  - Two valid resolutions; pick **A** as default:
    - **A** (default): Remove the link wrapping while keeping the prose. Replace `<a href="/docs/onboarding/local-ai-with-opencode">Local AI with OpenCode</a>` with plain text `Local AI with OpenCode (guide coming soon)` — no broken hyperlink.
    - **B** (only if executor verifies during execution that an `onboarding` doc category is planned within Wave 6): Create `src/content/docs/onboarding/local-ai-with-opencode.mdx` as a minimum 150-word guide and keep the link.
  - Default to **A** unless 36 (mail docs expansion) explicitly requests **B** — keep scope contained.

  **Must NOT do**: Leave the broken link as-is.

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: none
  - **Reason**: Targeted single-line edit

  **Parallelization**:
  - **Can Run In Parallel**: YES (file is also touched by 20, but 10 is a tiny fix; 20 is full rewrite — sequence: 10 first, then 20 builds on cleaner state)
  - **Blocks**: 20 (depends on this fix being in)
  - **Blocked By**: 1

  **References**:
  - `website/src/pages/pricing.astro:~83`
  - `website/src/content/docs/` — verify no `onboarding/` directory exists (confirms link is dead)

  **Acceptance Criteria**:
  - [ ] No `/docs/onboarding/local-ai-with-opencode` href in `pricing.astro`
  - [ ] FAQ section reads cleanly without dangling link

  **QA Scenarios**:
  ```
  Scenario: Dead link removed from pricing
    Tool: Bash
    Steps:
      1. grep -n "onboarding/local-ai-with-opencode" website/src/pages/pricing.astro
      2. Assert no matches
    Evidence: .omo/evidence/task-10-pricing-link.txt

  Scenario: Pricing page still renders correctly
    Tool: Playwright
    Steps:
      1. Navigate to /pricing
      2. Assert FAQ section renders
      3. Find the FAQ entry that previously held the dead link; assert it is now plain text containing "OpenCode"
      4. Click any link in the FAQ section; assert no 404
    Evidence: .omo/evidence/task-10-pricing-faq.png
  ```

  **Commit**: YES (with P0 group)
  - Files: `website/src/pages/pricing.astro`

- [x] 11. Audit favicon, 404 page, RSS feed, manifest.json status — surfaces and locks down peripheral artifacts before deeper passes

  **What to do**:
  - Verify presence/absence of each:
    - **Favicon**: check `BaseLayout.astro:17` references `/logo.svg` (already does); confirm `public/logo.svg` exists. Decision: KEEP as-is (no action).
    - **404 page**: check `src/pages/404.astro` — if missing, this is a P1 gap, file a follow-up task in Wave 8.
    - **manifest.json**: check `public/manifest.json` — if missing, OK for now, no action.
    - **RSS feed**: check whether `starlight-blog` plugin emits `/blog/rss.xml`. Run `cd website && bun run build && ls dist/blog/`. Document result.
  - Write findings to `.omo/refs/peripheral-artifacts-audit.md` with explicit decisions per item: KEEP / FIX-IN-WAVE-8 / DEFER.
  - If 404 page is missing, append a Wave 8 task entry to plan: "Create `src/pages/404.astro`".

  **Must NOT do**: Create new pages or assets in this task. Audit only + writeup.

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: none
  - **Reason**: Read-only audit + decision capture

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Blocks**: Wave 8 task 46 (Footer link audit may consume 404 finding)
  - **Blocked By**: 1

  **References**:
  - `website/src/layouts/BaseLayout.astro:17` — favicon
  - `website/src/pages/` — check for 404.astro
  - `website/public/` — check for manifest.json
  - `website/astro.config.mjs:23–28` — starlight-blog config (`rss: true`)

  **Acceptance Criteria**:
  - [ ] `.omo/refs/peripheral-artifacts-audit.md` exists with 4 sections
  - [ ] Each section has explicit decision label

  **QA Scenarios**:
  ```
  Scenario: Audit doc complete
    Tool: Bash
    Steps:
      1. test -f .omo/refs/peripheral-artifacts-audit.md
      2. for item in "Favicon" "404 page" "manifest.json" "RSS feed"; do grep -q "$item" .omo/refs/peripheral-artifacts-audit.md || echo "MISSING: $item"; done
      3. Assert no MISSING output
    Evidence: .omo/evidence/task-11-audit.txt

  Scenario: RSS feed presence verified by build
    Tool: Bash
    Steps:
      1. cd website && bun run build > /dev/null 2>&1
      2. find website/dist -name "*.xml" -path "*/blog/*"
      3. Document presence/absence in audit doc
    Evidence: .omo/evidence/task-11-rss-presence.txt
  ```

  **Commit**: YES (with P0 group)
  - Files: `.omo/refs/peripheral-artifacts-audit.md`

---

### Wave 2 — SEO Infrastructure

- [x] 12. `website/src/layouts/BaseLayout.astro`: enrich meta tags + add per-page OG image override prop — fills gaps in OG/Twitter card completeness

  **What to do**:
  - Add to BaseLayout `Props` type: `image?: string` (defaults to `/og-image.png`), `imageAlt?: string` (defaults to `"Maho — Browser and Mail for modern work"`).
  - Add inside `<head>`:
    - `<meta property="og:image:width" content="1200">`
    - `<meta property="og:image:height" content="630">`
    - `<meta property="og:image:alt" content={imageAlt}>`
    - `<meta property="og:locale" content="en_US">`
    - `<meta name="twitter:site" content="@maho_app">` (placeholder handle — flag in `.omo/refs/social-handles-todo.md` if real handle differs)
    - `<meta name="twitter:image:alt" content={imageAlt}>`
  - Replace existing hard-coded `/og-image.png` with `{Astro.site}{image}` resolution so per-page override works.
  - All marketing pages (index, mail, pricing, security, privacy, terms, compare/*) gain ability to pass custom `image` prop.
  - Do NOT override og:image on each page yet — that's task 25.

  **Must NOT do**: Change `<title>`, `<description>`, or canonical URL logic. Touch Starlight pages (different head mechanism).

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
  - **Skills**: none
  - **Reason**: Layout-level edit with prop-type changes; needs care to not break callers

  **Parallelization**:
  - **Can Run In Parallel**: YES (with 13, 14)
  - **Blocks**: 25 (per-page OG override usage), 27-33 (compare pages can use override)
  - **Blocked By**: 1

  **References**:
  - `website/src/layouts/BaseLayout.astro:1–40` — current Props + head structure
  - All marketing page `Astro.props` callers

  **Acceptance Criteria**:
  - [ ] BaseLayout Props type includes optional `image` and `imageAlt`
  - [ ] All 6 new meta tags present in built HTML for index page
  - [ ] No marketing page errors after build (props are optional)
  - [ ] Twitter handle TODO recorded in `.omo/refs/social-handles-todo.md`

  **QA Scenarios**:
  ```
  Scenario: All new meta tags present in built HTML
    Tool: Bash
    Steps:
      1. cd website && bun run build
      2. for tag in 'og:image:width' 'og:image:height' 'og:image:alt' 'og:locale' 'twitter:site' 'twitter:image:alt'; do grep -q "$tag" website/dist/index.html || echo "MISSING: $tag"; done
      3. Assert no MISSING output
    Evidence: .omo/evidence/task-12-meta-tags.txt

  Scenario: Per-page override works
    Tool: Bash
    Steps:
      1. Temporarily edit one page (pricing.astro) to pass image="/test-override.png" prop
      2. Build
      3. grep og:image website/dist/pricing/index.html
      4. Assert /test-override.png present
      5. Revert the test edit
    Evidence: .omo/evidence/task-12-override.txt
  ```

  **Commit**: YES (groups with feat(website/seo): enrich meta tags and add JSON-LD)
  - Files: `website/src/layouts/BaseLayout.astro`, `.omo/refs/social-handles-todo.md`

- [x] 13. `website/src/components/JSONLD.astro`: create reusable JSON-LD component — enables structured data on all marketing pages without duplication

  **What to do**:
  - Create `website/src/components/JSONLD.astro` accepting a `data` prop typed as `Record<string, unknown>` (or `unknown[]` for `@graph` array).
  - Body emits `<script type="application/ld+json" set:html={JSON.stringify(data)}></script>` (use `set:html` to avoid HTML-escaping JSON quotes).
  - Add type guards: ensure `@context` and `@type` exist; otherwise log warning at build time.
  - Provide JSDoc with usage examples for: `Organization` (index), `SoftwareApplication` (index/mail), `WebSite` (index), `WebPage` (waitlist/security/privacy/terms), `Product` (pricing).
  - Add unit-style smoke check in `src/components/__JSONLD-test.astro` (build-time only; delete after task 25 if unused) that imports JSONLD with a sample payload.

  **Must NOT do**: Add JSON-LD to actual pages in this task — that's task 25. Add npm dep for schema validation.

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
  - **Skills**: none
  - **Reason**: New reusable component with type contracts

  **Parallelization**:
  - **Can Run In Parallel**: YES (with 12, 14)
  - **Blocks**: 25, 26 (compare hub schema augmentation), 27-33 (compare pages can use it)
  - **Blocked By**: 1

  **References**:
  - `website/src/pages/compare/maho-mail-vs-superhuman.astro:32–61` — example existing inline JSON-LD (showing the @graph pattern)
  - https://schema.org/SoftwareApplication, https://schema.org/Organization, https://schema.org/Product

  **Acceptance Criteria**:
  - [ ] `website/src/components/JSONLD.astro` exists
  - [ ] Importable in any `.astro` page
  - [ ] Smoke test page imports + renders with no build errors

  **QA Scenarios**:
  ```
  Scenario: Component renders valid JSON
    Tool: Bash
    Steps:
      1. cd website && bun run build
      2. Find the smoke test page output
      3. Extract <script type="application/ld+json"> content
      4. Pipe to: node -e 'JSON.parse(process.argv[1])' "$content"
      5. Assert exit code 0
    Evidence: .omo/evidence/task-13-jsonld-parse.txt

  Scenario: Component refuses invalid input gracefully
    Tool: Bash
    Steps:
      1. Temporarily pass missing-@context payload
      2. Build → assert warning logged but build still succeeds
    Evidence: .omo/evidence/task-13-warn.txt
  ```

  **Commit**: YES (with SEO group)
  - Files: `website/src/components/JSONLD.astro`

- [x] 14. `website/public/robots.txt`: align robots.txt sitemap path with actual generated filename

  **What to do**:
  - After task 7 builds the sitemap, inspect `dist/` for the canonical filename (likely `sitemap-index.xml`).
  - Update `public/robots.txt` line 3 to match exactly. Add `User-agent`-specific rules if needed (default `Allow: /` is fine).
  - Optional addition: explicit `Disallow:` for `/draft/` if drafts ever appear (preventive, no harm).

  **Must NOT do**: Add `Disallow: /` (would deindex site). Add wildcards that conflict with Astro routing.

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: none

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Blocks**: F1
  - **Blocked By**: 7 (sitemap must build first)

  **References**: `website/public/robots.txt`, `website/dist/sitemap-index.xml` (post-task-7)

  **Acceptance Criteria**:
  - [ ] `robots.txt` Sitemap URL matches actual built file path
  - [ ] No deindexing rules added

  **QA Scenarios**:
  ```
  Scenario: robots.txt sitemap URL resolves
    Tool: Bash
    Steps:
      1. SITEMAP_URL=$(grep -i "Sitemap:" website/public/robots.txt | awk '{print $2}')
      2. SITEMAP_PATH=$(echo "$SITEMAP_URL" | sed 's|https://maho.app||')
      3. test -f "website/dist${SITEMAP_PATH}"
    Evidence: .omo/evidence/task-14-robots-resolve.txt
  ```

  **Commit**: YES (with SEO group)
  - Files: `website/public/robots.txt`

---

### Wave 3 — LandingPage.tsx Rewrite (Central)

- [x] 15. `website/src/components/LandingPage.tsx`: rewrite **browser** content record per truth table — removes open-source claims, $5/mo tier, Download CTAs

  **What to do**:
  - Read `LandingPage.tsx:67–161` (browser content record) end-to-end first.
  - Apply ALL of the following:
    - Subhead `LandingPage.tsx:70–72`: replace "An open-source Chromium fork…" with phrasing that frames the engine as Chromium (open-source upstream) but Maho itself as a built product. Suggested rewrite: `"A privacy-first Chromium-based browser with a native AppKit sidebar, project-based Spaces, built-in LLM capabilities, split view, and ad-blocking. Highly customizable and synced across your devices."`
    - Primary CTA `LandingPage.tsx:72`: change `"Download for macOS"` → `"Join the waitlist"` linking to `/waitlist`.
    - Secondary CTA `LandingPage.tsx:73`: keep "View on GitHub" but ensure URL uses Project-Maho/maho-browser per task 5.
    - WhySection `LandingPage.tsx:78–80` body: replace "Brave-style overlay" technical description with a user-facing sentence that doesn't claim open source. New: `"Maho patches Chromium with a Brave-style overlay to replace standard window layouts with persistent Spaces. The result: a browser that respects the way you actually work, not the way browser tabs were designed in 2008."`
    - Pricing section `LandingPage.tsx:127–155`: REMOVE the entire two-tier "AI Pro" $5/mo card structure. Replace with a single free panel: `"Maho is free. AI uses your own keys (BYOK)."` + `"Read the pricing page →"` link to `/pricing`.
    - Final CTA `LandingPage.tsx:158–160`: subhead replace `"It's free, private, and open source."` → `"Free, private, and built for the long haul."`. Button text `"Download for macOS"` → `"Join the waitlist"` linking to `/waitlist`.
  - Run rubric self-check: read `.omo/refs/brand-voice-rubric.md`, score the rewrite against each rule (1-5 each); minimum total 24/30 to pass.
  - Word count for the entire `content.browser` record must remain within ±30% of baseline (`.omo/evidence/baseline-wordcounts.json`).

  **Must NOT do**: Touch the React component machinery (sub-components, hooks, prop interfaces). Touch the `content.mail` record (that's task 16). Add new sections. Use "Download" anywhere user-visible.

  **Recommended Agent Profile**:
  - **Category**: `writing` — copy-heavy editorial work
  - **Skills**: `copywriting`, `copy-editing`
  - **Reason**: Rewrite under brand-voice constraints with measurable success criteria

  **Parallelization**:
  - **Can Run In Parallel**: YES (with 16 — different content keys in same file BUT same file means careful merge — recommend executor takes both 15 and 16 in same session OR uses git stash discipline)
  - **Blocks**: 17, 18, 19, 25, F1
  - **Blocked By**: 3 (truth table), 4 (rubric), 5 (repo URL), 8 (waitlist URL exists)

  **References**:
  - `website/src/components/LandingPage.tsx:67–161`
  - `.omo/refs/truth-table.md`
  - `.omo/refs/brand-voice-rubric.md`
  - `.omo/refs/repo-urls.json`

  **Acceptance Criteria**:
  - [ ] No `open-source` text in browser content record (Chromium qualifier OK)
  - [ ] No `$5`, `AI Pro`, `Managed Pro`, `Pro plan` text in browser record
  - [ ] No `Download for macOS`, `Download Maho` in browser record
  - [ ] All buttons in browser record link to `/waitlist` for primary OR `/pricing` for "see pricing" OR Project-Maho repo for GitHub
  - [ ] Word count diff vs baseline within ±30%
  - [ ] Rubric self-check rationale recorded in PR description / commit body

  **QA Scenarios**:
  ```
  Scenario: Forbidden strings absent from rendered browser landing
    Tool: Playwright
    Preconditions: dev server running
    Steps:
      1. Navigate to http://localhost:5555/
      2. Assert page text does NOT contain (case-insensitive): "open-source" referring to Maho, "$5", "AI Pro", "Managed Pro", "Download for macOS", "Download Maho"
      3. Assert primary hero CTA text matches /join the waitlist/i
      4. Click primary CTA → assert URL changes to /waitlist
    Evidence: .omo/evidence/task-15-browser-render.png + assertions.txt

  Scenario: Word count within bound
    Tool: Bash
    Steps:
      1. Re-run wordcount script on /
      2. Compute % diff vs baseline
      3. Assert |diff| <= 30%
    Evidence: .omo/evidence/task-15-wordcount-diff.txt
  ```

  **Commit**: YES (groups with feat(website): rewrite LandingPage for pre-release positioning)
  - Files: `website/src/components/LandingPage.tsx`

- [x] 16. `website/src/components/LandingPage.tsx`: rewrite **mail** content record per truth table — same direction as task 15 applied to mail product

  **What to do**:
  - Read `LandingPage.tsx:163–255` (mail content record).
  - Apply same patterns as task 15:
    - Hero CTA: "Download Maho Mail" → "Join the waitlist" → `/waitlist`
    - Pricing block: remove "Managed Pro" $5/mo. Single free panel.
    - Open-source claims about mail: remove. Mail can be described as "private, local-first" without "open-source".
    - Final CTA: "Download" → "Join the waitlist".
    - GitHub URL: `Project-Maho/maho-mail` per `.omo/refs/repo-urls.json` (or fallback per task 5).
    - Replace `heroImage: "/assets/images/command-bar-mockup-live-2.png" // Fallback placeholder` (LandingPage.tsx:170) — either confirm this image is final and remove the comment, OR specify a different image. Default: keep image, remove comment, document choice in commit body.
  - Same rubric self-check, same ±30% word count constraint.

  **Must NOT do**: Touch browser record. Touch React component machinery. Use "Download" anywhere user-visible.

  **Recommended Agent Profile**:
  - **Category**: `writing`
  - **Skills**: `copywriting`, `copy-editing`

  **Parallelization**:
  - **Can Run In Parallel**: WITH CARE (same file as 15; recommend serial OR same-session)
  - **Blocks**: 17, 18, 19, 25, F1
  - **Blocked By**: 3, 4, 5, 8

  **References**: same as task 15, but for mail content range

  **Acceptance Criteria**: identical structure to task 15, applied to `/mail` route

  **QA Scenarios**:
  ```
  Scenario: Forbidden strings absent from /mail
    Tool: Playwright
    Steps:
      1. Navigate to /mail
      2. Same assertions as task 15 scenario 1, but on mail route
    Evidence: .omo/evidence/task-16-mail-render.png

  Scenario: heroImage placeholder comment resolved
    Tool: Bash
    Steps:
      1. grep -n "Fallback placeholder" website/src/components/LandingPage.tsx
      2. Assert no matches
    Evidence: .omo/evidence/task-16-placeholder.txt
  ```

  **Commit**: YES (with Wave 3 group)
  - Files: `website/src/components/LandingPage.tsx`

- [x] 17. `website/src/components/LandingPage.tsx`: fix accessibility — empty alt-text titles + index-as-key

  **What to do**:
  - Lines 283, 349, 520, 561 — `title=""` (or equivalent empty alt) flagged by LSP. Either remove the empty `title` attribute (if truly decorative) or fill with descriptive text matching the image content. Decision rule: if image is a screenshot, set `title` to "[feature name] screenshot"; if decorative SVG/icon, remove the attribute.
  - Lines 328, 395 — replace `key={index}` with a stable key derived from item content (e.g., `key={item.title}` or `key={item.slug}`). If items lack a stable id, hash the title string.
  - Run `cd website && bun run typecheck` — must report zero errors on LandingPage.tsx.

  **Must NOT do**: Restructure components. Change rendered output beyond the targeted attributes/keys. Add `key` plus other prop changes.

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: none

  **Parallelization**:
  - **Can Run In Parallel**: NO (same file as 15, 16; sequence after them)
  - **Blocks**: F2 (code quality)
  - **Blocked By**: 15, 16

  **References**: LSP diagnostics on `website/src/components/LandingPage.tsx`

  **Acceptance Criteria**:
  - [ ] LSP reports zero errors on LandingPage.tsx
  - [ ] No `key={index}` patterns remain in file

  **QA Scenarios**:
  ```
  Scenario: Typecheck clean
    Tool: Bash
    Steps:
      1. cd website && bun run typecheck 2>&1 | tee /tmp/tc.log
      2. grep -i "LandingPage.tsx.*error" /tmp/tc.log
      3. Assert no matches
    Evidence: .omo/evidence/task-17-typecheck.txt

  Scenario: No index-as-key remaining
    Tool: Bash
    Steps:
      1. grep -nE "key=\{(index|i|idx)\}" website/src/components/LandingPage.tsx
      2. Assert no matches
    Evidence: .omo/evidence/task-17-keys.txt
  ```

  **Commit**: YES (with Wave 3 group)
  - Files: `website/src/components/LandingPage.tsx`

- [x] 18. `website/src/components/LandingPage.tsx`: optimize `<img>` tags — explicit width/height + lazy loading + meaningful alt

  **What to do**:
  - Find every `<img>` in `LandingPage.tsx`. For each:
    - Add `width` and `height` attributes (read PNG dimensions via `file public/assets/images/X.png` or `identify`); these prevent CLS.
    - Add `loading="lazy"` UNLESS the image is above-the-fold (hero image at line ~74 is above-fold and should NOT be lazy-loaded).
    - Add meaningful `alt` text — describe what the image shows, not "image of..." (e.g., `"Maho browser sidebar showing project Spaces"`).
  - Do NOT migrate to `astro:assets <Image>` — this is a React component, mixing is error-prone. Document the decision: "React component — use plain <img> with width/height; native browser handles optimization for static PNGs."

  **Must NOT do**: Add `astro:assets` imports inside React component (will fail). Skip width/height (causes CLS regression). Use generic alt text.

  **Recommended Agent Profile**:
  - **Category**: `visual-engineering`
  - **Skills**: none
  - **Reason**: Image optimization + meaningful alt requires visual literacy

  **Parallelization**:
  - **Can Run In Parallel**: NO (same file)
  - **Blocks**: F2, F3 (Lighthouse perf)
  - **Blocked By**: 15, 16, 17

  **References**:
  - `website/public/assets/images/` — actual PNG dimensions to copy in
  - Lighthouse "image elements have explicit width and height" rule

  **Acceptance Criteria**:
  - [ ] Every `<img>` in LandingPage.tsx has `width`, `height`, `alt`, and (where appropriate) `loading="lazy"`
  - [ ] Above-fold hero `<img>` does NOT have `loading="lazy"`
  - [ ] No `alt=""` (empty alt is OK only for purely decorative images — none expected here)

  **QA Scenarios**:
  ```
  Scenario: All images have dimensions
    Tool: Bash
    Steps:
      1. grep -c "<img " website/src/components/LandingPage.tsx
      2. Save count as N
      3. grep -cE 'width="[0-9]+"' website/src/components/LandingPage.tsx
      4. Assert >= N
      5. grep -cE 'height="[0-9]+"' website/src/components/LandingPage.tsx
      6. Assert >= N
    Evidence: .omo/evidence/task-18-img-dims.txt

  Scenario: Lighthouse CLS improvement
    Tool: Bash + Playwright
    Steps:
      1. After task 18, run lighthouse on /
      2. Compare CLS score vs baseline (task 1's build doesn't include Lighthouse — capture pre-task-18 score in this task's preflight)
      3. Assert post-task CLS <= pre-task CLS
    Evidence: .omo/evidence/task-18-lighthouse-cls.json
  ```

  **Commit**: YES (with Wave 3 group)
  - Files: `website/src/components/LandingPage.tsx`

- [x] 19. `website/src/pages/index.astro` + `website/src/pages/mail.astro`: hydration directive optimization — `client:visible` for below-fold

  **What to do**:
  - LandingPage hydrates `client:load` (601 lines, the entire component). Inspect `index.astro:11` and `mail.astro:11`.
  - LandingPage as a whole stays `client:load` IF it has interactive elements above the fold (verify by reading the component structure). Most likely the hero CTAs are static buttons (just `<a>` tags) — in that case, change to `client:visible` to defer hydration until user scrolls into view OR `client:idle` for low-priority hydration.
  - Decision rule: if hero contains a React-state-dependent element (e.g., copy that updates on click), keep `client:load`. Otherwise, switch to `client:idle` (hydrates on browser idle, doesn't block FCP).
  - Verify Nav remains `client:load` (theme toggle + product switcher are interactive immediately).

  **Must NOT do**: Set entire page to `client:only` (breaks SEO — content won't render server-side). Set Nav to `client:visible` (toggle wouldn't work on first render).

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
  - **Skills**: none
  - **Reason**: Trade-off between hydration cost and interactivity needs careful read

  **Parallelization**:
  - **Can Run In Parallel**: YES (different files than 17, 18)
  - **Blocks**: F3 (Lighthouse perf)
  - **Blocked By**: 15, 16

  **References**:
  - `website/src/pages/index.astro:11`, `website/src/pages/mail.astro:11`
  - Astro client directives docs

  **Acceptance Criteria**:
  - [ ] LandingPage uses `client:idle` or `client:visible` (justification recorded in commit body)
  - [ ] Nav remains `client:load`
  - [ ] No interactive element broken (Playwright verification)

  **QA Scenarios**:
  ```
  Scenario: Lighthouse Total Blocking Time improvement
    Tool: Bash + Playwright
    Steps:
      1. bunx lighthouse http://localhost:4321/ --quiet --output=json --output-path=/tmp/post-task-19.json --chrome-flags='--headless=new'
      2. Compare TBT vs pre-task-19 baseline
      3. Assert post <= pre
    Evidence: .omo/evidence/task-19-lighthouse-tbt.json

  Scenario: Interactive elements still work
    Tool: Playwright
    Steps:
      1. Navigate to /
      2. Wait until network idle
      3. Click theme toggle in nav → assert dark/light class flips on <html>
      4. Click ProductSwitcher → assert URL changes
      5. Click hero waitlist CTA → assert navigation to /waitlist
    Evidence: .omo/evidence/task-19-interactive.png
  ```

  **Commit**: YES (with Wave 3 group)
  - Files: `website/src/pages/index.astro`, `website/src/pages/mail.astro`

---

### Wave 4 — Marketing Pages (Non-LandingPage)

- [x] 20. `website/src/pages/pricing.astro`: full rewrite — apply truth table (waitlist CTA), apply rubric, JSON-LD ready

  **What to do**:
  - Read pricing.astro end-to-end. Apply:
    - Replace `<a href="/download">Download Maho</a>` (line ~32) with waitlist CTA: `<a href="/waitlist">Join the waitlist</a>`.
    - Update FAQ section to reflect pre-release status: at least one FAQ entry should clarify "When can I download Maho?" → answer references waitlist.
    - GitHub references → Project-Maho per `.omo/refs/repo-urls.json`.
    - Voice rubric self-check pass.
    - Word count within ±30%.
  - Do NOT add new sections. Do NOT remove the "Free, For everyone, Forever" hero.

  **Must NOT do**: Introduce paid tiers in copy. Touch the pricing math/structure (it's already truth-correct).

  **Recommended Agent Profile**:
  - **Category**: `writing`
  - **Skills**: `copywriting`, `copy-editing`

  **Parallelization**:
  - **Can Run In Parallel**: YES (with 21, 22, 23, 24, 25)
  - **Blocks**: F1, F3
  - **Blocked By**: 3, 4, 5, 8, 10 (dead link already removed)

  **References**: `website/src/pages/pricing.astro`, `.omo/refs/truth-table.md`, `.omo/refs/brand-voice-rubric.md`

  **Acceptance Criteria**:
  - [ ] No `/download` href in `pricing.astro`
  - [ ] FAQ has at least one entry addressing pre-release / waitlist
  - [ ] No `nicepkg` or `aspect-apps` references
  - [ ] Word count within ±30%

  **QA Scenarios**:
  ```
  Scenario: Pricing page reflects pre-release
    Tool: Playwright
    Steps:
      1. Navigate to /pricing
      2. Assert no element with text matching /^Download/i
      3. Assert at least one CTA links to /waitlist
      4. Open FAQ accordion entries; assert at least one mentions "waitlist" or "early access"
    Evidence: .omo/evidence/task-20-pricing.png

  Scenario: Forbidden strings absent
    Tool: Bash
    Steps:
      1. grep -niE 'download|nicepkg|aspect-apps' website/src/pages/pricing.astro
      2. Assert no user-visible matches (HTML comments OK)
    Evidence: .omo/evidence/task-20-grep.txt
  ```

  **Commit**: YES (groups with feat(website): refresh marketing pages with truth-table copy)
  - Files: `website/src/pages/pricing.astro`

- [x] 21. `website/src/pages/security.astro`: post-Bug-Bounty body refresh — voice rubric pass + GitHub org swap

  **What to do**:
  - Read post-task-9 state (Bug Bounty already removed).
  - Sweep for `nicepkg` / `aspect-apps` references → replace with Project-Maho per `.omo/refs/repo-urls.json`.
  - Apply rubric. The current security.astro is a brand-voice anchor — light edits only. If word count hasn't changed > ±30%, accept as-is.
  - Verify external audit references (Cure53, etc.) are factually unchanged. Do NOT modify factual claims about audits.

  **Must NOT do**: Restructure sections. Re-add Bug Bounty. Change audit/legal claims.

  **Recommended Agent Profile**:
  - **Category**: `writing`
  - **Skills**: `copy-editing`

  **Parallelization**:
  - **Can Run In Parallel**: YES (with 20, 22, 23, 24, 25)
  - **Blocks**: F1
  - **Blocked By**: 3, 4, 5, 9 (Bug Bounty removal)

  **References**: `website/src/pages/security.astro`, `.omo/refs/truth-table.md`, `.omo/refs/repo-urls.json`

  **Acceptance Criteria**:
  - [ ] No `nicepkg` / `aspect-apps` references in security.astro
  - [ ] Audit / Cure53 statements unchanged (verify by diff)
  - [ ] No Bug Bounty heading
  - [ ] Word count within ±30%

  **QA Scenarios**:
  ```
  Scenario: GitHub org swap clean
    Tool: Bash
    Steps:
      1. grep -nE 'nicepkg|aspect-apps' website/src/pages/security.astro
      2. Assert no matches
      3. grep -c 'Project-Maho' website/src/pages/security.astro
      4. Assert >= 1
    Evidence: .omo/evidence/task-21-org.txt
  ```

  **Commit**: YES (with Wave 4 group)
  - Files: `website/src/pages/security.astro`

- [x] 22. `website/src/pages/privacy.astro`: hyperlink bare github.com text + fix "may use analytics" copy + GitHub org swap

  **What to do**:
  - Line 41: bare `github.com/aspect-apps` text → wrap in `<a href="https://github.com/Project-Maho/maho-browser">github.com/Project-Maho/maho-browser</a>` (or mail equivalent depending on context).
  - Line 26 (or near): "may use privacy-respecting analytics to understand traffic patterns" — since no analytics is wired (truth confirmed), rewrite as: `"This site does not currently use any analytics or third-party tracking. If we add analytics in the future, we will update this page and choose a privacy-respecting provider that does not set cookies or sell data."`
  - Sweep for any other `nicepkg`/`aspect-apps` → Project-Maho.
  - DO NOT modify legal language elsewhere (data subject rights, GDPR clauses, etc.).

  **Must NOT do**: Add legal claims that aren't already there. Modify data-handling promises. Touch GDPR/CCPA sections.

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: none

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Blocks**: F1
  - **Blocked By**: 3, 5

  **References**: `website/src/pages/privacy.astro`, `.omo/refs/repo-urls.json`

  **Acceptance Criteria**:
  - [ ] All `github.com/...` text in privacy.astro is wrapped in `<a href="https://...">`
  - [ ] No `nicepkg` / `aspect-apps`
  - [ ] No "may use analytics" hedge text — replaced with definitive statement
  - [ ] Word count within ±30%

  **QA Scenarios**:
  ```
  Scenario: All github.com refs hyperlinked
    Tool: Bash
    Steps:
      1. grep -nE '(?<!href=")github\.com' website/src/pages/privacy.astro || true
      2. Assert all matches are inside <a> elements (manual review of grep context)
    Evidence: .omo/evidence/task-22-links.txt

  Scenario: Analytics hedge fixed
    Tool: Bash
    Steps:
      1. grep -i "may use" website/src/pages/privacy.astro
      2. Assert no matches
    Evidence: .omo/evidence/task-22-analytics.txt
  ```

  **Commit**: YES (with Wave 4 group)
  - Files: `website/src/pages/privacy.astro`

- [x] 23. `website/src/pages/terms.astro`: hyperlink bare github.com text + GitHub org swap

  **What to do**:
  - Line 29: bare `github.com/aspect-apps` → wrap in `<a>`. Replace org with Project-Maho.
  - No substantive legal language changes.

  **Must NOT do**: Modify license/warranty/limitation-of-liability clauses.

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: none

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Blocks**: F1
  - **Blocked By**: 3, 5

  **References**: `website/src/pages/terms.astro`

  **Acceptance Criteria**:
  - [ ] All `github.com` text wrapped in `<a>` with `https://` prefix
  - [ ] No `nicepkg` / `aspect-apps`
  - [ ] Word count within ±30%

  **QA Scenarios**:
  ```
  Scenario: terms.astro linkified and org-correct
    Tool: Bash
    Steps:
      1. grep -nE 'github\.com' website/src/pages/terms.astro
      2. Manually verify each match is inside an <a href> element
      3. grep -E 'nicepkg|aspect-apps' website/src/pages/terms.astro
      4. Assert no matches
    Evidence: .omo/evidence/task-23-terms.txt
  ```

  **Commit**: YES (with Wave 4 group)
  - Files: `website/src/pages/terms.astro`

- [x] 24. `website/src/pages/mail.astro`: reconcile title/description tone — single coherent positioning

  **What to do**:
  - Current: title "One client. Three interfaces." vs description "Desktop app, CLI, and MCP server in one email client."
  - Pick one direction. Recommended: keep evocative title; rewrite description to share rhythm but add value-prop. New: `"Maho Mail is the first email client with a desktop app, CLI, and MCP server. Three interfaces, one inbox — built for developers and AI agents."`
  - Verify the description fits OG/Twitter card character limits (≤ 200 chars recommended; ≤ 160 best).

  **Must NOT do**: Change LandingPage props (handled in task 16). Modify the page body — props only.

  **Recommended Agent Profile**:
  - **Category**: `writing`
  - **Skills**: `copywriting`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Blocks**: F1
  - **Blocked By**: 3, 4, 16

  **References**: `website/src/pages/mail.astro:7–9`

  **Acceptance Criteria**:
  - [ ] Title and description share a coherent voice (verified via rubric self-check)
  - [ ] Description ≤ 200 chars

  **QA Scenarios**:
  ```
  Scenario: Description coherence
    Tool: Bash
    Steps:
      1. grep -E "title=|description=" website/src/pages/mail.astro
      2. Manually read both — assert tone matches
      3. wc -c < <(grep -oE 'description="[^"]+"' website/src/pages/mail.astro | head -1)
      4. Assert <= 220 (allows for "description="" wrapper)
    Evidence: .omo/evidence/task-24-mail-meta.txt

  Scenario: Built HTML reflects new meta
    Tool: Bash
    Steps:
      1. cd website && bun run build
      2. grep "<meta name=\"description\"" website/dist/mail/index.html
      3. Assert content matches new copy
    Evidence: .omo/evidence/task-24-built.txt
  ```

  **Commit**: YES (with Wave 4 group)
  - Files: `website/src/pages/mail.astro`

- [x] 25. Add JSON-LD schemas to index, mail, pricing, security via `<JSONLD>` component — fills the structured data gap

  **What to do**:
  - For each page, add a `<JSONLD data={...} />` import + invocation:
    - `index.astro`: `WebSite` (with `potentialAction: SearchAction` if site search exists; otherwise omit) + `Organization` (Maho — with `logo`, `sameAs: [github URL]`)
    - `mail.astro`: `SoftwareApplication` (Maho Mail — with `applicationCategory: "Communication"`, `operatingSystem: "macOS"`, `offers: { price: "0", priceCurrency: "USD" }`)
    - `pricing.astro`: `Product` (with `offers` listing $0 free tier)
    - `security.astro`: `WebPage` with `breadcrumb`
  - Validate every payload with a Node parse: `node -e "JSON.parse(...)"`.
  - Run https://validator.schema.org/ equivalent locally: extract built `<script type="application/ld+json">` blocks and validate via offline schema-checker library if available; otherwise document validation as manual step in QA scenario.

  **Must NOT do**: Add JSON-LD to compare pages (they already have it). Add fictional sameAs URLs. Add reviews/ratings (no real data).

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
  - **Skills**: none
  - **Reason**: Schema.org structures + 4 pages

  **Parallelization**:
  - **Can Run In Parallel**: YES (different pages)
  - **Blocks**: F1, F3
  - **Blocked By**: 12, 13, 15, 16, 20, 21

  **References**: `website/src/components/JSONLD.astro`, schema.org docs, `website/src/pages/compare/maho-mail-vs-superhuman.astro:32–61` (existing pattern)

  **Acceptance Criteria**:
  - [ ] All 4 pages have `<JSONLD>` element
  - [ ] Each emits parseable JSON
  - [ ] No invented data (URLs verified live, prices match free-forever truth)

  **QA Scenarios**:
  ```
  Scenario: JSON-LD parses on all 4 pages
    Tool: Bash
    Steps:
      1. cd website && bun run build
      2. for path in index mail pricing security; do
           node -e "const html=require('fs').readFileSync('website/dist/$path/index.html','utf8'); const m=html.match(/<script type=\"application\\/ld\\+json\">([\\s\\S]*?)<\\/script>/g); if(!m) throw new Error('no ld+json on $path'); m.forEach(s=>JSON.parse(s.replace(/<\\/?script[^>]*>/g,'')));"
         done
      3. Assert no errors
    Evidence: .omo/evidence/task-25-jsonld-parse.txt

  Scenario: Truth-table compliance in schema
    Tool: Bash
    Steps:
      1. grep -A20 'application/ld\\+json' website/dist/pricing/index.html | grep -E '"price":\\s*"0"'
      2. Assert match (free price reflected)
    Evidence: .omo/evidence/task-25-pricing-schema.txt
  ```

  **Commit**: YES (with Wave 4 group)
  - Files: `website/src/pages/index.astro`, `website/src/pages/mail.astro`, `website/src/pages/pricing.astro`, `website/src/pages/security.astro`

---

### Wave 5 — Compare Pages (7 parallel, keyword-stable)

> **Critical guardrail**: Preserve primary keyword in title and H1 of every compare page. These pages may rank for "[product] vs Maho" queries — title/H1 keyword changes risk ranking loss.
> Pattern for each: facts current as of execution date, GitHub org swap, "Download" CTAs → waitlist, voice rubric pass, ±30% word count, JSON-LD already present (audit only).

- [x] 26. `website/src/pages/compare/index.astro`: compare hub refresh — voice + JSON-LD audit + CTA swap

  **What to do**:
  - Read the compare hub. Refresh body copy per rubric. Title and primary H1 **MUST NOT** change.
  - Existing `CollectionPage` + `BreadcrumbList` JSON-LD: verify it parses and reflects current 7 compare pages (no missing/extra).
  - Bottom CTA "Make email programmable" → swap any "Download" verbiage to waitlist.
  - GitHub org swap.

  **Must NOT do**: Change page title or H1 keywords. Add/remove compare entries.

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
  - **Skills**: `copy-editing`

  **Parallelization**:
  - **Can Run In Parallel**: YES (with 27-33)
  - **Blocks**: F1, F3
  - **Blocked By**: 3, 4, 5, 13

  **References**: `website/src/pages/compare/index.astro`

  **Acceptance Criteria**:
  - [ ] Title unchanged
  - [ ] H1 keyword tokens unchanged
  - [ ] CTA links to /waitlist
  - [ ] No `nicepkg` / `aspect-apps`
  - [ ] JSON-LD lists exactly 7 compare pages

  **QA Scenarios**:
  ```
  Scenario: Title and H1 stable
    Tool: Bash
    Steps:
      1. Compare title attribute pre/post via git diff: git diff website/src/pages/compare/index.astro | grep -E '^[-+].*title=|^[-+].*<h1'
      2. Assert no - lines OR matching + lines (i.e., either unchanged or trivial whitespace)
    Evidence: .omo/evidence/task-26-stability.txt
  ```

  **Commit**: YES (groups with feat(website): refresh compare pages with current facts)
  - Files: `website/src/pages/compare/index.astro`

- [x] 27. `website/src/pages/compare/maho-mail-vs-superhuman.astro`: refresh — facts current, keyword-stable, CTA swap, voice rubric

  **What to do**: Apply the shared compare-page procedure (see block below) to `maho-mail-vs-superhuman.astro`.

- [x] 28. `website/src/pages/compare/maho-mail-vs-thunderbird.astro`: refresh — same procedure

  **What to do**: Apply the shared compare-page procedure to `maho-mail-vs-thunderbird.astro`.

- [x] 29. `website/src/pages/compare/maho-mail-vs-apple-mail.astro`: refresh — same procedure

  **What to do**: Apply the shared compare-page procedure to `maho-mail-vs-apple-mail.astro`.

- [x] 30. `website/src/pages/compare/maho-mail-vs-spark.astro`: refresh — same procedure

  **What to do**: Apply the shared compare-page procedure to `maho-mail-vs-spark.astro`.

- [x] 31. `website/src/pages/compare/maho-mail-vs-shortwave.astro`: refresh — same procedure

  **What to do**: Apply the shared compare-page procedure to `maho-mail-vs-shortwave.astro`.

- [x] 32. `website/src/pages/compare/maho-mail-vs-himalaya.astro`: refresh — same procedure

  **What to do**: Apply the shared compare-page procedure to `maho-mail-vs-himalaya.astro`.

- [x] 33. `website/src/pages/compare/maho-mail-vs-aerc-mutt-neomutt.astro`: refresh — same procedure

  **What to do**: Apply the shared compare-page procedure to `maho-mail-vs-aerc-mutt-neomutt.astro`.

#### Shared procedure for tasks 27-33

  **What to do (per page)**:
  - Read full page. Identify factual claims about the competitor that may have aged (e.g., "Superhuman costs $30/mo" — verify still accurate as of execution date via a quick web check).
  - Update facts where stale. Document changes in commit body.
  - GitHub org swap to Project-Maho.
  - "Download" CTAs → waitlist.
  - Voice rubric pass.
  - Title and primary H1 keywords UNCHANGED. Year reference in title (e.g., "2026") may be updated only if currently misleading.
  - JSON-LD `Article` + `FAQPage` + `BreadcrumbList` audit: parse, ensure dates/URLs current.
  - Word count within ±30% of baseline for that page.

  **Must NOT do**: Change competitor's name, change comparison structure (table → bullets etc.), reorder sections, change H1 keyword tokens, restructure the FAQPage entries (search ranking risk).

  **Recommended Agent Profile**:
  - **Category**: `writing`
  - **Skills**: `copywriting`, `copy-editing`, `competitor-alternatives`

  **Parallelization**:
  - **Can Run In Parallel**: YES — all 7 are independent files
  - **Blocks**: F1, F3
  - **Blocked By**: 3, 4, 5, 13

  **References**: respective compare page; `.omo/refs/truth-table.md`; `.omo/refs/brand-voice-rubric.md`

  **Acceptance Criteria (per page)**:
  - [ ] Title primary keyword tokens unchanged (year token may update)
  - [ ] H1 unchanged
  - [ ] No "Download" CTAs (replaced with waitlist)
  - [ ] No `nicepkg` / `aspect-apps`
  - [ ] JSON-LD parses valid
  - [ ] Word count within ±30%
  - [ ] Stale fact log written to `.omo/evidence/task-{N}-fact-changes.md` (empty file OK if nothing stale)

  **QA Scenarios (per page)**:
  ```
  Scenario: Page renders, CTAs swap, keyword stable
    Tool: Playwright
    Preconditions: dev server
    Steps:
      1. Navigate to /compare/maho-mail-vs-{competitor}
      2. Assert <h1> textContent matches baseline (whitespace tolerance)
      3. Assert no link text matching /^Download/i
      4. Assert at least one link to /waitlist
      5. Take screenshot
    Evidence: .omo/evidence/task-{N}-compare-{slug}.png

  Scenario: JSON-LD parses
    Tool: Bash
    Steps:
      1. cd website && bun run build
      2. node -e "JSON.parse(...)" on each ld+json block
    Evidence: .omo/evidence/task-{N}-jsonld.txt

  Scenario: Stale-fact verification
    Tool: Bash + web check
    Steps:
      1. Read .omo/evidence/task-{N}-fact-changes.md
      2. For each entry, manually verify the new claim against a current authoritative source linked in the file
    Evidence: .omo/evidence/task-{N}-fact-changes.md
  ```

  **Commit**: YES (with compare group)
  - Files: respective compare page

---

### Wave 6 — Docs Rewrites (parallel by section)

- [x] 34. `website/src/content/docs/index.mdx`: expand from 6-line stub to a real orientation page

  **What to do**:
  - Current content is just `# Maho Documentation` + `Welcome to the Maho documentation.` Description frontmatter incorrectly says "API and CLI reference for Maho Mail" (covers browser too).
  - Rewrite frontmatter `description` to reflect docs scope: `"Documentation for Maho Browser and Maho Mail — guides, CLI reference, MCP API."`
  - Body: expand to 200-400 words covering: a one-line product positioning, 3-section overview (Browser docs / Mail docs / API ref), and a "where to start" callout linking to the most-clicked entry points (browser/index, mail/gui, cli/index).
  - Use Starlight's CardGrid component if appropriate (already available as Starlight built-in — no new dep).

  **Must NOT do**: Restructure the Starlight sidebar. Add new doc categories. Use Korean.

  **Recommended Agent Profile**:
  - **Category**: `writing`
  - **Skills**: `copywriting`

  **Parallelization**:
  - **Can Run In Parallel**: YES (with 35-39)
  - **Blocks**: F1
  - **Blocked By**: 3, 4

  **References**: `website/src/content/docs/index.mdx`, Starlight component docs (CardGrid)

  **Acceptance Criteria**:
  - [ ] Word count between 200 and 400
  - [ ] Description frontmatter accurate
  - [ ] Builds without errors
  - [ ] Internal links resolve

  **QA Scenarios**:
  ```
  Scenario: Docs index orientation works
    Tool: Playwright
    Steps:
      1. Navigate to /
      2. Hmm — Starlight docs root is the index of docs. Verify by reading routing in built dist/.
      3. Assert page renders with the new description in <meta>
      4. Assert links to /browser/, /mail/gui, /cli/ all resolve (200)
    Evidence: .omo/evidence/task-34-docs-index.png
  ```

  **Commit**: YES (groups with docs(website): expand thin docs and reconcile MCP tool list)
  - Files: `website/src/content/docs/index.mdx`

- [x] 35. Browser docs section pass (7 files: `index.mdx`, `spaces.mdx`, `ai.mdx`, `sync.mdx`, `boosts.mdx`, `extensions.mdx`, `faq.mdx`)

  **What to do**:
  - Read each file. Apply truth-table sweep: any `nicepkg`, `aspect-apps`, "open-source" claims about Maho's own code, "$5/mo" references → fix per truth table.
  - Specifically `faq.mdx:99` is the canonical "proprietary" statement — leave it as-is, it's the source of truth.
  - For each file, run rubric self-check; verify word count between 150-800.
  - `boosts.mdx`, `extensions.mdx`: ensure code samples are still accurate (URL pattern syntax, MV3 reference).

  **Must NOT do**: Change the proprietary claim in faq.mdx. Add new doc files in this section. Restructure sidebar.

  **Recommended Agent Profile**:
  - **Category**: `writing`
  - **Skills**: `copywriting`, `copy-editing`

  **Parallelization**:
  - **Can Run In Parallel**: YES (with 34, 36-39)
  - **Blocks**: F1
  - **Blocked By**: 3, 4, 5

  **References**: `website/src/content/docs/browser/*.mdx`

  **Acceptance Criteria**:
  - [ ] All 7 files pass truth-table sweep
  - [ ] Each file 150-800 words
  - [ ] `faq.mdx` proprietary statement unchanged

  **QA Scenarios**:
  ```
  Scenario: Truth sweep clean
    Tool: Bash
    Steps:
      1. for f in website/src/content/docs/browser/*.mdx; do grep -nE 'nicepkg|aspect-apps|\\$5/mo' "$f"; done
      2. Assert no matches
    Evidence: .omo/evidence/task-35-browser-docs.txt

  Scenario: Word counts in range
    Tool: Bash
    Steps:
      1. for f in website/src/content/docs/browser/*.mdx; do wc -w < "$f"; done
      2. Assert each value in [150, 800]
    Evidence: .omo/evidence/task-35-wordcounts.txt
  ```

  **Commit**: YES (with docs group)
  - Files: 7 browser docs files

- [x] 36. `website/src/content/docs/mail/gui.mdx` + `website/src/content/docs/mail/mcp.mdx`: expand thin docs + reconcile MCP tool list with `api/*`

  **What to do**:
  - **gui.mdx (currently 42 lines)**: expand to 250-500 words. Add: brief intro paragraph, screenshot placeholder reference (use existing `command-bar-mockup-live.png` or similar from `public/assets/images/`), keyboard shortcuts table (already there — verify accurate), account setup walkthrough, offline behavior section. Use Starlight's `Aside` component for tips/warnings.
  - **mcp.mdx (currently 49 lines)**: expand similarly. **Critical**: reconcile tool names with `api/*` references. Currently lists `search_emails`, `get_thread`, `list_folders`, `draft_reply` (4 tools). The `api/` ref lists `list_accounts`, `list_folders`, `list_emails`, `get_email`, `search_emails`, `list_thread` (6 different tools).
    - **Resolution**: Use the `api/` list as canonical (it's more granular and matches CLI command structure). Update `mcp.mdx` to match: 6 tools, same names. Add cross-links from each tool mention to its `/api/{tool-slug}` reference page.
  - Both files: rubric pass, word count 150-800.

  **Must NOT do**: Invent new tools that don't exist in the actual MCP server. Restructure the tool list to favor a different naming.

  **Recommended Agent Profile**:
  - **Category**: `writing`
  - **Skills**: `copywriting`, `copy-editing`

  **Parallelization**:
  - **Can Run In Parallel**: YES (with 34, 35, 37-39)
  - **Blocks**: F1, F3
  - **Blocked By**: 3, 4

  **References**: `website/src/content/docs/mail/gui.mdx`, `mail/mcp.mdx`, `website/src/content/docs/api/*.mdx`

  **Acceptance Criteria**:
  - [ ] gui.mdx 250-500 words
  - [ ] mcp.mdx 250-500 words
  - [ ] Tool names in mcp.mdx are exactly: `list_accounts`, `list_folders`, `list_emails`, `get_email`, `search_emails`, `list_thread`
  - [ ] Each tool mention links to `/api/{tool-slug}`

  **QA Scenarios**:
  ```
  Scenario: MCP tool names reconciled
    Tool: Bash
    Steps:
      1. for tool in list_accounts list_folders list_emails get_email search_emails list_thread; do grep -q "$tool" website/src/content/docs/mail/mcp.mdx || echo "MISSING $tool"; done
      2. Assert no MISSING output
      3. for tool in get_thread draft_reply; do grep -q "$tool" website/src/content/docs/mail/mcp.mdx && echo "FORBIDDEN $tool present"; done
      4. Assert no FORBIDDEN output
    Evidence: .omo/evidence/task-36-mcp-tools.txt

  Scenario: Cross-links resolve
    Tool: Playwright
    Steps:
      1. Navigate to /mail/mcp
      2. For each tool name, click its link
      3. Assert lands on /api/{tool-slug} page
    Evidence: .omo/evidence/task-36-crosslinks.png
  ```

  **Commit**: YES (with docs group)
  - Files: `website/src/content/docs/mail/gui.mdx`, `website/src/content/docs/mail/mcp.mdx`

- [x] 37. CLI ref consistency pass (7 files: `cli/index.mdx`, `accounts.mdx`, `folders.mdx`, `list.mdx`, `get.mdx`, `search.mdx`, `thread.mdx`)

  **What to do**:
  - Read each. Verify command syntax, flag descriptions, example outputs are consistent across files. Specifically: command names (`maho accounts`, `maho list`, etc.) should match the canonical `cli/index.mdx` table.
  - Truth table sweep (no GitHub org / open-source / pricing residue).
  - Each file 150-800 words. If under 150, expand with one usage example.

  **Must NOT do**: Invent CLI flags that don't exist. Add commands beyond the canonical 6.

  **Recommended Agent Profile**:
  - **Category**: `writing`
  - **Skills**: `copywriting`, `copy-editing`

  **Parallelization**:
  - **Can Run In Parallel**: YES (with 34-36, 38-39)
  - **Blocks**: F1
  - **Blocked By**: 3, 4

  **References**: `website/src/content/docs/cli/*.mdx`

  **Acceptance Criteria**:
  - [ ] All 7 files word count 150-800
  - [ ] Command names match `cli/index.mdx` table
  - [ ] Truth-table sweep clean

  **QA Scenarios**:
  ```
  Scenario: Command names consistent
    Tool: Bash
    Steps:
      1. Extract command names from cli/index.mdx table (canonical)
      2. For each command, verify the corresponding cli/{cmd}.mdx exists and has matching name in <h1>
    Evidence: .omo/evidence/task-37-cli-consistency.txt
  ```

  **Commit**: YES (with docs group)
  - Files: 7 CLI docs files

- [x] 38. API ref consistency pass (7 files: `api/index.mdx`, `list-accounts.mdx`, `list-folders.mdx`, `list-emails.mdx`, `get-email.mdx`, `search-emails.mdx`, `list-thread.mdx`)

  **What to do**:
  - Read each. Verify MCP tool names + parameter schemas match the actual MCP server contract (cross-ref `mail/mcp.mdx` post-task-36).
  - Each file: input schema, output schema, example invocation, error cases.
  - Truth-table sweep.
  - Word count 150-800.

  **Must NOT do**: Invent parameters not in the actual server. Change tool names away from canonical.

  **Recommended Agent Profile**:
  - **Category**: `writing`
  - **Skills**: `copywriting`, `copy-editing`

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Blocks**: F1
  - **Blocked By**: 3, 4, 36 (mcp.mdx canonical)

  **References**: `website/src/content/docs/api/*.mdx`, `website/src/content/docs/mail/mcp.mdx`

  **Acceptance Criteria**:
  - [ ] All 7 files word count 150-800
  - [ ] Tool names exactly match canonical (list_accounts, list_folders, list_emails, get_email, search_emails, list_thread)
  - [ ] Each tool ref has both input + output schema sections

  **QA Scenarios**:
  ```
  Scenario: Tool ref completeness
    Tool: Bash
    Steps:
      1. for f in website/src/content/docs/api/{list-accounts,list-folders,list-emails,get-email,search-emails,list-thread}.mdx; do
           grep -q -i "input\\|parameters\\|arguments" "$f" || echo "MISSING input section: $f"
           grep -q -i "output\\|response\\|returns" "$f" || echo "MISSING output section: $f"
         done
      2. Assert no MISSING output
    Evidence: .omo/evidence/task-38-api-completeness.txt
  ```

  **Commit**: YES (with docs group)
  - Files: 7 API docs files

- [x] 39. Blog post metadata fix + splash template verification

  **What to do**:
  - `website/src/content/docs/blog/agentic-browser-comparison-2026.mdx`: add `description:` frontmatter (currently missing). Use first ~150 chars of post body or a custom summary.
  - Verify `template: splash` renders correctly with starlight-blog: build the site, navigate to `/blog/agentic-browser-comparison-2026`, screenshot, confirm date + author + tags display.
  - If splash strips blog-specific metadata (date, tags) from rendered output, change template to default OR remove `template: splash` line.
  - Apply truth-table sweep to post body.

  **Must NOT do**: Change post date, change facts/comparisons in body (this is a published post — minor proofreading only).

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: none

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Blocks**: F1
  - **Blocked By**: 3

  **References**: `website/src/content/docs/blog/agentic-browser-comparison-2026.mdx`, starlight-blog plugin

  **Acceptance Criteria**:
  - [ ] `description` frontmatter present
  - [ ] Post page renders with date + author + tags visible
  - [ ] No `nicepkg`, no `aspect-apps`
  - [ ] No "open-source Maho" claims (Chromium open-source qualifier OK)

  **QA Scenarios**:
  ```
  Scenario: Blog post renders complete metadata
    Tool: Playwright
    Steps:
      1. Navigate to /blog/agentic-browser-comparison-2026
      2. Assert visible date, author name, tag pills
      3. Read <meta name="description"> from <head>
      4. Assert non-empty
    Evidence: .omo/evidence/task-39-blog-render.png

  Scenario: Truth sweep clean
    Tool: Bash
    Steps:
      1. grep -nE 'nicepkg|aspect-apps' website/src/content/docs/blog/*.mdx
      2. Assert no matches
    Evidence: .omo/evidence/task-39-blog-truth.txt
  ```

  **Commit**: YES (with docs group)
  - Files: `website/src/content/docs/blog/agentic-browser-comparison-2026.mdx`

---

### Wave 7 — Site-wide Swaps + a11y/Performance

- [x] 40. Site-wide GitHub org swap (Nav.astro, Footer.astro, any remaining files)

  **What to do**:
  - `cd website && rg -l 'nicepkg|aspect-apps' src/` — list every remaining file referencing the old orgs after Waves 3-6.
  - For each, replace with Project-Maho per `.omo/refs/repo-urls.json`. Differentiate browser vs mail repo per surrounding context.
  - Specifically check: `Nav.astro:20` (GitHub link in nav), `Footer.astro:14,17,31`, `LandingPage.tsx` already done by 15-16, all legal pages done by 22-23, all marketing done by 20-21.

  **Must NOT do**: Replace org references inside historical commit messages, AGENTS.md, or any file outside `website/`.

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: none

  **Parallelization**:
  - **Can Run In Parallel**: YES (with 41-43)
  - **Blocks**: F1
  - **Blocked By**: 5, 15-39 (any file might be touched)

  **References**: `.omo/refs/repo-urls.json`

  **Acceptance Criteria**:
  - [ ] `rg 'nicepkg|aspect-apps' website/src/ website/dist/` returns zero matches
  - [ ] All replacements use correct repo (browser vs mail)

  **QA Scenarios**:
  ```
  Scenario: Org references cleaned site-wide
    Tool: Bash
    Steps:
      1. cd website && bun run build
      2. cd .. && rg 'nicepkg|aspect-apps' website/src/ website/dist/
      3. Assert exit code != 0 (no matches)
    Evidence: .omo/evidence/task-40-org-sweep.txt
  ```

  **Commit**: YES (groups with chore(website): site-wide GitHub org and CTA swaps)
  - Files: any remaining files post-sweep

- [x] 41. Site-wide Download CTA swap — final sweep for any remaining "Download" verbiage outside LandingPage

  **What to do**:
  - `cd website && rg -i 'download for|download maho|download now' src/` — list every remaining match.
  - For each, replace with waitlist verbiage per truth table. Exception: `dist/docs/**` build instructions for developers (e.g., "git clone and build" — those are not user-facing CTAs, leave as-is).
  - Verify no nav element links to `/download` anymore.

  **Must NOT do**: Replace "download" inside user-facing developer docs about building the project from source (rare but exists).

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: none

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Blocks**: F1
  - **Blocked By**: 8 (waitlist exists), 15-39 (most CTAs already swapped in their respective tasks)

  **References**: `.omo/refs/truth-table.md`

  **Acceptance Criteria**:
  - [ ] `rg -i 'download for|download maho|download now' website/src/` returns only matches inside HTML comments OR developer-build instructions

  **QA Scenarios**:
  ```
  Scenario: User-visible Download CTAs eliminated
    Tool: Playwright
    Steps:
      1. Start dev server. For each route in baseline-links inventory, navigate.
      2. Assert no <a> or <button> with text matching /^Download/i (case-insensitive whole-text match)
    Evidence: .omo/evidence/task-41-download-sweep.txt + screenshots per page
  ```

  **Commit**: YES (with Wave 7 group)
  - Files: any remaining

- [x] 42. `BaseLayout.astro`: font preload + self-hostable evaluation

  **What to do**:
  - Add `<link rel="preload" href="https://fonts.gstatic.com/s/instrumentserif/v..." as="font" type="font/woff2" crossorigin>` for Instrument Serif italic+regular. Find exact woff2 URLs from current Google Fonts CSS response.
  - Add `crossorigin="anonymous"` to first `<link rel="preconnect" href="https://fonts.googleapis.com">` (currently missing).
  - Optionally: evaluate self-hosting. If executor finds time, download Instrument Serif woff2 to `public/fonts/`, swap CDN reference for local. If not, document as future work in `.omo/refs/font-todo.md`.

  **Must NOT do**: Change font family. Add a new font.

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
  - **Skills**: none
  - **Reason**: Font loading is performance-sensitive

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Blocks**: F3 (Lighthouse perf)
  - **Blocked By**: 12 (BaseLayout edits land first)

  **References**: `website/src/layouts/BaseLayout.astro:32–34`

  **Acceptance Criteria**:
  - [ ] preload link tag emitted for Instrument Serif woff2
  - [ ] First preconnect has `crossorigin`
  - [ ] Lighthouse "preload key requests" advisory reduced

  **QA Scenarios**:
  ```
  Scenario: Preload tag present
    Tool: Bash
    Steps:
      1. cd website && bun run build
      2. grep "rel=\"preload\".*font/woff2" website/dist/index.html
      3. Assert match
    Evidence: .omo/evidence/task-42-preload.txt

  Scenario: Lighthouse perf no regression, ideally improved
    Tool: Bash
    Steps:
      1. Run lighthouse on / before and after task 42
      2. Assert post-perf >= pre-perf
    Evidence: .omo/evidence/task-42-perf.json
  ```

  **Commit**: YES (with Wave 7 group)
  - Files: `website/src/layouts/BaseLayout.astro`

- [x] 43. Heading hierarchy audit + fix — no skipped levels site-wide

  **What to do**:
  - Build site (`cd website && bun run build`).
  - Write a small Node script `.omo/scripts/heading-hierarchy.mjs` that, for each `dist/**/index.html`, parses headings (h1-h6) in document order and reports any skip (e.g., h2 → h4 with no h3).
  - For each violation, fix at source. Common fixes: demote (`h4` → `h3`) or insert intermediate.
  - Edge case: Starlight emits its own h1 in many docs; verify mdx files don't manually add a second h1 (would be a hierarchy break).

  **Must NOT do**: Change heading text content. Restructure sections.

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
  - **Skills**: none

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Blocks**: F2 (a11y in code quality), F3 (Lighthouse a11y)
  - **Blocked By**: 15-39 (all content rewrites stable)

  **References**: WCAG 1.3.1 + Lighthouse "heading order" rule

  **Acceptance Criteria**:
  - [ ] Script reports zero violations across all `dist/**/index.html`
  - [ ] No mdx file has more than one `# ` heading (Starlight handles the page h1 from frontmatter)

  **QA Scenarios**:
  ```
  Scenario: Heading hierarchy clean
    Tool: Bash
    Steps:
      1. cd website && bun run build
      2. node .omo/scripts/heading-hierarchy.mjs
      3. Assert exit code 0 and report shows 0 violations
    Evidence: .omo/evidence/task-43-headings.txt
  ```

  **Commit**: YES (with Wave 7 group)
  - Files: `.omo/scripts/heading-hierarchy.mjs`, any source files needing fixes

---

### Wave 8 — Cleanup

- [x] 44. Decide unused component fate: `Hero.astro`, `WhySection.astro`, `Features.astro`, `FinalCTA.astro`

  **What to do**:
  - Verify each is truly unused: `cd website && rg -l 'Hero\\.astro|WhySection\\.astro|Features\\.astro|FinalCTA\\.astro' src/` — should match only the component files themselves.
  - Verify nothing in git's last 30 days indicates active development on them: `git log --oneline --since='30 days ago' -- website/src/components/{Hero,WhySection,Features,FinalCTA}.astro`
  - **Decision rule**:
    - If unused AND no recent commits → leave files in place; add a `<!-- TODO: this component is currently unused. Decide consolidation vs deletion in a follow-up. -->` comment at top of each. Do NOT delete (per Metis guardrail).
    - If recent commits exist → leave files completely alone. Active work in another branch/session.
  - Document decision in `.omo/refs/unused-components-decision.md`.

  **Must NOT do**: Delete files. Modify the components' rendered output.

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
  - **Skills**: none

  **Parallelization**:
  - **Can Run In Parallel**: YES (with 45, 46)
  - **Blocks**: F4 (scope fidelity)
  - **Blocked By**: 15, 16 (LandingPage rewrites might wire one of them — task 44 must run AFTER to confirm true unused state)

  **References**: 4 standalone components in `website/src/components/`

  **Acceptance Criteria**:
  - [ ] `.omo/refs/unused-components-decision.md` exists with decision rationale
  - [ ] Each unused component file has TODO comment OR is left untouched (per decision rule)
  - [ ] No file deleted

  **QA Scenarios**:
  ```
  Scenario: Decision documented
    Tool: Bash
    Steps:
      1. test -f .omo/refs/unused-components-decision.md
      2. for f in Hero WhySection Features FinalCTA; do head -3 website/src/components/$f.astro; done
      3. Verify either: (a) file unchanged from baseline, OR (b) file has TODO comment header
    Evidence: .omo/evidence/task-44-decision.txt
  ```

  **Commit**: YES (groups with chore(website): cleanup unused components and CSS hacks)
  - Files: `.omo/refs/unused-components-decision.md`, possibly the 4 components

- [x] 45. Remove single-post CSS hack from `global.css` — replace with scalable blog-wide rule

  **What to do**:
  - `global.css:55-68` currently has a selector targeting `:root:has(.blog-post--agentic-browser-comparison-2026)` — a rule for one specific post.
  - Determine intent: does this rule alter layout/typography for the post? If yes, generalize the selector (`.blog-post`, `[data-template="splash"]`, etc.) so it applies to all blog posts. Test that the original post still renders identically.
  - If the rule's specific properties don't make sense generalized, document why and remove entirely.
  - Verify post still renders correctly post-change.

  **Must NOT do**: Leave the original single-post hack. Add a different post-specific hack.

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: none

  **Parallelization**:
  - **Can Run In Parallel**: YES (with 44, 46)
  - **Blocks**: F2 (code quality)
  - **Blocked By**: 39 (blog post settled)

  **References**: `website/src/styles/global.css:55–68`

  **Acceptance Criteria**:
  - [ ] No selector containing the post's slug remains in global.css
  - [ ] Blog post visual matches baseline screenshot (allow ≤ 5px tolerance)

  **QA Scenarios**:
  ```
  Scenario: Single-post selector removed
    Tool: Bash
    Steps:
      1. grep "agentic-browser-comparison-2026" website/src/styles/global.css
      2. Assert no matches
    Evidence: .omo/evidence/task-45-css.txt

  Scenario: Blog post visual stable
    Tool: Playwright
    Steps:
      1. Navigate to /blog/agentic-browser-comparison-2026
      2. Take screenshot
      3. Diff against baseline screenshot
      4. Assert within 5% pixel tolerance
    Evidence: .omo/evidence/task-45-blog-visual.png
  ```

  **Commit**: YES (with Wave 8 group)
  - Files: `website/src/styles/global.css`, possibly the blog post mdx if class name changes

- [x] 46. Footer link audit + on-site changelog decision

  **What to do**:
  - Read `Footer.astro`. Footer "Changelog" links to external GitHub releases. Two options:
    - **A** (default for pre-release status): Replace "Changelog" text with "Release notes" still pointing to GitHub releases. Pre-release means there are few/no releases yet — external link is honest.
    - **B**: Remove the Changelog link entirely. Pre-release products may not need this.
  - Default to **A**.
  - Sweep footer for any remaining truth-table violations: `nicepkg`, `aspect-apps`, "Download" CTAs.
  - Verify all footer links resolve (HEAD-request each).

  **Must NOT do**: Add new footer columns. Remove the Privacy/Terms links.

  **Recommended Agent Profile**:
  - **Category**: `quick`
  - **Skills**: none

  **Parallelization**:
  - **Can Run In Parallel**: YES (with 44, 45)
  - **Blocks**: F1
  - **Blocked By**: 5, 8, 11 (peripheral audit informs decisions)

  **References**: `website/src/components/Footer.astro`, `.omo/refs/peripheral-artifacts-audit.md`

  **Acceptance Criteria**:
  - [ ] Footer text reflects pre-release status (no "Changelog" if option B chosen)
  - [ ] All footer links return 200
  - [ ] No `nicepkg` / `aspect-apps`

  **QA Scenarios**:
  ```
  Scenario: All footer links resolve
    Tool: Playwright + Bash
    Steps:
      1. Build, then extract all <a href> from dist/index.html footer region
      2. For each href: if relative, check file exists in dist; if absolute, curl -sI -o /dev/null -w "%{http_code}" "$href"
      3. Assert each returns 200 (or 301/302 with final 200)
    Evidence: .omo/evidence/task-46-footer-links.txt
  ```

  **Commit**: YES (with Wave 8 group)
  - Files: `website/src/components/Footer.astro`

---

## Final Verification Wave (MANDATORY — after ALL implementation tasks)

> 4 review agents in PARALLEL. ALL must APPROVE. Present consolidated results to user and get explicit "okay" before completing.

- [x] F1. **Plan Compliance Audit** — `oracle`

  **What to do**:
  - Read this plan end-to-end. For every "Must Have", verify implementation exists by reading file / curl-ing endpoint / running command.
  - For every "Must NOT Have", search codebase + `dist/` for forbidden patterns. Reject with file:line if found.
  - Verify all 5 source-of-truth resolutions reflected consistently. Specifically: no "open-source" claims about Maho's own code; no "$5/mo" tier; no `nicepkg/maho` or `aspect-apps/maho-mail`; all "Download" CTAs replaced with waitlist verbiage; Bug Bounty section absent.
  - Check evidence files exist in `.omo/evidence/` for each implementation task.
  - Compare deliverables against "Concrete Deliverables" list.

  **Output**: `Must Have [N/N] | Must NOT Have [N/N] | Tasks [N/N] | VERDICT: APPROVE/REJECT`

- [x] F2. **Code Quality Review** — `unspecified-high`

  **What to do**:
  - Run `cd website && bun run typecheck && bun run build`. Reject on any error or warning.
  - Review all changed files for: `as any`, `@ts-ignore`, empty catches, `console.log` in prod, commented-out code, unused imports.
  - Check AI slop patterns: excessive comments, over-abstraction, generic names (`data`/`result`/`item`/`temp`/`stuff`).
  - Verify React fixes: no index-as-key remaining in `LandingPage.tsx:328,395` (or anywhere else); no empty alt text titles.
  - Verify `<img>` migration: every `<img>` has `width`, `height`, and either `loading="lazy"` or above-the-fold justification.

  **Output**: `Build [PASS/FAIL] | Typecheck [PASS/FAIL] | Files [N clean/N issues] | VERDICT`

- [x] F3. **Real Manual QA** — `unspecified-high` (+ `playwright` skill)

  **What to do**:
  - Start `bun run preview` from `website/`. Wait for ready signal.
  - Execute EVERY QA scenario from EVERY task — follow exact steps, capture evidence to `.omo/evidence/final-qa/`.
  - Cross-task integration: navigate full site flow (index → CTA → waitlist; index → docs → API; index → compare hub → individual compare → CTA).
  - Edge cases: mobile viewport (375×667), broken-network state (block external font CDN, verify fallback), keyboard-only navigation.
  - Run Lighthouse CLI on 5 key pages (index, mail, pricing, security, compare-hub). Save JSON reports. Verify SEO ≥ 95, A11y ≥ 90, Perf ≥ 85.
  - Run link-check script on `dist/`: extract `href`/`src`, HEAD-request all internal routes, fail on any 404.

  **Output**: `Scenarios [N/N pass] | Lighthouse [5/5 pass] | Link check [N/N OK] | VERDICT`

- [x] F4. **Scope Fidelity Check** — `deep`

  **What to do**:
  - For each task: read "What to do", read actual diff (`git log --name-only -- website/`), `git diff` for content changes.
  - Verify 1:1 — everything in plan was built (nothing missing); nothing beyond plan was built (no scope creep).
  - Check Metis guardrail compliance: no URL changes (compare list of routes pre/post via `find website/src/pages -type f`), no new deps beyond sitemap (`git diff package.json`), no Starlight config changes beyond sitemap integration (`git diff astro.config.mjs`), no new components beyond `JSONLD.astro` (`git diff --stat src/components/`).
  - Detect cross-task contamination: did Task N touch files belonging to Task M's scope?
  - Verify ±30% word count constraint per page: extract pre/post word counts (Wave 0 baseline vs current), flag any page > ±30%.
  - Flag any unaccounted file additions or deletions.

  **Output**: `Tasks [N/N compliant] | Word counts [N/N within ±30%] | Contamination [CLEAN/N issues] | Unaccounted [CLEAN/N files] | VERDICT`

---

## Commit Strategy

One commit per logical group (not per task — too granular). Suggested groupings:

- **chore(website): wave 0 baseline + truth lock** — Wave 0 artifacts (baselines, rubric, truth table)
- **fix(website): repair P0 broken assets and links** — Wave 1 (OG image, sitemap, waitlist, Bug Bounty removal, dead link fix)
- **feat(website/seo): enrich meta tags and add JSON-LD** — Wave 2
- **feat(website): rewrite LandingPage for pre-release positioning** — Wave 3
- **feat(website): refresh marketing pages with truth-table copy** — Wave 4
- **feat(website): refresh compare pages with current facts** — Wave 5
- **docs(website): expand thin docs and reconcile MCP tool list** — Wave 6
- **chore(website): site-wide GitHub org and CTA swaps** — Wave 7 (Org + Download CTA + font + heading audit)
- **chore(website): cleanup unused components and CSS hacks** — Wave 8

Each commit must pass `cd website && bun run typecheck && bun run build`.

---

## Success Criteria

### Verification Commands

```bash
# Build + typecheck
cd website && bun run typecheck && bun run build

# Sitemap exists
ls website/dist/sitemap-index.xml

# OG image exists with correct dimensions
file website/public/og-image.png  # Expected: PNG image data, 1200 x 630

# No forbidden strings
rg -n 'nicepkg/maho|aspect-apps/maho-mail' website/src website/dist && exit 1 || true
rg -n '\$5/mo|AI Pro|Managed Pro' website/dist && exit 1 || true
rg -n 'Download for macOS|Download Maho' website/dist | grep -v 'docs/.*build' && exit 1 || true
rg -n 'Coming soon|TODO|FIXME|Fallback placeholder' website/dist && exit 1 || true

# Required strings present
rg -n 'Project-Maho/maho-browser' website/src/components/Footer.astro
rg -n 'Join the waitlist|Join waitlist|Get early access' website/src/components/LandingPage.tsx

# Lighthouse on key pages (after `bun run preview`)
for path in / /mail /pricing /security /compare; do
  bunx lighthouse "http://localhost:4321${path}" --output=json --quiet --chrome-flags='--headless=new' \
    --output-path=".omo/evidence/lighthouse-$(echo $path | tr / _).json"
done
```

### Final Checklist

- [ ] All "Must Have" present (verified F1)
- [ ] All "Must NOT Have" absent (verified F1)
- [ ] `bun run build` exits 0 (verified F2)
- [ ] All QA scenarios pass + Lighthouse thresholds met (verified F3)
- [ ] Scope fidelity intact, no creep (verified F4)
- [ ] User explicitly approved final verification results
