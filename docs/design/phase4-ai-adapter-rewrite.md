# Phase 4.1 — AI Adapter Rewrite (Typed Task-Graph)

**Status:** Locked. Burn-rewrite approved at Q-2 gate.
**Owner:** Sisyphus
**Scope summary:** Replace `MahoUnifiedAgentAdapter` + `MahoBrowserToolExecutor` + `MahoChatOrchestrationService` + `MahoAiLlmClient` SSE accumulators with three typed primitives: `FfiCallbackHandle<T>`, `ConversationTask`, `ToolExecutionGraph`.

**Audit findings closed:** C2, C5, H6, H13, M16.

**References:**
- Audit: `docs/audits/fork-memory-perf-audit-2026-06-28.md`
- Locked plan: `docs/plans/memory-perf-remediation-locked-2026-06-28.md` (Phase 4.1, Wave 4)
- Hyperplan provenance: creative finding #6b "burn-rewrite", approved by user at gate Q-2.

---

## Context — current architecture

```
WebUI (chrome://maho-ai)
   │  mojom calls
   ▼
MahoChatOrchestrationService          ← stack-local executor on tool round (C2)
   │  HandleMessage / round driver
   ▼
MahoUnifiedAgentAdapter               ← leaks user_data_handle_ on Reset (C5)
   │  SubmitMessage → ffi_user_data_  ← unbounded accumulated_text_ (H13)
   ▼
maho_agent_send_message  (FFI)        ← raw void* user_data, no lifetime
   │   on_token / on_complete / on_error    contract (H6)
   ▼
maho-core LLM client / MahoAiLlmClient (HTTP+SSE path)
                                      ← O(n²) SSE buffer erase (M16)
                                      ← unbounded full_text accumulator (H13)
```

### Specific bugs

| # | Finding | File:line | Symptom |
|---|---------|-----------|---------|
| C2 | Stack-local `MahoBrowserToolExecutor` outlives nothing | `maho_chat_orchestration_service.cc:608` | Executor goes out of scope before async tool callback fires; `OnToolInvocationComplete` runs against a dangling `*this` in nested helpers. |
| C5 | `user_data_handle_` heap leak | `maho_unified_agent_adapter.cc:112` (alloc), `:168` (intentional leak on `Reset()`); declared `maho_unified_agent_adapter.h:113` | `new std::weak_ptr<FfiUserData>` is allocated per session, comment at `:167` says "intentionally leaked" because there is no Rust-side `free_fn`. Leaks ~16 B per session and grows unboundedly across long-running profiles. |
| H6 | No FFI callback lifetime contract | `maho_ffi.h:1588-1594` (chat send), `:1743-1750` (agent send), `:1881-1885` (routine run) | C-ABI signature accepts `void* user_data` with **zero** lifetime documentation. The C++ side smuggles a `std::weak_ptr<FfiUserData>*` (`maho_unified_agent_adapter.cc:120-121, 135`) and hopes Rust never derefs after `Reset()`. UAF is gated solely by the leak in C5. |
| H13 | Unbounded accumulators | `maho_unified_agent_adapter.cc:117` (member decl), `:321-323` (`OnTokenOnSequence` does `accumulated_text_ += token`); `maho_ai_llm_client.cc:200` (`completion_result->full_text += *content`); `maho_ai_llm_client.cc:242` (`parsed_call.arguments_json += *arguments`) | A pathological model that streams forever (or just a 10 MB JSON tool-args blob) keeps growing `std::string` heaps. No cap, no telemetry. |
| M16 | O(n²) SSE buffer erase | `maho_ai_llm_client.cc:150-152` (`while (find("\n\n")) ... erase(0, pos+2)`) | Every SSE event triggers a `std::string::erase(0, …)` that memmoves the entire trailing tail. With a 10 MB stream and 1 KB events that is 10⁷ moves of 10⁷ bytes → 10¹⁴ byte-ops worst case. Cursor advance fixes it. |

### Why incremental fix is not enough

The four bugs share a single root cause: there is no typed owner for the FFI session and no typed state machine for a conversation. C2 + C5 + H6 are the same defect viewed from three layers. A point-fix on any one of them leaves the other two intact and adds a new "almost-RAII" abstraction that future contributors will route around. The user opted for burn-rewrite (Q-2) so we replace the abstraction, not the call sites.

---

## Target architecture

```
WebUI (chrome://maho-ai)
   │  mojom calls (unchanged)
   ▼
MahoChatOrchestrationService          ← owns ConversationTask per active turn
   │
   ▼
ConversationTask (state machine)      ← bounded accumulator (2 MB cap)
   │  ├─ FfiCallbackHandle<ConversationTask>   ← RAII owner of user_data ptr
   │  └─ ToolExecutionGraph            ← member, not stack-local
   │       └─ unique_ptr<MahoBrowserToolExecutor>[]   ← heap-owned per node
   ▼
maho_agent_send_message (FFI)         ← user_data = FfiCallbackHandle::raw()
   │                                     dtor cancels FFI session BEFORE freeing
   ▼
maho-core / MahoAiLlmClient
        SSE buffer = cursor-based BoundedAccumulator    (fixes M16 + H13)
```

### Component responsibilities

#### `FfiCallbackHandle<T>`

Owns the `void*` passed to Rust FFI callbacks. Generic over `T` = the C++ owner type (e.g. `ConversationTask`).

- **Construction:** takes a `scoped_refptr<base::SequencedTaskRunner>` and a `base::WeakPtr<T>`. Allocates a heap pointee that stores both. The raw address of this pointee is the `user_data` passed to FFI.
- **Access:** `void* user_data() const noexcept` returns the stable pointer.
- **Dispatch:** static thunks (`OnToken`, `OnComplete`, `OnError`) downcast `user_data` back to the handle pointee, lock the weak ptr, and `PostTask` to the owner sequence. A `WeakPtr<T>` that is invalidated causes the post to be dropped silently — no UAF.
- **Destruction (RAII):** dtor MUST cancel the FFI session first (caller injects a `base::OnceClosure cancel_fn` at construction; the dtor invokes it), THEN frees the pointee. Order is load-bearing: cancelling first guarantees Rust will never enqueue a new callback that dereferences the about-to-be-freed pointee. Closes C5 (no leak) and H6 (typed contract, RAII-enforced).
- **Storage:** `std::unique_ptr<FfiCallbackHandle<ConversationTask>>` member of `ConversationTask`. Move-only.

#### `ConversationTask`

A single conversation turn's state machine. Heap-owned by `MahoChatOrchestrationService` per active turn.

States: `Idle → Streaming → ToolCall → (Streaming|Done) | Error`.

- **Stable `TaskId`:** monotonically allocated 64-bit id. All async callbacks carry the `TaskId` so cancellation never relies on raw pointers being valid. The owning service drops a task from its map; subsequent posted callbacks look the id up, find it missing, and return.
- **Bounded text accumulator:** `BoundedAccumulator` member with a configurable cap (default 2 MiB; surfaced via Finch param `maho_ai.max_assistant_text_bytes`). `OnToken` appends until the cap; once at capacity, further tokens are counted (`overflow_byte_count_`) and emitted via UMA, but the string does not grow. Closes H13 (adapter side).
- **Owns its `FfiCallbackHandle<ConversationTask>`:** the handle's cancel closure is `base::BindOnce(&maho_agent_cancel, agent_session_)`. Destroying the task cancels the FFI session and frees the user_data atomically.
- **Owns its `ToolExecutionGraph`:** the graph lives as long as the task. Tool callbacks bind a `WeakPtr<ConversationTask>` plus the `TaskId`. Closes C2.
- **Cancellation:** `Cancel()` transitions to `Done`, invokes the `FfiCallbackHandle` dtor (which cancels FFI), drops the graph.

#### `ToolExecutionGraph`

Replaces the stack-local `MahoBrowserToolExecutor`. Owned as a member of `ConversationTask`.

- **Heap-owned nodes:** `Execute(name, args, CompletionCallback)` adds a node; the node owns its `std::unique_ptr<MahoBrowserToolExecutor>`. The executor lives until the callback runs, regardless of which stack frame initiated the call.
- **Deadline timer:** `base::OneShotTimer` per node (default 30 s, configurable per tool). On expiry, the node's callback is invoked with a synthetic `BuildErrorResult("tool deadlined")`, and the executor is destroyed. Closes the secondary risk in C2 where a hung tool could pin memory forever.
- **CancelAll():** invoked by `ConversationTask::Cancel`. Drops all nodes; pending callbacks are no-ops because they bind a `WeakPtr<ConversationTask>`.
- **Post-back:** every tool callback hops through `ConversationTask::OnToolResult(TaskId, …)`. The task verifies `TaskId` matches its own id before acting. Closes C2 against the dangling-`this` shape.

---

## Public API sketch (pseudo-code; not final)

### `maho_ffi_callback_handle.h`

```cpp
template <typename T>
class FfiCallbackHandle {
 public:
  FfiCallbackHandle(scoped_refptr<base::SequencedTaskRunner> runner,
                    base::WeakPtr<T> owner,
                    base::OnceClosure cancel_fn);
  ~FfiCallbackHandle();  // runs cancel_fn_, THEN frees pointee_

  FfiCallbackHandle(const FfiCallbackHandle&) = delete;
  FfiCallbackHandle& operator=(const FfiCallbackHandle&) = delete;

  // Stable address handed to Rust as user_data. Valid for the lifetime of *this.
  void* user_data() const noexcept { return pointee_.get(); }

  // Static thunks; signature matches maho_ffi.h:1588-1594, 1743-1750.
  // Each downcasts user_data, locks weak_owner, posts to runner.
  static void OnToken(void* user_data, const char* token);
  static void OnComplete(void* user_data, const char* full_text,
                         const char* tool_calls_json);
  static void OnError(void* user_data, const char* error);

 private:
  struct Pointee {
    scoped_refptr<base::SequencedTaskRunner> runner;
    base::WeakPtr<T> weak_owner;
  };
  std::unique_ptr<Pointee> pointee_;
  base::OnceClosure cancel_fn_;
};
```

### `maho_conversation_task.h`

```cpp
class ConversationTask {
 public:
  enum class State { kIdle, kStreaming, kToolCall, kDone, kError };

  using TaskId = uint64_t;
  static TaskId NextId();

  ConversationTask(TaskId id,
                   Browser* browser,
                   PrefService* prefs,
                   size_t accumulator_cap_bytes = 2 * 1024 * 1024);
  ~ConversationTask();

  TaskId id() const noexcept { return id_; }
  State state() const noexcept { return state_; }

  // Transitions:
  void StartStreaming(const std::string& message,
                      RuntimeEventCallback on_event);
  void OnToken(std::string token);          // appends to accumulator_
  void EnterToolCall(std::vector<ToolInvocation> calls);
  void Complete(std::string full_text);
  void Error(std::string message);
  void Cancel();

  std::string_view accumulated_text() const { return accumulator_.view(); }
  bool is_at_capacity() const { return accumulator_.at_capacity(); }
  size_t overflow_byte_count() const { return accumulator_.overflow_bytes(); }

 private:
  void OnToolResult(TaskId expected_id,
                    ToolInvocation invocation,
                    base::DictValue result);

  SEQUENCE_CHECKER(sequence_checker_);
  const TaskId id_;
  State state_ = State::kIdle;
  BoundedAccumulator accumulator_;
  std::unique_ptr<FfiCallbackHandle<ConversationTask>> ffi_handle_;
  std::unique_ptr<ToolExecutionGraph> tool_graph_;
  RuntimeEventCallback on_event_;
  base::WeakPtrFactory<ConversationTask> weak_factory_{this};
};
```

### `maho_tool_execution_graph.h`

```cpp
class ToolExecutionGraph {
 public:
  using CompletionCallback = base::OnceCallback<void(base::DictValue)>;

  explicit ToolExecutionGraph(Browser* browser,
                              base::TimeDelta default_deadline =
                                  base::Seconds(30));
  ~ToolExecutionGraph();

  void AddExecutor(const std::string& tool_name,
                   std::unique_ptr<MahoBrowserToolExecutor> executor);

  void Execute(const std::string& tool_name,
               const base::DictValue& arguments,
               CompletionCallback on_done);

  void SetDeadlineFor(const std::string& tool_name, base::TimeDelta deadline);
  void CancelAll();

 private:
  struct Node;
  void OnNodeDone(Node* node, base::DictValue result);
  void OnNodeDeadline(Node* node);
  std::vector<std::unique_ptr<Node>> nodes_;
  raw_ptr<Browser> browser_;
  base::TimeDelta default_deadline_;
  SEQUENCE_CHECKER(sequence_checker_);
};
```

---

## Migration plan (Wave 4 sub-tasks)

| Task | Files touched | Depends on | Success criteria |
|------|---------------|------------|------------------|
| **4.1.1** Implement `FfiCallbackHandle<T>` + unit tests | NEW `browser/ai/maho_ffi_callback_handle.{h,cc}`, `…_unittest.cc`; `browser/ai/BUILD.gn` | — | Unit test proves: (a) dtor invokes `cancel_fn` before freeing pointee (instrumented order check); (b) `WeakPtr` invalidation drops posted callbacks; (c) thunks survive nullptr `user_data` / nullptr payload. `lsp_diagnostics` clean. |
| **4.1.2** Implement `ConversationTask` + `BoundedAccumulator` + unit tests | NEW `browser/ai/maho_conversation_task.{h,cc}`, `browser/ai/maho_bounded_accumulator.{h,cc}`, `…_unittest.cc`; `BUILD.gn` | 4.1.1 | Accumulator caps at configured byte count; overflow counter increments; state-machine transitions reject illegal moves; `TaskId` survives a cancel race in test. |
| **4.1.3** Implement `ToolExecutionGraph` + deadline timer + unit tests | NEW `browser/ai/maho_tool_execution_graph.{h,cc}`, `…_unittest.cc`; `BUILD.gn` | 4.1.2 (uses `TaskId` shape) | Deadline timer fires synthetic error; `CancelAll()` drops nodes; node executor outlives synchronous return from `Execute`. |
| **4.1.4** Migrate `MahoUnifiedAgentAdapter` to the 3 primitives | `browser/ai/maho_unified_agent_adapter.{h,cc}`, `…_unittest.cc` | 4.1.1, 4.1.2 | `user_data_handle_` field deleted (was `.h:113`); `accumulated_text_` field deleted (was `.h:117`); `Reset()` (was `.cc:159-174`) calls into `ConversationTask::Cancel()` and no longer leaks. Adapter holds `std::unique_ptr<ConversationTask>` per active turn. |
| **4.1.5** Migrate `MahoChatOrchestrationService` to use `ConversationTask` | `browser/ai/maho_chat_orchestration_service.{h,cc}` | 4.1.4 | Stack-local `MahoBrowserToolExecutor executor(...);` at `:608` replaced by `task->tool_graph().Execute(...)`. `ConversationRoundState` (was `.h:82-102`) collapses into `ConversationTask`. `active_round_state_` (was `.h:203`) replaced by `task_id_`. |
| **4.1.6** Migrate `MahoAiLlmClient` SSE buffer to cursor-based `BoundedAccumulator` | `browser/ai/maho_ai_llm_client.{h,cc}` | 4.1.2 | `ProcessSseBuffer` (was `.cc:146-247`) replaced: advance a `size_t cursor_` past consumed events instead of `erase(0, …)`. `completion_result->full_text` (was `.cc:200`) and `parsed_call.arguments_json` (was `.cc:242`) wrapped in `BoundedAccumulator`. Fixes M16 + H13. |
| **4.1.7** Delete legacy fields, sweep | All of `browser/ai/` | 4.1.6 | `grep` verifications below all pass; no `delete this` patterns; legacy `FfiUserData` struct removed; unit tests still green. |

**Intermediate-state rule:** every task ends in a tree where `lsp_diagnostics` is clean. No half-converted commits. Legacy code path remains usable until 4.1.7 deletes it.

---

## Success criteria (the contract)

### Audit findings closure proof

- **C2** closed when `maho_chat_orchestration_service.cc:608` no longer constructs a stack-local `MahoBrowserToolExecutor`. Replacement is heap-owned by `ToolExecutionGraph`; tool callback binds `WeakPtr<ConversationTask>` + `TaskId`.
- **C5** closed when `maho_unified_agent_adapter.h:113` (`user_data_handle_`) is deleted and the "intentionally leaked" comment at `maho_unified_agent_adapter.cc:167` is gone. `FfiCallbackHandle` dtor frees the pointee deterministically.
- **H6** closed when every `void* user_data` site in `browser/ai/` is sourced from `FfiCallbackHandle::user_data()` and freed by its dtor. The lifetime contract is encoded in the type, not in prose.
- **H13** closed when `accumulated_text_` (`maho_unified_agent_adapter.h:117`), `completion_result->full_text` (`maho_ai_llm_client.cc:200`), and `parsed_call.arguments_json` (`maho_ai_llm_client.cc:242`) are routed through `BoundedAccumulator` with a default cap and an overflow counter.
- **M16** closed when `maho_ai_llm_client.cc:152` (`sse_buffer->erase(0, pos + 2);`) is replaced by cursor advancement; the buffer is compacted only when `cursor_ > buffer_.size() / 2` (amortized O(1) per event).

### Source-only verification (no Chromium build this session)

- `lsp_diagnostics` clean on every file in the diff.
- Unit tests for 4.1.1, 4.1.2, 4.1.3 compile and pass under `bun run typecheck`-style source checks.

### Grep verifications (must produce empty output)

```bash
grep -R 'user_data_handle_' maho-chromium/browser/ai/                 # must be empty
grep    'accumulated_text_' maho-chromium/browser/ai/maho_unified_agent_adapter.h
grep -R 'delete this'       maho-chromium/browser/ai/                 # must be empty
grep    'erase(0,'          maho-chromium/browser/ai/maho_ai_llm_client.cc
```

### New files must exist

- `maho-chromium/browser/ai/maho_ffi_callback_handle.{h,cc}`
- `maho-chromium/browser/ai/maho_conversation_task.{h,cc}`
- `maho-chromium/browser/ai/maho_tool_execution_graph.{h,cc}`
- `maho-chromium/browser/ai/maho_bounded_accumulator.{h,cc}`

---

## Risks & rollback

- **Risk:** behavior drift across the 7-step migration. **Mitigation:** legacy `MahoUnifiedAgentAdapter` path stays functional through 4.1.6; the new `ConversationTask` runs in parallel behind a runtime flag (`maho.ai.use_typed_task_graph`, default false) for tasks 4.1.4 and 4.1.5, then becomes default-on at 4.1.7. Intermediate states are compilable and shippable.
- **Risk:** Mojo schema changes might be needed. **Mitigation:** NONE required. `maho_ai.mojom` is unchanged; only the internal C++ types swap. The Rust-side FFI signatures at `maho_ffi.h:1588-1594, 1743-1750, 1881-1885` are unchanged — `FfiCallbackHandle` is purely a C++-side typed wrapper around the same `void*` ABI.
- **Risk:** new bounded cap rejects a legitimate long response. **Mitigation:** cap is Finch-controlled (`maho_ai.max_assistant_text_bytes`); overflow emits a UMA histogram so we can size from data before locking the default.
- **Rollback:** revert the branch. Tasks 4.1.1 through 4.1.6 leave the legacy code in tree; only 4.1.7 removes it, and 4.1.7 is a single atomic commit. Rolling back 4.1.7 restores the pre-rewrite state with no schema migration.

---

## Out of scope

- Performance tuning of `ConversationTask::OnToken` allocator strategy (arena, slab, small-string preallocation). Future work; the bounded accumulator's default `std::string` allocator is acceptable for the 2 MiB cap.
- Replacing the Mojo IPC layer between WebUI and `MahoChatOrchestrationService`. Out of scope; the chat surface contract is unchanged.
- The Rust-side `MahoUnifiedAgent` is NOT touched. Only the C++ adapter is rewritten. The audit's Rust-side findings live in a separate cluster (see `docs/audits/rust-core-memory-perf-audit-2026-06-28.md`).
- Cross-tab conversation state (one `ConversationTask` per tab vs. one per browser window). Out of scope; current single-task-per-service assumption is preserved.
