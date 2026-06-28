# Aside Demo Scripts

This document outlines the demo scripts for marketing and onboarding walkthroughs of Maho Browser. All walkthroughs demonstrate desktop capabilities on the Chromium-fork browser shell and the Tauri native mail client shell.

---

## Demo 1: Zero-Setup AI

**Duration**: 30-45 seconds
**Goal**: Demonstrate how a new user can immediately use Maho's AI capabilities without pasting an API key or signing up for a LLM provider.

1. **Scene 1 (0-10s)**: Start on the browser welcome screen. The user clicks "Sign Up" or "Start Free Trial". They create a Maho account and immediately land on a new tab.
2. **Scene 2 (10-25s)**: The user opens the side AI Panel. They type: *"Summarize my active window"* or ask a coding question. The AI immediately responds in streaming format (SSE).
3. **Scene 3 (25-45s)**: Zoom into the AI Panel header showing: `Maho Managed Tier (Active)`. Show the user's quota indicator in Settings → Billing showing: `$0.10 used / $0.50 free limit`. Note that they can switch to BYOK at any time under Settings.

---

## Demo 2: Tab Tidy UI Helper

**Duration**: 30 seconds
**Goal**: Demonstrate the Mojo-connected "Tidy" action that groups open tabs into semantic folders dynamically.

1. **Scene 1 (0-10s)**: Show a browser window with 20 chaotic, ungrouped tabs (GitHub issues, MDN documentation pages, StackOverflow threads, and flight tickets).
2. **Scene 2 (10-20s)**: The user clicks the Sparkles icon ("Tidy") in the sidebar top bar. A popup modal appears showing a suggested folder structure: `Maho Development`, `MDN & StackOverflow`, and `Travel Plans`.
3. **Scene 3 (20-30s)**: The user clicks "Apply". The chaotic tabs instantly group into beautifully organized, colored native Chromium tab groups.

---

## Demo 3: Proxy Tier Upgrade Flow

**Duration**: 45 seconds
**Goal**: Demonstrate the LemonSqueezy subscription checkout and immediate edge sync to the browser billing proxy.

1. **Scene 1 (0-15s)**: Start in the Settings → Billing panel. Show the user is currently on the Free Tier and has reached their monthly usage limit. The AI Panel shows a "Quota Exceeded" message.
2. **Scene 2 (15-30s)**: The user clicks "Upgrade to Pro" in the settings card. It redirects to a secure LemonSqueezy checkout page. They complete a test payment.
3. **Scene 3 (30-45s)**: The user returns to the browser. The Settings page immediately updates to `Pro Tier` (effective ceiling `$20.00`). They type a message in the AI Panel, and the stream immediately resumes processing.
