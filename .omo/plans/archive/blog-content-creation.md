# Maho Browser Blog — Content Creation Plan

**Created**: 2026-06-20
**Scope**: 41 posts (30 browser-focused + 11 browser comparisons). Mail content is out of scope for this batch.
**Cadence**: 2 posts per week (Tue + Fri), spanning 2026-06-23 through 2026-11-17.

---

## 1. Goal

Build an evergreen browser-focused blog that wins long-tail organic search, supports the `/waitlist` CTA, and gives Maho a defensible position against the 2026 wave of AI browsers. Every post must be concrete, voice-compliant, and contradiction-free with the truth table.

## 2. Hard constraints

Apply to every post without exception. Pulled from `.omo/refs/truth-table.md` and `.omo/refs/brand-voice-rubric.md`.

- **Voice**: Short, declarative sentences. Anti-hype. "We" for Maho with self-limiting framing. "You" for the reader, never flattered. Sentence-case headings. No em-dashes. No exclamation marks.
- **Forbidden words**: revolutionary, seamless, world-class, cutting-edge, game-changer, unleash, supercharge, unlock potential, paradigm-shifting.
- **Truth table**:
  - No "open-source" applied to Maho. (Chromium is upstream open-source; Maho itself is proprietary.)
  - No paid tier, no subscription pricing, no Pro plan, no dollar amounts.
  - No "Download" CTAs. All CTAs route to `/waitlist`.
  - No `github.com/...` repository links to Maho. Generic "built on Chromium" mentions are fine without a GitHub link.
  - No Bug Bounty mention.
- **CTA verbs**: Bare imperatives chosen from `Join the waitlist`, `Get early access`, `Get notified`. Never "Sign up", never "Try free", never with exclamation.
- **Internal linking**: Every post links to one relevant `/compare/<x>` page (when applicable), one relevant `/browser/*` doc page, and `/waitlist`.
- **Frontmatter requirements** (starlight-blog schema):
  - `title` — under 65 chars for SERP fit
  - `description` — 145-165 chars (meta-description optimal)
  - `date` — YAML date
  - `tags` — first tag is `browser` (mandatory for series separation), then 2-4 sub-tags
  - `excerpt` — 1-3 sentences (used in blog listing + tags pages)
  - `authors` — `[{ name: "Maho Team" }]`
  - Note: `template: splash` and `tableOfContents: false` are reserved for the existing 2026-04-22 post (a structured comparison). New posts use Starlight defaults (`doc` template, ToC enabled) so readers get jump links and reading-progress.
- **Word count**:
  - Flagship: 1500-2000 (Pillar 1 mostly)
  - Programmatic: 1100-1500
  - Glossary: 800-1200
  - Comparison: 1300-1700
- **Image refs**: Every post references three image paths under `/assets/blog/<slug>/{hero,body-1,body-2}.png`. Image prompts live in `.omo/refs/blog-image-prompts.md`. The image files themselves are generated separately by the user.
- **Word-tag policy**: First tag is always `browser`. This enables `/blog/tags/browser/` as the de-facto Browser series archive page. Mail posts (when added later) tag `mail` first.

## 3. Categorization & tagging

The blog is split into Browser and Mail series via the **first tag in `tags[]`**. starlight-blog auto-generates `/blog/tags/<tag>/` archives, so:

- Browser series archive: `https://maho.app/blog/tags/browser/`
- Mail series archive (future): `https://maho.app/blog/tags/mail/`

This requires no URL restructure, no plugin patching, and no new components. Sub-tags drive faceted discovery.

**Sub-tag taxonomy (browser):**

| Sub-tag | Used by |
|---------|---------|
| `agentic` | Pillar 1 |
| `mcp` | Pillar 1, some Pillar 5 |
| `local-ai` | Pillar 2 |
| `byok` | Pillar 2 |
| `power-user` | Pillar 3 |
| `privacy` | Pillar 4 |
| `terminal` | Pillar 5 |
| `comparison` | Comparison posts |
| `glossary` | Glossary posts |
| `how-to` | Tutorials in Pillar 2/5 |

## 4. Pillar allocation (30 browser-focused posts)

| Pillar | Posts | Format |
|--------|------:|--------|
| P1: Agentic Browsing & MCP | 8 | Mostly flagship |
| P2: Local-First AI & BYOK | 6 | Flagship + how-to |
| P3: Browser Power-User Workflows | 5 | Programmatic + how-to |
| P4: Privacy & Security | 4 | Flagship-leaning |
| P5: Terminal & Browser Integration | 4 | How-to + programmatic |
| Glossary | 3 | Short, evergreen |
| **Total (browser pillars)** | **30** | |
| Browser comparisons | 11 | Programmatic |
| **Grand total** | **41** | |

## 5. Editorial calendar

Two posts per week (Tue + Fri). 21 weeks total. Dates assume publishing starts 2026-06-23.

| # | Date | Slug | Pillar/Type | Word target | CTA verb |
|---|------|------|-------------|------------:|----------|
| 01 | 2026-06-23 | what-is-an-agentic-browser | Glossary | 1000 | Get early access |
| 02 | 2026-06-26 | maho-vs-arc | Comparison | 1600 | Join the waitlist |
| 03 | 2026-06-30 | what-is-mcp-model-context-protocol | Glossary | 900 | Get notified |
| 04 | 2026-07-03 | mcp-tools-in-the-browser | P1 Agentic | 1900 | Join the waitlist |
| 05 | 2026-07-07 | maho-vs-dia | Comparison | 1500 | Get early access |
| 06 | 2026-07-10 | byok-architecture-explained | P2 BYOK | 1700 | Join the waitlist |
| 07 | 2026-07-14 | what-is-byok-bring-your-own-key | Glossary | 950 | Get notified |
| 08 | 2026-07-17 | ollama-with-maho-step-by-step | P2 BYOK | 1300 | Get early access |
| 09 | 2026-07-21 | maho-vs-brave-leo | Comparison | 1500 | Join the waitlist |
| 10 | 2026-07-24 | agentic-browser-permission-model | P1 Agentic | 1800 | Get notified |
| 11 | 2026-07-28 | spaces-when-tab-groups-arent-enough | P3 Power-user | 1300 | Get early access |
| 12 | 2026-07-31 | maho-vs-edge-copilot | Comparison | 1500 | Join the waitlist |
| 13 | 2026-08-04 | tool-calling-without-the-mess | P1 Agentic | 1700 | Get notified |
| 14 | 2026-08-07 | lm-studio-with-maho | P2 BYOK | 1300 | Get early access |
| 15 | 2026-08-11 | maho-vs-chrome-gemini | Comparison | 1600 | Join the waitlist |
| 16 | 2026-08-14 | page-context-vs-conversation-context | P1 Agentic | 1600 | Get notified |
| 17 | 2026-08-18 | zero-telemetry-by-default | P4 Privacy | 1700 | Get early access |
| 18 | 2026-08-21 | maho-vs-opera-aria | Comparison | 1400 | Join the waitlist |
| 19 | 2026-08-25 | why-sqlite-for-prompt-history | P2 BYOK | 1800 | Get notified |
| 20 | 2026-08-28 | boosts-explained | P3 Power-user | 1200 | Get early access |
| 21 | 2026-09-01 | maho-vs-sigmaos | Comparison | 1400 | Join the waitlist |
| 22 | 2026-09-04 | agentic-browser-failure-modes | P1 Agentic | 1900 | Get notified |
| 23 | 2026-09-08 | sync-without-an-account | P3 Power-user | 1500 | Get early access |
| 24 | 2026-09-11 | maho-vs-safari | Comparison | 1600 | Join the waitlist |
| 25 | 2026-09-15 | local-models-cost-vs-cloud | P2 BYOK | 1500 | Get notified |
| 26 | 2026-09-18 | browser-history-encryption-explained | P4 Privacy | 1500 | Get early access |
| 27 | 2026-09-22 | maho-vs-firefox | Comparison | 1500 | Join the waitlist |
| 28 | 2026-09-25 | building-an-agentic-workflow | P1 Agentic | 2000 | Get notified |
| 29 | 2026-09-29 | byok-vs-managed-ai | P2 BYOK | 1400 | Get early access |
| 30 | 2026-10-02 | maho-vs-vivaldi | Comparison | 1500 | Join the waitlist |
| 31 | 2026-10-06 | side-panel-as-control-surface | P1 Agentic | 1700 | Get notified |
| 32 | 2026-10-09 | mcp-server-for-your-browser | P5 Terminal | 1500 | Get early access |
| 33 | 2026-10-13 | maho-vs-zen-browser | Comparison | 1500 | Join the waitlist |
| 34 | 2026-10-16 | keyboard-first-browsing-in-2026 | P3 Power-user | 1300 | Get notified |
| 35 | 2026-10-20 | sync-architecture-end-to-end | P4 Privacy | 1700 | Get early access |
| 36 | 2026-10-23 | extensions-that-still-work | P3 Power-user | 1200 | Get notified |
| 37 | 2026-10-27 | share-tab-to-cli | P5 Terminal | 1300 | Get early access |
| 38 | 2026-10-30 | agentic-vs-chat | P1 Agentic | 1700 | Join the waitlist |
| 39 | 2026-11-03 | browser-fingerprinting-defense | P4 Privacy | 1500 | Get notified |
| 40 | 2026-11-06 | headless-browsing-with-maho | P5 Terminal | 1400 | Get early access |
| 41 | 2026-11-10 | cli-for-browser-history | P5 Terminal | 1300 | Get notified |

The two flagship-heavy weeks (#28 and the run #22-#28) are intentionally seeded in the second half so the front of the calendar has higher comparison density (HN, Reddit signal). Comparison posts are spread evenly so the SEO surface grows linearly.

## 6. Detailed post specs

Each spec contains: slug, title (≤65), description (145-165), tags, search intent, target keyword, outline (4-7 H2s), three image references. Body images are diagrams or screenshots; the **user** generates them from `.omo/refs/blog-image-prompts.md`.

---

### 01. what-is-an-agentic-browser

- **Title**: What is an agentic browser? A working definition
- **Description**: Plain-English definition of an agentic browser, what separates it from chat-with-a-page, and why the line matters in 2026.
- **Tags**: `[browser, glossary, agentic]`
- **Search intent**: Informational
- **Target keyword**: what is an agentic browser
- **Outline**:
  - The shift from chat to action
  - The four signals that make a browser agentic (tool use, page context, tab awareness, permission model)
  - Where the line is drawn (and contested)
  - Why this term will be misused
  - What to look for when evaluating one
- **Internal links**: `/blog/agentic-browser-comparison-2026`, `/browser/ai/`, `/waitlist`
- **Images**: hero (definition card), body-1 (four-signals diagram), body-2 (chat-vs-action timeline)

### 02. maho-vs-arc

- **Title**: Maho vs Arc: where the agentic browser goes after Arc
- **Description**: Arc Browser company shut down in 2024. Here is how Maho picks up what Arc was reaching for, and where it does not.
- **Tags**: `[browser, comparison, agentic]`
- **Search intent**: Comparison
- **Target keyword**: arc browser alternative mac
- **Outline**:
  - The Arc context, in three paragraphs
  - What Arc got right (Spaces, command bar, sidebar)
  - What Arc never finished (agentic, programmable surface)
  - How Maho approaches the same problems
  - Where Maho is not Arc (no DRM-style account requirement, no design ambition for design's sake)
  - Migration notes for Arc users
- **Internal links**: `/compare/`, `/browser/spaces/`, `/waitlist`
- **Images**: hero (Maho/Arc comparison card), body-1 (feature axis chart), body-2 (Spaces UI screenshot)

### 03. what-is-mcp-model-context-protocol

- **Title**: What is MCP? Model Context Protocol explained for browsers
- **Description**: A short, browser-focused definition of MCP and why it matters when an LLM needs to do something instead of just talk about it.
- **Tags**: `[browser, glossary, mcp]`
- **Search intent**: Informational
- **Target keyword**: what is mcp model context protocol
- **Outline**:
  - The one-paragraph definition
  - Why MCP is not "just an API"
  - What an MCP server looks like in a browser context
  - Three concrete example tools
  - Where MCP stops and the model takes over
- **Internal links**: `/blog/mcp-tools-in-the-browser`, `/browser/ai/`, `/waitlist`
- **Images**: hero (MCP logo + browser), body-1 (tool-call sequence diagram), body-2 (MCP server registration UI)

### 04. mcp-tools-in-the-browser

- **Title**: MCP in the browser: what it actually means in 2026
- **Description**: A flagship walkthrough of how MCP tools live inside a browser, what permissions look like, and why most browsers will not get this right.
- **Tags**: `[browser, agentic, mcp]`
- **Search intent**: Informational
- **Target keyword**: mcp browser tools
- **Outline**:
  - Where MCP came from, briefly
  - The browser as an MCP host (not just a client)
  - Tool taxonomy: read-only, mutating, network, system
  - The permission gate: per-tool, per-origin, per-session
  - What we ship as built-ins, and why
  - The escape valve: bringing your own MCP server
  - Where this still goes wrong
- **Internal links**: `/blog/what-is-mcp-model-context-protocol`, `/browser/ai/`, `/waitlist`
- **Images**: hero (browser-as-MCP-host diagram), body-1 (tool taxonomy 2x2), body-2 (permission prompt UI)

### 05. maho-vs-dia

- **Title**: Maho vs Dia: two takes on the agentic browser in 2026
- **Description**: Dia and Maho both call themselves agentic. They are not the same. A side-by-side on context model, permissions, and where each one stops.
- **Tags**: `[browser, comparison, agentic]`
- **Search intent**: Comparison
- **Target keyword**: dia browser alternative
- **Outline**:
  - The pitch each browser makes
  - Context model: page-scoped vs conversation-scoped
  - Tool model: built-in vs hosted
  - BYOK posture
  - Privacy posture
  - Where each one wins today
- **Internal links**: `/compare/`, `/browser/ai/`, `/waitlist`
- **Images**: hero (Dia vs Maho card), body-1 (context-model diagram), body-2 (feature axis matrix)

### 06. byok-architecture-explained

- **Title**: BYOK in the browser: the architecture, in detail
- **Description**: A flagship explainer on how Bring Your Own Key works inside Maho. Endpoints, transport, key storage, prompt routing, fallbacks.
- **Tags**: `[browser, byok, local-ai]`
- **Search intent**: Informational
- **Target keyword**: byok browser architecture
- **Outline**:
  - Why BYOK is the default, not a checkbox
  - Endpoint shapes we accept (OpenAI-compatible, Anthropic, Ollama, LM Studio, custom)
  - Where keys live (Keychain, never the cloud)
  - Routing: which prompt goes to which provider
  - Streaming, retries, fallbacks
  - What we never see, and how you verify
- **Internal links**: `/blog/ollama-with-maho-step-by-step`, `/browser/ai/`, `/waitlist`
- **Images**: hero (BYOK architecture), body-1 (key-storage flow), body-2 (provider-routing decision tree)

### 07. what-is-byok-bring-your-own-key

- **Title**: What is BYOK? Bring Your Own Key, explained
- **Description**: A short definition of BYOK in the AI tooling world, why it differs from "we use OpenAI", and what it gets you in practice.
- **Tags**: `[browser, glossary, byok]`
- **Search intent**: Informational
- **Target keyword**: what is byok bring your own key
- **Outline**:
  - The short definition
  - The historical context (early SaaS bundling)
  - What BYOK guarantees, and what it does not
  - Common implementation traps
  - When BYOK matters most
- **Internal links**: `/blog/byok-architecture-explained`, `/pricing/`, `/waitlist`
- **Images**: hero (BYOK card), body-1 (BYOK vs managed flow), body-2 (key-flow diagram)

### 08. ollama-with-maho-step-by-step

- **Title**: BYOK with Ollama: connect a local model to Maho in five steps
- **Description**: Tutorial. Run a local LLM via Ollama, point Maho at it, verify nothing leaks. Tested on macOS.
- **Tags**: `[browser, how-to, byok, local-ai]`
- **Search intent**: How-to
- **Target keyword**: ollama browser byok mac
- **Outline**:
  - Prerequisites and assumptions
  - Step 1: install Ollama and pull a model
  - Step 2: confirm the OpenAI-compatible endpoint
  - Step 3: add the provider in Maho
  - Step 4: verify with the side panel
  - Step 5: confirm no outbound calls
  - Troubleshooting
- **Internal links**: `/blog/lm-studio-with-maho`, `/browser/ai/`, `/waitlist`
- **Images**: hero (Ollama + Maho), body-1 (terminal output of `ollama serve`), body-2 (Maho provider settings)

### 09. maho-vs-brave-leo

- **Title**: Maho vs Brave Leo: privacy plus AI, two approaches
- **Description**: Brave bundles Leo with the browser. Maho ships BYOK. A comparison of privacy posture, model choice, and where each one trades off.
- **Tags**: `[browser, comparison, privacy]`
- **Search intent**: Comparison
- **Target keyword**: brave leo alternative
- **Outline**:
  - Brave's approach: bundled, hosted, opt-out telemetry
  - Maho's approach: BYOK, local-first, no telemetry
  - Privacy comparison: a concrete table
  - AI capability comparison
  - Browser-feature comparison (ad blocking, sync)
  - Pick guide
- **Internal links**: `/compare/`, `/security/`, `/waitlist`
- **Images**: hero (Brave/Maho privacy card), body-1 (privacy-posture matrix), body-2 (BYOK vs bundled diagram)

### 10. agentic-browser-permission-model

- **Title**: Permissions for an agentic browser, in practice
- **Description**: A flagship on the permission model in Maho. Per-tool, per-origin, per-session. What "ask before doing" actually looks like.
- **Tags**: `[browser, agentic, privacy]`
- **Search intent**: Informational
- **Target keyword**: agentic browser permission model
- **Outline**:
  - Three permission failure modes most browsers fall into
  - Maho's three-axis grant model (tool, origin, session)
  - The auto-deny path
  - Recovering from a wrong grant
  - Persistence: how grants survive restarts
  - The audit log
  - Edge cases and known gaps
- **Internal links**: `/blog/agentic-browser-failure-modes`, `/security/`, `/waitlist`
- **Images**: hero (permission prompt UI), body-1 (three-axis grant diagram), body-2 (audit-log timeline)

### 11. spaces-when-tab-groups-arent-enough

- **Title**: Spaces in Maho: when tab groups are not enough
- **Description**: A practical look at Spaces. Different from tab groups, different from windows. When to use them and when not.
- **Tags**: `[browser, power-user]`
- **Search intent**: Commercial-investigation
- **Target keyword**: browser spaces vs tab groups
- **Outline**:
  - The tab-group problem on a real workday
  - What a Space is, defined precisely
  - Three workflows that need Spaces
  - Three workflows that do not
  - Switching, archiving, restoring
- **Internal links**: `/browser/spaces/`, `/blog/maho-vs-arc`, `/waitlist`
- **Images**: hero (Spaces UI), body-1 (tab-group vs Space diagram), body-2 (Spaces switcher)

### 12. maho-vs-edge-copilot

- **Title**: Maho vs Edge with Copilot: the bundle vs the BYOK
- **Description**: Microsoft ships Copilot inside Edge. Maho ships BYOK. A comparison on routing, data handling, and what you give up either way.
- **Tags**: `[browser, comparison, byok]`
- **Search intent**: Comparison
- **Target keyword**: edge copilot alternative
- **Outline**:
  - The Microsoft offer in plain terms
  - Where Copilot routes prompts
  - The Maho BYOK alternative
  - Side-by-side on context, agency, telemetry
  - Migration notes
- **Internal links**: `/compare/`, `/browser/ai/`, `/waitlist`
- **Images**: hero (Edge/Maho card), body-1 (prompt-routing comparison), body-2 (telemetry table)

### 13. tool-calling-without-the-mess

- **Title**: Tool calling, without the mess: how Maho ships browser tools
- **Description**: A flagship on tool calling in the browser. Why we ship narrow tools, how we keep them composable, and what we refuse to ship.
- **Tags**: `[browser, agentic, mcp]`
- **Search intent**: Informational
- **Target keyword**: browser tool calling
- **Outline**:
  - Tool calling is not a feature; it is a contract
  - The narrow-tool principle
  - Composing across tools (without an orchestrator)
  - The tools we shipped, and the ones we cut
  - Tool authorship for power users
  - Where tool calling will keep failing
- **Internal links**: `/blog/mcp-tools-in-the-browser`, `/browser/ai/`, `/waitlist`
- **Images**: hero (tool-call sequence), body-1 (narrow vs wide tool diagram), body-2 (built-in tool list UI)

### 14. lm-studio-with-maho

- **Title**: LM Studio plus Maho: a fully local AI panel on macOS
- **Description**: Tutorial. Use LM Studio's OpenAI-compatible server with Maho's side panel for a fully local browsing AI.
- **Tags**: `[browser, how-to, byok, local-ai]`
- **Search intent**: How-to
- **Target keyword**: lm studio browser byok
- **Outline**:
  - Why LM Studio (vs Ollama)
  - Setup: model, server, port
  - Wire it into Maho
  - Test cases that should work locally
  - Test cases that will not work locally
  - Notes on context length
- **Internal links**: `/blog/ollama-with-maho-step-by-step`, `/browser/ai/`, `/waitlist`
- **Images**: hero (LM Studio + Maho), body-1 (LM Studio server screen), body-2 (Maho provider config)

### 15. maho-vs-chrome-gemini

- **Title**: Maho vs Chrome with Gemini: agentic-first vs AI-bolted-on
- **Description**: Chrome ships Gemini integrations. Maho ships an agentic side panel. A comparison on how the AI is invoked, what it can do, and what it sees.
- **Tags**: `[browser, comparison, agentic]`
- **Search intent**: Comparison
- **Target keyword**: chrome gemini alternative
- **Outline**:
  - Chrome's Gemini placement, in context
  - Maho's side panel, in context
  - Invocation comparison
  - Tool-call comparison
  - Privacy comparison
  - Pick guide
- **Internal links**: `/compare/`, `/browser/ai/`, `/waitlist`
- **Images**: hero (Chrome/Maho card), body-1 (invocation flow comparison), body-2 (tool capability matrix)

### 16. page-context-vs-conversation-context

- **Title**: Page context vs conversation context: two scopes, one panel
- **Description**: A clear breakdown of the two context models inside Maho's AI panel, when each one is right, and how to switch between them without losing your place.
- **Tags**: `[browser, agentic]`
- **Search intent**: Informational
- **Target keyword**: page context browser ai
- **Outline**:
  - The two scopes, defined
  - Why the distinction matters
  - The cost of mixing them
  - Maho's switching model
  - Power-user patterns
- **Internal links**: `/blog/agentic-browser-failure-modes`, `/browser/ai/`, `/waitlist`
- **Images**: hero (two-scope diagram), body-1 (scope switcher UI), body-2 (context overflow indicator)

### 17. zero-telemetry-by-default

- **Title**: Zero telemetry by default: what that actually means in code
- **Description**: A flagship on what "no telemetry" means in Maho. The exact endpoints, the exact crash-report behavior, the exact opt-in.
- **Tags**: `[browser, privacy]`
- **Search intent**: Informational
- **Target keyword**: browser zero telemetry
- **Outline**:
  - The phrase has been overused; here is what we mean
  - The endpoint table
  - Crash reports and how to opt in
  - Update checks (the one network call we keep)
  - How to verify with little Snitch or pf rules
  - Why this is harder than it sounds
- **Internal links**: `/security/`, `/privacy/`, `/waitlist`
- **Images**: hero (telemetry endpoints crossed out), body-1 (network-call table), body-2 (Little Snitch verification screenshot)

### 18. maho-vs-opera-aria

- **Title**: Maho vs Opera with Aria: AI sidebar, two designs
- **Description**: Opera's Aria sidebar versus Maho's agentic panel. Where each one wins, and where the design choices diverge.
- **Tags**: `[browser, comparison, agentic]`
- **Search intent**: Comparison
- **Target keyword**: opera aria alternative
- **Outline**:
  - Opera's Aria: the pitch
  - Maho's panel: the pitch
  - Capability gap
  - Privacy posture
  - Performance impact
  - Pick guide
- **Internal links**: `/compare/`, `/browser/ai/`, `/waitlist`
- **Images**: hero (Opera/Maho card), body-1 (sidebar vs panel diagram), body-2 (capability matrix)

### 19. why-sqlite-for-prompt-history

- **Title**: Why we put browser AI history in SQLite
- **Description**: A flagship on the choice to store prompt history in a local SQLite database. The alternatives, the trade-offs, the migration story.
- **Tags**: `[browser, byok, local-ai]`
- **Search intent**: Informational
- **Target keyword**: browser ai prompt history sqlite
- **Outline**:
  - The default in 2026 is "send it to us"
  - The three alternatives we considered
  - Why SQLite won
  - The schema, in plain terms
  - What you can do with it directly
  - Backup, sync, and the encrypted-at-rest story
- **Internal links**: `/blog/sync-without-an-account`, `/security/`, `/waitlist`
- **Images**: hero (SQLite + Maho), body-1 (storage-options matrix), body-2 (schema diagram)

### 20. boosts-explained

- **Title**: Boosts in Maho: per-site customization without an extension
- **Description**: A walkthrough of Boosts. What they do, how they differ from extensions, and the small surface where they win.
- **Tags**: `[browser, power-user]`
- **Search intent**: Commercial-investigation
- **Target keyword**: browser per-site customization
- **Outline**:
  - The extension trap
  - Boosts, defined
  - The Boost surface (CSS, JS, settings)
  - Three Boosts that change the way I read the web
  - Sharing Boosts
- **Internal links**: `/browser/boosts/`, `/blog/extensions-that-still-work`, `/waitlist`
- **Images**: hero (Boost editor UI), body-1 (Boost vs extension diagram), body-2 (Boost gallery)

### 21. maho-vs-sigmaos

- **Title**: Maho vs SigmaOS: two macOS-native power browsers
- **Description**: SigmaOS targets keyboard-first power users on macOS. So does Maho, with a different ceiling. A comparison.
- **Tags**: `[browser, comparison, power-user]`
- **Search intent**: Comparison
- **Target keyword**: sigmaos alternative
- **Outline**:
  - SigmaOS's design philosophy
  - Maho's design philosophy
  - Workspace model: workspaces vs Spaces
  - AI integration comparison
  - Pricing comparison (this is where Maho wins outright)
  - Pick guide
- **Internal links**: `/compare/`, `/pricing/`, `/waitlist`
- **Images**: hero (SigmaOS/Maho card), body-1 (workspace-model diagram), body-2 (pricing-table comparison)

### 22. agentic-browser-failure-modes

- **Title**: Agentic browser failure modes (and how Maho handles them)
- **Description**: A flagship on the four ways an agentic browser breaks in production. Prompt injection, runaway loops, context overflow, silent denial.
- **Tags**: `[browser, agentic, privacy]`
- **Search intent**: Informational
- **Target keyword**: agentic browser failure
- **Outline**:
  - Failure mode 1: prompt injection from page content
  - Failure mode 2: runaway tool loops
  - Failure mode 3: context overflow
  - Failure mode 4: silent denial of action
  - Maho's handling for each
  - What still goes wrong
- **Internal links**: `/blog/agentic-browser-permission-model`, `/security/`, `/waitlist`
- **Images**: hero (failure-mode quadrant), body-1 (prompt-injection vector), body-2 (runaway-loop trace)

### 23. sync-without-an-account

- **Title**: Sync without an account: the Maho relay model
- **Description**: How Maho syncs tabs, history, and Spaces between devices without an account. End-to-end, zero-knowledge, opt-in.
- **Tags**: `[browser, power-user, privacy]`
- **Search intent**: Informational
- **Target keyword**: browser sync no account
- **Outline**:
  - The "create an account" problem
  - The pairing model in Maho
  - The relay's role (and what it cannot see)
  - End-to-end encryption details
  - Recovery
  - Limits and known gaps
- **Internal links**: `/browser/sync/`, `/blog/sync-architecture-end-to-end`, `/waitlist`
- **Images**: hero (pairing UI), body-1 (relay architecture diagram), body-2 (recovery flow)

### 24. maho-vs-safari

- **Title**: Maho vs Safari: defaults are sticky, agentic is not optional
- **Description**: Safari is the macOS default. Maho is built for the same machine but a different decade. A comparison on defaults, AI, sync, and tabs.
- **Tags**: `[browser, comparison]`
- **Search intent**: Comparison
- **Target keyword**: safari alternative mac 2026
- **Outline**:
  - The default-tax argument
  - Where Safari is still better
  - Where Maho is built for the next era
  - Sync model comparison
  - AI integration comparison
  - Pick guide
- **Internal links**: `/compare/`, `/browser/ai/`, `/waitlist`
- **Images**: hero (Safari/Maho card), body-1 (default vs alternative diagram), body-2 (AI capability matrix)

### 25. local-models-cost-vs-cloud

- **Title**: Local models vs cloud APIs: the real cost comparison
- **Description**: Real numbers on running an LLM locally on Apple Silicon versus paying per token to a cloud API. kWh, latency, ceiling.
- **Tags**: `[browser, byok, local-ai]`
- **Search intent**: Commercial-investigation
- **Target keyword**: local llm vs cloud api cost
- **Outline**:
  - Pick a benchmark workload
  - Local: hardware assumptions, kWh per 1M tokens
  - Cloud: API price per 1M tokens, latency floor
  - Crossover point
  - Where the comparison breaks
  - Recommendation
- **Internal links**: `/blog/byok-architecture-explained`, `/blog/ollama-with-maho-step-by-step`, `/waitlist`
- **Images**: hero (cost crossover chart), body-1 (kWh-per-token calc), body-2 (latency comparison chart)

### 26. browser-history-encryption-explained

- **Title**: Browser history encryption: how Maho stores yours
- **Description**: A walkthrough of how browsing history is encrypted at rest in Maho. Keys, transport, recovery, and what you can verify.
- **Tags**: `[browser, privacy]`
- **Search intent**: Informational
- **Target keyword**: browser history encryption mac
- **Outline**:
  - History as a sensitive surface
  - The encryption boundary
  - Where the key lives (Keychain)
  - The cold-restart story
  - Verifying with the file system
  - Limits and known gaps
- **Internal links**: `/security/`, `/blog/sync-architecture-end-to-end`, `/waitlist`
- **Images**: hero (encrypted SQLite db), body-1 (key-flow diagram), body-2 (file-system verification)

### 27. maho-vs-firefox

- **Title**: Maho vs Firefox: two takes on user agency
- **Description**: Firefox has the longest claim to user agency in browsers. Maho rebuilds that claim for the agentic era.
- **Tags**: `[browser, comparison, privacy]`
- **Search intent**: Comparison
- **Target keyword**: firefox alternative mac 2026
- **Outline**:
  - The Mozilla mission, fairly stated
  - Where Firefox is still ahead
  - Where Firefox left agentic on the table
  - Maho's coverage of the gap
  - Privacy posture comparison
  - Pick guide
- **Internal links**: `/compare/`, `/security/`, `/waitlist`
- **Images**: hero (Firefox/Maho card), body-1 (agency-axis diagram), body-2 (privacy-posture table)

### 28. building-an-agentic-workflow

- **Title**: Building an agentic workflow: from search to ticket
- **Description**: A flagship walkthrough of one real workflow: research a topic, summarize sources, file a Linear ticket. End-to-end, with the Maho side panel.
- **Tags**: `[browser, agentic, mcp]`
- **Search intent**: How-to
- **Target keyword**: agentic browser workflow example
- **Outline**:
  - The workflow, before automation
  - Stage 1: search and gather
  - Stage 2: summarize with page context
  - Stage 3: cross-cite with conversation context
  - Stage 4: tool-call into Linear (MCP)
  - Stage 5: verify and commit
  - What to do when a step fails
- **Internal links**: `/blog/tool-calling-without-the-mess`, `/browser/ai/`, `/waitlist`
- **Images**: hero (workflow stages), body-1 (side-panel screenshot mid-workflow), body-2 (MCP tool-call to Linear)

### 29. byok-vs-managed-ai

- **Title**: BYOK vs managed AI: what you give up, what you keep
- **Description**: A direct comparison of BYOK and managed-AI pricing models, with a focus on what each one costs in privacy, latency, and operational burden.
- **Tags**: `[browser, byok, local-ai]`
- **Search intent**: Commercial-investigation
- **Target keyword**: byok vs managed ai
- **Outline**:
  - The two pricing shapes
  - What you give up with managed
  - What you give up with BYOK
  - Cost comparison at three usage tiers
  - The hybrid path most users will land on
- **Internal links**: `/blog/byok-architecture-explained`, `/pricing/`, `/waitlist`
- **Images**: hero (BYOK vs managed card), body-1 (cost-curve chart), body-2 (trade-off matrix)

### 30. maho-vs-vivaldi

- **Title**: Maho vs Vivaldi: two power-user browsers, two centuries
- **Description**: Vivaldi pioneered modern browser customization. Maho takes a different ceiling. Where each one wins for the macOS power user.
- **Tags**: `[browser, comparison, power-user]`
- **Search intent**: Comparison
- **Target keyword**: vivaldi alternative mac
- **Outline**:
  - The Vivaldi heritage, briefly
  - Maho's heritage, briefly
  - Customization comparison: chrome panels vs Boosts
  - Tab-handling comparison: stacks vs Spaces
  - AI integration comparison
  - Pick guide
- **Internal links**: `/compare/`, `/browser/boosts/`, `/waitlist`
- **Images**: hero (Vivaldi/Maho card), body-1 (customization-axis diagram), body-2 (tab-handling comparison)

### 31. side-panel-as-control-surface

- **Title**: The side panel as a control surface, not a chat box
- **Description**: A flagship on the side panel as Maho's primary AI control surface. Why it sits where it does, what it controls, and why this is not just chat.
- **Tags**: `[browser, agentic]`
- **Search intent**: Informational
- **Target keyword**: browser ai side panel
- **Outline**:
  - Three placements we tried before settling
  - Why the side panel won
  - The four operations the panel exposes
  - Keyboard control surface
  - The composability story
  - Where the panel still falls short
- **Internal links**: `/browser/ai/`, `/blog/page-context-vs-conversation-context`, `/waitlist`
- **Images**: hero (side panel UI), body-1 (placement-comparison diagram), body-2 (keyboard cheatsheet)

### 32. mcp-server-for-your-browser

- **Title**: Run an MCP server for your browser
- **Description**: Tutorial. Set up a custom MCP server, register it with Maho, and let your AI panel call your own tools.
- **Tags**: `[browser, how-to, mcp, terminal]`
- **Search intent**: How-to
- **Target keyword**: mcp server browser tutorial
- **Outline**:
  - What you need before you start
  - Step 1: scaffold an MCP server
  - Step 2: define one tool that does one thing
  - Step 3: register the server in Maho
  - Step 4: test from the side panel
  - Step 5: ship it
  - Common pitfalls
- **Internal links**: `/blog/mcp-tools-in-the-browser`, `/browser/ai/`, `/waitlist`
- **Images**: hero (MCP server code), body-1 (registration UI), body-2 (test from side panel)

### 33. maho-vs-zen-browser

- **Title**: Maho vs Zen Browser: two ambitious 2026 macOS browsers
- **Description**: Zen Browser is a Firefox-based 2026 newcomer. Maho is Chromium-based with an agentic core. A comparison on engine, AI, and ceiling.
- **Tags**: `[browser, comparison, power-user]`
- **Search intent**: Comparison
- **Target keyword**: zen browser alternative
- **Outline**:
  - Zen's premise
  - Maho's premise
  - Engine comparison and what it implies
  - Customization comparison
  - AI comparison
  - Pick guide
- **Internal links**: `/compare/`, `/browser/ai/`, `/waitlist`
- **Images**: hero (Zen/Maho card), body-1 (engine-implication diagram), body-2 (feature matrix)

### 34. keyboard-first-browsing-in-2026

- **Title**: Keyboard-first browsing in 2026: not an afterthought
- **Description**: A walkthrough of Maho's keyboard model. Defaults, customization, and where keyboard control still leaks back to the mouse.
- **Tags**: `[browser, power-user]`
- **Search intent**: Commercial-investigation
- **Target keyword**: keyboard first browser mac
- **Outline**:
  - Why most browsers fail at keyboard
  - The Maho default cheatsheet
  - Customization (and where it stops)
  - Three workflows that should be keyboard-only
  - Where the mouse still wins
- **Internal links**: `/browser/`, `/blog/spaces-when-tab-groups-arent-enough`, `/waitlist`
- **Images**: hero (keyboard cheatsheet), body-1 (default-binding diagram), body-2 (customization UI)

### 35. sync-architecture-end-to-end

- **Title**: Maho sync architecture, end to end
- **Description**: A flagship on the relay-based sync architecture. Pairing, key derivation, message format, and the threat model.
- **Tags**: `[browser, privacy]`
- **Search intent**: Informational
- **Target keyword**: end to end browser sync architecture
- **Outline**:
  - The pairing handshake
  - Key derivation
  - The relay's role (and what it cannot see)
  - Message format
  - Threat model
  - Limits and known gaps
- **Internal links**: `/blog/sync-without-an-account`, `/security/`, `/waitlist`
- **Images**: hero (architecture overview), body-1 (handshake sequence), body-2 (threat-model table)

### 36. extensions-that-still-work

- **Title**: Extensions that still work: the Chromium MV3 story in Maho
- **Description**: Maho is built on Chromium. Most Chrome extensions work. Some do not, by design. A practical breakdown of what to install and what to skip.
- **Tags**: `[browser, power-user]`
- **Search intent**: Commercial-investigation
- **Target keyword**: chromium extensions on maho
- **Outline**:
  - The MV3 baseline
  - What Maho restricts on top of MV3
  - Categories that always work
  - Categories that do not (and why)
  - Extension hygiene checklist
- **Internal links**: `/browser/extensions/`, `/blog/boosts-explained`, `/waitlist`
- **Images**: hero (extension gallery), body-1 (works/doesn't matrix), body-2 (hygiene checklist)

### 37. share-tab-to-cli

- **Title**: Share a tab to the terminal (and back) with Maho
- **Description**: Tutorial. Pipe the current tab into a shell, run something, send a result back. The tab as a unix object.
- **Tags**: `[browser, how-to, terminal]`
- **Search intent**: How-to
- **Target keyword**: browser tab to terminal mac
- **Outline**:
  - The unix-tab idea
  - The two integration paths (CLI command, MCP tool)
  - Step 1: enable the CLI helper
  - Step 2: pipe the tab
  - Step 3: round-trip a result
  - Three real workflows
- **Internal links**: `/cli/`, `/blog/mcp-server-for-your-browser`, `/waitlist`
- **Images**: hero (terminal + browser), body-1 (pipe diagram), body-2 (round-trip example)

### 38. agentic-vs-chat

- **Title**: Agentic vs chat: when the browser should act, not talk
- **Description**: A flagship on the line between chat and action. When to chat. When to call a tool. When to refuse to do either.
- **Tags**: `[browser, agentic]`
- **Search intent**: Informational
- **Target keyword**: agentic vs chat browser
- **Outline**:
  - The mode problem
  - When chat is the right answer
  - When action is the right answer
  - When neither is the right answer
  - The mode signal in Maho
  - The cost of getting the mode wrong
- **Internal links**: `/blog/what-is-an-agentic-browser`, `/browser/ai/`, `/waitlist`
- **Images**: hero (mode-decision diagram), body-1 (chat vs act flowchart), body-2 (mode-signal UI)

### 39. browser-fingerprinting-defense

- **Title**: Browser fingerprinting defense: what we ship, what we do not
- **Description**: An honest accounting of fingerprinting defenses in Maho. What we block, what we cannot, and why we do not pretend otherwise.
- **Tags**: `[browser, privacy]`
- **Search intent**: Informational
- **Target keyword**: browser fingerprinting defense mac
- **Outline**:
  - The fingerprinting surface, in 2026
  - What is reasonable to block
  - What is impossible to block without breaking the web
  - Maho's defaults
  - Power-user knobs
  - Where this is still an arms race
- **Internal links**: `/security/`, `/privacy/`, `/waitlist`
- **Images**: hero (fingerprinting surface map), body-1 (block/allow table), body-2 (knobs UI)

### 40. headless-browsing-with-maho

- **Title**: Headless browsing with Maho: scripts that read the web
- **Description**: Tutorial. Spin up Maho headlessly, run a script that visits a list of URLs, return structured data. End-to-end on macOS.
- **Tags**: `[browser, how-to, terminal]`
- **Search intent**: How-to
- **Target keyword**: headless browser mac script
- **Outline**:
  - When headless is the right tool
  - Setup: enable the headless mode
  - Step 1: a single-page fetch
  - Step 2: structured extraction
  - Step 3: a list run
  - Resource and rate-limit notes
  - Where this is not Playwright (and that is fine)
- **Internal links**: `/cli/`, `/blog/cli-for-browser-history`, `/waitlist`
- **Images**: hero (headless run terminal), body-1 (extraction-pipeline diagram), body-2 (list-run output)

### 41. cli-for-browser-history

- **Title**: A CLI for your browser history
- **Description**: Search and pipe your Maho browser history from the command line. Real query examples, schema notes, and one-liners worth keeping.
- **Tags**: `[browser, how-to, terminal]`
- **Search intent**: How-to
- **Target keyword**: cli browser history mac
- **Outline**:
  - The case for a CLI here
  - The schema, in plain terms
  - Query 1: last seven days, by domain
  - Query 2: anything you visited from a Slack link
  - Query 3: pipe to fzf for live search
  - Backup and export
- **Internal links**: `/cli/`, `/blog/headless-browsing-with-maho`, `/waitlist`
- **Images**: hero (terminal query), body-1 (schema diagram), body-2 (fzf demo)

---

## 7. Image prompts file

All image prompts live in `.omo/refs/blog-image-prompts.md`. Each post has three entries: `hero`, `body-1`, `body-2`. The image filenames the posts reference are predictable: `/assets/blog/<slug>/hero.png`, `/assets/blog/<slug>/body-1.png`, `/assets/blog/<slug>/body-2.png`. The user generates these images separately and drops them at those paths. The prompts file is the deliverable; the images are not.

## 8. Definition of done (per post)

A post is done when all are true:

- [ ] Frontmatter complete (title, description, date, tags, excerpt, authors)
- [ ] First tag is `browser`
- [ ] Word count within ±10% of target
- [ ] No forbidden words (revolutionary, seamless, world-class, cutting-edge, game-changer, unleash, supercharge, paradigm-shifting)
- [ ] No em-dashes (use period or comma)
- [ ] No exclamation marks
- [ ] No "open-source" applied to Maho
- [ ] No `github.com/...` links
- [ ] No "Download" CTA
- [ ] CTA at the bottom links to `/waitlist`, verb from approved set
- [ ] Three internal links present (one to a related blog post or compare page, one to a relevant doc, one to /waitlist)
- [ ] Three image references present at predictable paths
- [ ] At least four H2 headings (5-7 typical)
- [ ] Headings are sentence-case
- [ ] Content matches the truth table

## 9. Risk and mitigation

- **Risk**: 41 posts at ~55,000 total words exceeds a single-session attention window for one writer. **Mitigation**: dispatch in waves of 3-4 parallel subagents producing 3-4 posts each, with style and truth checks between waves.
- **Risk**: Comparison posts contain factual claims about competitors that drift. **Mitigation**: comparison posts include a "Last verified" date in frontmatter; refresh schedule is 30 days per `blog-strategy.md` maintenance rules.
- **Risk**: Forbidden-word and em-dash leakage. **Mitigation**: a `.omo/scripts/voice-check.mjs` pre-publish lint that scans for the prohibited list. Run on every wave output.
- **Risk**: Internal links pointing at posts that have not been written yet. **Mitigation**: build link-graph during planning; write posts in dependency order so back-links are filled in by later posts. Where unavoidable, link to the future canonical URL; the link will go live when that post ships.
- **Risk**: starlight-blog rejects frontmatter on schema mismatch. **Mitigation**: validate one pilot post in the build before dispatching writing waves.

## 10. Out of scope

- Mail blog content (deferred per user instruction).
- Image generation (user-owned).
- Distribution execution (covered by `.omo/refs/blog-strategy.md`).
- Newsletter, social posts, paid placement.

## 11. Execution waves

| Wave | Posts | Subagents |
|------|-------|-----------|
| Pilot | #01, #02 (one glossary + one comparison, validates voice + frontmatter + JSON-LD) | 1 (writing) |
| Wave 1 | #03-#11 | 3 parallel (writing), 3 posts each |
| Wave 2 | #12-#20 | 3 parallel (writing), 3 posts each |
| Wave 3 | #21-#29 | 3 parallel (writing), 3 posts each |
| Wave 4 | #30-#38 | 3 parallel (writing), 3 posts each |
| Wave 5 | #39-#41 | 1 (writing), 3 posts |
| QA | All 41 | 1 (deep) voice check + link check + truth-table sweep |

Each wave's prompt MUST attach: the spec for those posts (lifted from §6), the truth table, the brand voice rubric, and the definition of done. Output target: `website/src/content/docs/blog/<slug>.mdx`.
