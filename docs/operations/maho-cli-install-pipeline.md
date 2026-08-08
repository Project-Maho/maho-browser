# maho-cli install pipeline

One-line installer surfaced in `chrome://maho-settings` → Developers:

```
curl -fsSL https://mahobrowser.com/install-cli.sh | sh
```

## Components

| Piece | Location | Role |
|-------|----------|------|
| Installer script | `website/public/install-cli.sh` | Detects OS/arch, downloads + verifies + installs the `maho` binary to `~/.maho/bin` |
| Hosting headers | `website/public/_headers` | Serves `/install-cli.sh` as `text/plain` with a 5-minute cache |
| Build/package CI | `.github/workflows/release-cli.yml` | Builds per-platform binaries, signs+notarizes macOS, tars them, emits `.sha256` |
| macOS signing | `maho/scripts/sign_maho_cli.sh` | Developer ID codesign (hardened runtime) + notarization, called by the darwin CI job |
| Download source | `github.com/Project-Maho/release` (release-only repo) | Hosts the published tarballs |

## Asset naming (contract between script and CI)

Fixed, version-free names so `/releases/latest/download/<asset>` always resolves the newest:

- `maho-cli-darwin-arm64.tar.gz`
- `maho-cli-darwin-x86_64.tar.gz`
- `maho-cli-linux-x86_64.tar.gz`
- `maho-cli-linux-arm64.tar.gz`

Each tarball contains a single `maho` executable; each ships a sibling `<asset>.sha256`.

## Release flow

1. Push a `cli-v*.*.*` tag (or run the workflow manually). CLI versioning is independent of the browser's `v*.*.*` release tags.
2. `release-cli.yml` builds on the self-hosted runners: the `macos-arm64` runner builds both `aarch64-apple-darwin` (native) and `x86_64-apple-darwin` (cross); the Linux runners build natively.
3. The workflow uploads tarballs + checksums as artifacts.
4. A human publishes them to `Project-Maho/release` with the locally authenticated `gh` CLI, matching the browser release flow in `release.yml`.

The website deploy is automatic: `deploy-website.yml` ships `install-cli.sh` and `_headers` on any `website/**` change to `main`.

## macOS code signing + notarization

The darwin CI job signs each binary before packaging via `maho/scripts/sign_maho_cli.sh`,
reusing the same convention as `build_maho.py`:

- **Identity**: the CI `Import Developer ID certificate` step loads the Developer ID Application cert (team `5DUM8WPB4C`) from the `MACOS_CERT_P12` / `MACOS_CERT_PASSWORD` secrets into a temporary keychain and runs `set-key-partition-list` so `codesign` never prompts. `sign_maho_cli.sh` then selects the `Developer ID Application` identity (or `MAHO_CODESIGN_IDENTITY`) and signs with `codesign --force --timestamp --options runtime`. Apple Development / Distribution certs are rejected for out-of-store notarization, so the script errors if no Developer ID Application identity is present.
- **Notarization**: runs only when `APPLE_API_KEY` (the .p8 key contents), `APPLE_API_KEY_ID`, and `APPLE_API_ISSUER` repo secrets are set; submits via `xcrun notarytool submit --wait`. These secrets are configured on `Project-Maho/maho-browser`, reusing the shared team-`5DUM8WPB4C` App Store Connect key (`HLC2K69Y8G`).
- **No stapling**: a bare Mach-O binary cannot be stapled (stapler only accepts `.app`/`.dmg`/`.pkg`), so Gatekeeper performs the notarization check online on first launch. The installer also clears the `com.apple.quarantine` xattr.

Configured repo secrets on `Project-Maho/maho-browser`:
- `MACOS_CERT_P12` (base64 Developer ID Application .p12) + `MACOS_CERT_PASSWORD` — codesign identity.
- `APPLE_API_KEY` (.p8 contents) + `APPLE_API_KEY_ID` + `APPLE_API_ISSUER` — notarization (shared team-`5DUM8WPB4C` App Store Connect key `HLC2K69Y8G`).

All five are set, so the darwin job signs and notarizes end-to-end. If the notarization trio is absent, the binary is still signed but not notarized (non-fatal). Local signing needs the same Developer ID Application cert in the login keychain.
