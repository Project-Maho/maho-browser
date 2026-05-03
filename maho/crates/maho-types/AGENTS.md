# CRATE: maho-types

## OVERVIEW

Shared types, traits, and event definitions used by all other crates.

## STRUCTURE

| File | Lines | Purpose |
|------|-------|---------|
| `src/common.rs` | 274 | Core types: `Url`, `Rect`, `Cookie`, `Profile`, `NavigationEntry` |
| `src/identifiers.rs` | 45 | Strongly-typed IDs: `TabId`, `SpaceId`, `ProfileId` (UUID wrappers) |
| `src/tab.rs` | 128 | Tab state, `TabState` enum, navigation state |
| `src/space.rs` | 96 | Space (tab container) definitions |
| `src/events/shell_event.rs` | 305 | Shell-to-core events: `CreateTab`, `Navigate`, `CloseTab` |
| `src/events/core_update.rs` | 199 | Core-to-shell updates: `TabCreated`, `TitleChanged`, `LoadProgress` |
| `src/traits/shell_renderer.rs` | 312 | `ShellRenderer` trait for UI callbacks |
| `src/traits/webview_engine.rs` | 89 | `WebViewEngine` trait for platform webview abstraction |

## WHERE TO LOOK

| Task | File |
|------|------|
| URL/Rect/Cookie types | `common.rs` |
| TabId, SpaceId, ProfileId | `identifiers.rs` |
| Shell → Core events | `events/shell_event.rs` |
| Core → Shell updates | `events/core_update.rs` |
| UI rendering trait | `traits/shell_renderer.rs` |
| WebView abstraction | `traits/webview_engine.rs` |

## CONVENTIONS

- **serde derive**: All public types implement `Serialize` + `Deserialize`
- **UUID for IDs**: All identifiers use `uuid::Uuid` internally
- **chrono timestamps**: All timestamps use `chrono::DateTime<Utc>`
- **Non-exhaustive**: Public enums marked `#[non_exhaustive]` for forward compatibility
