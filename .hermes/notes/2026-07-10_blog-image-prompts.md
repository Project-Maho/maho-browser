# Maho Blog — Image Generation Prompts

Generated: 2026-07-10
Author: Sisyphus-Junior
Scope: hero images (only) for 10 blog posts currently missing `/assets/blog/<slug>/hero.png`.

These prompts are optimized for state-of-the-art image models — Midjourney v7, Nano Banana, DALL·E 3, Stable Diffusion XL/3.5, Flux 1.1 Pro. Copy the prompt block into your preferred tool. Adjust weight syntax (`::2`, `--stylize 250`, etc.) per tool as needed.

---

## Style Guide (READ FIRST)

The Maho blog uses two established hero visual styles. Do not invent a third. Match one of the two references pixel-perfect in mood.

### Style A: Editorial 3D Poster
Reference: `website/public/assets/blog/maho-vs-arc/hero.png` (1376×768).

**What you see in the reference:**
Two vertical slab silhouettes on a near-black stage, symmetrically composed, split by a very subtle central seam. The left slab is warm off-white cream with soft matte surfaces and a slight isometric tilt; the right slab is a darker charcoal with a single vertical persimmon accent stripe on its right edge and a tiny "AI" label baked into the surface. Both slabs sit above small abstract building silhouettes (a landmark/skyline motif). Centered typography: bold sans-serif title with an italic sans-serif subtitle beneath it. Cinematic soft lighting, gentle top-down key light, no harsh shadows, no HDR punch.

**Style A constants:**
- Aspect ratio: 16:9 (target 1376×768 for direct drop-in; models default to 1024×576 → upscale or specify 1408×768 depending on tool).
- Background: near-black (`#080808` to `#0C0C0C`), matte, non-vignetted; imagine a photo studio sweep.
- Left slab / primary object: warm off-white cream (`#EDE8DF` to `#E6DFCF`), matte, subtle drop shadow, isometric perspective, tall vertical proportions.
- Right slab / secondary object: dark warm charcoal (`#1A1817` to `#242220`), same isometric perspective, one persimmon accent (`#E85A2C` / `#EE5A3F`) as a stripe / label / edge highlight.
- Optional 3rd/4th objects: repeat the slab motif at smaller scale in the lower third to imply skyline/landmark.
- Composition: symmetrical two-column split OR asymmetric "hero object + smaller supporting object" — both are on-brand.
- Typography: bold sans-serif primary (Inter / Geist / IBM Plex Sans Bold), italic sans-serif subtitle (Inter Italic / Geist Italic). Cream (`#EDE8DF`) title, muted grey (`#8A857D`) subtitle. Roughly 42-48pt title, 22-24pt subtitle at 1376 wide.
- Lighting: soft top-down key + faint rim light on slab edges. No lens flare, no bloom, no HDR.
- Mood: calm, editorial, expensive-print-magazine, minimal, unhurried.

**Style A forbidden elements:**
- No people, no faces, no hands.
- No literal browser chrome / competitor logos / real UI screenshots. Slabs are abstract proxies, not renders of any real browser.
- No Korean text.
- No em dashes in overlay text.
- No open-source claim language ("open source", "MIT", "auditable source", etc.).
- No neon glow, no cyberpunk, no "AI aesthetic" default (glowing brains, circuit boards, robot heads). Reject those instantly.

### Style B: Editorial-Literary Card
Reference: `website/public/assets/blog/what-is-byok-bring-your-own-key/hero.png` (1376×768).

**What you see in the reference:**
A large centered rounded rectangle card sitting on a solid near-black stage. The card is a slightly-lighter warm charcoal, matte, with a soft edge. In the top-right corner of the card, a Lucide-style stroke-only line icon (a persimmon key silhouette). Centered inside the card: a very large italic serif primary word (e.g. "BYOK") in warm cream, then two lines of sans-serif subtitle in cream slightly below. Enormous negative space around the card. No shadows, no gradients, no textures.

**Style B constants:**
- Aspect ratio: 16:9 (1376×768).
- Background: near-black (`#080808` to `#0C0C0C`), one flat color, no gradient.
- Card: warm-tinted charcoal (`#1B1A18` to `#221F1C`), gently rounded corners (~24px radius at 1376 wide), matte, roughly 62% of canvas width, centered vertically slightly above the midline.
- Icon: single Lucide-style icon (2px stroke, no fill except where the Lucide default is filled), persimmon (`#E85A2C` / `#EE5A3F`), placed in the top-right interior of the card. About 6-8% of card height.
- Primary text: large italic serif, warm cream (`#EDE8DF`). Reference uses a display serif close to DM Serif Display / Playfair Display italic. All caps or title-case as appropriate.
- Subtitle: 2-line sans-serif (Inter / Geist), warm cream, roughly 28-32pt at 1376 wide, tight line-height.
- Layout: title on line 1, subtitle on lines 2-3. Centered horizontally.
- Mood: quiet, editorial, literary. Like a book chapter opener.

**Style B forbidden elements:**
Same forbidden list as Style A, plus:
- No competing decoration inside the card (no small shapes, no line art besides the single icon).
- No dropshadows.

### Global constants (both styles)

- **Dimensions:** 1376×768 (aspect ratio 1.79:1, close to 16:9). Match exactly if the tool supports arbitrary sizes; otherwise use 1408×768 or 1536×864 and downscale.
- **Format:** PNG. Save at 8-bit RGB.
- **File size target:** under 400KB (compress with `pngquant` or export at 85% quality). See SEO audit Section 5 — the existing Style A `hero.png` weighs 900KB, which hurts LCP.
- **Save path:** `website/public/assets/blog/<slug>/hero.png`.
- **Language of any overlay text:** English only.
- **No competitor logos or trademarked UI.** Use abstract shape/color proxies.

### Universal negative prompt (append to every prompt)

```
no people, no faces, no hands, no logos, no brand marks, no trademarked UI, no browser chrome, no competitor screenshots, no Korean text, no Chinese text, no Japanese text, no code snippets, no circuit boards, no glowing brains, no robot heads, no neon, no cyberpunk, no HDR, no lens flare, no bloom, no cluttered composition, no oversaturation, no watermarks, no photo stock cliches, no stock illustration style, no 3D character art, no cartoon, no comic style, no anime, no gradient rainbow, no purple-blue tech gradient
```

---

## Post 1: agentic-browser-comparison-2026

Slug: `agentic-browser-comparison-2026`
Post type: overview comparison (splash template, pillar page for the "agentic browser" cluster)
Save to: `website/public/assets/blog/agentic-browser-comparison-2026/hero.png`
Content signal: compares 7 browsers (Maho, Dia, Chrome + Gemini, Edge + Copilot, Brave Leo, Opera Aria, SigmaOS Airis). Long-form (2003 words), 10 H2s, table-driven.

Style: **A (Editorial 3D Poster)** — adapted for a *group* composition, not a duel.

### Prompt

```
Editorial 3D poster on a matte near-black studio stage (#0A0A0A). Seven abstract vertical slab silhouettes standing in a loose horizontal line, gently receding in one-point perspective. One slab in the near-center foreground is taller and made of warm off-white cream (#EDE8DF), matte, isometric tilt, with a slim vertical persimmon accent stripe (#E85A2C) on its right edge. The other six slabs step away in scale: charcoal (#1A1817), taupe (#2A2724), dark warm grey (#141412), varying subtly in width and height so each reads distinct without labels. Soft top-down key light, faint rim highlights, no HDR. Centered typography above the slab row: bold sans-serif title "Agentic browsers, 2026" in cream (#EDE8DF), then a shorter italic sans-serif subtitle "seven ways to make the browser do things" in muted grey (#8A857D). Cinematic, minimal, calm. Aspect ratio 16:9, target 1376 by 768 pixels.
```

**Suggested negative prompts:** (universal negative prompt block above)

**Composition:** asymmetric hero — the cream slab sits slightly left of center; the six other slabs form a receding line to the right. Negative space above and below title. Focal point: the cream slab.

**Text overlay instructions:**
- Title (line 1): `Agentic browsers, 2026` — Inter Bold or Geist Bold, 44pt at 1376 wide, letter-spacing -1%, color `#EDE8DF`.
- Subtitle (line 2, below title): `seven ways to make the browser do things` — Inter Italic or Geist Italic, 22pt, color `#8A857D`.
- Both centered horizontally, occupying the upper third of the canvas.

### Alt text (to add to MDX after generation)

`Seven abstract vertical slabs on a dark stage representing the agentic browsers compared in 2026 — Maho, Dia, Chrome + Gemini, Edge + Copilot, Brave Leo, Opera Aria, SigmaOS Airis.`
(206 chars — long, so trim to:) `Seven abstract slabs on a dark stage representing the seven agentic browsers compared: Maho, Dia, Chrome, Edge, Brave, Opera, SigmaOS.` (128 chars, tune to <=125 if needed)

### MDX insertion

The post currently has no images. Insert the hero image reference immediately after the frontmatter fence and before the first paragraph. Around line 16 of `src/content/docs/blog/agentic-browser-comparison-2026.mdx`, just after the `<div class="blog-post--agentic-browser-comparison-2026" hidden></div>` marker and before the first prose paragraph on line 19:

```mdx
![Seven abstract slabs on a dark stage representing the seven agentic browsers compared: Maho, Dia, Chrome, Edge, Brave, Opera, SigmaOS.](/assets/blog/agentic-browser-comparison-2026/hero.png)
```

---

## Post 2: maho-vs-aside-browser

Slug: `maho-vs-aside-browser`
Post type: 1-vs-1 comparison
Save to: `website/public/assets/blog/maho-vs-aside-browser/hero.png`
Content signal: Both are Chromium forks. Aside leads on autonomous agent benchmarks; Maho pairs steered AI with cross-device Spaces and BYOK pricing. Different philosophies — autonomous vs consented.

Style: **A (Editorial 3D Poster)** — canonical duel composition.

### Prompt

```
Editorial 3D poster on a matte near-black studio stage (#0A0A0A). Two vertical slab silhouettes facing each other, symmetric two-column split composition with a subtle central seam. Left slab is warm off-white cream (#EDE8DF), matte, isometric tilt, quiet and grounded, with a tiny abstract "device" glyph (a small stacked-rectangle icon suggesting cross-device sync) embedded low on its front face in a slightly darker cream. Right slab is dark warm charcoal (#1A1817), same isometric tilt, with a bright persimmon accent stripe (#E85A2C) running vertically along its right edge and a small compass-arrow glyph in persimmon near its top suggesting autonomous direction. Both slabs sit above two small abstract building silhouettes in cream, one under each slab. Soft top-down key light, faint rim highlights. Centered typography above the slabs: bold sans-serif title "Maho vs Aside" in cream, italic sans-serif subtitle "when the agent asks first" beneath it in muted grey (#8A857D). Cinematic, minimal, calm. Aspect ratio 16:9, target 1376 by 768.
```

**Negative:** universal block.

**Composition:** symmetric duel. Left = Maho (cream). Right = Aside (charcoal + persimmon).

**Text overlay:**
- Title: `Maho vs Aside` — bold sans-serif, 46pt, cream `#EDE8DF`, centered.
- Subtitle: `when the agent asks first` — italic sans-serif, 22pt, muted grey `#8A857D`, centered below title.

### Alt text

`Two abstract slabs facing each other on a dark stage. Left cream slab represents Maho; right dark slab with a persimmon accent represents Aside Browser.` (147 chars, trim as needed)

### MDX insertion

Insert immediately after the frontmatter (currently line 12 is the last frontmatter line, line 14 starts the intro paragraph). Add on line 13:

```mdx
![Two abstract slabs facing each other on a dark stage. Left cream slab represents Maho; right dark slab with a persimmon accent represents Aside.](/assets/blog/maho-vs-aside-browser/hero.png)
```

---

## Post 3: maho-mail-vs-apple-mail

Slug: `maho-mail-vs-apple-mail`
Post type: mail-vs comparison
Save to: `website/public/assets/blog/maho-mail-vs-apple-mail/hero.png`
Content signal: The pre-installed default (Apple Mail) vs a programmable email client with CLI, MCP, and BYOK AI. Deliberate choice vs zero-setup default.

Style: **A (Editorial 3D Poster) adapted for MAIL context.** Replace browser-slab motif with envelope/inbox-tray motif.

### Prompt

```
Editorial 3D poster on a matte near-black studio stage (#0A0A0A). Two abstract mail-object silhouettes facing each other, symmetric two-column split composition. Left object is a stack of three closed envelopes rendered in warm off-white cream (#EDE8DF), matte, isometric tilt, tidy stack with slight edge lift on the top envelope, subtle drop shadow. Right object is the same isometric envelope stack but in dark warm charcoal (#1A1817), with one thin persimmon (#E85A2C) horizontal stripe across the front of the topmost envelope suggesting a highlighted state, and a small terminal-window rectangle in cream floating above the stack with three tiny cream dots indicating a command line. Both stacks sit above small abstract building silhouettes in cream. Soft top-down key light, faint rim highlights. Centered typography above the stacks: bold sans-serif title "Maho Mail vs Apple Mail" in cream, italic subtitle "the default vs the deliberate choice" beneath in muted grey (#8A857D). Cinematic, minimal, calm. Aspect ratio 16:9, target 1376 by 768.
```

**Negative:** universal block + `no realistic paper envelope, no wax seal, no stamps, no return address, no email UI mockup, no gmail-style icons`.

**Composition:** symmetric duel. Left = Apple Mail (cream envelope stack). Right = Maho Mail (charcoal stack + terminal glyph).

**Text overlay:**
- Title: `Maho Mail vs Apple Mail` — bold sans-serif, 42pt, cream.
- Subtitle: `the default vs the deliberate choice` — italic sans-serif, 22pt, muted grey.

### Alt text

`Two abstract envelope stacks facing each other on a dark stage. Left cream stack represents Apple Mail; right charcoal stack with a terminal window and persimmon stripe represents Maho Mail.` (~180 chars, trim to:) `Two abstract envelope stacks on a dark stage — cream for Apple Mail, charcoal with terminal glyph and persimmon accent for Maho Mail.` (~130 chars)

### MDX insertion

Insert immediately after the frontmatter (line 11) and before the tagline paragraph on line 13:

```mdx
![Two abstract envelope stacks on a dark stage — cream for Apple Mail, charcoal with terminal glyph and persimmon accent for Maho Mail.](/assets/blog/maho-mail-vs-apple-mail/hero.png)
```

---

## Post 4: maho-mail-vs-superhuman

Slug: `maho-mail-vs-superhuman`
Post type: mail-vs comparison
Save to: `website/public/assets/blog/maho-mail-vs-superhuman/hero.png`
Content signal: Premium keyboard-first email ($30/mo) vs free programmable client with CLI + MCP + BYOK.

Style: **A adapted for MAIL.** Duel motif with keyboard-key metaphor for Superhuman side.

### Prompt

```
Editorial 3D poster on a matte near-black studio stage (#0A0A0A). Two abstract mail-object silhouettes in a symmetric two-column split composition. Left object is a single tall vertical envelope in warm off-white cream (#EDE8DF), matte, isometric tilt, with a small floating cream keyboard-key glyph hovering just above its top edge (a rounded square with a chamfered edge suggesting a keyboard cap). Right object is the same isometric envelope shape but in dark warm charcoal (#1A1817) with a bright persimmon accent stripe (#E85A2C) running vertically along its right edge, and a tiny cream terminal-window rectangle floating above with three dots indicating a command prompt. Both envelopes sit above two small abstract building silhouettes in cream. Soft top-down key light, faint rim highlights. Centered typography above: bold sans-serif title "Maho Mail vs Superhuman" in cream, italic subtitle "premium speed vs programmable power" in muted grey (#8A857D). Cinematic, minimal, calm. Aspect ratio 16:9, 1376 by 768.
```

**Negative:** universal block + `no realistic keyboard photo, no gmail-style icon, no smartphone`.

**Composition:** symmetric duel. Left = Superhuman (cream envelope + keyboard-key glyph). Right = Maho Mail (charcoal envelope + terminal glyph + persimmon).

**Text overlay:**
- Title: `Maho Mail vs Superhuman` — bold sans-serif, 42pt, cream.
- Subtitle: `premium speed vs programmable power` — italic sans-serif, 22pt, muted grey.

### Alt text

`Two abstract envelopes on a dark stage — cream envelope with keyboard-key glyph for Superhuman, charcoal envelope with terminal window and persimmon accent for Maho Mail.` (~168 chars, trim:) `Two envelopes on a dark stage — cream with keyboard-key for Superhuman, charcoal with terminal and persimmon accent for Maho Mail.` (~130 chars)

### MDX insertion

Insert after frontmatter (line 11), before the tagline paragraph on line 13:

```mdx
![Two envelopes on a dark stage — cream with keyboard-key for Superhuman, charcoal with terminal and persimmon accent for Maho Mail.](/assets/blog/maho-mail-vs-superhuman/hero.png)
```

---

## Post 5: maho-mail-vs-thunderbird

Slug: `maho-mail-vs-thunderbird`
Post type: mail-vs comparison
Save to: `website/public/assets/blog/maho-mail-vs-thunderbird/hero.png`
Content signal: Modern minimal proprietary email client vs Mozilla's veteran open-source desktop incumbent with calendar, contacts, addons.

Style: **A adapted for MAIL.** Duel motif contrasting a *stacked / feature-rich* form on Thunderbird side with a *singular / focused* form on Maho side.

### Prompt

```
Editorial 3D poster on a matte near-black studio stage (#0A0A0A). Two abstract mail-object compositions in a symmetric two-column split. Left composition is a tall stack of six varied cream (#EDE8DF) rectangular slabs arranged like a filing cabinet (envelope, envelope, calendar-page, address-card, envelope, envelope), matte, isometric tilt, subtle drop shadow, dense and layered. Right composition is a single tall clean isometric envelope in dark warm charcoal (#1A1817) with a bright persimmon accent stripe (#E85A2C) along its right edge and a small cream terminal-window glyph floating just above it with three dots. Both compositions sit above two small abstract building silhouettes in cream. Soft top-down key light, faint rim highlights. Centered typography above: bold sans-serif title "Maho Mail vs Thunderbird" in cream, italic subtitle "the legacy client vs the modern one" in muted grey (#8A857D). Cinematic, minimal, calm. Aspect ratio 16:9, 1376 by 768.
```

**Negative:** universal block + `no real thunderbird logo, no bird illustration, no Mozilla mark, no calendar UI screenshot`.

**Composition:** asymmetric emphasis. Left = Thunderbird (dense cream stack — implies "everything"). Right = Maho Mail (single focused envelope + terminal glyph).

**Text overlay:**
- Title: `Maho Mail vs Thunderbird` — bold sans-serif, 42pt, cream.
- Subtitle: `the legacy client vs the modern one` — italic sans-serif, 22pt, muted grey.

### Alt text

`Left: a dense cream stack of envelope, calendar and address-card shapes representing Thunderbird. Right: a single charcoal envelope with terminal glyph and persimmon accent for Maho Mail.` (~184 chars, trim:) `A dense cream stack for Thunderbird contrasts a single charcoal envelope with terminal glyph and persimmon accent for Maho Mail.` (~127 chars)

### MDX insertion

Insert after frontmatter (line 11), before the tagline paragraph on line 13:

```mdx
![A dense cream stack for Thunderbird contrasts a single charcoal envelope with terminal glyph and persimmon accent for Maho Mail.](/assets/blog/maho-mail-vs-thunderbird/hero.png)
```

---

## Post 6: maho-mail-vs-spark

Slug: `maho-mail-vs-spark`
Post type: mail-vs comparison
Save to: `website/public/assets/blog/maho-mail-vs-spark/hero.png`
Content signal: Team collaboration email (shared drafts, assignments) vs solo local automation with CLI + MCP.

Style: **A adapted for MAIL.** Contrast "many envelopes tied together" (team) vs "one envelope, one terminal" (solo).

### Prompt

```
Editorial 3D poster on a matte near-black studio stage (#0A0A0A). Two abstract mail compositions in a symmetric two-column split. Left composition is three cream (#EDE8DF) envelopes arranged in a loose cluster with thin cream connector lines between them suggesting a shared thread or handoff, matte, isometric tilt. Right composition is a single clean isometric envelope in dark warm charcoal (#1A1817) with a persimmon accent stripe (#E85A2C) along its right edge and a small cream terminal-window glyph floating above it. Both compositions sit above small abstract building silhouettes in cream. Soft top-down key light, faint rim highlights. Centered typography above: bold sans-serif title "Maho Mail vs Spark" in cream, italic subtitle "team collaboration vs solo automation" in muted grey (#8A857D). Cinematic, minimal, calm. Aspect ratio 16:9, 1376 by 768.
```

**Negative:** universal block + `no avatar circles, no chat bubbles, no team photo, no group emoji`.

**Composition:** asymmetric. Left = Spark (3-envelope cluster with connector lines). Right = Maho Mail (single envelope + terminal glyph).

**Text overlay:**
- Title: `Maho Mail vs Spark` — bold sans-serif, 42pt, cream.
- Subtitle: `team collaboration vs solo automation` — italic sans-serif, 22pt, muted grey.

### Alt text

`A three-envelope cluster with connector lines representing Spark's team email, next to a single charcoal envelope with terminal glyph and persimmon accent for Maho Mail.` (~164 chars, trim:) `A three-envelope cluster for Spark's team email next to a charcoal envelope with terminal glyph and persimmon accent for Maho Mail.` (~130 chars)

### MDX insertion

Insert after frontmatter (line 11), before the tagline paragraph on line 13:

```mdx
![A three-envelope cluster for Spark's team email next to a charcoal envelope with terminal glyph and persimmon accent for Maho Mail.](/assets/blog/maho-mail-vs-spark/hero.png)
```

---

## Post 7: maho-mail-vs-shortwave

Slug: `maho-mail-vs-shortwave`
Post type: mail-vs comparison
Save to: `website/public/assets/blog/maho-mail-vs-shortwave/hero.png`
Content signal: AI wrapper on Gmail (Shortwave) vs AI-native local architecture (Maho Mail). Wrapper vs foundation.

Style: **A adapted for MAIL.** Contrast an envelope *wrapped in a translucent outer shell* (Shortwave AI layer) vs a solid envelope with a terminal (Maho).

### Prompt

```
Editorial 3D poster on a matte near-black studio stage (#0A0A0A). Two abstract mail compositions in a symmetric two-column split. Left composition is a warm off-white cream (#EDE8DF) envelope enclosed inside a slightly larger translucent cream outer shell (a rounded rectangle wrap with 30% opacity) suggesting an added AI layer wrapping a familiar email, matte, isometric tilt. Right composition is a single solid isometric envelope in dark warm charcoal (#1A1817) with a persimmon accent stripe (#E85A2C) along its right edge and a small cream terminal-window glyph floating above it. Both compositions sit above small abstract building silhouettes in cream. Soft top-down key light, faint rim highlights. Centered typography above: bold sans-serif title "Maho Mail vs Shortwave" in cream, italic subtitle "AI wrapper vs AI-native architecture" in muted grey (#8A857D). Cinematic, minimal, calm. Aspect ratio 16:9, 1376 by 768.
```

**Negative:** universal block + `no gmail logo, no google workspace mark, no chat interface, no sparkle icon`.

**Composition:** asymmetric. Left = Shortwave (envelope inside translucent wrap). Right = Maho Mail (envelope + terminal + persimmon).

**Text overlay:**
- Title: `Maho Mail vs Shortwave` — bold sans-serif, 42pt, cream.
- Subtitle: `AI wrapper vs AI-native architecture` — italic sans-serif, 22pt, muted grey.

### Alt text

`A cream envelope wrapped in a translucent outer shell representing Shortwave's AI layer, next to a charcoal envelope with terminal glyph and persimmon accent for Maho Mail.` (~168 chars, trim:) `A cream envelope in a translucent shell for Shortwave's AI wrapper, next to a charcoal envelope with terminal and persimmon for Maho Mail.` (~137 chars)

### MDX insertion

Insert after frontmatter (line 11), before the tagline paragraph on line 13:

```mdx
![A cream envelope in a translucent shell for Shortwave's AI wrapper, next to a charcoal envelope with terminal and persimmon for Maho Mail.](/assets/blog/maho-mail-vs-shortwave/hero.png)
```

---

## Post 8: maho-mail-vs-himalaya

Slug: `maho-mail-vs-himalaya`
Post type: mail-vs comparison
Save to: `website/public/assets/blog/maho-mail-vs-himalaya/hero.png`
Content signal: Himalaya is a pure-CLI Rust email client. Maho Mail adds a GUI + MCP + AI to the CLI model.

Style: **A adapted for MAIL + terminal aesthetic.** Both sides emphasize the CLI motif; Maho adds envelope; Himalaya is just terminal.

### Prompt

```
Editorial 3D poster on a matte near-black studio stage (#0A0A0A). Two abstract compositions in a symmetric two-column split. Left composition is a single cream (#EDE8DF) terminal-window rectangle in isometric tilt with three tiny cream traffic-light dots at its top-left and a suggestion of four short horizontal cream lines inside it representing text output, matte, subtle drop shadow, standing tall like a slab. Right composition is a cream terminal-window rectangle of the same size joined at its base to a dark warm charcoal (#1A1817) isometric envelope, with a persimmon accent stripe (#E85A2C) along the envelope's right edge, suggesting the CLI and the envelope belong to the same product. Both compositions sit above small abstract building silhouettes in cream. Soft top-down key light, faint rim highlights. Centered typography above: bold sans-serif title "Maho Mail vs himalaya" in cream, italic subtitle "both have a CLI, only one has everything else" in muted grey (#8A857D). Cinematic, minimal, calm. Aspect ratio 16:9, 1376 by 768.
```

**Negative:** universal block + `no snow mountain photo (himalaya name is unrelated to mountains here), no code text you can read, no real terminal UI screenshot`.

**Composition:** asymmetric. Left = himalaya (single terminal-slab). Right = Maho Mail (terminal + envelope joined + persimmon).

**Text overlay:**
- Title: `Maho Mail vs himalaya` — bold sans-serif, 42pt, cream. Note: "himalaya" lowercase intentionally, matching the tool's own casing.
- Subtitle: `both have a CLI, only one has everything else` — italic sans-serif, 20pt, muted grey.

### Alt text

`A cream terminal window slab representing the himalaya CLI, next to a joined terminal-and-envelope composition with persimmon accent representing Maho Mail's CLI plus GUI plus MCP.` (~183 chars, trim:) `A cream terminal slab for the himalaya CLI, next to a joined terminal-and-envelope with persimmon accent for Maho Mail's CLI + GUI + MCP.` (~135 chars)

### MDX insertion

Insert after frontmatter (line 12), before the tagline paragraph on line 14:

```mdx
![A cream terminal slab for the himalaya CLI, next to a joined terminal-and-envelope with persimmon accent for Maho Mail's CLI + GUI + MCP.](/assets/blog/maho-mail-vs-himalaya/hero.png)
```

---

## Post 9: maho-mail-vs-aerc-mutt-neomutt

Slug: `maho-mail-vs-aerc-mutt-neomutt`
Post type: 3-way mail-vs comparison
Save to: `website/public/assets/blog/maho-mail-vs-aerc-mutt-neomutt/hero.png`
Content signal: Three terminal-UI email clients (aerc, mutt, neomutt) vs Maho Mail's GUI + CLI + MCP triple interface.

Style: **A adapted for MAIL + TUI aesthetic.** Three tiny terminal slabs vs one triple-composition on the Maho side.

### Prompt

```
Editorial 3D poster on a matte near-black studio stage (#0A0A0A). Two abstract compositions in an asymmetric split. Left composition is three cream (#EDE8DF) terminal-window rectangles arranged in a loose descending stagger, each in isometric tilt, each with three tiny traffic-light dots at the top-left and a few short cream horizontal lines suggesting text output; different heights to imply distinct tools that share a family resemblance. Right composition is a triple-stack of three joined shapes standing as one taller slab: a cream terminal window at the top, a warm charcoal (#1A1817) isometric envelope in the middle, and a smaller cream card at the base suggesting an MCP-connected surface; a bright persimmon accent stripe (#E85A2C) runs vertically along the right edge of the whole triple-stack. All compositions sit above small abstract building silhouettes in cream. Soft top-down key light, faint rim highlights. Centered typography above: bold sans-serif title "Maho Mail vs aerc, mutt, neomutt" in cream, italic subtitle "TUI email vs three interfaces in one" in muted grey (#8A857D). Cinematic, minimal, calm. Aspect ratio 16:9, 1376 by 768.
```

**Negative:** universal block + `no real terminal text you can read, no vim UI, no ncurses screenshot, no ASCII art, no penguin`.

**Composition:** asymmetric. Left = three staggered terminal slabs (aerc + mutt + neomutt). Right = one triple-stack composition with persimmon (Maho Mail).

**Text overlay:**
- Title: `Maho Mail vs aerc, mutt, neomutt` — bold sans-serif, 40pt, cream. (Note: title updated to the SEO-audit suggested rewrite — <=60 chars.)
- Subtitle: `TUI email vs three interfaces in one` — italic sans-serif, 22pt, muted grey.

### Alt text

`Three staggered cream terminal-window slabs representing aerc, mutt and neomutt, next to a taller triple-stack composition with a persimmon accent representing Maho Mail's GUI plus CLI plus MCP.` (~194 chars, trim:) `Three cream terminal slabs for aerc, mutt and neomutt next to a triple-stack with persimmon for Maho Mail's GUI + CLI + MCP.` (~123 chars)

### MDX insertion

Insert after frontmatter (line 12), before the tagline paragraph on line 14:

```mdx
![Three cream terminal slabs for aerc, mutt and neomutt next to a triple-stack with persimmon for Maho Mail's GUI + CLI + MCP.](/assets/blog/maho-mail-vs-aerc-mutt-neomutt/hero.png)
```

---

## Post 10: verify-maho-locally

Slug: `verify-maho-locally`
Post type: how-to (privacy / trust)
Save to: `website/public/assets/blog/verify-maho-locally/hero.png`
Content signal: Step-by-step guide to verifying Maho's privacy claims without reading source. Uses Activity Monitor, Little Snitch, Wireshark, Console.app. The theme is verification, receipts, honest audit.

Style: **B (Editorial-Literary Card).** Follow the reference exactly: dark stage, centered rounded card, one persimmon Lucide icon in the top-right, big italic serif primary word, 2-line sans-serif subtitle.

Icon: `ShieldCheck` (Lucide) — a shield outline with a checkmark inside. Fits the "verify" theme cleanly. Alternative: `Fingerprint`. Prefer `ShieldCheck` because the checkmark reinforces "confirmed / passes audit."

### Prompt

```
Editorial-literary card composition on a matte near-black studio stage (#0A0A0A). Centered rounded rectangle card occupying about 62% of the canvas width, positioned slightly above vertical center, warm-tinted charcoal fill (#1B1A18), gently rounded corners about 24 pixel radius, matte flat surface, no gradient, no drop shadow. In the top-right interior of the card, a single Lucide-style stroke-only line icon of a shield with a checkmark inside, rendered in bright persimmon (#E85A2C), 2-pixel stroke, no fill, about 7% of card height. Centered inside the card: a very large italic serif primary word "Verify" in warm cream (#EDE8DF), display serif close to Playfair Display Italic or DM Serif Display Italic, roughly 96pt at 1376 wide. Below the primary word, a two-line sans-serif subtitle in warm cream (Inter or Geist), roughly 30pt, tight line-height, reading "How to check Maho's privacy" on line one and "claims yourself, without source code." on line two, both centered. Enormous negative space around the card. Quiet, editorial, literary, like a book chapter opener. Aspect ratio 16:9, 1376 by 768.
```

**Negative:** universal block + `no padlock, no eye icon, no key icon (reserved for BYOK post), no privacy-cliche imagery, no magnifying glass, no security-camera icon, no server rack`.

**Composition:** dead-center card, generous negative space, single persimmon icon accent in top-right of card.

**Text overlay:**
- Icon: Lucide `ShieldCheck` in persimmon `#E85A2C`, 2px stroke, top-right of card interior.
- Primary: `Verify` — display serif italic (Playfair Display Italic / DM Serif Display Italic), 96pt, warm cream `#EDE8DF`, centered.
- Subtitle line 1: `How to check Maho's privacy` — sans-serif (Inter or Geist), 30pt, warm cream, centered.
- Subtitle line 2: `claims yourself, without source code.` — same font, same size, same color, centered.

### Alt text

`Editorial card on a dark stage. Bold italic serif word "Verify" and a persimmon shield-with-check icon representing local verification of Maho's privacy claims without reading source code.` (~192 chars, trim:) `Editorial card with the italic serif word "Verify" and a persimmon shield-with-check icon, representing local verification of Maho's privacy claims.` (~148 chars, trim further:) `Editorial card with italic serif "Verify" and a persimmon shield-check icon for local verification of Maho's privacy claims.` (~123 chars)

### MDX insertion

Insert after frontmatter (line 12), before the `import { Aside }` line on line 14:

```mdx
![Editorial card with italic serif "Verify" and a persimmon shield-check icon for local verification of Maho's privacy claims.](/assets/blog/verify-maho-locally/hero.png)
```

Note: the MDX imports statement should remain on line 14 after insertion; the image line goes on line 13.

---

## Post-generation checklist

For each image, before saving into `website/public/assets/blog/<slug>/hero.png`:

1. Confirm dimensions are 1376×768 (or crop/resize losslessly to that).
2. Confirm the file is <400KB. If not, compress with `pngquant --quality=70-85` or export at reduced quality. LCP suffers past 400KB.
3. Confirm no faces, no logos, no readable Korean/Japanese/Chinese text, no readable code snippets.
4. Confirm the persimmon accent hex is in the `#E85A2C` to `#EE5A3F` range (not orange-red or pure red). Adjust in Photoshop / Affinity / Figma if the model drifted.
5. Confirm the cream hex is in the `#EDE8DF` to `#E6DFCF` range (warm off-white, not pure white and not yellow).
6. Confirm the background is matte near-black, not gradient, not blue-black, not grey.
7. Confirm no dropshadow bleed off the card in Style B.
8. Confirm typography — if the model can't render the exact overlay text, generate the image *without* text and add text in Figma / Sketch as a text layer over the export. This is the more reliable path.

## Batch metadata for follow-up

Once all 10 hero images ship, per-post follow-ups (out of scope for this deliverable but flagged for planning):
- Body illustrations (`body-1.png`, `body-2.png`) can be added later using the same Style A/B constants; not blocking.
- Update `StarlightHead.astro` JSON-LD so each post's `image` field points to `/assets/blog/<slug>/hero.png` — see SEO audit Section 1.1.
- Update the SEO-audit-suggested title rewrites for `maho-mail-vs-aerc-mutt-neomutt` (currently 73 chars) and `maho-vs-aside-browser` (currently 21 chars) — see SEO audit Appendix A.
