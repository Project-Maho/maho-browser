# Demo 03: Tier Upgrade

**Duration:** ~55 seconds
**Premise:** A free-tier user hits the $0.50/mo ceiling mid-chat. Inline card offers three paths: Pro $5, $10 credit pack, or BYOK. Pick one, keep typing.
**Primary message:** Hitting the ceiling shouldn't break your flow. Three options, all visible at once.

---

## Act 1: Hook (0:00 to 0:05)

**On-screen action**
- Open mid-conversation. AI panel on the right, six messages deep. User types a follow-up.
- Lower-third caption: "$0.50/mo free quota. What happens when you hit it?"

**Voiceover**
> "You've been chatting with Maho's AI all week. You hit the free ceiling mid-question. Here's what that looks like."

---

## Act 2: Value (0:05 to 0:45)

**Beat 1: The ceiling (0:05 to 0:13)**
- User submits the next message. Short pause. An inline card appears in the AI thread where the next response would go.
- Card reads: "You've used your $0.50 of AI this month." Three buttons stack below: "Go Pro, $5/mo", "Buy $10 credits", "Use my own key".

> "No error toast. No locked screen. Three paths, right where the next answer would have appeared."

**Beat 2: Click Pro (0:13 to 0:22)**
- Click "Go Pro, $5/mo". A modal slides in with the Pro tier summary: "$5/mo, ten times the quota, cancel anytime."
- Click "Continue to checkout".

> "Five bucks a month for ten times the quota. Click continue."

**Beat 3: LemonSqueezy checkout (0:22 to 0:36)**
- LemonSqueezy checkout iframe loads inside the modal. Email is prefilled. User types the card number, hits pay.
- Spinner for a beat. Success state.

> "Checkout opens right in the window. Card, pay, done. No redirect, no separate tab."

**Beat 4: Tier flips, chat resumes (0:36 to 0:45)**
- Modal dismisses. The inline ceiling card morphs into "You're on Pro now" and fades out.
- The original message resends automatically. Response streams in.

> "Your tier flips on the spot. The question you were stuck on gets answered. You don't even type it again."

---

## Act 3: CTA (0:45 to 0:55)

**On-screen action**
- A quiet toast slides in lower-right, 1.5 seconds: "Heavy user? Try Max for $20/mo." Fades.
- End card. Text: "Free $0.50/mo. Pro $5/mo. Max $20/mo."
- Subtext: "Or pay as you go. Or bring your own key."
- Maho logo and URL.

**Voiceover**
> "Three tiers. Free, Pro, Max. Credits if you'd rather not subscribe. Or your own key. Download Maho."

---

## Production notes

- Stage the free-tier account in advance. Spend the $0.50 so the demo hits the ceiling on the first new message.
- LemonSqueezy checkout uses a sandbox card (4242 4242 4242 4242).
- The auto-resend of the stuck message is the hero moment. The streamed answer must clearly resolve the blocked question.
- The "Try Max" toast should be subtle, not pushy.
- Music holds through checkout. The tier-flip chime is the audio anchor.
- Blur account email and card data. Use a fixture account.
