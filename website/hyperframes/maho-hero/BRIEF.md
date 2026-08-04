---
workflow: general-video
flow: automation
storyboard: yes
---

# Maho HyperFrames Hero

## Approved static requirements

- Produce exactly three independent, silent 1920x1080 HyperFrames outputs at 30fps in later approved work: Spaces, Split View, and Command/AI.
- Adopt the immutable local originals documented in `source-manifest.json`; preserve each recording from media time zero at 1x and retain its full locked duration.
- Use the adopted Water Lilies image as the later full-bleed child background from frame zero.
- Keep all runtime assets local and pinned. CDN scripts, remote runtime media, and render-time network fetches are forbidden.
- Do not trim, retime, reorder, replace, recreate captured browser UI, add audio, captions, labels, synthetic cursors, transitions, or publish/render output in this task.

## Gate B hold

Composition HTML is forbidden until Gate B approval. In particular, do not create `compositions/*.html` before the approved storyboard and frame contract exist.
