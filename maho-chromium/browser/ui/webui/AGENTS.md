# PROJECT KNOWLEDGE BASE — browser/ui/webui

**Generated:** 2026-07-14

## OVERVIEW
Mojo-backed WebUI surfaces. 12 production features + 1 test harness. Each feature = `{feature}.mojom` + `_page_handler.cc/h` + `_ui.cc/h`. Handlers are thin — delegate to `browser/ai/` helpers or `browser/ui/` managers.

## SURFACES
| Dir | URL | Notes |
|-----|-----|-------|
| `maho_settings/` | `chrome://maho-settings` | App settings hub |
| `maho_ai/` | `chrome://maho-ai` | Chat, BYOK config, tool use |
| `maho_boost/` | `chrome://maho-boost` | Site injection settings |
| `maho_space_config/` | `chrome://maho-space-config` | Space name/icon/color config |
| **`maho_space_create/`** | **`chrome://maho-space-create`** | **Theme picker** (permanent — see ADR) |
| `maho_space_manage/` | — | Space list management |
| `maho_sync/` | `chrome://maho-sync` | Sync status and account |
| `maho_live_folders/` | `chrome://maho-live-folders` | Smart folder rules |
| `maho_inline_edit/` | `chrome://maho-inline-edit` | Inline text editing |
| `maho_routines/` | `chrome://maho-routines` | Automation rules |
| `maho/` | shared | Auth utils, account prefs, AI prefs (cross-surface helpers) |
| `maho_test/` | `chrome://maho-test` | Dev/test harness only |

## WHERE TO LOOK
| Task | Location |
|------|----------|
| Add new WebUI surface | New `maho_{feature}/` dir with mojom + handler + ui + BUILD.gn |
| Auth token helpers | `maho_auth_utils.cc/h` |
| Account/AI prefs | `maho_account_prefs.h`, `maho_ai_prefs.h` |
| Private boundary test | `maho_webui_private_boundary.cc` |
| WebUI URL registration | `components/constants/webui_url_constants.h` |

## CONVENTIONS
- **Pattern**: `{feature}.mojom` → `_page_handler.cc/h` → `_ui.cc/h` → `resources/{feature}/app.ts`
- Page handlers are thin: param translation only; delegate to `browser/ai/` or view managers
- Mojo URLs registered in `components/constants/webui_url_constants.h`, not as raw strings
- Each surface has its own `BUILD.gn`

## ANTI-PATTERNS
- No complex logic in Mojo page handlers
- No raw `chrome://maho-*` URL strings — use constants header
- No new WebUI without a `.mojom` interface
- **Theme picker stays at `chrome://maho-space-create`** — do NOT migrate; ADR is permanent

## NOTES
- `maho_webui_private_boundary_unittest.cc` enforces that private Mojo interfaces are not accessible from renderer
- `maho/` subdir = shared cross-surface C++ helpers, not a feature surface
