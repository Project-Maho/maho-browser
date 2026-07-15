# PROJECT KNOWLEDGE BASE — maho/crates

**Generated:** 2026-07-14

## OVERVIEW
Rust workspace members providing all browser backend logic. 11 crates with clear separation of concerns. Root workspace at `maho/Cargo.toml`.

## CRATE MAP
| Crate | Role | Key Exports |
|-------|------|-------------|
| `maho-core` | Browser orchestrator — tab lifecycle, spaces, commands, settings | `MahoCore`, managers (see crates/maho-core/AGENTS.md) |
| `maho-types` | Contract types only — events, shell types, no business logic | `ShellEvent`, `CoreCommand`, `Tab`, `Space` |
| `maho-storage` | Persistence layer — LMDB, profile data, migration | `MahoStorage`, `ProfileStore` |
| `maho-ffi` | C FFI bridge — exposes `maho-core` to Chromium C++ and Swift shells | `maho_core_handle_command`, `maho_core_handle_event` (thin wrappers only) |
| `maho-engine` | Browser engine abstraction | `EngineHandle`, `NavigationRequest` |
| `maho-import` | Browser data import logic (Rust side) — parsers for Chrome/Firefox/Safari/Arc/Zen | Import parsers, cookie/favicon/bookmark/history models |
| `maho-agent` | AI agent runtime — tool execution, multi-step task orchestration | Agent runtime, CLI ↔ MCP coordination |
| `maho-browser-mcp` | MCP server embedded in the browser process | Browser tool registry, MCP session handling |
| `maho-cli` | Browser-side CLI binary (`maho` command) | Standalone binary entry |
| `maho-jni` | Android JNI bridge | JNI bindings for Android shell |
| `maho-bench` | Benchmarks (not shipped) | criterion benchmarks for core + storage |

## DEPENDENCY RULES
- `maho-types` has NO dependencies on other maho crates (contract-only)
- `maho-core` depends on `maho-types` and `maho-storage`; NO FFI, NO platform code
- `maho-ffi` depends on `maho-core`; wraps commands/events, NO business logic
- `maho-import` is the Rust parser layer; the C++ orchestration side lives in `maho-chromium/browser/importer/`

## WHERE TO LOOK
| Task | Location |
|------|----------|
| Add new shell event | `maho-types/src/shell_event.rs` + `maho-core/src/event_dispatcher.rs` |
| Add new browser command | `maho-types` command enum + `maho-core/src/maho_core.rs` match arm |
| New LMDB-persisted field | `maho-storage`, whitelist in `event_dirties_lmdb()` in core |
| Browser data import (Rust parsers) | `maho-import/src/parsers/` |
| AI agent / tool use | `maho-agent/` |
| Browser-embedded MCP | `maho-browser-mcp/src/server.rs` |

## ANTI-PATTERNS
- No domain types in `maho-core` — all public types go in `maho-types`
- No platform-specific code (Keychain, JNI) in `maho-core`
- No direct storage calls from `maho-core` — go through `maho-storage` trait
- No business logic in `maho-ffi` — thin wrappers only
- Do NOT add `relay/` crates to this workspace (libsqlite3-sys conflict)
- No `.unwrap()` / `.expect()` in non-test code (clippy denies it workspace-wide)

