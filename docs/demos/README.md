# Demo Videos: Launch Bundle

Three short demos showing launch-committed Maho features. Each runs 30 to 60 seconds with a 3-act structure (Hook, Value, CTA).

---

## The three demos

| # | File | Length | Premise |
|---|---|---|---|
| 01 | [01-zero-setup-ai.md](./01-zero-setup-ai.md) | ~55s | Sign up, open AI panel, ask a question. No keys. |
| 02 | [02-tab-tidy.md](./02-tab-tidy.md) | ~50s | 25 messy tabs, one click, AI groups them into named folders. |
| 03 | [03-tier-upgrade.md](./03-tier-upgrade.md) | ~50s | Hit the $0.50 ceiling, upgrade inline, keep typing. |

---

## Estimated production cost

Per demo, in-house with one editor on a Mac:

| Line item | Cost |
|---|---|
| Screen recording session (2-3 takes per beat) | $0 (in-house) |
| Voiceover (ElevenLabs or local talent, one read) | $20-$60 |
| Music license (Artlist or Musicbed, single track) | $30-$50 |
| Editor time (4-6 hours per demo) | $200-$400 |
| Color, sound mix, captions | $50-$80 |
| **Per-demo total** | **$300-$590** |
| **Bundle total (3 demos)** | **$900-$1,770** |

Outsourced to a freelance editor on Upwork or Cameo would land at $1,500-$3,000 for the bundle. Quality differential is small at this length, so do it in-house unless the marketing window is tight.

Renders to deliver:
- 1920x1080 (website hero, ProductHunt, YouTube)
- 1080x1080 (Twitter/X feed, LinkedIn)
- 1080x1920 (vertical, optional, for short-form recuts)

---

## Recommended ordering for marketing

Lead with **#1 (Zero-Setup AI)**. It's the strongest hook because every AI browser asks for an API key before you can do anything, and Maho doesn't. The contrast lands in five seconds.

Follow with **#2 (Tab Tidy)**. It's the "satisfying" demo. The tab collapse animation is the most shareable single shot in the bundle. Use this one to show Maho has personality beyond AI.

Close with **#3 (Tier Upgrade)**. This one is for visitors who already get it and want to know what they pay for. It also pre-empts the most common objection ("what happens when I hit the free ceiling?") and shows the answer is "almost nothing".

The ordering is also a funnel: #1 is awareness, #2 is interest, #3 is conversion.

---

## Distribution plan

### Twitter / X
- Day 0: Post #1 as a standalone tweet. Native video, 1080x1080. Caption is one line: "Most AI browsers want your API key. Maho doesn't."
- Day 2: Quote-retweet #1 with #2 attached. Caption: "And it cleans up your 25 tabs too."
- Day 4: Reply-thread to #1 with #3. Caption: "Hit the free ceiling? It looks like this."
- All three pinned to a thread on the founder account.

### ProductHunt
- Use **#1** as the primary launch video (hero slot, autoplays muted).
- Use **#2** as the second gallery video.
- Skip #3 on ProductHunt. The pricing demo reads as "asking for money on launch day", which hurts vote conversion. Put #3 in the comments instead, when someone asks "how does the pricing work?".

### Website
- **Hero slot:** #1 (autoplay, muted, looped, 1920x1080).
- **Features section, "Tidy tabs in one click":** #2 (click-to-play).
- **Pricing page:** #3 (click-to-play, near the Pro tier card).

### Reuse
- Cut a 15-second highlight from #2 (just the tab collapse) for Instagram Reels and TikTok.
- Embed all three in the launch blog post in the order above.
- Add to the docs site's "What is Maho?" page.

---

## Hard constraints (do not break)

- Scoped mobile claims: Mobile claims are limited strictly to mobile chat (`#chat`, `#conversations`). Full mobile browser parity is not claimed (Superseded for mobile chat by ADR 0015; historical note: prior policy banned all iOS or Android claims).
- Scoped MCP/CLI & Routines claims: Claims for tools-only MCP GA, single `maho` CLI, and automation routines are permitted as defined in ADR 0015 (historical note: prior policy banned all claims about Routines, MCP host, or CLI). MCP resources/prompts remain out of scope (ADR 0014).
- No mention of "Aside" or any other competitor by name in the voiceover. The comparison work belongs on the compare pages, not in the demos.
- Pricing on every CTA card: Free $0/mo (with a $0.50 included AI ceiling), Pro $5/mo, Max $20/mo.

---

**Last updated:** 2026-07-22
