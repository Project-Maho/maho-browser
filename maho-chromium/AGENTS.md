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
│   ├── ai/               # LLM client, page-context extraction, tool registry (see browser/ai/AGENTS.md)
│   ├── mcp/              # MCP server session + pipe transport (browser-side)
│   ├── net/              # Ad-block throttle, cosmetic filters, UA throttle
│   ├── updates/          # Update delegation per platform (mac/win/linux)
│   ├── sync/             # Sync bridge to relay
│   ├── os_crypt/         # OS-level key encryption wrappers
│   ├── passwords/        # Password management integration
│   ├── extensions/       # Extension support
│   ├── importer/         # C++ import orchestration (see browser/importer/AGENTS.md)
│   ├── keys/             # API key management
│   └── ui/
│       ├── views/        # Native Views — sidebar, command palette, split view (see browser/ui/views/AGENTS.md)
│       │   └── sidebar/  # Biggest subsystem: container, tabs, favorites, DnD, search, spaces
│       ├── webui/        # Mojo-backed WebUI surfaces (14 features; see browser/ui/webui/AGENTS.md)
│       ├── theme/        # Color mixer, space theme state, color IDs
│       ├── context_menu/ # Tab, space, folder, favorites, address bar menus
│       ├── find_bar/     # Find-bar theming
│       ├── reader/       # Reader mode theming
│       ├── downloads/    # Download UI theming
│       ├── permissions/  # Permission prompt theming
│       ├── tab_preview/  # Tab preview controller
│       ├── notifications/ # Notification overlay
│       └── site_control/ # Page info UI
│   └── resources/        # WebUI TS + HTML per feature (see browser/resources/AGENTS.md)
├── components/
│   ├── constants/        # WebUI URL constants
│   └── maho_sidebar/     # Shared sidebar Mojo IDL
├── third_party/maho/     # Prebuilt maho-core static lib + C++ bridge
├── build/
│   ├── config/           # GN args per platform (mac, linux, win)
│   └── scripts/          # build_maho_core.py
├── branding/             # App icons, logos (mac/generic)
└── docs/                 # Build guides, reference docs
```

## WHERE TO LOOK
| Task | Location |
|------|----------|
| Add new WebUI surface | `browser/ui/webui/maho_{feature}/` + `browser/resources/maho_{feature}/` |
| Sidebar changes | `browser/ui/views/sidebar/` (Views) + `browser/ui/webui/maho_sidebar/` (WebUI) |
| Command palette | `browser/ui/views/command/` (native Views only) |
| AI / LLM features | `browser/ai/` (helpers) + `browser/ui/webui/maho_ai/` (WebUI) |
| Browser-side MCP server | `browser/mcp/` |
| Theme / colors | `browser/ui/theme/` — `maho_color_mixer.cc`, `maho_color_id.h` |
| Ad blocking | `browser/net/maho_ad_block_throttle.cc` |
| Auto-updates | `browser/updates/` — per-platform delegates (mac/win/linux) |
| Sync bridge | `browser/sync/` |
| Upstream patches | `chromium_src/` — Brave-style #define + #include |
| Maho-core FFI bridge | `third_party/maho/maho_bridge.cc` + `maho_ffi.h` |
| Build config | `build/config/args_{mac,linux,win}.gn` |
| Mojo interfaces | `browser/ui/webui/maho_{feature}/maho_{feature}.mojom` |
| Native Views icons (C++) | `browser/ui/views/maho_lucide_icons/` — Lucide-backed `aggregate_vector_icons` GN target. Use via `ui::ImageModel::FromVectorIcon(maho_lucide_icons::kFooIcon, color, size)`. See `../docs/icon-system.md`. **Never hand-edit `.icon` files.** To update Lucide icons: bump `lucide_version` in `browser/ui/views/maho_lucide_icons/manifest.json` and run `python3 build/scripts/lucide_icons/generate.py --write`. The build (`build_maho.py`) runs `--check` automatically and fails if manifest and files drift. |
| WebUI icons (lucide-react) | `lucide-react` is the canonical icon library for React-based WebUI surfaces. Traditional SVG files inside `icons/` are deprecated for new surfaces. |

## CONVENTIONS
- **WebUI pattern**: Each feature = `{feature}.mojom` + `_page_handler.cc/h` + `_ui.cc/h` + `resources/{feature}/app.ts` + `BUILD.gn`
- **WebUI Styling**: WebUI surfaces use **shadcn + Tailwind CSS v4**. Theme variables and base styling are defined centrally in `maho_common/react/theme/tokens.css` and compiled via Tailwind CLI integration in the GN build.
- **Shared UI Library**: Shared UI primitives are hand-authored following the shadcn registry specification at `maho_common/components.json` and located in `maho_common/react/ui/`.
- **Theme bootstrapping**: Theme state (light/dark resolution) is controlled by writing a `data-theme` attribute to the `<html>` root, managed by `@theme/apply_theme`.
- **Upstream overrides**: `chromium_src/` mirrors upstream paths; uses `#define` to rename original, then `#include` original, then add patches
- **C++ style**: Chromium conventions — explicit includes, `DCHECK_CALLED_ON_VALID_SEQUENCE`, namespace-local helpers
- **Shared build lock**: `build/scripts/build_maho.py` acquires the workspace root `.build.lock` once for the combined Rust + Chromium build flow; the Rust helper in `build/scripts/build_maho_core.py` still uses the same lock when run standalone, so this build flow can wait and print `Waiting for build lock: ...`
- **Windows official-release sccache policy**: `is_official_build=true` and sccache are compatible, but sccache is active only when `out/Release/args.gn` explicitly sets `cc_wrapper` to the Windows `sccache.exe` path. Installing `sccache.exe` or placing it on `PATH` does **not** enable Chromium C++ caching. Do not claim a Windows release build used sccache until `sccache --show-stats` reports `Compile requests executed > 0` for that build. Preserve official release flags; enable or change `cc_wrapper` only between builds, regenerate GN, then run the canonical wrapper. Do not modify `args.gn` while a canonical build is running.
- **Mojo handlers**: Keep thin; delegate heavy logic to `browser/ai/` or reusable helpers
- **Page-context results**: Use structured objects with status/title/url/text/warnings, not single text blobs
- **GN builds**: Each WebUI and Views subsystem has its own `BUILD.gn`
- **Naming**: All Maho classes/files prefixed with `maho_`; WebUI URLs use `chrome://maho-{feature}`

## ANTI-PATTERNS (THIS PROJECT)
- No embedding complex logic in WebUI page handlers — extract to `browser/ai/` or helpers
- No raw string WebUI URLs — use `components/constants/webui_url_constants.h`
- No skipping Mojo IDL for new WebUI features
- No modifying upstream Chromium files directly — always use `chromium_src/` overrides
- **Theme picker is permanently `chrome://maho-space-create`** — Do NOT migrate it to `maho_space_config`. Do NOT resurrect a native Views theme picker. Do NOT introduce a second production WebUI for theme selection. See `.sisyphus/plans/theme-picker-architecture-decision.md` (ADR, 2026-05-22) — this decision is permanent and supersedes all prior plans proposing migration.
- **빌드 실패 시 GN 파일 강제 수정 금지** — `build_maho.py` 가 GN regen (`gn gen` / `Regenerating ninja files`) 단계 또는 그 이후 단계에서 실패하면 즉시 중단하고 사용자에게 보고. `BUILD.gn`, `chromium/src/chrome/test/BUILD.gn`, override 파일 등을 GN 통과를 위해 임의로 편집하지 말 것. unresolved deps / missing target 류 에러는 다른 세션 미완 작업·working tree dirty state 가 원인인 경우가 대부분이며, 임의 cleanup 은 추후 충돌과 데이터 손실을 부른다. **사용자 명시 승인 없이는 GN 편집 금지.**
- **Clipboard-copy must confirm with a toast** — every "copy link/URL" surface (Cmd+Shift+C / command-palette `copy_url` and `copy_url_markdown`, and the Maho Mini copy button) must show the shared `MahoNotificationOverlay` "Link copied" toast via `ShowLinkCopiedToast()` in `browser/ui/views/command/maho_command_action_handler.cc`. A copy path that writes the clipboard without the toast is the regression that made Cmd+Shift+C copy silently while the Maho Mini button showed the toast.

## WEBUI SURFACES (12)
| Feature | URL | Mojom |
|---------|-----|-------|
| Settings | `chrome://maho-settings` | `maho_settings.mojom` |
| Sidebar | `chrome://maho-sidebar` | `maho_sidebar.mojom` |
| AI | `chrome://maho-ai` | `maho_ai.mojom` |
| Boost | `chrome://maho-boost` | `maho_boost.mojom` |
| Space Config | `chrome://maho-space-config` | `maho_space_config.mojom` |
| **Space Create (Theme Picker)** | **`chrome://maho-space-create`** | **`maho_space_create.mojom`** |
| Space Manage | `chrome://maho-space-manage` | `maho_space_manage.mojom` |
| Sync | `chrome://maho-sync` | `maho_sync.mojom` |
| Live Folders | `chrome://maho-live-folders` | `maho_live_folders.mojom` |
| Inline Edit | `chrome://maho-inline-edit` | `maho_inline_edit.mojom` |
| Routines | `chrome://maho-routines` | `maho_routines.mojom` |
| Test | `chrome://maho-test` | — |

> **Space Create note**: Despite the directory name, this surface is the **theme picker** (sparkle/sun/moon mode chips, color swatches, brightness dial). It is the only production theme picker — invoked from the native sidebar "Choose a Theme" row via `MahoSpaceThemePickerDialog::Open` which hosts the WebUI in a `views::Widget` dialog. See ADR at `.sisyphus/plans/theme-picker-architecture-decision.md`.

## COMMANDS
```bash
# Canonical build from the overlay repo root (builds chrome target).
# build_maho.py holds the shared .build.lock, so builds SERIALIZE. This is intentional:
# concurrent Chromium builds on one machine exhaust RAM (tens of GB of swap) and make ALL
# builds slower. Always build through this wrapper.
python3 build/scripts/build_maho.py

# Narrow target through the wrapper (lock + maho-core + overrides applied).
# Build the NARROWEST target that verifies your change — avoid full unit_tests /
# interactive_ui_tests / browser_tests (they compile every test TU) unless you must run them.
python3 build/scripts/build_maho.py --skip-rust --skip-lucide-check --no-launch --ninja-target <target>

# DO NOT run raw `autoninja -C out/Default` on the shared build machine — it bypasses the
# build lock, so parallel agents build at once and swap-thrash. If you truly need raw ninja:
#   python3 ../maho/scripts/build_lock.py -- autoninja -C out/Default <target>

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
- New subdirectory AGENTS.md: `browser/ai/AGENTS.md`, `browser/ui/views/AGENTS.md`, `browser/ui/webui/AGENTS.md`, `browser/resources/AGENTS.md`
