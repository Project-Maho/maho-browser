# Gate B — storyboard and camera-sketch approval request

## Approval status

**Gate B approval is pending.** Please review the exact storyboard, shared frame contract, source-derived stills, and annotated contact sheets listed below. No approval is inferred from delivery of this packet.

To approve, please explicitly confirm the reviewed hashes in this packet. To revise, name the scene and requested frame, camera, crop, checkpoint, or source change.

> **Invalidation warning:** any requested frame, camera, crop, checkpoint, or source change invalidates this packet and returns the work to Todo 3. Composition work cannot begin until a replacement packet is reviewed and explicitly approved.

## What this packet does and does not show

- It proposes exactly three independent later outputs: Spaces, Split View, and Command Bar + AI.
- Raw stills are decoded directly from immutable local recordings at the exact `STORYBOARD.md` checkpoints and retain `2818x2060` source dimensions.
- Contact sheets only scale those stills and add visibly labelled `GATE B REVIEW ANNOTATION - NOT RUNTIME` text plus the approved source-crop rectangle.
- The raw recordings do not contain the future external background. The t=0 annotation means the later composition must show the external Water Lilies child behind the centered browser from its first rendered frame.
- No footage was cropped, retimed, modified, reconstructed, or composed for this packet. No UI or cursor was simulated.
- No `compositions/*.html`, final MP4, poster, HyperFrames preview/check/render output, or website production change is included.

## Review artifacts

| Scene | Source checkpoints | Contact sheet | Contact dimensions |
| --- | --- | --- | ---: |
| Spaces | `0.000s`, `10.000s`, `12.600s` | [spaces-contact-sheet.png](./spaces-contact-sheet.png) | 2112×595 |
| Split View | `0.000s`, `3.300s`, `6.600s` | [split-view-contact-sheet.png](./split-view-contact-sheet.png) | 2112×595 |
| Command Bar + AI | `0.000s`, `6.500s`, `20.000s`, `26.600s` | [command-ai-contact-sheet.png](./command-ai-contact-sheet.png) | 2816×595 |

The Command Bar + AI sheet retains two action checkpoints because one still cannot honestly review both the central Command Bar push and the later rightward AI Panel focus.

## Visual review checklist

- [ ] Exactly three independent Frame blocks are approved; no master or cross-scene frame is authorized.
- [ ] All three recordings play later from source time `0.000s` at `1x` for their complete locked durations.
- [ ] The external Water Lilies child is visible behind the browser at t=0 in every later output.
- [ ] Source crop `x=68 y=52 width=2682 height=1924` is approved.
- [ ] The fixed `1440x1033` browser object is centered on the `1920x1080` canvas.
- [ ] Spaces camera: neutral start, left-sidebar drift to approximately `1.10x`, final settle near `1.03x`; recorded internal Monet remains captured content.
- [ ] Split View camera: full-browser start, push to the recorded drag/drop region, zoom back to the complete split result; page reflow, signup, and cookie UI remain untouched.
- [ ] Command + AI camera: t=0 Water Lilies, Command Bar push near `1.09x`, right pan for AI Panel with browser context retained, finish near `1.02x`; captured Split View/page reactions/palette/panel/final state remain untouched.
- [ ] No rendered cross-scene transition is approved; any future website handoff remains outside the three videos.
- [ ] Review annotations are understood as non-runtime guidance, not a rendered treatment.

## Reviewed SHA-256 values

The canonical machine-readable list is [`SHA256SUMS`](./SHA256SUMS).

| Reviewed artifact | SHA-256 |
| --- | --- |
| `STORYBOARD.md` | `a1c5b5848e652f122d329119515528ccd68185e2bf8cd19b3d8274dbd05fc0c6` |
| `frame.md` | `7306139af3caab46274e808aa2a6a689007009ba0fba037184eb40e3dbd5e48a` |
| `stills/command-ai-0000ms-source.png` | `bac506762cf8638a4ba7499231d1cc4b46d035498df61ddb70a8a94f7a4f72be` |
| `stills/command-ai-6500ms-source.png` | `5729f80ee61b1e5c618bb89e02e559b03cb2d8cf3edb52078d60191714e45fc1` |
| `stills/command-ai-20000ms-source.png` | `7b1723d4d3b1a354ec6df77af8018cb51a7455c699dbcbdd4b92079f5805ef5b` |
| `stills/command-ai-26600ms-source.png` | `66ebebf106b3d15920895c581b08acc0eed676a879aff6ec4e2c4d4f2480f6c4` |
| `stills/spaces-0000ms-source.png` | `19365407c21b1194889156e58a8134c1469671c4fda28171ca4c48cc7673f959` |
| `stills/spaces-10000ms-source.png` | `22fc9c37270b2844570462611d6a361afd8afc6af27ce7f737b0fb83a83579de` |
| `stills/spaces-12600ms-source.png` | `9c4c26eebebe912ce0c8ac292c16bbdbb4aa3eff33bfc5ab1a178a69056cca61` |
| `stills/split-view-0000ms-source.png` | `83c030051b9a0c961a526d11e48b7b1a7b16c312a4cfc539767adb5a8838f3fe` |
| `stills/split-view-3300ms-source.png` | `ba54775c913e2bca5375a195bf46194a2dc4ac48aeeafd91130094ddd4738fe9` |
| `stills/split-view-6600ms-source.png` | `8e0743549d1151e28f25b15304331a0051a53c1871ec483f8c7321aee31a5208` |
| `command-ai-contact-sheet.png` | `53779cbecb7ed564b39e911aae86585bc979d3d07e4118dd157979a3090682de` |
| `spaces-contact-sheet.png` | `3d00fc0a80659704e26efc45fbc349a41a1af1a55e6549f85cb22f0fde61612b` |
| `split-view-contact-sheet.png` | `8a9e5f5a35a450049e2c5e014bdec28e181a870630aca3357a7dcb2607d65904` |

## Requested decision

Please either:

1. explicitly approve Gate B for the exact hashes above, or
2. request changes by scene and checkpoint.

Until that explicit decision is recorded, Gate B remains pending and composition HTML remains forbidden.
