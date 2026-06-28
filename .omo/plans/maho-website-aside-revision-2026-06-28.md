# Maho Website — Aside Competitive Revision Plan

**Date:** 2026-06-28
**Scope:** `website/` only — Maho marketing site
**Trigger:** Competitive analysis vs aside.com revealed (a) saturated OSS misrepresentation despite Maho being closed-source, (b) broken waitlist form, (c) pricing contradiction, (d) weak positioning relative to a competing AI browser
**Deliverable type:** Plan + precise copy specifications. No code changes in this document.
**License decision (user-confirmed):** Maho is **fully proprietary / closed source**. All OSS claims must be removed.
**Positioning decision (user-confirmed):** Multi-axis differentiation against aside.com:
  1. Cross-platform (macOS + iOS + Android) — Aside is macOS only
  2. Economic (free forever, BYOK, no credits) — Aside has credits + paid tiers
  3. Product philosophy (Spaces / workspace) — Aside is AI agent paradigm

---

## §0 — Decision Log

| Decision | Value | Source |
|---|---|---|
| License is | Closed source / proprietary | User, this session |
| OSS copy treatment | Remove entirely; replace with proprietary-honest framing | User, this session |
| Positioning lead | Cross-platform first | User, this session (option 4: multi-axis) |
| Positioning support 1 | Free forever + BYOK + no credits | User, this session |
| Positioning support 2 | Spaces workspace model | User, this session |
| Cure53 audit framing | Use as primary trust signal in place of "verify code" | Plan author recommendation, **OVERRIDDEN — see locked decisions below** |
| Deliverable scope | Both: precise copy + deep designs (download, compare/aside, Cure53 rollout) | User, this session |

### §0.1 Owner-locked decisions (2026-06-28)

These supersede §11 open questions:

| Topic | Owner answer | Plan path activated |
|---|---|---|
| **Cure53 audit status (§11 Q1, §1.6)** | **C — Not contracted** | §9.3 4-layer Fallback Track activated everywhere. Remove ALL Cure53 references from hero/Final CTA/Security/Privacy/Compare. |
| **iOS / Android platform state (§11 Q3, §2.0)** | **c — Pre-launch / in development** | H1 = **Candidate Y "Your AI. Your data. Your browser."** (§2.1). "iPhone and Android in development" in micro-CTA. Cross-platform demoted from H1 to supporting promise. |
| **Pro vs Max tier structure (§17.3, §17.4)** | **A — Keep $0/$20/$200 with Max substantiation needed** | §17.4 Decision A path. **BUT Max feature delta is sub-blocked** (see next row). |
| **Max enterprise feature delta (§17.3 sub)** | **TBD — undecided** | Plan locks "Max card" copy as placeholder ("Max: Enterprise plan — contact us") until owner specifies. Implementer: do NOT ship specific Max features as fact. |
| **`website/maho-website/` duplicate dir (§11 Q5, §1.4)** | **Unknown — defer** | §1.4 demoted from P0 to P1. Add pre-resolution step: identify live build target via `cat website/astro.config.mjs` and `cat website/package.json`. |
| **Waitlist backend (§11 Q7, §1.1)** | **Unknown — defer; hide /waitlist for now** | §1.1 deferred. Activate intermediate state: REMOVE `/waitlist` link from Nav and Footer until backend is wired. Form-page stays at URL but unreachable from navigation. |
| **Legal entity (§11 Q8, §1.3)** | **Individual developer (no incorporated entity)** | Terms ToS L21/L29 rewritten in individual-developer voice, NOT "Aspect Apps." Specific draft below. |

### §0.2 Terms ToS — individual-developer rewrite (replaces §1.3 spec for owner-locked path)

**L29 (final replacement)**:
> "Maho Browser and Maho Mail are proprietary software, developed and provided by an individual developer (hereinafter 'the Developer'). The software is licensed, not sold, under the terms of this agreement. Source code is not publicly distributed. The Developer may, at sole discretion, release individual components as open source under separate license terms; such components are governed by their respective licenses."

**L21 (final replacement)**:
> "...unless explicitly permitted in writing by the Developer or under a separate open-source license accompanying a specific component."

**Add new clause near top of Terms (recommended)**:
> "Contact: For all legal inquiries, including license, privacy, security disclosure, and Terms questions, please reach the Developer at legal@maho.app (or specific email). Maho is currently developed and operated by an individual; this contact is the canonical legal-correspondence channel."

**Owner decision needed**: confirm legal-correspondence email address.

---

## §1 — P0 Critical Fixes (must ship within 24h)

These are independent of all other work. Each is a credibility-or-legal risk.

### 1.1 Waitlist form backend (file: `website/src/pages/waitlist.astro`)

**Current state**: `<form action="#TODO-replace-with-real-backend">` — submissions go nowhere.

**Required**:
- Wire to a real backend (Formspree / ConvertKit / Resend / Supabase / Notion). Recommended: Resend with a Supabase table for storage, or Formspree if no infra preference.
- On submit, redirect to `/waitlist/thanks` with confirmation copy.
- Validate email server-side, store platform preference and referral.

**Owner decision needed**: which backend.

### 1.2 Pricing contradiction (files: `LandingPage.tsx` L99-128 vs `pages/pricing.astro`)

**Current contradiction**:
| Surface | Free tier | Mid tier | Top tier |
|---|---|---|---|
| Homepage Pricing section (`LandingPage.tsx`) | $0 | **AI Pro $5/mo** | (none) |
| `/pricing` page | $0 | **Pro $20/mo** | Max $200/mo |

**Required**: Pick one canonical pricing. Update both surfaces to match.

**Owner decision needed**: which tier structure is real. Plan recommendation = use `/pricing` as canonical ($0 / $20 / $200) and update homepage to match, since `/pricing` has more detailed copy (token quotas, FAQ, etc.) that implies it was the more-considered surface.

### 1.3 Terms of Service false OSS claim (file: `website/src/pages/terms.astro` L21, L29)

**Current L29 (false)**:
> "Maho Browser and Maho Mail are open-source software released under their respective licenses. The source code is available at github.com/aspect-apps. Your use of the software is also governed by the terms of the applicable open-source license."

**Replace with**:
> "Maho Browser and Maho Mail are proprietary software provided by Aspect Apps. The software is licensed, not sold, under the terms of this agreement. Source code is not publicly distributed. Aspect Apps may, at its sole discretion, release individual components as open source under separate license terms; such components are governed by their respective licenses."

**Current L21 (partial false)**:
> "...unless explicitly permitted under the applicable open-source license."

**Replace with**:
> "...unless explicitly permitted in writing by Aspect Apps or under a separate open-source license accompanying a specific component."

### 1.4 FAQ duplication and self-contradiction

**Current state**:
- `website/src/content/docs/browser/faq.mdx` L97-101 — implies Maho is OSS (false)
- `website/maho-website/src/content/docs/browser/faq.mdx` L84-90 — states Maho is NOT OSS (true)

**Required**:
1. Confirm which file the live Astro build uses. Most likely `website/src/` (standard Astro layout). The `website/maho-website/` directory is suspect — likely a nested copy or abandoned legacy.
2. If `website/maho-website/` is unused, delete it.
3. Migrate the honest L84-90 content into the live `faq.mdx`, replacing the existing L97-101 entry verbatim:
   > "## Is Maho open source?
   >
   > No. Maho is not open source. Its core is proprietary Rust code, with native platform shells for macOS, iOS, and Android.
   >
   > That puts it in a different category from Nook and Zen. Nook is open source and free, while Zen is open source under MPL-2.0. Those projects may be better fits if source transparency or community modification is your top priority.
   >
   > Maho instead focuses on a tightly controlled product experience, cross-platform native apps, privacy-first defaults, and a feature set that is built and maintained as one coherent browser. For some people, that tradeoff is worth it. For others, an open source browser will still be the better choice."

**Verification command** (for owner): `cd website && bun run build && grep -r "is open source" dist/ | head` — must show zero matches.

### 1.5 Nav lacks conversion CTA (file: `Nav.astro` L20)

**Current**: GitHub icon link only.

**Required**: Replace GitHub icon with a `Download` pill button. Sticky behavior preserved.

**Sequence dependency (Oracle critique)**: `/download` (§5) is P1, not P0. If §1.5 ships before §5, the Nav button points to a 404. Mitigation:
- **Phase 1 (this P0)**: `href="/download"` but with a fallback redirect handler: if `/download` returns 404, redirect to current `https://github.com/nicepkg/maho/releases`. Implement via a minimal Astro page at `/download` that *immediately* redirects to GitHub releases until §5 builds the full self-hosted experience.
- **Phase 2 (P1, §5)**: Replace the minimal redirect page with the full self-hosted `/download` page per §5 spec.

This eliminates the "broken link" risk while letting §1.5 ship in 24h.

### 1.6 Cure53 contractual status check — HARD GATE (P0 prerequisite)

**Issue (Oracle critique)**: §11 Q1 (Cure53 audit contractual status) was listed as an "open question" but Oracle correctly identifies it as a **hard gate** blocking ~30% of the proposed copy. Marketing an audit that is not contracted is fraud-adjacent legal exposure. Treat this as P0.

**Required before any hero / Final CTA / Security / Privacy copy ships**:
1. Confirm with the security team / founder: is Cure53 (or any external auditor) under signed contract?
2. If YES → record audit state (A/B/C/D/E per §9.2). Cure53 copy may ship per spec.
3. If NO → activate **§9.3 Fallback Copy Track** (specified below). Cure53 must be removed from ALL marketing surfaces before any §2/§3/§4/§8 copy ships.

**§9.3 Fallback Copy Track**: see §9.3 for the canonical layered fallback specification (4 layers: network-verifiable behavior + public architecture docs + responsible disclosure program + founder/team identity). The fallback substance lives in §9.3 to avoid duplication.

**Required check before any P1 copy ships**:
```
grep -ri "Cure53\|external audit\|independently audited" website/src/ | wc -l
```
- If Cure53 IS contracted → expected non-zero (use per §9 spec)
- If Cure53 is NOT contracted → MUST be zero (fallback track applied)

---

## §2 — Hero Rewrite (Homepage Browser side)

**File**: `website/src/components/LandingPage.tsx`, `browserContent.hero` block (lines ~58-70)

### 2.0 Hero copy selection — Candidate Y as primary (Oracle 2nd-pass)

Per Oracle 2nd-pass + §16.3: **Candidate Y (sovereignty-led) is the primary recommendation regardless of platform-shipping state.** The original "One workspace. Every device." H1 is retained as Option 4, used *only* when iOS AND Android both ship publicly today. All other branches use Y as default.

**H1 decision matrix (Oracle-revised, supersedes prior branch logic)**:

| Platform state (§11 Q3 answer) | Primary H1 recommendation | Fallback options |
|---|---|---|
| macOS only, no iOS/Android plans | **Candidate Y** "Your AI. Your data. Your browser." | X (Spaces-led) or Z (anti-agent) — but reconsider entire cross-platform axis |
| macOS shipping, iOS+Android pre-launch | **Candidate Y** | X or Z |
| macOS shipping, iOS+Android in beta | **Candidate Y** (sub mentions cross-device) | Original "One workspace. Every device." with beta disclosure |
| macOS shipping, iOS+Android shipping | **Candidate Y** OR Original — owner choice | All four candidates viable |

**Rationale for Y as default** (per §16.3 + Oracle): Y asserts a *belief* (sovereignty), not a *feature* (cross-platform breadth). Belief-anchored headlines are defensible even when features are still maturing. Feature-anchored headlines age poorly when features lag the promise.

### 2.1 Hero copy specification — Candidate Y as default

**File**: `website/src/components/LandingPage.tsx`, `browserContent.hero` block (lines ~58-70)

| Element | Copy (Candidate Y) |
|---|---|
| H1 | **"Your AI. Your data. Your browser."** |
| Sub | **"Spaces, AI assistance, and end-to-end sync — without metered credits or surveillance. macOS today, more soon."** |
| Primary CTA label | **"Download for macOS"** |
| Primary CTA href | `/download` *(new page — see §5; §1.5 Phase 1 redirect fallback applies)* |
| Secondary CTA | **REMOVE entirely.** |
| Micro-CTA | **"Free, macOS 12+. iPhone and Android in development."** + Cure53 line ONLY IF §1.6 confirms audit is contracted. |

### 2.1-alt Hero copy alternatives (X / Z / Original)

Per Oracle: when cross-platform is a future promise, anchor H1 on what's actually true *today* and use platform breadth only as supporting sub-copy.

Three candidates ordered by recommended strength:

**Candidate X — Spaces-led (strongest if iOS/Android pre-launch)**
| Element | Copy |
|---|---|
| H1 | **"Every project. One browser."** |
| Sub | **"Spaces keep your work separated. AI on demand, never on its own. Bring your own API key — free forever."** |
| Micro-CTA | "macOS today. iPhone and Android in development." + Cure53 line if contracted |

**Candidate Y — Sovereignty-led (Oracle's preferred reframe)**
| Element | Copy |
|---|---|
| H1 | **"Your AI. Your data. Your browser."** |
| Sub | **"Spaces, AI assistance, and end-to-end sync — without metered credits or surveillance. macOS today, more soon."** |
| Micro-CTA | "Free, macOS 12+. iPhone and Android in development." |

**Candidate Z — Anti-agent-led (commits hardest to non-Aside frame)**
| Element | Copy |
|---|---|
| H1 | **"A browser with AI that waits to be asked."** |
| Sub | **"Spaces for every project. Your own API key. No agents acting in your accounts without you. Free forever."** |
| Micro-CTA | "macOS 12+. iPhone and Android in development." |

**Owner decision needed**: confirm Candidate Y, or override to X / Z / Original given §11 Q3 outcome. Default if no override: Candidate Y per §2.0 matrix.

### 2.1-alt-Original Option 4 — Original "One workspace. Every device." H1 (USE ONLY IF iOS+Android both ship today)

This was the prior default in earlier plan drafts. Oracle 2nd-pass demoted it from default to Option 4. Retained here so the owner can select it explicitly if both platforms are genuinely shipping.

| Element | Current | If selected |
|---|---|---|
| H1 | "The browser that works like you do." | **"One workspace. Every device."** |
| Sub | "Spaces for every project. AI built in. Split view, notes, and boosts — with sync across every device. Available on macOS, iOS, and Android." | **"Spaces sync seamlessly between your Mac, iPhone, and Android. Free forever — bring your own AI key, no credits, no surveillance."** |
| Primary CTA label | "Download for Mac" | **"Download for macOS"** |
| Primary CTA href | `https://github.com/nicepkg/maho/releases` | `/download` *(see §5; §1.5 Phase 1 fallback applies)* |
| Secondary CTA | "View on GitHub" → GitHub repo | **REMOVE entirely.** |
| Micro-CTA | (none) | **"Free, ~80MB, macOS 12+."** + Cure53 line only if §1.6 contracted |

### 2.2 Honesty check on platform claim

The current homepage says "Available on macOS, iOS, and Android." But `waitlist.astro` shows iOS and Android as disabled radio options ("disabled" attribute). This means the claim is currently *also false*.

**Required**:
- If iOS/Android are not yet shipping: state "macOS today. iPhone and Android arriving soon."
- If iOS/Android are in beta: state "macOS shipping. iPhone and Android in beta — join waitlist."
- The Sub copy ("Spaces sync seamlessly between your Mac, iPhone, and Android.") is **aspirational** for the product vision. To make it honest today, append the Micro-CTA clarification.

**Owner decision needed**: iOS/Android current shipping state. The plan assumes "macOS shipping; iOS/Android coming soon." If wrong, the H1 message itself must be reconsidered.

### 2.3 Hero visual treatment

**Current**: Single image `/assets/images/sidebar-target-prototype-verified.png`, only visible on `lg:` breakpoint (hidden on mobile and tablet).

**Required (P2, deferred from hero-fix sprint)**:
- Asset must be visible on mobile.
- Should depict Spaces switching (the unique differentiator vs Aside).
- Avoid copying Aside's `<canvas>` animation pattern; an animated GIF or short MP4 looping is sufficient.
- Backup: a single static image showing three Spaces side by side with shared sync state (e.g., labeled "Work / Research / Personal").

**Out of scope for this revision**: actual asset production. The current image stays in place; only the layout class `lg:` is the immediate fix to be flagged.

---

## §3 — Hero Rewrite (Mail product page)

**File**: same, `mailContent.hero` block (lines ~140-145)

| Element | Current | New |
|---|---|---|
| H1 | "Email for humans and machines." | **Keep as-is.** This headline is strong and earned. |
| Sub | "A desktop email client with a CLI and MCP server. AI-powered, privacy-first, completely free and open source." | **"A desktop email client with a CLI and MCP server. AI-powered, privacy-first, free forever."** |
| Primary CTA | "Download for Mac" → GitHub releases | **"Download for macOS"** → `/download?product=mail` |
| Secondary CTA | "View on GitHub" → GitHub repo | **REMOVE.** |
| Micro-CTA (new) | (none) | **"Free, macOS 12+. CLI and MCP server included."** |

---

## §4 — LandingPage.tsx Full Edit Specification

This is the single largest copy surface. All edits are exact before/after pairs to be applied verbatim.

### 4.1 Browser product — Section Pricing (L99-128)

**L100 (current)**: badge → unchanged. Keep `"Pricing"` pill.

**L101 (current)**: `title: "Free. Forever."` → **unchanged**.

**L104 (current body, false)**:
> "Maho Browser is completely free and open source. Every feature, no limits. For AI features, bring your own API key or subscribe to our managed AI plan."

**L104 (new)**:
> "Maho Browser is free, forever. Every feature, no limits, no credits. For AI features, bring your own API key (OpenAI, Anthropic, Ollama) — or subscribe to our managed AI for a single monthly price."

**L106-118 (tier cards)**: depends on §1.2 pricing-canonicalization decision. If `/pricing` is canonical, the two homepage tiles should mirror it:

```tsx
tiers: [
  {
    name: "Free",
    price: "$0",
    description: "Every feature unlocked. BYOK for unlimited AI.",
    features: [
      "All features unlocked",
      "Unlimited spaces and tabs",
      "Cross-device sync (E2E encrypted)",
      "BYOK: OpenAI, Anthropic, Ollama",
    ],
  },
  {
    name: "Pro",
    price: "$20/mo",
    description: "Managed AI. No API key required.",
    features: [
      "Everything in Free",
      "Pre-configured premium models",
      "$20/mo of managed AI usage",
      "Up to 4,000 tokens per call",
    ],
  },
],
```

### 4.2 Browser product — Final CTA (L130-135)

**L132 (current title)**: `"Browse without compromise."` → **change to** `"No paywalls. No agents acting without you. No data harvest."`

**L133 (current sub, false)**:
> "Download Maho. It's free, private, and open source."

**L133 (new)**:
> "Download Maho. Free forever, privacy by default, independently audited."

*(Adjust "independently audited" wording per Cure53 status — see §11.)*

### 4.3 Mail product — Why section (L150-160)

**Current state**: section title and copy intact, no OSS claims here. **No change needed.**

### 4.4 Mail product — Features section (L165-200)

**L175 (current, false)**:
> "MIT licensed. No paywalls, no feature gates. Optional AI subscription via OpenRouter for those who want a managed experience."

**L175 (new)**:
> "Free, forever. No paywalls, no feature gates. Optional AI subscription via OpenRouter for those who want managed inference."

Other feature cards in this block: review for any "open source" or "MIT" mentions. Replace per same pattern.

### 4.5 Mail product — Pricing section (L177-205)

**L181 (current, false)**:
> "Maho Mail is completely free and open source. Every feature, no limits. For AI features, bring your own API key or subscribe to our managed AI plan through OpenRouter."

**L181 (new)**:
> "Maho Mail is free, forever. Every feature, no limits. For AI features, bring your own API key — or use our managed AI through OpenRouter for a single monthly price."

### 4.6 Mail product — Final CTA (L207-212)

**L209 (current title)**: `"Make email programmable."` → **unchanged.**

**L210 (current sub, false)**:
> "Download Maho Mail. It's free, private, and open source."

**L210 (new)**:
> "Download Maho Mail. Free forever, private, programmable."

### 4.7 GitHub URL constants (L53-56)

**Current**:
```ts
const browserReleaseUrl = "https://github.com/nicepkg/maho/releases";
const browserGithubUrl = "https://github.com/nicepkg/maho";
const mailReleaseUrl = "https://github.com/aspect-apps/maho-mail/releases";
const mailGithubUrl = "https://github.com/aspect-apps/maho-mail";
```

**New**:
```ts
const browserDownloadUrl = "/download";        // new self-hosted page (§5)
const mailDownloadUrl = "/download?product=mail";
// browserGithubUrl, mailGithubUrl: REMOVED
// browserReleaseUrl, mailReleaseUrl: REMOVED
```

All consumers of removed URL constants must be updated. Audit: search `LandingPage.tsx` for the four removed identifiers; replace any reference with the download routes. The secondary "View on GitHub" CTA references (L65, L143) are deleted entirely.

---

## §5 — New Page: `/download` (file: `website/src/pages/download.astro`)

**Purpose**: Replace the current "click → GitHub releases" flow (Aside ships 1-click).

### 5.1 Behavior

| Step | What happens |
|---|---|
| 1 | User clicks "Download for macOS" on homepage |
| 2 | Browser navigates to `/download` |
| 3 | On page load, JS detects platform via `navigator.userAgent` |
| 4 | If macOS detected: trigger DMG download automatically (anchor with `download` attribute clicked via JS) |
| 5 | Page displays install instructions (3 cards: Open DMG → Drag to Applications → Launch from Applications) |
| 6 | Below the fold: optional "Get update notifications" email input (single field, optional) |

### 5.2 Copy

| Element | Copy |
|---|---|
| H1 | "Your download is starting." |
| Sub | "Maho is downloading to your Downloads folder. Didn't work? <a href={dmgUrl}>Download manually</a>." |
| 3-card layout titles | "1. Open the DMG", "2. Drag Maho to Applications", "3. Launch from Applications" |
| Each card body | (short instruction + illustrated step) |
| Optional email input label | "Get notified about new releases (optional)" |
| Submit button | "Notify me" |
| Below email | "We email about releases and security. Nothing else. Unsubscribe in one click." |

### 5.3 Cross-platform handling

URL parameters:
- `/download` — macOS default
- `/download?platform=ios` — show "iPhone version coming soon. <a href='/waitlist'>Join the waitlist</a>."
- `/download?platform=android` — same pattern
- `/download?product=mail` — same flow but Maho Mail DMG

### 5.4 DMG hosting

Currently GitHub Releases is the de-facto host. Two options:

| Option | Pros | Cons |
|---|---|---|
| Keep GitHub Releases as backend, link to it from `/download` | Zero infra change | URL is `github.com/...` — leaks repo existence; if repo is private, user can't see it |
| Self-host on CDN (e.g., `releases.maho.app`) | Brand consistency, Aside-style; works regardless of repo visibility | Requires CDN setup, release pipeline change |

**Plan recommendation**: Option B (self-host). It matches the closed-source posture. Until ready, Option A is the temporary backend with the public-facing URL hidden (the user sees only `/download`, JS triggers the GitHub release URL silently).

**Owner decision needed**: which option, and timeline for B.

### 5.5 Update existing CTA destinations

All current `https://github.com/nicepkg/maho/releases` references in `Footer.astro` (L14, L31) and elsewhere must be repointed to `/download`.

---

## §6 — `/compare/aside` Full Rewrite

**File**: `website/src/pages/compare/aside.astro`

**Current state**: exists with feature comparison table. Several issues:
- L64 lists Maho as "Source Available / OSS Core" — false.
- The page is generic, doesn't leverage the specific Aside weaknesses identified in the librarian audit.
- Missing acknowledgment of where Aside wins (benchmark, agentic capability) — this acknowledgment, paradoxically, builds trust.

### 6.1 New page structure

```
1. H1 + intro (positioned framing)
2. The 30-second answer (TL;DR table)
3. Where Aside leads — and why benchmarks are the wrong measure
4. Where Maho wins (specific axes)
5. Detailed comparison table
6. Use-case picker ("Pick Aside if... / Pick Maho if...")
7. Pricing comparison
8. FAQ
9. CTA
```

### 6.2 Section-by-section copy

**H1**: "Maho vs Aside Browser"

**Subtitle**: "Two takes on the AI browser. Different beliefs, different products."

**Intro paragraph**:
> "Both browsers are Chromium forks. Both target AI-assisted workflows. They disagree on almost everything else: how AI should be priced, how many devices it should run on, and how much autonomy an agent should have. This is an honest comparison."

**The 30-second answer (table)**:

| | **Aside** | **Maho** |
|---|---|---|
| **Best at** | Autonomous agent tasks across logged-in websites | Organized browsing across every device you own |
| **Platforms** | macOS only | macOS today; iPhone, Android arriving |
| **Free plan** | $0, 500 credits/month | $0, unlimited BYOK |
| **Paid plan** | $20–$200/month (managed AI) | $20/month (managed AI) |
| **AI model** | Autonomous agent (acts on your behalf) | Assistant (acts when you ask) |
| **Source** | Proprietary | Proprietary |
| **Sync** | (not advertised) | E2E encrypted, cross-device |

**Where Aside leads — and why benchmarks are the wrong measure (REVISED per Oracle 2nd-pass critique)**:

The original draft conceded too much. Oracle 1st pass: "saying 'Aside is the strongest option today' sends curious visitors straight to Aside." Revised to acknowledge the benchmark fact but immediately reject the agentic frame.

> "Aside ranks #1 on three browser agent benchmarks. We're not going to pretend otherwise. If your mental model is 'AI does work for me while I'm not looking,' Aside leads.
>
> We think that's the wrong mental model.
>
> Autonomous agent products in 2025–26 share a documented failure surface — not specific to Aside, but inherent to the paradigm Aside leads. The category has produced agents that bought the wrong flight, replied to a boss with hallucinated context, and leaked credentials to a phishing page that looked official. Benchmarks measure task completion under sanitized conditions. They don't measure what happens when something goes wrong in your actual life.
>
> Maho's answer isn't a worse agent. It's a different question: what would AI look like if it ran when you asked, on data you saw, with consent every step? That's a different product. The rest of this page is about that product."

The sequence — acknowledge benchmark → reject the frame → re-anchor visitor → name the alternative — preserves credibility without surrendering the AI narrative. Failure modes are attributed to the *category*, not to Aside specifically, to keep the legal posture clean.

**Where Maho wins**:

Each row is one differentiator with a one-paragraph proof.

1. **Cross-platform from day one**
> "Aside is macOS only. Maho ships macOS today, with iPhone and Android in development. Spaces, bookmarks, and history sync end-to-end encrypted across every device you own. If you work across phone and laptop, Aside makes you choose. Maho doesn't."

2. **Free forever, no credit meter**
> "Aside's free tier gives you 500 credits per month. Heavy users hit the wall. Maho's free tier gives you unlimited usage — you bring your own API key (OpenAI, Anthropic, or local Ollama) and pay your provider directly. No middleman markup, no credit anxiety. If you'd rather not manage keys, Maho's $20/mo managed AI is a flat fee, not a metered one."

3. **Steered AI, not autonomous AI** *(label revised per Oracle — "hand-on-wheel" analogy makes the trade legible)*
> "Think of it like driving. Aside is full-self-driving: the agent picks the route, navigates the websites, taps the buttons. Maho is hand-on-wheel: you steer, the AI accelerates. Summarize this page. Clean up these tab titles. Organize my downloads. Draft a reply, but let me send it. The AI is powerful — it just doesn't act in your accounts when you're not watching. For most real work, that's not a limitation. It's the point."

4. **Spaces, not tabs**
> "Aside organizes your browser around AI tasks. Maho organizes around projects. Each Space is a persistent workspace with its own tabs, AI thread, notes, and split view state. Switch contexts in one click. Pick up tomorrow exactly where you left off. This is the model Arc fans loved — preserved and shipped cross-platform."

5. **Sync that's actually private**
> "Both browsers claim local-first. Maho's sync architecture is documented publicly: X25519 handshake, six-digit verification code, AES-GCM per-message authentication. The [full design lives in our engineering blog](/blog/sync-architecture-end-to-end). Aside doesn't publish their sync design — because they don't ship cross-device sync."

**Detailed comparison table** (replace current):

| Feature | Aside Browser | Maho Browser |
|---|---|---|
| Source | Proprietary | Proprietary |
| Free tier | $0 / 500 credits / month | $0 / unlimited / BYOK |
| Mid tier | $20/mo (3× credits) | $20/mo (managed AI) |
| High tier | $200/mo | (not offered) |
| Free trial of paid | 14 days | Not needed — free works forever |
| BYOK | Partial (Pro+ feature) | Yes, free tier, unlimited |
| Cross-device sync | No | Yes, E2E encrypted |
| macOS | Yes | Yes |
| iOS | No | Coming soon |
| Android | No | Coming soon |
| Windows | No (in development) | Coming soon |
| Linux | No | Coming soon |
| AI model | Autonomous agent | Assistant on demand |
| AI benchmark leadership | #1 on 3 benchmarks | (not pursued) |
| Workspace model | Tab list + agent panel | Spaces (persistent workspaces) |
| Split view | No | Yes |
| Per-site customization (Boosts) | No | Yes |
| Manifest V3 extensions | (unclear) | Yes |
| External audit | Not published | Cure53 (status: pending) |
| Sync architecture | Not published | Public spec |
| Telemetry default | (per privacy policy) | Off by default |
| Founded | 2024 (YC F25) | (Maho founding year here) |

**Use-case picker**:

> "**Pick Aside if** you primarily work in a few logged-in tools, your work is repetitive enough that an agent can finish multi-step tasks, you're macOS-only, and you're comfortable letting AI take actions in your accounts.
>
> **Pick Maho if** you work across multiple devices, you organize work by project rather than by task, you prefer AI that runs only when you ask, and you'd rather pay $0 (BYOK) than $20–$200/mo for managed agentic AI."

**Pricing comparison** (visual table — match `/pricing` page tier structure)

**FAQ** (3-5 questions):
1. "Is Maho a fork of Aside?" — No. Both are independent Chromium forks.
2. "Why doesn't Maho compete on agent benchmarks?" — Different product philosophy. We chose Spaces + on-demand AI over autonomous agents. See our [philosophy doc / blog post].
3. "Can I migrate from Aside to Maho?" — Yes. Maho imports bookmarks, history, and passwords from Aside and most Chromium-based browsers.
4. "Is Maho open source like Brave or Firefox?" — No. Maho is proprietary, the same as Aside. We focus instead on external audits, public architecture docs, and verifiable network behavior.

**Final CTA**: "Try Maho. It's free." → `/download`

### 6.3 SEO metadata

- Title: `Maho vs Aside Browser — Honest comparison (2026)`
- Meta description: `Both are Chromium-based AI browsers. Aside leads agent benchmarks; Maho ships cross-device sync, Spaces, and free-forever BYOK. An honest comparison.`
- OG image: per-page (currently shared global image — flag for separate asset)

---

## §7 — Compare Pages 7× OSS Patch

Single-file patch for each. All Mail-vs-* pages have the same OSS pattern.

| File | Line | Find | Replace |
|---|---|---|---|
| `compare/maho-mail-vs-apple-mail.astro` | L24 | "Maho Mail is also open source, which Apple Mail is not." | "Maho Mail also runs entirely on your device, with no cloud dependency or telemetry, which Apple Mail's iCloud-integrated model does not match." |
| | L92 | "with AI features and open-source transparency." | "with AI features and an independently auditable privacy architecture." |
| | L200 | "Maho Mail is open source. Every line of code is auditable. Everything stays on your machine..." | "Maho Mail runs entirely on your machine. No cloud sync, no telemetry. Our privacy claims are verifiable through network traffic inspection and our external security audit. If you use Ollama for AI, even your AI inference is local. For users who want verifiable privacy rather than promised privacy, this is the difference." |
| | L227 | "Value open-source transparency" | "Value verifiable, audited privacy" |
| `compare/maho-mail-vs-superhuman.astro` | L7 | "A free, open-source email client with CLI and MCP vs the premium $30/mo..." | "A free email client with CLI and MCP vs the premium $30/mo cloud email experience." |
| | L20 | "Maho Mail is free and open source but offers..." | "Maho Mail is free but offers..." |
| | L92 | "free, open-source client with CLI access..." | "free client with CLI access..." |
| | L200 | "Maho Mail is free and open source. All data stays on your machine. There is no subscription, no cloud dependency, and no telemetry." | "Maho Mail is free, forever. All data stays on your machine. There is no subscription, no cloud dependency, and no telemetry." |
| | L230 | "Care about local data and open-source transparency" | "Care about local data and verifiable privacy" |
| `compare/maho-mail-vs-spark.astro` | L20 | "Maho Mail is completely free and open source." | "Maho Mail is completely free, with no paywalls or feature gates." |
| | L234 | "Care about local data privacy and open source" | "Care about local data and verifiable privacy" |
| `compare/maho-mail-vs-shortwave.astro` | L24 | "Maho Mail is completely free and open source." | "Maho Mail is completely free, with no paywalls or feature gates." |
| | L234 | "Care about local privacy and open source" | "Care about local privacy" |
| `compare/maho-mail-vs-thunderbird.astro` | L7 | "A modern email client with CLI and AI vs the legacy open-source desktop client." | "A modern CLI-and-AI email client vs the legacy open-source desktop incumbent." *(retained: Thunderbird IS open source, just don't claim Maho is)* |
| | L82 | "Two open-source email clients with very different philosophies." | "A modern proprietary email client and a legacy open-source incumbent with very different philosophies." |
| `compare/maho-mail-vs-himalaya.astro` | L27-28 | "Are both projects open source? / Yes. Both Maho Mail and himalaya are open source projects." | "Q: Is Maho open source like himalaya? / A: No. himalaya is open source under MIT; Maho is proprietary but free forever. Both projects share a commitment to local-first email, but they differ on source distribution." |
| `compare/maho-mail-vs-aerc-mutt-neomutt.astro` | L20 | "Maho is open source and under active development..." | "Maho is under active development and has a modern, SQLite-backed architecture that avoids many of the concurrency issues older tools face. Maho is proprietary and free." |
| | L27-28 | "Is Maho Mail open source like mutt? / Yes. Maho Mail is open source. Both mutt and Maho share a commitment to transparency and community development..." | "Q: Is Maho Mail open source like mutt? / A: No. mutt is open source with a long history of community development; Maho is proprietary and free. Both share a commitment to local-first email, but they differ on source distribution." |
| `compare/index.astro` | L26 | "Local, free, open source vs polished cloud UX at $30/mo" | "Local, free, programmable vs polished cloud UX at $30/mo" |

---

## §8 — Security / Privacy Pages OSS Removal

### 8.1 `pages/security.astro`

**L31 (current, false)**:
> "Maho's security story isn't a marketing badge. It's an architecture, a published audit, and source you can read. Each section below is something you can check yourself."

**Replace**:
> "Maho's security story isn't a marketing badge. It's an architecture, an external audit by Cure53, and verifiable network behavior. Each section below is something you can check yourself — without taking our word for it."

**Additional section to add (after "Audit status")**:

A new subsection titled **"Verifying our claims without source code"** with three verification methods:

> 1. **Network traffic inspection.** Maho runs locally. Open Activity Monitor on macOS, sort by network activity, and confirm that Maho does not phone home in normal browsing. Detailed methodology: [/blog/verify-maho-locally].
>
> 2. **Cure53 audit report.** When the Cure53 audit completes, the full report will be published at [security@maho.app or specific URL]. The report is written by an independent third party, not us.
>
> 3. **Public architecture documentation.** Our sync, encryption, and AI privacy designs are published as engineering blog posts. You can review them at [/blog/sync-architecture-end-to-end] and [/blog/byok-design].

### 8.2 `pages/privacy.astro` L44

**Current (false)**:
> "All Maho source code is publicly auditable at github.com/aspect-apps. You can verify our privacy claims by reviewing the code yourself."

**Replace**:
> "Maho is proprietary software. We don't ask you to take our privacy claims on faith. They are verifiable through (1) the external Cure53 audit, (2) inspecting network traffic on your own device, and (3) reviewing the public architecture documentation at [/security]."

---

## §9 — Cure53 Audit Marketing Asset Plan

If Cure53 audit is real (status currently labeled "in progress" on `/security`), it is Maho's most valuable trust signal — and the natural replacement for "you can read the source."

### 9.1 Where Cure53 appears

| Surface | Treatment |
|---|---|
| Homepage hero micro-CTA | "Independently audited by Cure53." (small text under primary CTA) |
| Homepage Final CTA | "Free forever, privacy by default, independently audited." |
| `/security` page | Main trust anchor — full audit status with link to report when available |
| `/privacy` page | Cited as one of three verification methods |
| `/compare/aside` page | Listed in the comparison table (Aside doesn't have one) |
| `/pricing` page | (No mention needed — pricing is not a trust surface) |
| Footer | (Optional) "Audited by Cure53" mini-badge |
| Blog | One launch post: "What Cure53 audited and what they found" (post-audit) |
| Meta tags | Mention in OG description on /security and homepage |

### 9.2 Audit-state-dependent copy variants

**State A: Audit not yet started** → use "Cure53 audit engaged for Q3 2026." (or specific date)

**State B: Audit in progress (current state per /security page)** → use "Cure53 audit in progress. Report expected Q1 2027." (or specific date)

**State C: Audit complete, no findings** → "Cure53 completed our security audit with [N] findings, all resolved. <a href='...'>Read the report</a>."

**State D: Audit complete, findings resolved** → same as C with finding count.

**State E: Audit found unresolved issues** → publish anyway. Honest disclosure builds more trust than silence. Copy: "Cure53 completed our audit. [N] findings, [M] resolved, [P] in active remediation. <a>Read the report and our remediation plan</a>."

**Owner decision needed**: confirm current state. Plan defaults to State B until confirmed.

### 9.3 If Cure53 audit is not actually scheduled (canonical Fallback Copy Track)

**This section is the canonical fallback specification.** §1.6 references it as the gate; this section provides the substance.

If §1.6 confirms the audit is aspirational rather than contracted, **do not market it anywhere**. Activate the layered fallback below — four mutually reinforcing trust signals, none load-bearing:

**Layer 1: Network-verifiable behavior (always true, runtime-checkable)**
- /security page intro replaces "external audit" anchor with: "Maho's claims about local-first behavior are verifiable on your own device. Open Activity Monitor on macOS, sort by network activity, and confirm Maho does not transmit data in normal browsing. Step-by-step guide: [/blog/verify-maho-locally]."

**Layer 2: Public architecture documentation (Maho-controlled)**
- Engineering blog posts already exist (`/blog/sync-architecture-end-to-end.mdx`). Cross-link from /security and /privacy.
- New blog post commitment: "BYOK design — how we never see your API key" (P2, ~1 day work)

**Layer 3: Responsible disclosure program (Maho commitment)**
- ⚠️ **Conditional on RDP existing**. Confirm before shipping copy. If no RDP, omit this layer.
- If RDP exists: /security adds: "Security researchers: report vulnerabilities to security@maho.app. We acknowledge within 48h and publish remediation timelines."

**Layer 4: Founder & team identity (compensates for missing source-code signal — added per Oracle 2nd-pass)**
- /about or /team page with founder name(s), photo, brief bio, LinkedIn link. Closed-source products need a human face. Wire from /security: "Maho is built by [founder name(s)]. We don't hide behind a logo."
- This layer is hoisted from §18.3 into the active fallback chain.

**Copy applied across surfaces if Cure53 fallback is active**:

| Surface | Replacement copy |
|---|---|
| Homepage hero micro-CTA | "Free, ~80MB, macOS 12+. Built with privacy-first defaults." |
| Homepage Final CTA sub | "Free forever, privacy by default, fully local-first." |
| /security L31 | "Maho's security story isn't a marketing badge. It's an architecture you can verify, network behavior you can observe, and a team you can name. Each section below is something you can check yourself — without taking our word for it." |
| /privacy L44 | "Maho is proprietary software. Our privacy claims are verifiable through (1) inspecting network traffic on your own device, (2) reviewing the public architecture documentation at [/security], and (3) reaching out to the team directly at [/about]." |
| /compare/aside external-audit row | Change Maho cell from "Cure53 (status: pending)" to "Roadmap — currently relies on layered verification (see /security)." Aside cell stays "Not published." Row remains parity-neutral. |

**Re-evaluation cadence**: if Cure53 (or another firm) is contracted later, swap fallback copy with §9 spec on a single release. Treat the substitution as a marketing milestone with its own blog post.

---

## §10 — Nav & Footer Cleanup

### 10.1 `components/Nav.astro`

**L20 (current)**:
```astro
<a data-product-github-link href={activeProduct === "browser" ? "https://github.com/nicepkg/maho" : "https://github.com/aspect-apps/maho-mail"} target="_blank" rel="noopener noreferrer" class="text-muted-foreground hover:text-foreground transition-colors" aria-label="GitHub">
  <!-- GitHub icon SVG -->
</a>
```

**Replace with**:
```astro
<a href="/download" class="inline-flex items-center gap-2 px-4 py-2 rounded-full bg-foreground text-background text-sm font-medium hover:opacity-90 transition-opacity">
  Download
</a>
```

If preserving the dual-product nav structure: change `data-product-github-link` ProductSwitcher logic to route to `/download` and `/download?product=mail` respectively.

### 10.2 `components/Footer.astro`

| Line | Action |
|---|---|
| L14 (Browser Download link) | Change `https://github.com/nicepkg/maho/releases` → `/download` |
| L17 (Maho GitHub link) | **DELETE.** Remove entire `<a>` element. |
| L31 (Mail Download link) | Change `https://github.com/aspect-apps/maho-mail/releases` → `/download?product=mail` |

The "Resources" column "Changelog" link (currently → GitHub releases) — depends on §5.4 decision. If self-hosting downloads, also self-host changelog at `/changelog`.

Footer "Docs" link: currently → `/cli/`. Should route by active product (Browser → `/browser/`, Mail → `/cli/`). Use the same `data-product-*-link` pattern as Nav.

---

## §11 — Open Questions Requiring User Decision

Each blocks parts of the plan from being final. Order = highest leverage first.

| # | Question | Blocks |
|---|---|---|
| 1 | Cure53 audit: is this actually contracted? What state (A/B/C/D/E from §9.2)? | §9 entire section; hero/Final CTA micro-copy |
| 2 | Canonical pricing: $0/$5/(none) (homepage) or $0/$20/$200 (/pricing)? | §1.2, §4.1, §6.2 pricing rows |
| 3 | iOS/Android current shipping state: not shipped / beta / shipping? | §2.1 H1 honesty, §2.2 platform claim, /waitlist copy, /compare/aside platform row |
| 4 | DMG hosting: keep GitHub Releases as backend (Option A) or self-host CDN (Option B)? | §5.4, §5.5 |
| 5 | `website/maho-website/` directory: live build target or abandoned? | §1.4 FAQ resolution |
| 6 | GitHub repos `nicepkg/maho` and `aspect-apps/maho-mail`: real / empty / private / 404? | §10 footer cleanup (whether to delete GitHub references entirely) |
| 7 | Backend choice for waitlist (Formspree / Resend / Supabase / other)? | §1.1 |
| 8 | Maho company name in legal copy — "Aspect Apps" or different? | §1.3 Terms rewrite |
| 9 | Spaces hero visual asset: budget for new animation/video, or static placeholder for now? | §2.3 |

---

## §12 — Verification Plan

After all changes are applied, verify with the following checklist:

### 12.1 Static verification

| Check | Command | Pass condition |
|---|---|---|
| Build succeeds | `cd website && bun run build` | Exit 0 |
| Zero OSS claims in built site | `grep -r "open source\|open-source\|opensource\|OSS\|MIT licensed\|source you can read\|publicly auditable\|View on GitHub" website/dist/ \| grep -v "Brave\|Firefox\|Zen\|himalaya\|mutt\|Thunderbird\|MPL-2.0\|MIT" \| head` | Zero output (allow whitelisted competitor references) |
| Pricing consistency | `grep -r "\$[0-9]" website/src/pages/pricing.astro website/src/components/LandingPage.tsx` | Same numbers appear in both |
| Waitlist form action | `grep "TODO-replace-with-real-backend" website/src/pages/waitlist.astro` | Zero matches |
| GitHub links removed from main funnel | `grep -r "github.com/nicepkg\|github.com/aspect-apps" website/src/components/Nav.astro website/src/components/LandingPage.tsx` | Zero matches |

### 12.2 Behavioral verification (manual QA — required)

| Scenario | Steps | Pass condition |
|---|---|---|
| Hero CTA conversion | Open homepage → click "Download for macOS" | Navigates to `/download`, DMG starts downloading within 2s |
| Mobile hero | Resize to 375×812 → load homepage | H1 visible without horizontal scroll; CTA tap target ≥44px |
| Waitlist submission | Fill `/waitlist` form → submit | Receives confirmation page; backend stores submission (verify in chosen backend dashboard) |
| Pricing consistency | Load `/` then `/pricing` | Tier names, prices, and tokens match exactly |
| FAQ honesty | Search "Is Maho open source" in site search / load `/browser/faq` | Answer = "No. Maho is not open source." Once. No duplicates. |
| Compare/aside accuracy | Load `/compare/aside` | License row says "Proprietary" for both products. No "OSS Core" claim anywhere on page. |
| Terms legal | Load `/terms` | Contains "proprietary software"; does NOT contain "open-source software released" |

### 12.3 Regression verification

| Check | Pass condition |
|---|---|
| All Astro pages render | `bun run build` exit 0, no broken-link warnings |
| Sitemap generated | `dist/sitemap-index.xml` lists all expected URLs |
| `/` lighthouse score | ≥ 90 across all categories (baseline; may decline if new hero asset is heavy — note as known) |
| All compare/* pages still link from `/compare` index | Click each from index, lands on page |

---

## §13 — Out of Scope (explicit deferrals)

These are NOT in this revision. Listed to prevent scope creep and to acknowledge them as known deferrals.

| Item | Why deferred |
|---|---|
| Spaces animation asset production | Asset design work, separate sprint |
| Per-page OG image generation | Lower priority than copy correctness |
| New `/manifesto` or `/why` page | Strategic, not urgent |
| `/compare/dia`, `/compare/arc`, `/compare/comet` | Additional content lift; do after `/compare/aside` lands |
| `/changelog` self-hosted page | Depends on §5.4 self-host decision |
| Press kit / `/press` page | Not blocking |
| Accent color introduction | Visual design, separate sprint |
| Customer testimonials / social proof | Requires actual users + permissions |
| Newsletter signup beyond waitlist | Separate marketing infrastructure decision |
| Blog tone audit (any blog post with implicit OSS framing) | Lower priority than core pages; do after P0-P2 ship |
| Mobile hero visual | Depends on Spaces asset (§2.3) |

---

## §14 — Execution Sequence

**⚠️ This section is SUPERSEDED by §19 — Critical Path After Oracle Integration.** §14 is retained as a historical record of the pre-Oracle execution plan. **Use §19 for actual implementation sequencing.** §19 reflects the Oracle revisions (P0 hard gates on Cure53 / iOS-Android shipping / pricing structure, hoisted in-scope items from §18.3, fallback redirect for §1.5).

The original §14 dependency graph (now obsolete):

```
                  ┌── §1.1 Waitlist backend ──────────────┐
                  │                                       │
P0 (24h) ─────────┼── §1.2 Pricing canonicalization ─┐    │
                  │   (needs owner decision)         │    │
                  ├── §1.3 Terms ToS fix ─────────────┤    │
                  ├── §1.4 FAQ duplicate resolve ─────┤    │
                  └── §1.5 Nav Download CTA ──────────┘    │
                                                            ▼
P1 (1 week) ───── §2 Hero rewrite ─┐                       │
                  §3 Mail hero ────┤                       │
                  §4 LandingPage   ├── depends on §1.2 ────┤
                  §10 Nav/Footer ──┘                       │
                                                            ▼
P1 (1 week) ───── §5 /download page ─── depends on §5.4 owner decision
                  §6 /compare/aside ──── depends on §1.2 & §11 platforms
                                                            
P2 (1 month) ──── §7 Compare 7× ───────────────────────────────────
                  §8 Security/Privacy ── depends on §9 Cure53 state
                  §9 Cure53 asset plan ─ depends on owner decision
                                                            
P3 (later) ────── §13 (deferred items)
```

The pre-Oracle critical path (now obsolete): §1.2 pricing decision and §11 questions 1, 3 unblock the most downstream work.

**See §19 for current canonical execution sequence.**

---

## §15 — Review Plan

This plan will be reviewed by:
1. **Momus** (Plan Critic) — clarity, verifiability, completeness → ✅ PASSED 2026-06-28
2. **Oracle** (Strategic Critic) — positioning, hidden risks, alternatives → ⚠️ MIXED 2026-06-28; key revisions integrated as §1.5/§1.6/§2.0/§2.1-alt/§6.2 + new §16/§17/§18
3. **Owner** (user) — strategic validity, business alignment, decisions on §11 open questions → PENDING

Following review feedback, this document is revised in place. Implementation begins only after owner approval on §11 open questions.

---

## §16 — Positive Thesis (added per Oracle critique)

Oracle: "Maho's entire positioning is reactive. Every value proposition is defined as the negation of Aside. A defensible product needs a positive thesis."

This section establishes Maho's affirmative worldview — what Maho is *for*, independent of Aside.

### 16.1 The thesis (one paragraph — Oracle 2nd-pass tightened)

> **Knowledge work happens in a browser.** Research, decisions, communication, building — most of a working day passes through the tab strip. The tools that frame that work should respect the person doing it. Their data stays theirs. Their AI assists when asked, and only when asked. Their projects follow them across every device they own. Maho is the browser built on that belief.

This thesis must be visible somewhere on the site **as its own page** — proposed location: `/why` or `/manifesto`. It is referenced from the homepage, /security, and /compare/aside.

*(Oracle 1st-pass had a draft that opened with "A browser should belong to its user" followed by three negations. Oracle 2nd-pass: the negations leaned on the comparison silhouette. This revised version drops the negations entirely and leads with the positive workflow frame, then asserts the values as positive features rather than rejections of anti-features.)*

### 16.2 How the thesis appears in each surface

| Surface | Thesis surfaces as |
|---|---|
| Homepage hero | H1 derived from thesis (sovereignty + workspace) |
| Homepage Why section | "Browsers weren't built for how you work" → recast as positive: "A browser that belongs to you" |
| /security | Thesis paragraph as page intro, before architecture details |
| /compare/aside | "We think that's the wrong mental model" passage in §6.2 derived directly from thesis |
| /why or /manifesto (new) | Thesis as H1. Full essay as body. Author byline. |

### 16.3 Implications for H1 selection

Given the thesis, the **strongest H1 candidates align with sovereignty, not platform breadth**. Re-examine §2.1-alt Candidate Y ("Your AI. Your data. Your browser.") — this is the thesis compressed to 6 words. Even if iOS/Android ship today, Candidate Y may be stronger than "One workspace. Every device." because it asserts belief, not feature.

**Updated H1 recommendation (supersedes §2.1 default)**: lead with Candidate Y or a sovereignty-anchored variant. Cross-platform becomes the sub-headline support, not the H1 itself.

---

## §17 — Business Model Transparency (added per Oracle critique)

Oracle: "Sophisticated buyers — exactly the audience that values audits and architecture — will ask: *how does Maho make money?* 'Free forever' without an answer reads as existential risk."

### 17.1 Required addition

A single sentence near pricing-adjacent surfaces, answering "how Maho is funded":

> **"Maho's free tier is sustained by Maho Pro and Maho Max — managed AI subscriptions for users who want premium models without managing API keys. We don't sell ads, we don't sell data, and we don't take a cut of your API spend."**

### 17.2 Where it appears

| Surface | Placement | Variant |
|---|---|---|
| `/pricing` page | Above the tier cards | Full sentence as a small intro paragraph |
| Homepage Pricing section | Under the section sub-line | Compressed: *"Free is sustained by Pro and Max — no ads, no data sales, no API-spend cut."* |
| Homepage Final CTA | (not here — would be too dense) | — |
| `/about` or `/why` page | Mentioned in the thesis essay | Full paragraph explanation |
| `/compare/aside` | New FAQ entry: "How does Maho make money?" | Single sentence + link to pricing |
| Footer "Company" column | New link: "How we make money" → `/pricing#how-we-fund-this` | Anchor to pricing-page intro |

### 17.3 Sub-decision: pricing structure rigor

Oracle's separate critique (§7 in review): the $0/$20/$200 structure is **adopted procedurally, not strategically examined**. Open issues:
- "$20/mo of managed AI usage with up to 4,000 tokens per call" — 4,000 tokens is below modern model context windows. Either limit is mis-worded, or Pro is artificially crippled.
- $200/mo Max — what does it unlock beyond Pro? Currently unspecified in `/pricing`. If just "more managed AI usage," Max users can buy raw API directly with BYOK and get more.

**Required before pricing copy ships**:
1. Confirm Pro vs Max actual feature delta.
2. If Max only differs in usage volume, consider dropping Max entirely (keep $0/$20).
3. If Max retains value, document the differentiating features (SSO, audit logs, multi-user, SLA?) and add to /pricing.

**Owner decision needed**: re-examine Pro/Max value props or drop Max.

### 17.4 Downstream change sets per pricing decision (Oracle 2nd-pass)

Each owner decision on Pro/Max triggers concrete changes in downstream surfaces. Specifying them upfront so the implementer doesn't guess.

**Decision A: Keep $0 / $20 / $200 (current /pricing structure)**
- `LandingPage.tsx` §4.1 — show 2 tiles (Free + Pro) on homepage (Max is enterprise; doesn't belong on hero homepage)
- `/pricing` page — 3 tiles (Free, Pro, Max). Add Max differentiating features (SSO, audit logs, multi-seat, SLA) per §17.3 item 3
- `/compare/aside` §6.2 detailed table — keep current rows: "Mid tier $20/mo / High tier $200/mo / not offered (Maho's Max)"
- §17 business model sentence stays as drafted

**Decision B: Drop Max — simplify to $0 / $20**
- `LandingPage.tsx` §4.1 — 2 tiles unchanged
- `/pricing` page — 2 tiles (Free, Pro). Delete Max card and Max-related FAQ. Update tier description for Pro to absorb power-user copy.
- `/compare/aside` §6.2 detailed table — change "High tier $200/mo / not offered" row to: Aside cell = "$200/mo (Max)" / Maho cell = "Not offered — Pro covers power users." This becomes a *positive* differentiator (Aside's 10× tier vs Maho's flatter pricing).
- `/compare/aside` use-case picker — add line: "Pick Aside if you need usage above $20/mo of managed AI; Maho recommends BYOK at that volume."
- §17 business model sentence: tighten to "Free is sustained by Maho Pro — managed AI for users who want premium models without managing API keys."

**Decision C: Redefine Max with real enterprise features**
- /pricing — restructure Max card to lead with SSO, audit logs, multi-user, SLA, dedicated support. Move usage volume to second position.
- Add /enterprise page (new) — dedicated enterprise sales surface. Out of current plan scope but flagged.
- /compare/aside — add enterprise-specific row to comparison table.
- §17 business model sentence stays as drafted; add follow-up line on enterprise.

**Default if owner doesn't decide before P1**: Decision B (drop Max). Rationale: simpler pricing reduces sophistication-tax on the homepage, simpler stack reduces support burden, and the "$200/mo Aside / Maho recommends BYOK" angle is a clean fairplay differentiator.

---

## §18 — Oracle Review Integration Log

This section records each Oracle critique and how it was resolved. For future plan revisions or post-mortem.

| # | Oracle critique | Resolution | Location in plan |
|---|---|---|---|
| 1 | Positioning is reactive, not proactive | Added §16 Positive Thesis; recommended H1 pivot to sovereignty-led | §16, §2.1-alt Candidate Y |
| 2 | Cure53 = single point of failure | Promoted §11 Q1 to P0 prerequisite (§1.6); added Fallback Copy Track | §1.6, §9.3 expansion |
| 3 | H1 "Every device" is currently false | Added §2.0 branch logic on platform state; §2.1-alt with three candidates | §2.0, §2.1-alt |
| 4 | §6.2 "Where Aside wins" is a lead-gen leak | Rewrote to acknowledge → reframe → re-anchor sequence | §6.2 revised |
| 5 | Anti-agent positioning weak as "AI on demand" label | Relabeled "Steered AI" with hand-on-wheel/full-self-driving analogy | §6.2 row 3 revised |
| 6 | No business-model transparency | Added §17; required sentence on /pricing and homepage | §17 |
| 7 | Pricing not strategically examined | Flagged as §17.3 sub-decision; awaiting owner Pro/Max feature delta confirmation | §17.3 |
| 8 | Sequence bug §1.5 / §5 | Added Phase 1/Phase 2 to §1.5 with redirect-fallback | §1.5 revised |
| 9 | "BYOK" implies users have API accounts (most don't) | Acknowledged in revised §6.2 row 2 framing ("If you'd rather not manage keys, Maho's $20/mo managed AI...") | §6.2 already had this; flagged for stronger emphasis |
| 10 | Spaces is not validated as moat (Arc had Spaces and died) | Out of scope for this plan; flagged as strategic risk for separate work | §18.2 below |
| 11 | Removing GitHub icon closes dev-relations channel | Owner decision — Oracle suggests keeping a public-issues-only GitHub presence | §11 Q6 expanded |
| 12 | /download JS platform-detect can fail on edge browsers | Added §5.6 explicit fallback for JS-disabled / unrecognized UA | §5.6 below |
| 13 | Founder/team page missing | Hoisted from §13 (out of scope) to in-scope P2 | §18.3 |
| 14 | Public roadmap missing | Hoisted from §13 to in-scope P2 | §18.3 |

### 18.1 Items NOT auto-applied (require owner direction)

- **Final H1 selection** between original §2.1 ("One workspace. Every device.") vs §2.1-alt Candidate X/Y/Z. Default if owner doesn't choose: Candidate Y (sovereignty-led).
- **Pricing structure** $0/$20/$200 vs simplified $0/$20. Pending Pro/Max feature delta.
- **Cure53 audit contractual state** (§1.6). Determines whether fallback track activates.

### 18.2 Strategic risk flag — out of this plan's scope

Oracle: "Spaces is not validated as a moat. Arc had Spaces and died." This plan inherits Spaces as a differentiator without product-strategy validation. **Recommendation**: separate workstream — user research / analytics on actual Maho user Spaces adoption rates. If adoption is low, the entire positioning around Spaces (§16, §2.1-alt Candidate X, §6.2 row 4) needs reconsideration. **Not blocking this revision**, but flagged for next planning cycle.

### 18.3 In-scope additions (hoisted from §13)

Move the following from "out of scope" to P2 in-scope. Closed-source products require identity surfaces to compensate for missing source artifact.

| Item | New target | Brief |
|---|---|---|
| `/about` or `/team` page | P2 in-scope | Founder(s) + small team page with real names, photos, brief bios, LinkedIn links. Compensates for missing source-code identity signal. ~1 day work. |
| `/roadmap` page | P2 in-scope | Three-column "Shipped / Building / Considering" public roadmap. Maintained manually. Signals project liveness. ~0.5 day work. |
| `/why` or `/manifesto` page | P2 in-scope | The §16 positive thesis as a full essay page. Author byline. ~1 day work for first draft. |
| "How we make money" link in footer | P2 in-scope | Anchor link to `/pricing#how-we-fund-this`. Trivial. ~15 min. |

### 18.4 Sub-spec added: §5.6 /download fallback for edge browsers

JavaScript-based platform detection (§5.1 step 3) can fail on iPad Safari, locked-down enterprise browsers, ad blockers, or no-JS environments. Required fallback:

1. The page server-side detects `User-Agent` and renders the appropriate platform option as the primary visible button. JS only enhances the download trigger.
2. If UA detection fails entirely → show all platforms as equal-weight buttons (macOS · iPhone · Android · Windows · Linux).
3. The auto-trigger download in §5.1 step 4 is JS-only. No-JS fallback: user clicks the manually-rendered download button.

---

## §19 — Critical Path After Oracle Integration

After Oracle review, the actual blocking chain is:

```
[OWNER DECISIONS — these gate everything]
   §11 Q1 (Cure53 status) → unlocks §1.6, §8, §9, hero/Final CTA copy
   §11 Q3 (iOS/Android status) → unlocks §2.0 → §2.1 OR §2.1-alt H1 selection
   §17.3 (Pro/Max feature delta) → unlocks §1.2 canonicalization + §4.1 + §6.2 pricing rows
   §11 Q5 (website/maho-website/ live or dead) → unlocks §1.4 FAQ resolution
        ▼
[P0 24h]
   §1.1 Waitlist backend
   §1.3 Terms ToS fix
   §1.5 Nav CTA (with §1.5 Phase 1 redirect fallback)
   §1.6 Cure53 status confirmation + fallback activation
        ▼
[P1 week]
   §2 + §3 Hero rewrites (per owner H1 selection)
   §4 LandingPage.tsx
   §5 /download (full version, supersedes §1.5 Phase 1)
   §6 /compare/aside (revised per §6.2 reframe)
   §10 Nav/Footer
   §16 /why or /manifesto page (positive thesis essay)
   §17 Business model line added
        ▼
[P2 month]
   §7 Compare 7× OSS patch
   §8 Security/Privacy
   §9 Cure53 full rollout (if Q1 contracted)
   §18.3 In-scope additions: /about, /roadmap
        ▼
[P3 deferred]
   §13 remainder
```

**End of plan.**
