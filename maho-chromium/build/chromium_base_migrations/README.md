# Chromium 154 base patch migrations

`apply_base_patch` only considers migrations after both the current base patch's
reverse check and forward check fail. Migration registration is restricted to
the canonical revision/path and an exact SHA-256 of the complete old file.
Unknown old files, including files with unrelated edits, are rejected unchanged.
Pristine/current files retain ordinary git-apply behavior, preserving unrelated
edits outside patch hunks.

The six initial old states were observed on both local Chromium and omarchy at
revision `f89f3a4363808e117c592adedcf9947882ac3b79` on 2026-09-30. The
app-controller state includes the duplicated `IDC_NEW_TAB` block. Patch index
lines are not trusted as content hashes; the SHA-256 values were checked against
omarchy's actual files.

The final API repair wave extends registration to fifteen paths. The nine
additional hashes match saved applied diffs and actual local Chromium files.
Their migrations and the refreshed delegate migration target the finalized
canonical patches recorded in `plan/rev-unify/rev154/api-patch-receipts.md`.
All ten derived endpoints match that worker's applied scratch files byte-for-byte
by SHA-256. The original five unaffected migrations remain unchanged.

Before mutation, the migration is checked and applied to an isolated copy, and
the current base patch must reverse-check there. The live source hash is checked
again, then the migration is checked and applied with git. A final reverse check
against the current base patch is mandatory. Dry-run performs the isolated checks
without changing the source tree. No reset, restore, checkout, or replacement of
the live file is used.

## Extending the registered states

For another changed 154 base patch, capture the exact old applied content first.
Derive the new content from the pinned pristine blob plus the new canonical base
patch. Add their minimal unified diff under the revision directory here, using
the source-relative path plus `.patch`. Register the old complete file SHA-256
in `_UPDATED154_OLD_SHA256` in `apply_chromium_src_overrides.py`. The state matrix
tests automatically exercise every registered path.

If a canonical patch changes again, update its migration to the latest intended
result before use. The reverse check rejects an obsolete migration rather than
silently accepting a partially updated file. The tests reconstruct the old state
by reversing the migration from pristine-plus-current and require its registered
hash, so they also detect incorrect migration endpoints.

Additional parent-requested snapshots are in the ignored local directory
`plan/updated154-migration-snapshot/`: nine original canonical patches at its root
and fifteen observed applied diffs under `known-old/`. These are handoff evidence,
not automatically trusted migration registrations.

## Remote verification commands

After the parent synchronizes the script, tests, canonical patches, and migration
directory to omarchy, run from the parent session:

```sh
ssh omarchy 'cd /home/indo/maho-workspace/maho-chromium/build/scripts && MAHO_CHROMIUM_SRC=/home/indo/maho-workspace/chromium/src python3 -B -m unittest test_updated154_base_migrations test_apply_chromium_src_overrides.MahoBasePatchLayerTest -v'
```

This uses temporary file fixtures and real git apply; it does not change the
upstream working tree. Missing pinned blobs fail rather than skip tests.

Read-only preflight of the registered paths against the actual remote tree:

```sh
ssh omarchy 'cd /home/indo/maho-workspace/maho-chromium/build/scripts && python3 -B -c '\''from apply_chromium_src_overrides import _UPDATED154_OLD_SHA256, _UPDATED154_REVISION, apply_base_patch, default_chromium_src, repo_root; root = repo_root() / "build" / "chromium_base_patches" / _UPDATED154_REVISION; [print(path, apply_base_patch(default_chromium_src(), path, root / (path + ".patch"), True)) for path in _UPDATED154_OLD_SHA256]'\'''
```

Neither command starts a build. The parent reported eight tests passing on
omarchy for the initial six-path implementation. The fifteen-path extension
and added registration-coverage test still require the commands above; no tests
or builds were run in the migration implementation session.
