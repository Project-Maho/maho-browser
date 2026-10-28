# Rolling Maho onto a new Chromium revision

The canonical revision every builder checks out is `build/CHROMIUM_REVISION`
(one 40-hex hash). It must name a directory `build/chromium_base_patches/<rev>/`
holding the whole-file Maho patches for that revision. `windows_builder/10-checkout.ps1`
and the override tests read this file; nothing infers the revision from directory
mtimes. `_PINNED_CHROMIUM_REVISIONS` in `scripts/apply_chromium_src_overrides.py`
lists only legacy anchor-only trees (no base-patch layer); do not add new
revisions there.

Owner rule: audit upstream API changes across ALL Maho sources before any full
build. Do not discover breakage one full build at a time.

## Checklist

1. Pick the revision.
   - `curl -s 'https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Mac&num=1'`
   - Record milestone, version, and `hashes.chromium`. Use the Mac Stable hash so
     all three platforms share one tree.
2. Fetch upstream on a builder with full history (omarchy):
   `git -C ~/chromium/src fetch origin refs/tags/<version>` and keep the old
   revision available for blob diffs. No `gclient sync` yet.
3. Inventory the base-patch layer.
   - For every `chromium_base_patches/<old>/<path>.patch`, run
     `git apply --check` against `git show <new>:<path>`.
   - Bucket: applies as-is / clean 3-way merge / upstream file deleted or moved /
     hand port. Save the inventory JSON as evidence.
4. Add `build/chromium_base_patches/<new>/`.
   - Carry over clean patches, regenerate 3-way merges, hand-port the rest from
     the pristine `<new>` blob plus the Maho diff. Keep whole-file `--full-index`
     diffs; no conflict markers; LF line endings.
5. Upstream API audit across Maho sources (before any compile).
   - Every `#include` in `maho-chromium/**` (and `chromium_src/**`) must exist at
     `<new>`; list removed headers and their replacements.
   - Grep removed/renamed symbols (Browser accessors, `BrowserWindowInterface`
     shims, `CreateParams`, side-panel, views setters, vector icons, mojom enum
     values, throttle signatures). Check `.h/.inc/.inl` too, not only `.cc`.
   - Patch all callers in one batch.
6. Migrations (only for trees already patched with the old canonical layer):
   add `chromium_base_migrations/<new>/` entries and register exact old-file
   SHA-256 values as described in `chromium_base_migrations/README.md`.
7. Update the pin and revision constants.
   - Write `<new>` to `build/CHROMIUM_REVISION`.
   - Point the migration revision constant (`_UPDATED154_REVISION` today) at `<new>`
     or add a new constant for the new migration set.
8. Run the override suite:
   `python3 -m pytest maho-chromium/build/scripts/test_apply_chromium_src_overrides.py maho-chromium/build/scripts/test_updated154_base_migrations.py maho-chromium/build/scripts/test_maho_shortcut_binding_gate.py`
9. Override dry-run on a pristine `<new>` checkout:
   `python3 maho-chromium/build/scripts/apply_chromium_src_overrides.py --chromium-src <src> --dry-run`
   must exit 0 with zero rejected patches / unmatched anchors; then apply, and a
   rerun must report no-op.
10. `gclient sync -D --nohooks --no-history -j 4 --revision src@<new>` per builder
    (lower `-j` on GoogleSource HTTP 429), then `gclient runhooks`. Refresh PGO
    profiles (`tools/update_pgo_profiles.py`) and V8 builtins PGO
    (`v8/tools/builtins-pgo/download_profiles.py download --deps`); fix toolchain
    sync hooks (devtools rollup, Dawn Go) with upstream tools, never by
    suppressing failures.
11. Narrow compile targets first (locked `build_maho.py`, S3 sccache only):
    Maho source sets and test executables touched by step 5, on omarchy.
12. Full `chrome` build on maho-win (schtasks, `-j 12`, Release, see the
    maho-win-release-build skill). Exit 0 required.
13. omarchy Linux full build (locked wrapper). Exit 0 required.
14. Local macOS build (never launch the app on the owner's Mac).
15. Release notes: new Chromium version string, notable upstream changes,
    and the version shown in `chrome://version`.
16. Retire the old layer only after all builders are on `<new>`: remove the old
    `chromium_base_patches/<old>` directory and its migrations.
