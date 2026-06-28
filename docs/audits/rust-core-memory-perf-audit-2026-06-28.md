# Rust Core Crates — Memory, Deadlock & Performance Audit

**Date:** 2026-06-28  
**Scope:** `maho/crates/maho-core/src/`, `maho/crates/maho-storage/src/`  
**Auditor:** Automated deep-read analysis  
**Files read:** 30+ source files (all managers, providers, storage layer)

---

## Critical

### C-1: `block_on` on shared runtime — potential deadlock

**Location:** `maho-core/src/llm_provider.rs:30-31`

```rust
let rt = crate::memory_manager::get_runtime();
rt.block_on(validate_api_key_async(provider, api_key))
```

**Also:** `maho-core/src/chat_session.rs:161`

```rust
let _ = get_runtime().block_on(task);
```

**Scenario:** If `validate_api_key` or the chat_session cleanup is ever called from within a `tokio::spawn` on the same shared runtime (e.g., from a memory_manager background task that transitively calls key validation), `block_on` inside an async context deadlocks the runtime's only thread pool.

**Fix:** Use `spawn` + channel, or ensure these are only called from synchronous (non-tokio) contexts. Add `#[cfg(debug_assertions)]` assertion that current thread is not a tokio worker.

---

### C-2: `MemoryManager` — no `Drop` impl, orphaned tasks on shutdown

**Location:** `maho-core/src/memory_manager.rs:154,167,186`

```rust
self.active_tasks.push(h);  // JoinHandle<()> pushed, never aborted on drop
```

**Scenario:** When `MahoCore` (and thus `MemoryManager`) is dropped (app quit, profile switch), spawned tasks (`extract_facts_json`, `summarize_browsing`, `embed_fact`) continue executing on the shared static runtime. They hold `result_tx` clones that send to a now-dropped `result_rx` — the sends silently fail but the tasks still consume CPU/network. On rapid profile switches, tasks accumulate.

**Fix:** Implement `Drop for MemoryManager` that calls `h.abort()` on all `active_tasks`. Or store an `Arc<AtomicBool>` cancellation token checked by each task.

---

## High

### H-1: Unbounded channels — no backpressure on LLM streaming

**Locations:**
- `maho-core/src/llm_provider.rs:78` — `tokio::sync::mpsc::unbounded_channel()`
- `maho-core/src/providers/anthropic.rs:29` — same
- `maho-core/src/memory_manager.rs:77` — `mpsc::unbounded_channel()` for `MemoryResult`

```rust
let (tx, rx) = tokio::sync::mpsc::unbounded_channel();
// ...producer spawned, consumer is lazy Stream polled by caller
```

**Scenario:** If the consumer (UI/shell) stops polling the stream (tab backgrounded, app frozen, slow WebUI bridge), the producer continues reading from the HTTP response and buffering into the channel. A 200K-token Anthropic response at ~4 bytes/token = ~800KB queued per stalled stream. Multiple concurrent requests multiply this.

**Fix:** Replace with `mpsc::channel(capacity)` (e.g., 256 or 1024). The spawned producer will naturally pause when the channel is full (`.send().await` blocks). This provides backpressure all the way to the HTTP stream.

---

### H-2: `reqwest::Client::new()` created per-request — connection pool waste

**Locations (6 total):**
- `maho-core/src/llm_provider.rs:35` (validate_api_key)
- `maho-core/src/llm_provider.rs:79` (OpenAI streaming)
- `maho-core/src/providers/anthropic.rs:30` (Anthropic streaming)
- `maho-core/src/llm_client.rs:64` (extract_facts)
- `maho-core/src/llm_client.rs:132` (summarize_browsing)
- `maho-core/src/chat_session.rs:217` (chat streaming)

```rust
let client = reqwest::Client::new();
```

**Scenario:** Each `Client::new()` allocates a fresh connection pool, DNS resolver cache, and TLS session cache. For streaming chat (called on every user message), this means a new TLS handshake every time instead of reusing an existing connection. Adds ~100-300ms latency per request.

**Fix:** Create a single `static` or struct-level `reqwest::Client` (it's designed to be cloned cheaply and shared). Example:

```rust
use std::sync::LazyLock;
static HTTP: LazyLock<reqwest::Client> = LazyLock::new(|| {
    reqwest::Client::builder()
        .timeout(std::time::Duration::from_secs(120))
        .build()
        .unwrap()
});
```

---

### H-3: `sync_manager.rs` — `entity_versions` HashMap grows without bound

**Location:** `maho-core/src/sync_manager.rs` — `entity_versions: HashMap<String, u64>`

**Scenario:** Every synced entity (bookmark, tab, note, space) gets a version entry keyed by `"{entity_type}:{entity_id}"`. Entries are never removed, even when the underlying entity is deleted. Over months of use with thousands of bookmarks/history entries synced, this HashMap grows to tens of thousands of entries (~200 bytes each = several MB).

**Fix:** Remove entries from `entity_versions` when the entity is deleted. Or switch to a bounded LRU structure. Or eliminate in-memory tracking entirely — the SQLite `sync_state` table already tracks per-type versions.

---

### H-4: `sqlite.rs` — sync queue operations lack batching (N+1)

**Location:** `maho-storage/src/sqlite.rs:1780-1798`

```rust
pub fn mark_sync_entities_sent(&self, ids: &[i64]) -> Result<(), StorageError> {
    let now = chrono::Utc::now().timestamp();
    for id in ids {
        self.conn.execute(
            "UPDATE sync_queue SET sent_at = ?1 WHERE id = ?2",
            rusqlite::params![now, id],
        )?;
    }
    Ok(())
}

pub fn ack_sync_entities(&self, ids: &[i64]) -> Result<(), StorageError> {
    for id in ids {
        self.conn.execute("DELETE FROM sync_queue WHERE id = ?1", rusqlite::params![id])?;
    }
    Ok(())
}
```

**Scenario:** With 100 pending sync entities, this executes 100 individual SQL statements (each with its own journal write). Without explicit transaction wrapping, SQLite auto-commits each statement — 100 fsync operations. Can take 5-10 seconds on HDD, 500ms-1s on SSD.

**Fix:** Wrap in a transaction:
```rust
let tx = self.conn.transaction()?;
for id in ids { tx.execute(...)?; }
tx.commit()?;
```
Or use `WHERE id IN (...)` with a dynamically built parameter list.

---

### H-5: `tool_dispatcher.rs` / `mcp_manager.rs` — `active_tool_rounds` entries leak

**Location:** `maho-core/src/tool_dispatcher.rs:354,449` and `maho-core/src/mcp_manager.rs` (identical code)

```rust
self.active_tool_rounds.insert(pending.original_request_id.clone(), pending.tool_round);
```

**Scenario:** `active_tool_rounds` is populated when a tool loop completes a round. It's only *consumed* (removed) at line 189 when the same `request_id` enters `handle_llm_response_with_tools` again. If the LLM's final response has no tool calls (the normal completion path), the entry is never removed. Over a session with many tool-using conversations, entries accumulate — one per completed tool interaction.

**Fix:** Remove the entry in the non-tool-call path (when LLM returns a plain text response that completes the request). Or add a `cleanup_request(request_id)` method called when a chat completion is delivered to the UI.

---

## Medium

### M-1: `notification_manager.rs` — unbounded notification queue

**Location:** `maho-core/src/notification_manager.rs:80`

```rust
self.notifications.push(notification);
```

**Scenario:** `notifications: Vec<Notification>` grows until explicit `dismiss()` or `dismiss_all()`. A misbehaving website (or many sites with push notifications) can queue hundreds of notifications. Each contains cloned `ImageData` (icon bytes) — potentially KB each.

**Fix:** Cap at `MAX_NOTIFICATIONS` (e.g., 100). When exceeded, evict oldest. Or auto-expire after a TTL (e.g., 24 hours).

---

### M-2: `llm_manager.rs` — `preview_cache` has no eviction

**Location:** `maho-core/src/llm_manager.rs` — `preview_cache: HashMap<String, (String, String)>`

**Scenario:** Every preview request (URL summary, page context) inserts into this cache keyed by URL. No max size, no TTL, no LRU eviction. Long browser sessions with many page navigations grow this indefinitely.

**Fix:** Use a bounded `LruCache` (e.g., `lru` crate, max 128 entries), or clear on page navigation / session end.

---

### M-3: `sync_manager.rs` — `outgoing_messages` and `pending_pushes` grow during offline

**Location:** `maho-core/src/sync_manager.rs:89,100,103,118,129`

```rust
self.outgoing_messages.push(SyncMessage::Encrypted { data });
self.pending_pushes.push(entity);
```

**Scenario:** When the device is offline (or sync server is unreachable), every user action that triggers sync (bookmark create, tab change, note edit) appends to these Vecs. They're only drained on successful `flush()`. Extended offline periods (airplane mode, network issues) with active browsing can grow these to thousands of entries.

**Fix:** Cap `pending_pushes` with a maximum (e.g., 10000). For `outgoing_messages`, implement a flush-to-disk strategy or drop oldest messages when a threshold is hit. The SQLite `sync_queue` table already provides durable queuing — these in-memory buffers may be redundant.

---

### M-4: `llm_manager.rs` — `pending_requests` may leak on disconnection

**Location:** `maho-core/src/llm_manager.rs` — `pending_requests: HashMap<String, LLMRequest>`

**Scenario:** Entries are inserted when an LLM request starts. If the response never arrives (network timeout, server error without explicit error event, WebSocket disconnect), the entry remains in the map forever. Each entry holds the full request context including message history.

**Fix:** Add a periodic sweep (e.g., every 60s) that removes entries older than a timeout threshold (e.g., 5 minutes). Or remove on explicit session/tab close.

---

## Notes (Low / Informational)

### N-1: Architecture is sound for single-threaded access

The `MahoCore` struct owns all managers by value — no `Arc<Mutex<>>` between managers, no reference counting cycles. This eliminates a large class of deadlock and cycle-leak bugs. Async work is properly isolated to the `memory_manager`'s static tokio runtime.

### N-2: Bounded collections where it matters

- `tab_lifecycle.rs`: `MAX_CLOSED_STACK=25`, `MAX_ARCHIVED_PER_SPACE=100` ✓
- `command_bar.rs`: `MAX_HISTORY_ENTRIES=10000`, trigram LRU `max_cache_size=50` ✓
- `chat_session.rs`: `MAX_CHAT_HISTORY=20` (VecDeque) ✓
- `tool_dispatcher.rs`: `MAX_TOOL_ROUNDS=5` prevents infinite tool loops ✓

### N-3: `conversation_manager.rs` is clean

Stateless delegation to SQLite storage. No in-memory state accumulation.

### N-4: `maho-engine` crate is test-only stub

`StubWebViewEngine` (554 lines) — no production memory concerns.

### N-5: `memory_manager.rs` — `drain_results()` cleans finished tasks

```rust
self.active_tasks.retain(|h| !h.is_finished());  // line 193
```

This prevents unbounded `active_tasks` growth *during normal operation* — but only if `drain_results()` is called regularly by `MahoCore`. The Drop concern (C-2) is about the shutdown path.

### N-6: `chat_session.rs` — raw pointer FFI bridge

```rust
pub struct MahoChatEventSink { ptr: *mut c_void }
unsafe impl Send for MahoChatEventSink {}
unsafe impl Sync for MahoChatEventSink {}
```

This is an FFI boundary concern (managed by `maho-ffi`), not a Rust memory safety issue per se. The lifetime is managed by the C++ side.

---

## Summary

| Severity | Count | Key Theme |
|----------|-------|-----------|
| Critical | 2 | Deadlock risk (`block_on` on shared runtime), resource leak on Drop |
| High | 5 | Unbounded channels, per-request Client, HashMap leaks, N+1 SQL |
| Medium | 4 | Unbounded Vec growth, missing eviction, offline accumulation |
| Note | 6 | Architecture strengths, bounded collections, FFI boundary |

**Top 3 fixes by impact/effort ratio:**
1. **H-2** (reqwest::Client) — One `static LazyLock` eliminates 6 per-request allocations. 5 min fix, immediate latency improvement.
2. **H-4** (sync queue batching) — Wrap in transaction. 2 min fix, prevents 10s stalls on bulk sync.
3. **H-1** (bounded channels) — Replace `unbounded_channel()` with `channel(1024)`. 10 min fix across 3 files, prevents OOM under stalled consumers.
