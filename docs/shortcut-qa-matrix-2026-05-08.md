# Shortcut QA Matrix

**Date:** 2026-05-08

## Purpose

This document records the verified runtime status of key Maho app shortcuts after the latest macOS rebuild and shortcut fix pass.

## Verification method

- Runtime proof was required; compile-only success was not treated as sufficient.
- Final shortcut truth came from physical keycode-style automation, not plain `keystroke` text injection.
- Earlier `keystroke`-style automation produced misleading failures for some shortcuts.

## Evidence roots

- `/var/folders/zh/7cc25lt91b1_dj577306nwdh0000gn/T/opencode/maho-ai-cmde-keydown-20260508-055130/`
- `/var/folders/zh/7cc25lt91b1_dj577306nwdh0000gn/T/opencode/shortcut-keycode-tg6fufv5/`
- `/var/folders/zh/7cc25lt91b1_dj577306nwdh0000gn/T/opencode/shortcut-tabnav-fixed-iblj1xdo/`

## Verified pass

- `Cmd+E` — AI panel toggle
- `Cmd+T` — command palette
- `Cmd+N` — new window
- `Cmd+L` — focus address bar
- `Cmd+F` — find in page
- `Cmd+R` — reload
- `Cmd+Y` — history
- `Cmd+J` — downloads
- `Cmd+W` — close tab/window evidence
- `Cmd+Option+B` — bookmarks
- `Cmd+Option+I` — developer tools
- `Cmd+Option+U` — view source
- `Cmd+Shift+D` — duplicate tab
- `Cmd+Shift+C` — copy URL
- `Cmd+Shift+Option+C` — copy URL as Markdown
- `Ctrl+N` — new note
- `Cmd+[` — back
- `Cmd+]` — forward
- `Cmd+Option+Down` — next tab
- `Cmd+Option+Up` — previous tab

## Fixed during this session

- `Cmd+Option+Down` / `Cmd+Option+Up` initially failed.
- Root cause: default shortcut bindings used `Down` / `Up`, while runtime key serialization used `arrowdown` / `arrowup`.
- Fix applied in `maho/crates/maho-core/src/shortcut_manager.rs`.
- Final passing evidence: `/var/folders/zh/7cc25lt91b1_dj577306nwdh0000gn/T/opencode/shortcut-tabnav-fixed-iblj1xdo/summary.json`

## Additional build blockers resolved during verification

These were unrelated to the tab-navigation shortcut logic but had to be cleared to complete the rebuild:

- stale/generated build graph issue resolved via `gn gen out/Default`
- `browser_view.h` namespace mismatch for `maho::MahoCommandOverlayController`

## Remaining gaps / untested

This matrix covers the shortcuts that were explicitly runtime-verified in the current QA pass. Other Maho shortcut actions not listed above should be treated as unverified in this artifact.