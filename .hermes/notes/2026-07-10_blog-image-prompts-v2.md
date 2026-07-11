# Blog Hero Image Regeneration Prompts — v2

**Date:** 2026-07-10
**Style:** B (Editorial Card) — replaces Style A (Editorial 3D Poster)
**Reference:** `website/public/assets/blog/what-is-byok-bring-your-own-key/hero.png`
**Canonical coral accent:** `#EE5A3F`

---

## HOW TO USE THIS DOCUMENT (do this for each of the 20 posts)

For every post section below, repeat these steps:

1. **Generate** — Copy the post's `#### Prompt` text into your image tool
   (Nano Banana / Midjourney / DALL·E / Flux / SDXL). Paste the **Canonical
   Negative Prompt** (top of this doc) into the negative-prompt field.
2. **Set output** — Force dimensions **1376×768 PNG**. Landscape. Keep file
   under ~400KB (run `pngquant` if needed).
3. **OVERWRITE the existing file** — Save the generated image to the post's
   `**Save path**`, replacing the old (bad) `hero.png`. Use the EXACT path,
   EXACT filename `hero.png`. Do not rename, do not add suffixes.
   Example:
   ```
   website/public/assets/blog/maho-vs-zen-browser/hero.png   ← overwrite
   website/public/assets/blog/maho-vs-firefox/hero.png       ← overwrite
   ```
4. **Verify visually before saving** — The image MUST have: no competitor
   logos, no browser UI, no garbled text. Only the card + headline + subhead
   + one coral line icon. If the model produced any logo/UI/gibberish,
   regenerate before overwriting.

### Scope
- Overwrite these **20** files (19 FAIL + 1 AMBIGUOUS). Full list in each
  section's `**Save path**`.
- **Do NOT touch the 31 PASS images** (listed at the bottom). Leave them alone.
- Partial is fine — overwrite whatever you finish, then tell the orchestrator
  "done with N images" and it will re-audit only the ones you replaced.

### After overwriting
Tell the orchestrator you're done. It will:
- Re-audit each overwritten `hero.png` (confirm no logo hallucination / slop)
- Run `bun run build` + preview to confirm the pages render + JSON-LD `image`
  + OG tags pick up the new files
- Flag any image that still fails and hand back an adjusted prompt

### Checklist (tick as you overwrite)
- [ ] maho-vs-zen-browser
- [ ] maho-vs-arc
- [ ] maho-vs-safari
- [ ] maho-vs-firefox
- [ ] maho-vs-brave-leo
- [ ] maho-vs-chrome-gemini
- [ ] maho-vs-edge-copilot
- [ ] maho-vs-opera-aria
- [ ] maho-vs-sigmaos
- [ ] share-tab-to-cli
- [ ] page-context-vs-conversation-context
- [ ] mcp-server-for-your-browser
- [ ] local-models-cost-vs-cloud
- [ ] keyboard-first-browsing-in-2026
- [ ] extensions-that-still-work
- [ ] cli-for-browser-history
- [ ] browser-history-encryption-explained
- [ ] boosts-explained
- [ ] agentic-browser-failure-modes
- [ ] maho-vs-aside-browser (AMBIGUOUS)

---

## Style B Specification (all prompts below follow this)

```
Style B: Editorial Card
- Canvas: 1376×768 PNG
- Background: solid near-black (#0A0A0A), no gradient, no texture
- Centered card: dark charcoal (#1A1A1A), rounded corners (~24px radius),
  approximately 960×540 (wider layout), no border, no shadow, no glow, flat
- Corner icon: single Lucide-style line icon in coral (#EE5A3F),
  approximately 72px, 2.5px stroke, positioned top-right of card with
  ~48px inset. Icon is abstract concept, not a brand mark.
- Headline: italic serif (Playfair Display / GT Sectra / editorial serif),
  approximately 90-110px, warm cream (#F2ECE0), centered inside card,
  upper-middle third
- Subhead: sans-serif medium weight (Inter / Söhne / Geist),
  approximately 38-44px, same cream color, centered, 1-2 lines, ends with period.
- No competitor logos, no browser UI, no product screenshots, no photos,
  no people, no faces, no gradients, no glows, no scenery, no textures
```

---

## Canonical Negative Prompt (use verbatim for ALL generations)

```
NO logos, NO brand marks, NO product logos, NO Firefox logo, NO Brave logo,
NO Zen logo, NO Chrome logo, NO Safari logo, NO Arc logo, NO Apple logo,
NO Microsoft logo, NO Superhuman logo, NO Thunderbird logo, NO Mozilla,
NO Opera logo, NO Edge logo, NO SigmaOS logo, NO Aside logo, NO Google logo,
NO browser UI, NO browser window, NO tab strip, NO address bar, NO URL bar,
NO sidebar, NO toolbar, NO traffic light window controls, NO close buttons,
NO application UI, NO software mockup, NO screenshots, NO device mockup,
NO phone, NO laptop, NO monitor illustration, NO people, NO faces, NO hands,
NO scenery, NO landscape, NO buildings, NO city, NO photograph, NO 3D render
of objects, NO glowing effects, NO gradient backgrounds, NO textured backgrounds,
NO noise, NO grain, NO decorative flourishes, NO shapes crawling out of frame,
NO speech bubbles, NO chat interface, NO forms, NO buttons, NO extra icons,
NO emojis, NO Korean text, NO Chinese text, NO Cyrillic, NO Arabic text,
NO garbled letters, NO overlapping text, NO drop shadow on text, NO glow on text,
NO code blocks, NO terminal output, NO file trees, NO diagrams, NO flowcharts,
NO charts, NO graphs, NO illustrations of animals, NO mascots, NO characters,
NO lions, NO foxes, NO birds, NO flames, NO fire, NO shields with logos inside,
NO pinwheels, NO swirls, NO compass roses, NO globes with continents.
```

---

## FAIL Images — Priority 1 (19 images)

---

### Post 1: maho-vs-zen-browser

**Status**: FAIL — Hallucinated Firefox flame logo; fake M-monogram; broken tab strip
**Save path**: `website/public/assets/blog/maho-vs-zen-browser/hero.png`

#### Prompt
Wide horizontal composition, 1376×768, solid near-black background hex 0A0A0A. Centered dark charcoal card hex 1A1A1A with 24px rounded corners, no border, no shadow. Top-right corner of the card: single minimalist line icon of two arrows pointing left and right (swap/compare symbol), coral color hex EE5A3F, 2.5px stroke, 72px size, 48px inset from edges. Inside the card, centered vertically in the upper-middle third: italic serif display text "Maho vs Zen" in warm cream hex F2ECE0, approximately 100px, tight letter-spacing. Below the headline with 60px gap: centered sans-serif medium weight subhead "Two ambitious macOS browsers, different engines." in same cream color, approximately 40px, single line, ending with a period. Editorial minimal typography poster style, absolute minimum visual noise.

#### Negative prompt
(Use canonical negative prompt above verbatim)

#### Icon suggestion
Lucide icon name: `ArrowLeftRight`

#### Headline / subhead
- Headline: "Maho vs Zen"
- Subhead: "Two ambitious macOS browsers, different engines."

#### Alt text (for MDX)
"Editorial card comparing Maho and Zen Browser — two ambitious macOS browsers with different rendering engines"

---

### Post 2: maho-vs-arc

**Status**: FAIL — Hallucinated architectural pseudo-logos; melted device bezels; stray AI badge
**Save path**: `website/public/assets/blog/maho-vs-arc/hero.png`

#### Prompt
Wide horizontal composition, 1376×768, solid near-black background hex 0A0A0A. Centered dark charcoal card hex 1A1A1A with 24px rounded corners, no border, no shadow. Top-right corner of the card: single minimalist line icon of a git-compare branching symbol, coral color hex EE5A3F, 2.5px stroke, 72px size, 48px inset from edges. Inside the card, centered vertically in the upper-middle third: italic serif display text "Maho vs Arc" in warm cream hex F2ECE0, approximately 100px, tight letter-spacing. Below the headline with 60px gap: centered sans-serif medium weight subhead "Where the agentic browser goes after Arc." in same cream color, approximately 40px, single line, ending with a period. Editorial minimal typography poster style, absolute minimum visual noise.

#### Negative prompt
(Use canonical negative prompt above verbatim)

#### Icon suggestion
Lucide icon name: `GitCompare`

#### Headline / subhead
- Headline: "Maho vs Arc"
- Subhead: "Where the agentic browser goes after Arc."

#### Alt text (for MDX)
"Editorial card comparing Maho and Arc Browser — the agentic browser successor to Arc on macOS"

---

### Post 3: maho-vs-safari

**Status**: FAIL — Broken trapezoidal UI chrome; pervasive diagonal-hatch AI fill; melted elements
**Save path**: `website/public/assets/blog/maho-vs-safari/hero.png`

#### Prompt
Wide horizontal composition, 1376×768, solid near-black background hex 0A0A0A. Centered dark charcoal card hex 1A1A1A with 24px rounded corners, no border, no shadow. Top-right corner of the card: single minimalist line icon of two arrows pointing left and right (swap/compare symbol), coral color hex EE5A3F, 2.5px stroke, 72px size, 48px inset from edges. Inside the card, centered vertically in the upper-middle third: italic serif display text "Maho vs Safari" in warm cream hex F2ECE0, approximately 90px, tight letter-spacing. Below the headline with 60px gap: centered sans-serif medium weight subhead "Defaults are sticky, agentic is not optional." in same cream color, approximately 40px, single line, ending with a period. Editorial minimal typography poster style, absolute minimum visual noise.

#### Negative prompt
(Use canonical negative prompt above verbatim)

#### Icon suggestion
Lucide icon name: `ArrowLeftRight`

#### Headline / subhead
- Headline: "Maho vs Safari"
- Subhead: "Defaults are sticky, agentic is not optional."

#### Alt text (for MDX)
"Editorial card comparing Maho and Safari — defaults vs agentic browsing on macOS"

---

### Post 4: maho-vs-firefox

**Status**: FAIL — Hallucinated Firefox fox-and-flame logo (trademark risk)
**Save path**: `website/public/assets/blog/maho-vs-firefox/hero.png`

#### Prompt
Wide horizontal composition, 1376×768, solid near-black background hex 0A0A0A. Centered dark charcoal card hex 1A1A1A with 24px rounded corners, no border, no shadow. Top-right corner of the card: single minimalist line icon of a vertical split divider, coral color hex EE5A3F, 2.5px stroke, 72px size, 48px inset from edges. Inside the card, centered vertically in the upper-middle third: italic serif display text "Maho vs Firefox" in warm cream hex F2ECE0, approximately 90px, tight letter-spacing. Below the headline with 60px gap: centered sans-serif medium weight subhead "Two takes on user agency." in same cream color, approximately 40px, single line, ending with a period. Editorial minimal typography poster style, absolute minimum visual noise.

#### Negative prompt
(Use canonical negative prompt above verbatim)

#### Icon suggestion
Lucide icon name: `Split`

#### Headline / subhead
- Headline: "Maho vs Firefox"
- Subhead: "Two takes on user agency."

#### Alt text (for MDX)
"Editorial card comparing Maho and Firefox — two approaches to user agency in browsers"

---

### Post 5: maho-vs-brave-leo

**Status**: FAIL — Hallucinated Brave lion head logo (trademark risk)
**Save path**: `website/public/assets/blog/maho-vs-brave-leo/hero.png`

#### Prompt
Wide horizontal composition, 1376×768, solid near-black background hex 0A0A0A. Centered dark charcoal card hex 1A1A1A with 24px rounded corners, no border, no shadow. Top-right corner of the card: single minimalist line icon of a shield with a checkmark inside, coral color hex EE5A3F, 2.5px stroke, 72px size, 48px inset from edges. Inside the card, centered vertically in the upper-middle third: italic serif display text "Maho vs Brave" in warm cream hex F2ECE0, approximately 100px, tight letter-spacing. Below the headline with 60px gap: centered sans-serif medium weight subhead "Privacy plus AI, two approaches." in same cream color, approximately 40px, single line, ending with a period. Editorial minimal typography poster style, absolute minimum visual noise.

#### Negative prompt
(Use canonical negative prompt above verbatim)

#### Icon suggestion
Lucide icon name: `ShieldCheck`

#### Headline / subhead
- Headline: "Maho vs Brave"
- Subhead: "Privacy plus AI, two approaches."

#### Alt text (for MDX)
"Editorial card comparing Maho and Brave Leo — two approaches to privacy-first AI in the browser"

---

### Post 6: maho-vs-chrome-gemini

**Status**: FAIL — Hallucinated Chrome pinwheel logo (trademark risk); malformed window chrome
**Save path**: `website/public/assets/blog/maho-vs-chrome-gemini/hero.png`

#### Prompt
Wide horizontal composition, 1376×768, solid near-black background hex 0A0A0A. Centered dark charcoal card hex 1A1A1A with 24px rounded corners, no border, no shadow. Top-right corner of the card: single minimalist line icon of two arrows pointing left and right (swap/compare symbol), coral color hex EE5A3F, 2.5px stroke, 72px size, 48px inset from edges. Inside the card, centered vertically in the upper-middle third: italic serif display text "Maho vs Chrome" in warm cream hex F2ECE0, approximately 90px, tight letter-spacing. Below the headline with 60px gap: centered sans-serif medium weight subhead "Agentic-first vs AI-bolted-on." in same cream color, approximately 40px, single line, ending with a period. Editorial minimal typography poster style, absolute minimum visual noise.

#### Negative prompt
(Use canonical negative prompt above verbatim)

#### Icon suggestion
Lucide icon name: `ArrowLeftRight`

#### Headline / subhead
- Headline: "Maho vs Chrome"
- Subhead: "Agentic-first vs AI-bolted-on."

#### Alt text (for MDX)
"Editorial card comparing Maho and Chrome with Gemini — agentic-first versus bolted-on AI"

---

### Post 7: maho-vs-edge-copilot

**Status**: FAIL — Hallucinated Edge swirl logo; severe garbled text throughout
**Save path**: `website/public/assets/blog/maho-vs-edge-copilot/hero.png`

#### Prompt
Wide horizontal composition, 1376×768, solid near-black background hex 0A0A0A. Centered dark charcoal card hex 1A1A1A with 24px rounded corners, no border, no shadow. Top-right corner of the card: single minimalist line icon of a key, coral color hex EE5A3F, 2.5px stroke, 72px size, 48px inset from edges. Inside the card, centered vertically in the upper-middle third: italic serif display text "Maho vs Edge" in warm cream hex F2ECE0, approximately 100px, tight letter-spacing. Below the headline with 60px gap: centered sans-serif medium weight subhead "The bundle vs the BYOK." in same cream color, approximately 40px, single line, ending with a period. Editorial minimal typography poster style, absolute minimum visual noise.

#### Negative prompt
(Use canonical negative prompt above verbatim)

#### Icon suggestion
Lucide icon name: `Key`

#### Headline / subhead
- Headline: "Maho vs Edge"
- Subhead: "The bundle vs the BYOK."

#### Alt text (for MDX)
"Editorial card comparing Maho and Edge with Copilot — bundled AI versus bring-your-own-key"

---

### Post 8: maho-vs-opera-aria

**Status**: FAIL — Hallucinated Opera red "O" logo (trademark risk)
**Save path**: `website/public/assets/blog/maho-vs-opera-aria/hero.png`

#### Prompt
Wide horizontal composition, 1376×768, solid near-black background hex 0A0A0A. Centered dark charcoal card hex 1A1A1A with 24px rounded corners, no border, no shadow. Top-right corner of the card: single minimalist line icon of a vertical split divider, coral color hex EE5A3F, 2.5px stroke, 72px size, 48px inset from edges. Inside the card, centered vertically in the upper-middle third: italic serif display text "Maho vs Opera" in warm cream hex F2ECE0, approximately 90px, tight letter-spacing. Below the headline with 60px gap: centered sans-serif medium weight subhead "AI sidebar, two designs." in same cream color, approximately 40px, single line, ending with a period. Editorial minimal typography poster style, absolute minimum visual noise.

#### Negative prompt
(Use canonical negative prompt above verbatim)

#### Icon suggestion
Lucide icon name: `Split`

#### Headline / subhead
- Headline: "Maho vs Opera"
- Subhead: "AI sidebar, two designs."

#### Alt text (for MDX)
"Editorial card comparing Maho and Opera with Aria — two AI sidebar designs for the browser"

---

### Post 9: maho-vs-sigmaos

**Status**: FAIL — Hallucinated Σ-squircle logo; broken chrome; radial AI icon-halo cliché
**Save path**: `website/public/assets/blog/maho-vs-sigmaos/hero.png`

#### Prompt
Wide horizontal composition, 1376×768, solid near-black background hex 0A0A0A. Centered dark charcoal card hex 1A1A1A with 24px rounded corners, no border, no shadow. Top-right corner of the card: single minimalist line icon of two arrows pointing left and right (swap/compare symbol), coral color hex EE5A3F, 2.5px stroke, 72px size, 48px inset from edges. Inside the card, centered vertically in the upper-middle third: italic serif display text "Maho vs SigmaOS" in warm cream hex F2ECE0, approximately 85px, tight letter-spacing. Below the headline with 60px gap: centered sans-serif medium weight subhead "Two macOS-native power browsers." in same cream color, approximately 40px, single line, ending with a period. Editorial minimal typography poster style, absolute minimum visual noise.

#### Negative prompt
(Use canonical negative prompt above verbatim)

#### Icon suggestion
Lucide icon name: `ArrowLeftRight`

#### Headline / subhead
- Headline: "Maho vs SigmaOS"
- Subhead: "Two macOS-native power browsers."

#### Alt text (for MDX)
"Editorial card comparing Maho and SigmaOS — two macOS-native browsers for keyboard-first power users"

---

### Post 10: share-tab-to-cli

**Status**: FAIL — Severely garbled article body text; stray orange overlay line artifact
**Save path**: `website/public/assets/blog/share-tab-to-cli/hero.png`

#### Prompt
Wide horizontal composition, 1376×768, solid near-black background hex 0A0A0A. Centered dark charcoal card hex 1A1A1A with 24px rounded corners, no border, no shadow. Top-right corner of the card: single minimalist line icon of a terminal prompt (rectangle with angle bracket), coral color hex EE5A3F, 2.5px stroke, 72px size, 48px inset from edges. Inside the card, centered vertically in the upper-middle third: italic serif display text "Tab to CLI" in warm cream hex F2ECE0, approximately 100px, tight letter-spacing. Below the headline with 60px gap: centered sans-serif medium weight subhead "Pipe the current tab into your terminal." in same cream color, approximately 40px, single line, ending with a period. Editorial minimal typography poster style, absolute minimum visual noise.

#### Negative prompt
(Use canonical negative prompt above verbatim)

#### Icon suggestion
Lucide icon name: `Terminal`

#### Headline / subhead
- Headline: "Tab to CLI"
- Subhead: "Pipe the current tab into your terminal."

#### Alt text (for MDX)
"Editorial card for sharing browser tabs to the command line — piping tab content into the terminal with Maho"

---

### Post 11: page-context-vs-conversation-context

**Status**: FAIL — Misspelled title "illustraton"; leaked prompt/brief label as final copy
**Save path**: `website/public/assets/blog/page-context-vs-conversation-context/hero.png`

#### Prompt
Wide horizontal composition, 1376×768, solid near-black background hex 0A0A0A. Centered dark charcoal card hex 1A1A1A with 24px rounded corners, no border, no shadow. Top-right corner of the card: single minimalist line icon of a vertical split divider, coral color hex EE5A3F, 2.5px stroke, 72px size, 48px inset from edges. Inside the card, centered vertically in the upper-middle third: italic serif display text "Two Scopes" in warm cream hex F2ECE0, approximately 100px, tight letter-spacing. Below the headline with 60px gap: centered sans-serif medium weight subhead "Page context vs conversation context, one panel." in same cream color, approximately 40px, single line, ending with a period. Editorial minimal typography poster style, absolute minimum visual noise.

#### Negative prompt
(Use canonical negative prompt above verbatim)

#### Icon suggestion
Lucide icon name: `Split`

#### Headline / subhead
- Headline: "Two Scopes"
- Subhead: "Page context vs conversation context, one panel."

#### Alt text (for MDX)
"Editorial card explaining page context versus conversation context — two AI scopes in Maho's side panel"

---

### Post 12: mcp-server-for-your-browser

**Status**: FAIL — Garbled TypeScript code; broken editor chrome
**Save path**: `website/public/assets/blog/mcp-server-for-your-browser/hero.png`

#### Prompt
Wide horizontal composition, 1376×768, solid near-black background hex 0A0A0A. Centered dark charcoal card hex 1A1A1A with 24px rounded corners, no border, no shadow. Top-right corner of the card: single minimalist line icon of a plug with a lightning bolt (plug-zap), coral color hex EE5A3F, 2.5px stroke, 72px size, 48px inset from edges. Inside the card, centered vertically in the upper-middle third: italic serif display text "MCP Server" in warm cream hex F2ECE0, approximately 100px, tight letter-spacing. Below the headline with 60px gap: centered sans-serif medium weight subhead "Run your own tools from the side panel." in same cream color, approximately 40px, single line, ending with a period. Editorial minimal typography poster style, absolute minimum visual noise.

#### Negative prompt
(Use canonical negative prompt above verbatim)

#### Icon suggestion
Lucide icon name: `PlugZap`

#### Headline / subhead
- Headline: "MCP Server"
- Subhead: "Run your own tools from the side panel."

#### Alt text (for MDX)
"Editorial card for running a custom MCP server — registering your own AI tools in the browser"

---

### Post 13: local-models-cost-vs-cloud

**Status**: FAIL — Duplicated X-axis tick label (100, 100) breaks chart credibility
**Save path**: `website/public/assets/blog/local-models-cost-vs-cloud/hero.png`

#### Prompt
Wide horizontal composition, 1376×768, solid near-black background hex 0A0A0A. Centered dark charcoal card hex 1A1A1A with 24px rounded corners, no border, no shadow. Top-right corner of the card: single minimalist line icon of a gauge/speedometer, coral color hex EE5A3F, 2.5px stroke, 72px size, 48px inset from edges. Inside the card, centered vertically in the upper-middle third: italic serif display text "Local vs Cloud" in warm cream hex F2ECE0, approximately 100px, tight letter-spacing. Below the headline with 60px gap: centered sans-serif medium weight subhead "The real cost crossover point." in same cream color, approximately 40px, single line, ending with a period. Editorial minimal typography poster style, absolute minimum visual noise.

#### Negative prompt
(Use canonical negative prompt above verbatim)

#### Icon suggestion
Lucide icon name: `Gauge`

#### Headline / subhead
- Headline: "Local vs Cloud"
- Subhead: "The real cost crossover point."

#### Alt text (for MDX)
"Editorial card comparing local AI model costs versus cloud API pricing — finding the real crossover point"

---

### Post 14: keyboard-first-browsing-in-2026

**Status**: FAIL — Heavy garbled shortcut labels ("Ratting", "Debut", "inceptions", "conopents")
**Save path**: `website/public/assets/blog/keyboard-first-browsing-in-2026/hero.png`

#### Prompt
Wide horizontal composition, 1376×768, solid near-black background hex 0A0A0A. Centered dark charcoal card hex 1A1A1A with 24px rounded corners, no border, no shadow. Top-right corner of the card: single minimalist line icon of a command key symbol (rounded square with corner marks), coral color hex EE5A3F, 2.5px stroke, 72px size, 48px inset from edges. Inside the card, centered vertically in the upper-middle third: italic serif display text "Keyboard First" in warm cream hex F2ECE0, approximately 100px, tight letter-spacing. Below the headline with 60px gap: centered sans-serif medium weight subhead "Not an afterthought." in same cream color, approximately 40px, single line, ending with a period. Editorial minimal typography poster style, absolute minimum visual noise.

#### Negative prompt
(Use canonical negative prompt above verbatim)

#### Icon suggestion
Lucide icon name: `Command`

#### Headline / subhead
- Headline: "Keyboard First"
- Subhead: "Not an afterthought."

#### Alt text (for MDX)
"Editorial card for keyboard-first browsing — Maho's keyboard model as primary interface in 2026"

---

### Post 15: extensions-that-still-work

**Status**: FAIL — Garbled captions ("Checklllist", "Translatted speechers"); contradictory status indicators
**Save path**: `website/public/assets/blog/extensions-that-still-work/hero.png`

#### Prompt
Wide horizontal composition, 1376×768, solid near-black background hex 0A0A0A. Centered dark charcoal card hex 1A1A1A with 24px rounded corners, no border, no shadow. Top-right corner of the card: single minimalist line icon of a puzzle piece, coral color hex EE5A3F, 2.5px stroke, 72px size, 48px inset from edges. Inside the card, centered vertically in the upper-middle third: italic serif display text "Extensions" in warm cream hex F2ECE0, approximately 100px, tight letter-spacing. Below the headline with 60px gap: centered sans-serif medium weight subhead "The Chromium MV3 story in Maho." in same cream color, approximately 40px, single line, ending with a period. Editorial minimal typography poster style, absolute minimum visual noise.

#### Negative prompt
(Use canonical negative prompt above verbatim)

#### Icon suggestion
Lucide icon name: `Puzzle`

#### Headline / subhead
- Headline: "Extensions"
- Subhead: "The Chromium MV3 story in Maho."

#### Alt text (for MDX)
"Editorial card about browser extension compatibility — which Chromium MV3 extensions work in Maho"

---

### Post 16: cli-for-browser-history

**Status**: FAIL — AI-hallucinated gibberish URLs; non-ASCII substitutions; broken timestamps
**Save path**: `website/public/assets/blog/cli-for-browser-history/hero.png`

#### Prompt
Wide horizontal composition, 1376×768, solid near-black background hex 0A0A0A. Centered dark charcoal card hex 1A1A1A with 24px rounded corners, no border, no shadow. Top-right corner of the card: single minimalist line icon of a terminal prompt with a square border (terminal-square), coral color hex EE5A3F, 2.5px stroke, 72px size, 48px inset from edges. Inside the card, centered vertically in the upper-middle third: italic serif display text "History CLI" in warm cream hex F2ECE0, approximately 100px, tight letter-spacing. Below the headline with 60px gap: centered sans-serif medium weight subhead "Search and pipe your browsing history." in same cream color, approximately 40px, single line, ending with a period. Editorial minimal typography poster style, absolute minimum visual noise.

#### Negative prompt
(Use canonical negative prompt above verbatim)

#### Icon suggestion
Lucide icon name: `TerminalSquare`

#### Headline / subhead
- Headline: "History CLI"
- Subhead: "Search and pipe your browsing history."

#### Alt text (for MDX)
"Editorial card for the Maho CLI history tool — search and pipe browsing history from the command line"

---

### Post 17: browser-history-encryption-explained

**Status**: FAIL — Duplicated "encrypted." text line (AI generation artifact)
**Save path**: `website/public/assets/blog/browser-history-encryption-explained/hero.png`

#### Prompt
Wide horizontal composition, 1376×768, solid near-black background hex 0A0A0A. Centered dark charcoal card hex 1A1A1A with 24px rounded corners, no border, no shadow. Top-right corner of the card: single minimalist line icon of a closed padlock, coral color hex EE5A3F, 2.5px stroke, 72px size, 48px inset from edges. Inside the card, centered vertically in the upper-middle third: italic serif display text "History, Encrypted" in warm cream hex F2ECE0, approximately 90px, tight letter-spacing. Below the headline with 60px gap: centered sans-serif medium weight subhead "How Maho stores yours at rest." in same cream color, approximately 40px, single line, ending with a period. Editorial minimal typography poster style, absolute minimum visual noise.

#### Negative prompt
(Use canonical negative prompt above verbatim)

#### Icon suggestion
Lucide icon name: `Lock`

#### Headline / subhead
- Headline: "History, Encrypted"
- Subhead: "How Maho stores yours at rest."

#### Alt text (for MDX)
"Editorial card about browser history encryption — how Maho encrypts browsing history at rest on macOS"

---

### Post 18: boosts-explained

**Status**: FAIL — Pervasive garbled text ("The Mington Pose", "pohypicte's", "Bnaidine", "Gloloies")
**Save path**: `website/public/assets/blog/boosts-explained/hero.png`

#### Prompt
Wide horizontal composition, 1376×768, solid near-black background hex 0A0A0A. Centered dark charcoal card hex 1A1A1A with 24px rounded corners, no border, no shadow. Top-right corner of the card: single minimalist line icon of a sparkles symbol (three-point star burst), coral color hex EE5A3F, 2.5px stroke, 72px size, 48px inset from edges. Inside the card, centered vertically in the upper-middle third: italic serif display text "Boosts" in warm cream hex F2ECE0, approximately 110px, tight letter-spacing. Below the headline with 60px gap: centered sans-serif medium weight subhead "Per-site customization without an extension." in same cream color, approximately 40px, single line, ending with a period. Editorial minimal typography poster style, absolute minimum visual noise.

#### Negative prompt
(Use canonical negative prompt above verbatim)

#### Icon suggestion
Lucide icon name: `Sparkles`

#### Headline / subhead
- Headline: "Boosts"
- Subhead: "Per-site customization without an extension."

#### Alt text (for MDX)
"Editorial card explaining Boosts in Maho — per-site CSS and JS customization without installing an extension"

---

### Post 19: agentic-browser-failure-modes

**Status**: FAIL — Duplicated "Context overflow" tile (content/generation defect)
**Save path**: `website/public/assets/blog/agentic-browser-failure-modes/hero.png`

#### Prompt
Wide horizontal composition, 1376×768, solid near-black background hex 0A0A0A. Centered dark charcoal card hex 1A1A1A with 24px rounded corners, no border, no shadow. Top-right corner of the card: single minimalist line icon of a shield with an exclamation mark (alert-triangle inside shield), coral color hex EE5A3F, 2.5px stroke, 72px size, 48px inset from edges. Inside the card, centered vertically in the upper-middle third: italic serif display text "Failure Modes" in warm cream hex F2ECE0, approximately 95px, tight letter-spacing. Below the headline with 60px gap: centered sans-serif medium weight subhead "Four ways an agentic browser breaks." in same cream color, approximately 40px, single line, ending with a period. Editorial minimal typography poster style, absolute minimum visual noise.

#### Negative prompt
(Use canonical negative prompt above verbatim)

#### Icon suggestion
Lucide icon name: `ShieldAlert`

#### Headline / subhead
- Headline: "Failure Modes"
- Subhead: "Four ways an agentic browser breaks."

#### Alt text (for MDX)
"Editorial card about agentic browser failure modes — prompt injection, runaway loops, context overflow, and silent denial"

---

## AMBIGUOUS Image — Priority 2 (1 image)

---

### Post 20: maho-vs-aside-browser

**Status**: AMBIGUOUS — Fabricated orange triangular mark doesn't match Aside's real branding
**Save path**: `website/public/assets/blog/maho-vs-aside-browser/hero.png`

#### Prompt
Wide horizontal composition, 1376×768, solid near-black background hex 0A0A0A. Centered dark charcoal card hex 1A1A1A with 24px rounded corners, no border, no shadow. Top-right corner of the card: single minimalist line icon of a git-compare branching symbol, coral color hex EE5A3F, 2.5px stroke, 72px size, 48px inset from edges. Inside the card, centered vertically in the upper-middle third: italic serif display text "Maho vs Aside" in warm cream hex F2ECE0, approximately 90px, tight letter-spacing. Below the headline with 60px gap: centered sans-serif medium weight subhead "Agentic AI, two philosophies." in same cream color, approximately 40px, single line, ending with a period. Editorial minimal typography poster style, absolute minimum visual noise.

#### Negative prompt
(Use canonical negative prompt above verbatim)

#### Icon suggestion
Lucide icon name: `GitCompare`

#### Headline / subhead
- Headline: "Maho vs Aside"
- Subhead: "Agentic AI, two philosophies."

#### Alt text (for MDX)
"Editorial card comparing Maho and Aside Browser — two philosophies of agentic AI in the browser"

---

## PASS Images — No Regeneration Needed (31 images)

The following images pass quality audit and should be preserved as-is:

1. `what-is-byok-bring-your-own-key` — Style B reference
2. `maho-vs-dia` — Clean UI mockups
3. `maho-vs-vivaldi` — Clean comparison
4. `agentic-browser-comparison-2026` — Abstract slabs
5. `zero-telemetry-by-default` — Typography + globe
6. `why-sqlite-for-prompt-history` — Clean mockup
7. `what-is-mcp-model-context-protocol` — Editorial card
8. `what-is-an-agentic-browser` — Clean illustration
9. `tool-calling-without-the-mess` — Flow diagram
10. `sync-without-an-account` — Pairing diagram
11. `sync-architecture-end-to-end` — Architecture diagram
12. `spaces-when-tab-groups-arent-enough` — Sidebar mockup
13. `side-panel-as-control-surface` — Browser mockup
14. `ollama-with-maho-step-by-step` — Terminal + browser
15. `mcp-tools-in-the-browser` — Tool grid
16. `lm-studio-with-maho` — Split panel
17. `headless-browsing-with-maho` — Terminal output
18. `byok-vs-managed-ai` — Scale illustration
19. `byok-architecture-explained` — Architecture diagram
20. `building-an-agentic-workflow` — Flow diagram
21. `browser-fingerprinting-defense` — Fingerprint illustration
22. `agentic-vs-chat` — Flowchart
23. `agentic-browser-permission-model` — Permission dialog
24. `verify-maho-locally` — Editorial card
25. `maho-mail-vs-aerc-mutt-neomutt` — Abstract 3D
26. `maho-mail-vs-himalaya` — Abstract 3D
27. `maho-mail-vs-shortwave` — Abstract 3D
28. `maho-mail-vs-spark` — Abstract 3D
29. `maho-mail-vs-thunderbird` — Abstract 3D
30. `maho-mail-vs-superhuman` — Abstract 3D
31. `maho-mail-vs-apple-mail` — Abstract 3D

---

## Generation Notes

### Model-agnostic usage
These prompts are designed to work across Midjourney, DALL-E 3, Flux, SDXL, and Nano Banana. Adapt syntax as needed:
- **Midjourney**: Paste prompt directly, add `--ar 16:9 --style raw --no` + negative items
- **DALL-E 3**: Use prompt as-is; prepend negative prompt items with "I NEED exactly..."
- **Flux/SDXL**: Use prompt in positive field, negative prompt in negative field
- **Nano Banana**: Combine prompt + negative, select 1376×768 resolution

### Consistency tips
- Generate all 20 images in one session for stylistic consistency
- Use the same seed/style-reference across the batch if your tool supports it
- Compare every output against the reference (`what-is-byok-bring-your-own-key/hero.png`)
- Reject any output that shows ANY element from the negative prompt list

### Quality gate (before saving)
For each generated image, verify:
- [ ] Background is solid near-black (no gradient, no texture)
- [ ] Card has no border, no shadow, no glow
- [ ] Icon is a single line-art symbol in coral — not a logo
- [ ] Headline is italic serif in cream — no typos, no extra words
- [ ] Subhead is sans-serif medium — ends with period, no extra lines
- [ ] No competitor logos anywhere
- [ ] No browser UI / window chrome anywhere
- [ ] No garbled text (zoom in and read every character)
- [ ] No AI artifacts (melted shapes, duplicates, extra elements)

---

*End of prompts document. Style A is retired. Do not reference it.*
