# Maho Hero shared frame contract

## Contract status

- Gate: B — storyboard and source-derived sketch review.
- Approval: pending. This file is a proposal, not an approval receipt.
- Composition hold: `compositions/*.html` is forbidden until a human approves the exact storyboard, frame contract, review images, and hashes.
- Invalidation: any requested frame, camera, crop, checkpoint, or source change invalidates this packet and returns the work to Todo 3.

## Output and ownership

- Later delivery consists of exactly three independent silent outputs: Spaces, Split View, and Command Bar + AI.
- Each output uses its one source recording from source time `0.000s`, at `1x`, for the complete duration locked in `source-manifest.json`.
- No source trim, retime, reorder, replacement, reconstruction, synthetic cursor, label, caption, audio, or generated UI is allowed.
- No master video, cross-scene frame, or rendered cross-scene transition is allowed. The website owns any future handoff between separate files.

## Fixed canvas and layer contract

- Canvas: `1920x1080`, later rendered at `30fps`.
- External background child: `assets/originals/water-lilies.jpg`, full bleed, visible at `t=0` in every independent output.
- Background ownership: the Water Lilies image is a child layer, never a composition-root background and never a remote/runtime fetch.
- Browser source crop: `x=68 y=52 width=2682 height=1924` from each immutable `2818x2060` recording.
- Browser object: fixed `1440x1033`, geometrically centered on the `1920x1080` canvas.
- Centered bounds: left `240px`, right `1680px`, top `23.5px`, bottom `1056.5px`; center point `(960px, 540px)`.
- Mask: quiet Maho squircle treatment; source pixels remain the captured browser image inside the mask.
- Camera ownership: only an untimed browser wrapper may receive later `x`, `y`, and `scale` transforms. The timed source video geometry and crop stay fixed.
- Cross-output alignment: the background geometry, crop, mask, browser bounds, center, and shadow stay identical across all three outputs before scene-specific wrapper transforms.

## Scene camera contracts

### Spaces

- Start neutral at the complete browser.
- Drift toward the left sidebar during recorded Space changes, reaching approximately `1.10x`.
- Settle near `1.03x` at the final state.
- The different recorded internal Monet artwork remains captured Maho content; it must not be confused with or replaced by the external Water Lilies child background.

### Split View

- Start on the complete browser.
- Push toward the recorded drag origin and drop region.
- Zoom back to the complete split result.
- Page reflow, signup UI, and cookie UI remain exactly as captured.

### Command Bar + AI

- Water Lilies is visible behind the centered browser at `t=0`.
- Push near `1.09x` around the Command Bar.
- Pan focus right when the AI Panel opens while retaining browser context.
- Finish near `1.02x`.
- Preserve the captured Split View, page reactions, palette, AI Panel, and final state.

## Gate B review-artifact contract

- Raw checkpoint stills are decoded from the immutable local MOVs at the exact `STORYBOARD.md` times and retain source dimensions `2818x2060`.
- Contact sheets may scale those stills and overlay review-only labels, the approved crop rectangle, and camera notes.
- Every overlay must be visibly labeled `GATE B REVIEW ANNOTATION — NOT RUNTIME`.
- Review artifacts do not add the external background, crop the source, retime footage, simulate UI, synthesize a cursor, or create video output.
- The t=0 annotation documents that the later external Water Lilies child must be visible outside the browser object from the first rendered frame; it does not pretend the raw source already contains that external layer.

## HyperFrames implementation constraints after approval

- Storyboard shape and `outline` status follow `.omo/reference/hyperframes/skills/hyperframes-core/references/storyboard-format.md:22-35,125-129`.
- Gate B is the sketch checkpoint described by `.omo/reference/hyperframes/skills/hyperframes-core/references/review-loop.md:17-27`; no composition implementation is authorized by this packet.
- Later motion must be deterministic, pre-calculated, and seekable; spatial motion uses wrapper transforms only (`.omo/reference/hyperframes/skills/hyperframes-animation/SKILL.md:60-67`).
- Fixed frame dimensions and transform/opacity motion follow `.omo/reference/hyperframes/skills/hyperframes-core/references/determinism-rules.md:33-57`.
- HyperFrames owns media playback; timed media dimensions remain fixed and motion targets a non-timed wrapper (`.omo/reference/hyperframes/skills/hyperframes-core/references/variables-and-media.md:63-103`).
- Maho marketing direction remains calm, premium, minimal, and product-first, with a restrained squircle/shadow treatment (`docs/brand/DESIGN.md:78-141`).
