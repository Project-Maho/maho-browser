# Blog Hero Image Audit Report

**Date:** 2026-07-10
**Auditor:** Visual QA (automated)
**Scope:** All `website/public/assets/blog/*/hero.png` images
**Reference target:** `what-is-byok-bring-your-own-key/hero.png` (Style B: Editorial Card)

---

## Summary

| Metric | Count |
|--------|-------|
| Total images audited | 51 |
| **PASS** | 31 |
| **FAIL** | 19 |
| **AMBIGUOUS** | 1 |
| Regen priority 1 (FAIL) | 19 |
| Regen priority 2 (AMBIGUOUS) | 1 |

---

## Per-Image Classification

### FAIL — Priority 1 (must regenerate)

| # | Slug | Issue Summary |
|---|------|---------------|
| 1 | `maho-vs-zen-browser` | Hallucinated Firefox flame logo on left panel; fake M-monogram on right; broken tab strip chrome |
| 2 | `maho-vs-arc` | Hallucinated architectural pseudo-logos for both Arc and Maho; melted/warped device bezels; stray "AI" badge |
| 3 | `maho-vs-safari` | Broken trapezoidal UI chrome on left panel; pervasive diagonal-hatch AI fill noise; melted window elements |
| 4 | `maho-vs-firefox` | Hallucinated Firefox fox-and-flame logo (trademark risk); asymmetric branding |
| 5 | `maho-vs-brave-leo` | Hallucinated Brave lion head logo (trademark risk); AI-stylized derivative of trademark |
| 6 | `maho-vs-chrome-gemini` | Hallucinated Chrome pinwheel logo (trademark risk); malformed right-panel window chrome |
| 7 | `maho-vs-edge-copilot` | Hallucinated Edge swirl logo; severe garbled text throughout both cards ("Ananuma", "Wintem", "logiveputhn") |
| 8 | `maho-vs-opera-aria` | Hallucinated Opera red "O" logo in sidebar (trademark risk) |
| 9 | `maho-vs-sigmaos` | Hallucinated Σ-squircle logo; broken window chrome (stray notch, disconnected strokes); radial AI icon-halo cliché |
| 10 | `share-tab-to-cli` | Severely garbled article body text (AI word-salad); stray orange overlay line artifact |
| 11 | `page-context-vs-conversation-context` | Misspelled title "illustraton" (AI text artifact); leaked prompt/brief label as final copy |
| 12 | `mcp-server-for-your-browser` | Garbled TypeScript code ("Iypecrns", "systal", "logiveputhn", "ctinitor", "doliname"); broken editor chrome |
| 13 | `local-models-cost-vs-cloud` | Duplicated X-axis tick label (100, 100 instead of 100, 1000) breaks chart credibility |
| 14 | `keyboard-first-browsing-in-2026` | Heavy garbled shortcut labels ("Ratting", "Debut", "inceptions", "conopents", "inthet-round", "dillease") |
| 15 | `extensions-that-still-work` | Multiple garbled captions ("Checklllist", "Translatted speechers", "rockter mary"); contradictory status indicators |
| 16 | `cli-for-browser-history` | AI-hallucinated gibberish URLs ("senfrian.com", "wwww.google.com"); non-ASCII substitutions; broken timestamps |
| 17 | `browser-history-encryption-explained` | Duplicated "encrypted." text line (AI generation artifact) |
| 18 | `boosts-explained` | Pervasive garbled text ("The Mington Pose", "pohypicte's team in windows", "Bnaidine", "Gloloies", "Commencity") |
| 19 | `agentic-browser-failure-modes` | Duplicated "Context overflow" tile (content/generation defect) |

### AMBIGUOUS — Priority 2 (regen recommended)

| # | Slug | Issue Summary |
|---|------|---------------|
| 1 | `maho-vs-aside-browser` | Fabricated orange triangular mark on right slab doesn't match Aside's real branding; borderline hallucinated logo |

### PASS — No action required

| # | Slug | Notes |
|---|------|-------|
| 1 | `what-is-byok-bring-your-own-key` | Reference Style B image — canonical target |
| 2 | `maho-vs-dia` | Clean UI mockups, no hallucinated logos, icons render correctly |
| 3 | `maho-vs-vivaldi` | Clean comparison mockups, text "VIVALDI" as label only (no logo), all UI consistent |
| 4 | `agentic-browser-comparison-2026` | Pure abstract slab composition, no logos, clean text |
| 5 | `zero-telemetry-by-default` | Typography + wireframe globe, no artifacts |
| 6 | `why-sqlite-for-prompt-history` | Clean browser mockup with SQLite logo (legitimate), correct typography |
| 7 | `what-is-mcp-model-context-protocol` | Minimal editorial card style, clean icon + text |
| 8 | `what-is-an-agentic-browser` | Clean illustration with generic icons, correct text |
| 9 | `tool-calling-without-the-mess` | 3-node flow diagram, clean rendering |
| 10 | `sync-without-an-account` | Two laptops + pairing code diagram, clean |
| 11 | `sync-architecture-end-to-end` | Architecture diagram (Device A → Relay → Device B), clean |
| 12 | `spaces-when-tab-groups-arent-enough` | Minimal sidebar mockup, clean icons and text |
| 13 | `side-panel-as-control-surface` | Clean browser mockup with labeled actions |
| 14 | `ollama-with-maho-step-by-step` | Clean terminal + browser mockup, no Ollama llama logo hallucinated |
| 15 | `mcp-tools-in-the-browser` | 3×3 tool grid, all labels correct, clean icons |
| 16 | `lm-studio-with-maho` | Clean split panel illustration, no LM Studio logo hallucinated |
| 17 | `headless-browsing-with-maho` | Terminal output mockup, all text correct, clean UI |
| 18 | `byok-vs-managed-ai` | Balance/scale illustration, clean text and icons |
| 19 | `byok-architecture-explained` | Architecture diagram with legitimate provider logos (Ollama, OpenAI, Anthropic) |
| 20 | `building-an-agentic-workflow` | 5-step horizontal flow diagram, clean rendering |
| 21 | `browser-fingerprinting-defense` | Fingerprint illustration + signal labels, clean |
| 22 | `agentic-vs-chat` | Flowchart (Prompt → Chat/Act/Refuse), clean |
| 23 | `agentic-browser-permission-model` | Permission dialog mockup, all text correct |
| 24 | `verify-maho-locally` | Editorial card with shield icon, clean text |
| 25 | `maho-mail-vs-aerc-mutt-neomutt` | Abstract 3D envelope props, no competitor logos |
| 26 | `maho-mail-vs-himalaya` | Abstract 3D envelope split, no logos |
| 27 | `maho-mail-vs-shortwave` | Abstract 3D envelope split, no Shortwave logo |
| 28 | `maho-mail-vs-spark` | Abstract 3D envelope split, no Spark logo |
| 29 | `maho-mail-vs-thunderbird` | Abstract envelope stack (no Thunderbird bird), clean |
| 30 | `maho-mail-vs-superhuman` | Abstract envelope split, no Superhuman logo |
| 31 | `maho-mail-vs-apple-mail` | Abstract envelope split, no Apple logo |

---

## Failure Pattern Summary

### Top 5 recurring failure modes

1. **Hallucinated competitor logos** (9/19 failures): Firefox flame, Brave lion, Chrome pinwheel, Edge swirl, Opera O, SigmaOS Σ, fabricated Arc/Maho pseudo-marks. The AI model attempts to render competitor brand marks from memory, producing trademark-infringing derivatives.

2. **Garbled/nonsense text** (8/19 failures): AI image generators produce convincing-looking but meaningless text in code blocks, article bodies, UI labels, and keyboard shortcut descriptions. Ranges from single misspellings to complete word-salad.

3. **Broken/hallucinated UI chrome** (6/19 failures): Malformed browser windows with melted bezels, stray notches, impossible geometry, disconnected strokes, and mixed platform affordances (macOS dots + Windows-style frames).

4. **Duplicated content** (3/19 failures): AI repeats elements — duplicate text lines, duplicate tiles, duplicate axis labels — a classic generation decoherence signal.

5. **AI composition clichés** (2/19 failures): Radial icon halos, diagonal hatch-fill patterns as "detail" — obvious generative model signatures that undermine editorial credibility.

### Pattern by post type

| Post Type | FAIL Rate | Primary Cause |
|-----------|-----------|---------------|
| Browser comparison (`maho-vs-*`) | 9/12 (75%) | Logo hallucination + broken chrome |
| Feature explanation | 7/14 (50%) | Garbled text in code/UI mockups |
| Mail comparison (`maho-mail-vs-*`) | 0/7 (0%) | Abstract 3D style avoids all pitfalls |
| Concept/architecture | 3/18 (17%) | Occasional text artifacts |

---

## Regen Priority List

### Priority 1 — Must regenerate (19 images)

1. `maho-vs-zen-browser`
2. `maho-vs-arc`
3. `maho-vs-safari`
4. `maho-vs-firefox`
5. `maho-vs-brave-leo`
6. `maho-vs-chrome-gemini`
7. `maho-vs-edge-copilot`
8. `maho-vs-opera-aria`
9. `maho-vs-sigmaos`
10. `share-tab-to-cli`
11. `page-context-vs-conversation-context`
12. `mcp-server-for-your-browser`
13. `local-models-cost-vs-cloud`
14. `keyboard-first-browsing-in-2026`
15. `extensions-that-still-work`
16. `cli-for-browser-history`
17. `browser-history-encryption-explained`
18. `boosts-explained`
19. `agentic-browser-failure-modes`

### Priority 2 — Recommended regeneration (1 image)

1. `maho-vs-aside-browser`

---

## Coral Accent Color Reference

From the Style B reference image (`what-is-byok-bring-your-own-key/hero.png`):
- Measured accent: approximately **`#EA5A3F`** (coral red-orange)
- Use canonical value **`#EE5A3F`** across all prompts for consistency with brand palette

---

*End of audit report.*
