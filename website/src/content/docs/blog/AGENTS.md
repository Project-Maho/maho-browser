---
title: "Maho Blog Post & Asset Creation Guidelines"
description: "Canonical reference for authoring, styling, verifying, and illustrating Maho Browser blog posts."
date: 2026-08-24
tags: ["guidelines", "internal"]
excerpt: "Canonical reference for authoring, styling, verifying, and illustrating Maho Browser blog posts."
draft: true
authors:
  - name: Maho Team
---

# MAHO BLOG POST & ASSET CREATION GUIDELINES

**Generated:** 2026-08-24  
**Location:** `website/src/content/docs/blog/`  
**Purpose:** Canonical reference for authoring, styling, verifying, and illustrating Maho Browser blog posts.

---

## 1. OVERVIEW & FILE STRUCTURE

Every blog post consists of an MDX document and three associated image assets:

```
website/
├── src/content/docs/blog/
│   └── <slug>.mdx                      # Article body with YAML frontmatter
└── public/assets/blog/<slug>/
    ├── hero.png                        # Cover card (Satori / Resvg rendered)
    ├── body-1.png                      # First body illustration (agy generated)
    └── body-2.png                      # Second body illustration (agy generated)
```

---

## 2. FRONTMATTER & METADATA CONTRACT

Each MDX file must begin with exact YAML frontmatter:

```yaml
---
title: "Article Title Here"
description: "A concise 1-2 sentence summary of the article."
date: 2026-08-24
tags: ["browser", "workflow", "privacy", "ux"]
excerpt: "A compelling summary displayed on the blog index and RSS feed."
authors:
  - name: Maho Team
---
```

- **Date:** Release date in `YYYY-MM-DD` format.
- **Authors:** Always `Maho Team` unless otherwise specified.
- **Language:** English only.

---

## 3. WRITING STANDARDS & CONTENT RULES

### Tone & Style
- **Clear, Technical, and Grounded:** Write with engineering depth. Explain *why* architectural choices matter rather than making vague marketing claims.
- **Word Count:** 1,200 to 1,600 words per article.
- **Structure:** Clear hierarchy (`##` H2 sections, `###` H3 subsections) with logical progression.

### Mandatory Compliance & Licensing Guardrails
- **PROPRIETARY / CLOSED-SOURCE ONLY:** Maho is closed-source.
  - **NEVER** use open-source claims ("open source", "MIT licensed", "auditable code", etc.).
  - **NEVER** link to public source repositories (`github.com/*/maho*`).
  - Validation check must return 0 matches:
    ```bash
    grep -inE 'open.?source|MIT licen[cs]|github\.com/[A-Za-z0-9_.-]+/maho' <post>.mdx
    ```
- **Security & Performance Claims:**
  - Do not promise unsubstantiated security ("unbreakable", "audited", "more secure") unless explicitly cleared. Focus on concrete UX, auto-lock, and consent mechanisms.
  - Do not fabricate synthetic benchmarks or RAM percentages; explain architectural trade-offs (e.g. native views vs extension background processes) and guide users to measure themselves.

---

## 4. IMAGE ARCHITECTURE: HERO VS. BODY SEPARATION (CRITICAL)

Blog images are divided into two distinct visual roles and production pipelines:

### A. Cover / Hero Image (`hero.png`) — Satori Diagram Card
- **Tool:** Deterministic Node.js script using Satori and Resvg:
  ```bash
  node website/scripts/generate-all-blog-images.mjs
  ```
  *(Note: Always run with `node`, never `bun`, to prevent parser crashes).*
- **Visual Style:**
  - Dark developer background (`#0d0d12`) with subtle radial gradients.
  - Typography: Serif header + monospace metadata tags.
  - Structured summary card with violet accent (`#8b7ff5`) and status indicator.
  - Dimension: `1376x768` (or `1200x630` for social OG).

### B. Body Illustrations (`body-1.png`, `body-2.png`) — `agy` Vector Art
- **Rule:** **NEVER** use the hero card / text-box diagram style inside the article body. Body images must be intuitive visual metaphors.
- **Tool:** `agy` CLI with Gemini image generation:
  ```bash
  agy --dangerously-skip-permissions --print-timeout 5m -p="Use your generate_image tool to create an image and save it exactly to /path/to/target.png. Image prompt: <prompt>. After saving, output only the file path."
  ```
- **Prompt Architecture:**
  - **Style Base:** `Flat vector illustration, dark developer-tool aesthetic: deep charcoal background (#0d0d12), soft violet accent (#8b7ff5), muted slate grays, clean rounded geometric shapes, generous negative space, subtle soft glow, no text, no letters, no watermark.`
  - **Subject:** Concrete conceptual metaphor visualizing the section's core idea (e.g., a robot pausing before a permission shield, an encrypted energy thread between laptop and smartphone, or isolated website cards scoped with a glowing boundary ring).
  - **Text Constraint:** Explicitly prohibit text, alphabet soup, pseudo-labels, and watermarks to maintain a polished, premium aesthetic.

---

## 5. IMAGE REFERENCE CONVENTION IN MDX

Every article must reference exactly three images in order:

```markdown
![Hero description covering the article concept](/assets/blog/<slug>/hero.png)

## Section One
...

![Intuitive visual metaphor for section one](/assets/blog/<slug>/body-1.png)

## Section Two
...

![Intuitive visual metaphor for section two](/assets/blog/<slug>/body-2.png)
```

---

## 6. VERIFICATION & BUILD CHECKLIST

Before finalizing or publishing any blog post:

1. **Compliance Scan:**
   ```bash
   grep -inE 'open.?source|MIT licen[cs]|github\.com/[A-Za-z0-9_.-]+/maho' website/src/content/docs/blog/<slug>.mdx
   ```
2. **Asset Validation:**
   Ensure all three PNG files exist and are valid non-empty images.
3. **Static Build:**
   ```bash
   PUBLIC_RELAY_URL=https://relay.mahobrowser.com bun run --cwd website build
   ```
4. **Dev Server Verification:**
   Verify rendered pages and image responsiveness on `http://localhost:5555/blog/<slug>/`.
