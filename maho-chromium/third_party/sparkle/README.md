# Sparkle Framework (vendored)

**Pinned version:** 2.9.4 (see `sparkle-version.txt`)
**Source:** https://github.com/sparkle-project/Sparkle/releases/tag/2.9.4
**License:** MIT (see `LICENSE`)

Sparkle is the macOS auto-update framework used by Maho Browser's update
delegate (`maho-chromium/browser/updates/maho_update_delegate_mac.mm`).

## Contents

- `sparkle-version.txt` — pinned version string (CI drift check reads this)
- `LICENSE` — Sparkle MIT license (redistribution requirement)
- `Sparkle.framework` — the framework binary (added during Phase A.4 build wiring;
  fetched from the pinned release tarball, NOT committed as source)

## Why 2.9.4 (minimum 2.9.2)

Do NOT downgrade below **2.9.2**. Releases 2.9.2+ contain required security
hardening:

- 2.9.2: guards against symlinks when applying delta update files; enforces
  installer connection validation before receiving appcast item data
- 2.9.3: fixes update failure when bundle ID ends with `.app`
- 2.9.4: fixes a race condition with backgrounded/dockless apps

## Signing key

The Sparkle EdDSA signing key for Maho was generated 2026-07-11 via this
release's `bin/generate_keys` tool.

- **SUPublicEDKey** (embedded in `Info.plist`): `iwdrdct4loEJl8WbHsfhhHMNcUygZ54N6uIveroNTbw=`
- Private key: stored locally in `.secrets/signing/sparkle_ed_private.pem`
  (gitignored) and in the macOS login Keychain of the release operator.

## Upgrade procedure

1. Download the new Sparkle release tarball from the GitHub releases page.
2. Update `sparkle-version.txt`.
3. Replace `Sparkle.framework` with the new version.
4. Update the security note above with any new hardening fixes.
5. Re-run the macOS build + notarization smoke test (Phase E.3).
6. The signing key does NOT change on framework upgrade — only the framework binary.

## Appcast signing

Release artifacts are signed with `bin/sign_update <file> <private-key>` during
the release workflow (D.5). The generated EdDSA signature goes into the
`appcast.xml` `sparkle:edSignature` attribute.
