# Maho Browser — client source

This repository contains the **client source** of the Maho Browser: a Chromium
overlay plus the Rust crates and WebUI surfaces that implement Maho's own
features.

It is **source-available, not OSI open source.** The Sustainable Use License
(`LICENSE`) permits use and modification for your own internal or personal
purposes, and free-of-charge non-commercial distribution, but it does **not**
permit selling or hosting the software commercially, and it does **not** grant
any rights to the Maho name or logo (`TRADEMARK.md`).

## What is in this repository

| Path | Contents |
|---|---|
| `maho/crates/` | Rust workspace: core engine, agent runtime, storage, CLI, MCP server, platform glue |
| `maho-chromium/` | The Chromium overlay: C++ browser code, WebUI resources, GN config, build scripts, branding |
| `maho-chromium/chromium_src/` | Overrides applied on top of upstream Chromium (Brave-style `#define`/`#include` pattern) |
| `maho/mail-core/` | Mail backend (isolated Cargo workspace, FFI static lib for the mail helper) |
| `maho/web-ai/` | Cross-platform AI WebView bundle (Preact) |
| `maho/ios-shell/`, `maho/android-shell/` | Native shells |
| `maho-chromium/build/scripts/` | Build entry points, including the Chromium source-override patcher |

## What is not in this repository

- **The hosted service.** Accounts, sync, the AI billing proxy, and the update
  feed are operated separately and are not part of this source tree. The client
  can be built and used without them.
- **Upstream Chromium.** You fetch that yourself (see below). It is large
  (~100 GB) and is not vendored here.
- **Release engineering.** The signing, notarization, packaging, and store
  publication pipelines are not published.

## Building

Prerequisites: a Chromium checkout for the supported revision, Rust toolchain,
Node/Bun for the WebUI bundles, and the platform SDK for your target.

```bash
# 1) Fetch and set up upstream Chromium in ./chromium per the Chromium docs,
#    then place this overlay where the build expects it:
ln -s ../../maho-chromium chromium/src/maho

# 2) Build the Rust core static library the C++ build links against
python3 maho-chromium/build/scripts/build_maho_core.py

# 3) Build
python3 maho-chromium/build/scripts/build_maho.py
```

Notes:

- `build_maho.py` serializes builds through a workspace lock. One build at a
  time is intentional — concurrent Chromium builds exhaust memory and make every
  build slower.
- Build the narrowest target that verifies your change.
- Do not pass a production OAuth client or any credential you do not own: the
  build takes provider keys and OAuth client ids as build arguments. **Create
  your own** if you need sign-in features in your own build.
- Artifacts produced from this source are not official builds and carry no
  support, update, or security-patch commitment from Maho.

### Branding, signing, and third-party marks

- **Do not ship Maho branding.** `maho-chromium/branding/` (icons, logo, installer
  artwork) and the product name are covered by `TRADEMARK.md`, not by the source
  license. A build you distribute must be rebranded.
- **Code signing.** `maho-chromium/branding/mac/{entitlements,helper-entitlements}.plist`
  and `maho-chromium/branding/BRANDING` reference Maho's Apple Developer Team ID
  (`5DUM8WPB4C`) and bundle id `com.maho.browser`. **Replace these with your own
  team id and bundle id** for any build you sign or distribute — they are not yours
  to use. `maho/ios-shell/Signing.xcconfig` intentionally ships with an empty
  `DEVELOPMENT_TEAM`; set your own there too.
- **Third-party browser logos.** The onboarding/Welcome WebUI shows small icons for
  other browsers (Chrome, Edge, Firefox, Safari, Brave, Opera, Zen, Arc). Those
  marks belong to their respective owners; Maho makes no claim to them. They are
  shown for identification only, and a rebranded build should not reuse them.

## Tests

```bash
cd maho && bun run typecheck   # cargo check --workspace
cd maho && bun run test        # cargo test --workspace
```

## Project facts you should know before contributing

- **English only.** Localization infrastructure is intentionally out of scope.
- **No telemetry.** The client does not add outbound calls to Maho hosts. Usage
  and funnel metrics are derived server-side, never from client pings. Please do
  not add any.
- **Source-available, not open source.** Do not describe this project as open
  source in issues, documentation, or third-party material.
- **Commercial use is restricted** by `LICENSE`. Read it before building a
  business on this code.
- **Contributions require a CLA** — see `CONTRIBUTING.md`.

## Documentation

- `CONTRIBUTING.md` — contribution rules, CLA, build and test expectations
- `SECURITY.md` — how to report a vulnerability (do not use public issues)
- `TRADEMARK.md` — what you may and may not do with the Maho name
- `LICENSES/` — third-party licenses for bundled components

## Status

This is a young project maintained by a small team. Interfaces, file layout, and
the supported Chromium revision change often. Community support is best-effort;
there is no service-level commitment for builds made from this source.
