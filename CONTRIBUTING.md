# Contributing to Maho

Thanks for your interest. Please read this before opening a pull request.

## What lives here

This repository is the **source of truth for the Maho client** (browser overlay, Rust crates, WebUI, native shells). Pull requests are reviewed and **merged here**.

The hosted service (accounts, sync, AI billing), the release/signing pipeline, and enterprise code live in a **separate private repository** and are not open to contribution. If your change touches the client, this is the right place; if it touches the service, open an issue here and describe the problem instead of sending a patch — we will route it.

Please note the project is **source-available, not OSI open source**: the Sustainable Use License restricts commercial use and redistribution. Commercial rebranding of the client is not permitted. See `LICENSE` and `TRADEMARK.md`.

## Licensing of contributions

This repository is **source-available** under the Sustainable Use License
(see `LICENSE`). It is **not** OSI open source.

**Before your first contribution is merged, you must sign our Contributor License
Agreement (CLA).** A `Signed-off-by` line (DCO) alone is **not** sufficient — the
CLA is what lets the project maintain the license over time. Pull requests from
contributors without a signed CLA cannot be merged.

By signing the CLA you confirm that:

- You own or have the right to submit the contribution.
- You grant the project a perpetual, worldwide, non-exclusive, royalty-free,
  irrevocable license to use, reproduce, modify, distribute, and **relicense**
  your contribution (including under different license terms in the future).
- You grant a patent license for patents you own that your contribution
  necessarily infringes.
- You are not required to provide support, and the contribution is provided
  as-is. Where local law reserves rights that cannot be assigned, you waive or
  agree not to assert them.

Ask for the CLA in your first pull request and a maintainer will send it.

### Files you must not relicense

Some files are derived from third-party projects and remain under their own
licenses:

- `maho-chromium/chromium_src/**` and other upstream-derived files — Chromium and
  its dependencies' licenses apply. Keep their notices intact; add a license
  header only for genuinely new Maho-authored files.
- Vendored or generated third-party assets — see `LICENSES/` and `NOTICE`.

Do not paste code from projects with incompatible licenses. If you are unsure,
ask first; a rejected license is much cheaper than a license audit later.

## Before you start

- **Open an issue first** for anything larger than a bug fix. Large, unannounced
  pull requests are likely to be declined even when the code is good.
- Search existing issues and pull requests to avoid duplicated work.
- For security issues, **do not open a public issue** — see `SECURITY.md`.

## Building

Maho is a Chromium overlay. The upstream Chromium source is **not** in this
repository; you fetch it yourself.

```bash
# 1) Fetch upstream Chromium (large, ~100 GB) and place the overlay
gclient sync        # from your chromium/src checkout, per your .gclient config
ln -s ../../maho-chromium chromium/src/maho

# 2) Rust core (static lib consumed by the C++ build)
python3 maho-chromium/build/scripts/build_maho_core.py

# 3) Build
python3 maho-chromium/build/scripts/build_maho.py
```

Notes:

- `build_maho.py` serializes builds through a workspace lock — one build at a
  time is intentional, not a bug.
- Build the **narrowest target** that verifies your change. Full
  `unit_tests`/`browser_tests` compile nearly everything and are usually not
  needed.
- Do not bypass the lock with raw ninja on a shared machine. If you must:
  `python3 maho/scripts/build_lock.py -- autoninja -C out/Default <target>`.

## Tests

```bash
cd maho && bun run typecheck    # cargo check --workspace
cd maho && bun run test         # cargo test --workspace
```

- Add a test that **can fail** for every behavioral change. A test that passes
  before your fix is not evidence of anything.
- Do not use fixed sleeps or timing-dependent waits. Subscribe to the event you
  are waiting for and await it with a bounded timeout.
- Do not weaken, skip, or delete a failing test to get a green run. Report
  pre-existing failures separately.

## Code conventions

- Rust: explicit error handling; no `unwrap()` on fallible paths in library code;
  no swallowed errors.
- C++: explicit includes, `DCHECK_CALLED_ON_VISUAL_SEQUENCE` on sequenced members,
  namespace-local helpers, thin Mojo/WebUI handlers that delegate.
- WebUI: each feature has `{feature}.mojom` + `_page_handler.cc/h` + `_ui.cc/h` +
  `resources/{feature}/app.ts`.
- Comments explain *why*, not *what*.

## Product policies that bind contributions

- **English only.** Localization and i18n infrastructure is intentionally out of
  scope. Do not add `.grd`/`.grdp` locale files, per-surface locale bundles, or
  new localization seams.
- **No telemetry.** The client must not add outbound calls to Maho hosts. Funnel
  or usage metrics are derived server-side, never from client pings.
- **Brand assets** are governed by `TRADEMARK.md`.

## Pull requests

- One logical change per pull request; keep diffs reviewable.
- Describe the problem, the change, and how you verified it. State what you did
  **not** verify.
- Expect review comments about scope: fixes should be minimal. Unrelated
  refactors in the same pull request will be asked out.
- Maintainers may decline a contribution that is correct but out of scope for the
  project's direction. That is not a judgment about the work.

## Support expectations

This project is maintained on a best-effort basis by a small team. There is no
guaranteed response time for issues or pull requests, and no service-level
commitment for the source distribution.
