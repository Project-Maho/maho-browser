# maho-core

## OVERVIEW

Browser core: tab lifecycle, spaces, commands, settings, event dispatch. 18 source files, 3811 lines.

## STRUCTURE

| File | Lines | Role |
|------|-------|------|
| `maho_core.rs` | 603 | Main orchestrator — holds all managers, processes commands |
| `tab_lifecycle.rs` | 604 | Tab create/close/navigate/reload/back/forward |
| `event_dispatcher.rs` | 465 | Routes CoreUpdates to shell via ShellEvent |
| `space_manager.rs` | 337 | Space (tab group) CRUD, reorder, active tracking |
| `command_bar.rs` | 279 | Omnibox: URL parse, search, command dispatch |
| `import_export_manager.rs` | 279 | Bookmark/session import-export (JSON, HTML) |
| `settings_manager.rs` | 218 | User preferences persistence |
| `split_view_manager.rs` | 156 | Split-screen tab layout |
| `boost_manager.rs` | 138 | Site-specific content injection |
| `crash_recovery.rs` | 114 | Session save/restore on crash |
| `download_manager.rs` | 114 | Download tracking and state |
| `atc_manager.rs` | 113 | Auto Tab Close — idle tab management |
| `note_manager.rs` | 96 | Per-tab notes |
| `find_manager.rs` | 95 | Find-in-page |
| `content_blocker.rs` | 68 | Ad/tracker blocking rules |
| `tab_preview_manager.rs` | 63 | Tab thumbnail generation |
| `permission_manager.rs` | 52 | Site permission grants (camera, mic, etc.) |
| `lib.rs` | 17 | Re-exports |

## WHERE TO LOOK

| Task | File |
|------|------|
| Add new browser command | `maho_core.rs` → match arm in process_command() |
| Tab state machine changes | `tab_lifecycle.rs` |
| New event to shell | `event_dispatcher.rs` + maho-types `shell_event.rs` |
| New manager/feature | New file + register in `maho_core.rs` |
| Space operations | `space_manager.rs` |
| URL bar / omnibox | `command_bar.rs` |

## CONVENTIONS

- `Rc<RefCell<>>` for shared mutable state between managers
- Manager-per-concern: each domain = separate file
- `UpdateListeners` pattern for reactive state propagation
- All public types come from `maho-types` — never define domain types here

## ANTI-PATTERNS

- ❌ Platform-specific code — use events/traits in maho-types
- ❌ Direct FFI calls — that's maho-ffi's job
- ❌ Storage calls — go through maho-storage
