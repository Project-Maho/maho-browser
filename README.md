# Maho

**A browser that works for you, not on you.**

Maho is a Chromium-based browser built around a simple idea: your attention is
yours. It organizes your work into **spaces** instead of one endless window,
blocks ads and trackers by default, and puts an AI assistant and your email
inside the browser so you stop switching between apps.

This repository is the **client source code** of that browser.

---

**English** · [中文](./README.zh-CN.md) · [日本語](./README.ja.md)

---

## Why Maho

Most browsers are built to keep you looking at them. Maho is built to get out
of your way.

- **Spaces, not tabs sprawl.** Group tabs by what you're actually doing —
  work, research, a trip — and switch between them instead of drowning in one
  flat tab strip.
- **Ads and trackers off by default.** Built-in blocking, tuned so pages stay
  readable and fast. No extension roulette.
- **An assistant that reads the page.** Ask questions about what you're
  looking at, summarize it, or have routine work run on a schedule — from the
  browser, not a separate app.
- **Email in the browser.** Your inbox lives next to your browsing, so
  checking mail doesn't mean leaving your work.
- **Your data stays yours.** No telemetry. The client does not phone home, and
  usage metrics are never sent from the app. You can also bring your own AI
  key and keep full control.

---

## About this source code

This is the **source-available** code for the Maho client: the Chromium overlay
and the Rust crates, WebUI, and native shells that implement Maho's own
features.

It is **not OSI open source.** The [Sustainable Use License](LICENSE) lets you
use and modify the code for your own internal or personal use, and to share it
for free, non-commercially. It does **not** let you sell or host it
commercially, and it does **not** grant rights to the Maho name or logo
([TRADEMARK](TRADEMARK.md)).

**What's here:** the client — the Chromium overlay (`maho-chromium/`), the Rust
crates (`maho/crates/`), the WebView bundle (`maho/web-ai/`), the mail backend
(`maho/mail-core/`), and the native shells (`maho/ios-shell/`,
`maho/android-shell/`).

**What's not here:** the hosted service (accounts, sync, the AI billing proxy,
the update feed) — it runs separately and is not part of this tree. Upstream
Chromium is also not here; you fetch it yourself.

---

## Build it yourself

You need a Chromium checkout, a Rust toolchain, and your platform's SDK.

```bash
# 1) Fetch upstream Chromium (large), then place this overlay:
ln -s ../../maho-chromium chromium/src/maho

# 2) Build the Rust core the C++ build links against
python3 maho-chromium/build/scripts/build_maho_core.py

# 3) Build
python3 maho-chromium/build/scripts/build_maho.py
```

- Builds serialize through a lock — one at a time on purpose.
- Build the narrowest target that verifies your change.
- Use **your own** API keys and OAuth clients for any build you run.

---

## Good to know

- **Source-available, not open source.** Please don't call it open source.
- **No telemetry.** Don't add any.
- **Commercial use is restricted** by the license — read it before building on this.
- **Contributions need a CLA** — see [CONTRIBUTING](CONTRIBUTING.md).
- **Report security issues privately** — see [SECURITY](SECURITY.md).

---

*This is the source code. For the product, see [mahobrowser.com](https://mahobrowser.com).*
