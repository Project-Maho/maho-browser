# maho-core

## OVERVIEW

`MahoCore` owns browser state orchestration, turning shell events into manager changes and `CoreUpdate` output.

## STRUCTURE

| Area | Files | Responsibility |
|------|-------|----------------|
| Orchestration | `maho_core.rs`, `maho_core/vault_items.rs` | Owns manager lifecycles, state hydration, persistence scheduling, and cross-manager workflows. |
| Tabs and spaces | `tab_lifecycle.rs`, `space_manager.rs`, `workspace_manager.rs`, `tab_clustering.rs` | Tab lifecycle, space hierarchy, active state, and grouping. |
| User input | `command_bar.rs`, `shortcut_manager.rs`, `find_manager.rs`, `suggestion_manager.rs` | Omnibox actions, key bindings, find state, and suggestions. |
| Browser features | `bookmark_manager.rs`, `reading_list_manager.rs`, `note_manager.rs`, `download_manager.rs`, `easel_manager.rs` | User-owned browser content and its view models. |
| Site behavior | `boost_manager.rs`, `boost_css_sanitizer.rs`, `css_mod_manager.rs`, `content_blocker.rs`, `permission_manager.rs`, `autofill_manager.rs` | Per-site customization, content rules, permissions, and form filling. |
| Sync and sharing | `sync_manager.rs`, `sync_crypto.rs`, `sync_models.rs`, `account_manager.rs`, `sharing_manager.rs`, `backup_manager.rs` | Entity synchronization, encryption, accounts, sharing, and backup. |
| Vault | `vault_manager.rs`, `vault_runtime.rs`, `vault_crypto.rs`, `oscrypt.rs` | Lock state, key handling, encrypted credential access, and platform key protection. |
| AI | `llm_manager.rs`, `llm_client.rs`, `memory_manager.rs`, `skills_manager.rs`, `tool_registry.rs` | Model access, memory, skills, and tool permissions. |

## WHERE TO LOOK

| Task | Location |
|------|----------|
| Trace an inbound shell action or outgoing update | `maho_core.rs` `handle_event`, then `event_dispatcher.rs` |
| Change tab creation, navigation, closing, archival, or restoration | `tab_lifecycle.rs` and its call site in `maho_core.rs` |
| Change space membership, ordering, folders, or active-space behavior | `space_manager.rs` |
| Add or revise a shortcut | `shortcut_manager.rs`, then the matching shell-event path in `maho_core.rs` |
| Add a command-bar action | `command_bar.rs` plus its event handling in `maho_core.rs` |
| Modify Boost CSS handling | `boost_manager.rs` and `boost_css_sanitizer.rs` |
| Change Easel creation, mutation, or view models | `easel_manager.rs`, then persistence in `maho_core.rs` |
| Change sync entity shape, encryption, or reconciliation | `sync_models.rs`, `sync_crypto.rs`, and `sync_manager.rs` |
| Change Vault lifecycle or encrypted item access | `vault_manager.rs`, `vault_runtime.rs`, and `maho_core/vault_items.rs` |
| Change AI skill injection or tool availability | `skills_manager.rs`, `tool_registry.rs`, and `maho_core.rs` |

## CONVENTIONS

- Keep each manager focused on one domain. `MahoCore` owns ordering where a workflow crosses domains.
- Emit shell-visible state changes as `CoreUpdate` values through the established listener and dispatcher path.
- Preserve the private-tab filtering applied before persistence, recovery, snapshots, and sync work.
- Treat Vault transitions as fail-closed. Keep keys and passphrases inside the Vault modules and avoid secret-bearing diagnostics.
- Keep CSS injection behind `boost_css_sanitizer.rs`, even when data was previously stored or imported.

## ANTI-PATTERNS

- Do not let managers reach into one another's mutable state. Add coordination in `MahoCore` instead.
- Do not add a shell action without its corresponding update, persistence, and recovery behavior where applicable.
- Do not persist, recover, snapshot, or synchronize private-tab identifiers or URLs.
- Do not bypass the Boost sanitizer or inject raw site CSS from a new call path.
- Do not weaken Vault lock, throttling, encryption, or transactional persistence behavior for convenience.
