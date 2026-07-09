# Sync Entity Coverage Checklist

**Date:** 2026-07-09
**Status:** 10/10 Entities Fully Covered & Verified

This document lists the push/apply synchronization wiring and integration test coverage for all 10 core entity types declared in `SyncEntityType`.

## Entity Coverage Status

| Entity Type | Push/Mutation wiring | Apply Handler | Integration Test Verified |
|-------------|----------------------|---------------|---------------------------|
| **Tab** | `MahoCore::push_sync_entity` inside tab close/restore | `SyncEntityType::Tab` case in `MahoCore::apply_sync_remote_entities` | Yes (`sync_all_entities_test.rs`) |
| **Space** | `MahoCore::push_sync_entity` inside space mutation | `SyncEntityType::Space` case in `MahoCore::apply_sync_remote_entities` | Yes (`sync_all_entities_test.rs`) |
| **Bookmark** | `MahoCore::push_sync_entity` inside bookmark manager | `SyncEntityType::Bookmark` case in `MahoCore::apply_sync_remote_entities` | Yes (`sync_all_entities_test.rs`) |
| **Note** | `MahoCore::push_sync_entity` inside note manager | `SyncEntityType::Note` case in `MahoCore::apply_sync_remote_entities` | Yes (`sync_all_entities_test.rs`) |
| **Boost** | `MahoCore::push_sync_entity` inside boost manager | `SyncEntityType::Boost` case in `MahoCore::apply_sync_remote_entities` | Yes (`sync_all_entities_test.rs`) |
| **ReadingListItem** | `MahoCore::push_sync_entity` inside reading list manager | `SyncEntityType::ReadingListItem` case in `MahoCore::apply_sync_remote_entities` | Yes (`sync_all_entities_test.rs`) |
| **SearchEngine** | `MahoCore::push_sync_entity` inside search engine manager | `SyncEntityType::SearchEngine` case in `MahoCore::apply_sync_remote_entities` | Yes (`sync_all_entities_test.rs`) |
| **Memory** | `MahoCore::push_sync_entity` inside memory manager | `SyncEntityType::Memory` case in `MahoCore::apply_sync_remote_entities` | Yes (`sync_all_entities_test.rs`) |
| **Shortcut** | `MahoCore::push_sync_entity` inside shortcut manager | `SyncEntityType::Shortcut` case in `MahoCore::apply_sync_remote_entities` | Yes (`sync_all_entities_test.rs`) |
| **Extension** | `MahoCore::push_sync_entity` inside extension bridge | `SyncEntityType::Extension` case in `MahoCore::apply_sync_remote_entities` | Yes (`sync_all_entities_test.rs`) |

## Verification Details

Verification is automated via the `sync_all_entities_test` integration test, which constructs and applies valid serialized JSON payloads for all 10 entity types, checking that they are correctly deserialized and inserted/updated in the core state and underlying storage.
