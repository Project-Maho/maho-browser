# PROJECT KNOWLEDGE BASE — maho-chromium

**Generated:** 2026-05-05

## OVERVIEW
Chromium overlay for Maho Browser. Brave-style `chromium_src/` overrides + original Maho C++/WebUI features. ~39k lines. Symlinked into `chromium/src/maho` at build time.

## STRUCTURE
```
maho-chromium/
├── chromium_src/         # Upstream overrides (#define + #include pattern)
│   └── chrome/browser/ui/views/frame/   # BrowserView layout patches
├── browser/
│   ├── ai/               # LLM client, page-context extraction, tool registry
│   ├── net/              # Ad-block throttle, cosmetic filters, UA throttle
│   ├── ui/
│   │   ├── views/        # Native Views — sidebar, command palette, split view, little arc
│   │   │   └── sidebar/  # Biggest subsystem: container, tabs, favorites, DnD, search, spaces
│   │   ├── webui/        # Mojo-backed WebUI surfaces (14 features)
│   │   ├── theme/        # Color mixer, space theme state, color IDs
│   │   ├── context_menu/ # Tab, space, folder, favorites, address bar menus
│   │   ├── link_preview/ # Hover preview manager
│   │   ├── find_bar/     # Find-bar theming
│   │   ├── reader/       # Reader mode theming
│   │   ├── downloads/    # Download UI theming
│   │   ├── permissions/  # Permission prompt theming
│   │   ├── tab_preview/  # Tab preview controller
│   │   ├── notifications/ # Notification overlay
│   │   └── site_control/ # Page info UI
│   └── resources/        # WebUI TS + HTML per feature
├── components/
│   ├── constants/        # WebUI URL constants
│   └── maho_sidebar/     # Shared sidebar Mojo IDL
├── third_party/maho/     # Prebuilt maho-core static lib + C++ bridge
├── build/
│   ├── config/           # GN args per platform (mac, linux, win)
│   └── scripts/          # build_maho_core.py
├── branding/             # App icons, logos (mac/generic)
├── maho/browser/ui/views/sidebar/ # Duplicate dir — sidebar views (also exists above)
├── maho-chromium/browser/ai/     # Duplicate dir — AI code (also exists above)
└── docs/                 # Build guides, reference docs
```

## WHERE TO LOOK
| Task | Location |
|------|----------|
| Add new WebUI surface | `browser/ui/webui/maho_{feature}/` + `browser/resources/maho_{feature}/` |
| Sidebar changes | `browser/ui/views/sidebar/` (Views) + `browser/ui/webui/maho_sidebar/` (WebUI) |
| Command palette | `browser/ui/views/command/` (Views) + `browser/ui/webui/maho_command/` (WebUI) |
| AI / LLM features | `browser/ai/` (helpers) + `browser/ui/webui/maho_ai/` (WebUI) |
| Theme / colors | `browser/ui/theme/` — `maho_color_mixer.cc`, `maho_color_id.h` |
| Ad blocking | `browser/net/maho_ad_block_throttle.cc` |
| Upstream patches | `chromium_src/` — Brave-style #define + #include |
| Maho-core FFI bridge | `third_party/maho/maho_bridge.cc` + `maho_ffi.h` |
| Build config | `build/config/args_{mac,linux,win}.gn` |
| Mojo interfaces | `browser/ui/webui/maho_{feature}/maho_{feature}.mojom` |

## CONVENTIONS
- **WebUI pattern**: Each feature = `{feature}.mojom` + `_page_handler.cc/h` + `_ui.cc/h` + `resources/{feature}/app.ts` + `BUILD.gn`
- **Upstream overrides**: `chromium_src/` mirrors upstream paths; uses `#define` to rename original, then `#include` original, then add patches
- **C++ style**: Chromium conventions — explicit includes, `DCHECK_CALLED_ON_VALID_SEQUENCE`, namespace-local helpers
- **Shared build lock**: `build/scripts/build_maho.py` acquires the workspace root `.build.lock` once for the combined Rust + Chromium build flow; the Rust helper in `build/scripts/build_maho_core.py` still uses the same lock when run standalone, so this build flow can wait and print `Waiting for build lock: ...`
- **Mojo handlers**: Keep thin; delegate heavy logic to `browser/ai/` or reusable helpers
- **Page-context results**: Use structured objects with status/title/url/text/warnings, not single text blobs
- **GN builds**: Each WebUI and Views subsystem has its own `BUILD.gn`
- **Naming**: All Maho classes/files prefixed with `maho_`; WebUI URLs use `chrome://maho-{feature}`

## ANTI-PATTERNS (THIS PROJECT)
- No embedding complex logic in WebUI page handlers — extract to `browser/ai/` or helpers
- No raw string WebUI URLs — use `components/constants/webui_url_constants.h`
- No skipping Mojo IDL for new WebUI features
- No modifying upstream Chromium files directly — always use `chromium_src/` overrides

## WEBUI SURFACES (14)
| Feature | URL | Mojom |
|---------|-----|-------|
| Settings | `chrome://maho-settings` | `maho_settings.mojom` |
| Sidebar | `chrome://maho-sidebar` | `maho_sidebar.mojom` |
| Command | `chrome://maho-command` | `maho_command.mojom` |
| AI | `chrome://maho-ai` | `maho_ai.mojom` |
| Boost | `chrome://maho-boost` | `maho_boost.mojom` |
| Notes | `chrome://maho-notes` | `maho_notes.mojom` |
| Easel | `chrome://maho-easel` | `maho_easel.mojom` |
| Space Config | `chrome://maho-space-config` | `maho_space_config.mojom` |
| Space Create | `chrome://maho-space-create` | `maho_space_create.mojom` |
| Space Manage | `chrome://maho-space-manage` | `maho_space_manage.mojom` |
| Sync | `chrome://maho-sync` | `maho_sync.mojom` |
| Live Folders | `chrome://maho-live-folders` | `maho_live_folders.mojom` |
| Inline Edit | `chrome://maho-inline-edit` | `maho_inline_edit.mojom` |
| Test | `chrome://maho-test` | — |

## COMMANDS
```bash
# Canonical build (from the overlay repo root)
python3 build/scripts/build_maho.py

# Chromium-only rebuild (from chromium/src/)
autoninja -C out/Default chrome

# Build maho-core prebuilt
python3 build/scripts/build_maho_core.py

# Symlink setup (one-time)
ln -s ../../maho-chromium chromium/src/maho
```

## NOTES
- Duplicate dirs exist: `maho/browser/` and `maho-chromium/browser/` — both contain AI code; check which is canonical
- Sidebar is the largest subsystem (~25 files in `browser/ui/views/sidebar/`)
- Tests: interactive tests and unit tests in `browser/ui/views/sidebar/maho_sidebar_*_test.cc`
- `.sisyphus/plans/` contains implementation plans for sidebar, command palette, split view, space profiles
