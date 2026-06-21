# A6. Streaming Protocol Design — Native ↔ WebView Token Stream

**Status:** Design (locked). Wave 4 B1 (Android) and B2 (iOS) implement against this contract.
**Scope:** The transport layer between Rust core's `LLMStreamChunk` events and the WebView JS layer's Preact components. Single source of truth for both platforms.

---

## 1. Why this exists

LLM streaming runs at 50-200 tokens/sec in steady state. A naive "one `evaluateJavascript` call per token" design pays ~5-10ms of bridge overhead per call, which means **250-2000ms of wasted CPU per second of streaming on serialization alone**. That's not an optimization to defer; it's the difference between "smooth typewriter" and "phone gets warm and the UI hitches every frame."

Three additional realities force coalescing into the protocol layer:

1. **GC pauses queue events.** Android ART or iOS autorelease drains can pause the bridge thread for 50-300ms. Tokens pile up. If we naively dispatch each as it arrives, ordering is at the mercy of the platform scheduler.
2. **WebView JS can't backpressure the producer.** `evaluateJavascript` is one-way push. The protocol must absorb bursts on the native side or drop deterministically.
3. **The producer is in Rust on a background thread; WebViews require UI thread.** Every flush is a cross-thread hop. Fewer flushes = fewer hops.

> **Threat model citation:** Per-token JS calls = 250-2000ms/sec serialization overhead. Coalescing is **mandatory, not optimization**.

---

## 2. Coalescing Algorithm

Native side keeps a per-session buffer + flush timer. Tokens stream in from `LLMStreamChunk::Token`, get pushed into the buffer, and flush triggers in this priority order (whichever fires first wins):

| Trigger | Threshold | Rationale |
|---|---|---|
| Token count | **32 tokens** | One screen of text; matches Preact reconciliation cost |
| Elapsed time | **60ms** since last flush | Below human flicker fusion (~16ms × 4 frames); feels like real-time typing |
| End-of-stream | always | Final batch must include any tail tokens |
| Error | always | Flush surviving tokens, then surface error |

**Flush procedure (atomic):** snapshot buffer → clear buffer → cancel timer → marshal to JS array literal → hop to UI thread → `evaluateJavascript`.

Empty flush = no-op. The timer is only started lazily when the first token enters an empty buffer.

**`LLMStreamChunk` variant handling.** `LLMStreamChunk` is the Rust enum; the bridge does not redefine it. Mapping:

- `Token(s)` → push `s` into the per-session token buffer; obeys 32/60ms coalescing.
- `ToolCallDelta { … }` and `ImageDelta { … }` → forwarded as separate JS events (`__mahoStreamToolDelta`, `__mahoStreamImageDelta`) and **not** coalesced. They're rare, structurally distinct, and the UI treats them as discrete.
- `Error(msg)` → flushes pending tokens, then calls `__mahoStreamError`.

This keeps the hot path (Token) cheap and the rare path (tool/image) simple.

---

## 3. JS API Contract

Native injects these globals at WebView load time (via `addJavascriptInterface` on Android, `WKUserScript` on iOS):

```javascript
// Coalesced batch of plain text tokens. droppedCount is omitted when 0.
window.__mahoStreamBatch(sessionId: string, tokens: string[], droppedCount?: number): void;

// Stream finished cleanly. finalMessage is the canonical assembled message
// (the source of truth; tokens were a preview).
window.__mahoStreamComplete(sessionId: string, finalMessage: string): void;

// Stream errored mid-flight. JS layer transitions session to error state.
window.__mahoStreamError(sessionId: string, error: string): void;

// Tool-call argument delta (not coalesced; passes through verbatim).
window.__mahoStreamToolDelta(sessionId: string, index: number, idOrNull: string | null,
                              nameOrNull: string | null, argumentsDelta: string): void;

// Inline image delta (not coalesced).
window.__mahoStreamImageDelta(sessionId: string, index: number,
                               mimeOrNull: string | null, b64Chunk: string): void;
```

The JS bridge handler reads these globals and dispatches to subscribed Preact components via the existing signal/observable pattern. Sessions unknown to JS (e.g. event arrives after the component unmounted) are logged once and dropped — no throw, no rejection.

---

## 4. Buffer Overflow (GC pause / device thrash)

**Cap: 256 tokens per session.** That's ~5 seconds of stalled streaming at 50 tok/sec. Anything beyond that is unrecoverable UX; we choose deterministic drop over unbounded memory.

Eviction policy: **FIFO drop oldest.** New tokens always make it in; the oldest get evicted. A per-session `dropped_count` increments on each eviction. The next flush includes the count as the third arg:

```javascript
window.__mahoStreamBatch("sess-abc", ["the", "rest", "of", "the", "sentence"], 47);
//                                                                              ^^ 47 oldest tokens dropped
```

JS uses this to render a "..." indicator inline. `dropped_count` is reset to 0 immediately after being included in a batch (it represents drops since last flush, not lifetime).

256 × ~5 bytes/token ≈ **1.3KB per session**, negligible memory cost.

---

## 5. Thread Safety

| Concern | Resolution |
|---|---|
| Buffer access | Per-session, accessed only from the dedicated streaming thread. No locks needed if we keep the buffer thread-confined. |
| Flush dispatch | Always hops to UI thread. Marshal happens on streaming thread; JS call happens on UI thread. |
| Timer | Lives on the streaming thread (NOT the UI thread). Cancelled on flush, end-of-stream, error, and session free. |
| Session free mid-stream | Flushes any pending tokens, then disposes the buffer + timer. No tokens leak across session boundaries. |

**Android skeleton (Kotlin):**

```kotlin
class StreamingBridge(private val webView: WebView) {
    private val mainHandler = Handler(Looper.getMainLooper())
    private val streamHandler = Handler(HandlerThread("maho-stream").apply { start() }.looper)
    private val sessions = mutableMapOf<String, SessionBuffer>()  // streamHandler-confined

    fun pushToken(sessionId: String, token: String) {
        streamHandler.post {
            val buf = sessions.getOrPut(sessionId) { SessionBuffer(sessionId) }
            buf.add(token)
            if (buf.size >= 32) flush(buf)
            else if (!buf.timerArmed) {
                streamHandler.postDelayed({ flush(buf) }, 60)
                buf.timerArmed = true
            }
        }
    }

    private fun flush(buf: SessionBuffer) {
        if (buf.isEmpty()) return
        val (tokens, dropped) = buf.snapshotAndClear()
        val js = buildBatchJs(buf.sessionId, tokens, dropped)  // pre-escaped
        mainHandler.post { webView.evaluateJavascript(js, null) }
    }
}
```

**iOS skeleton (Swift):**

```swift
final class WebViewBridgeController {
    private let streamQueue = DispatchQueue(label: "maho.stream", qos: .userInitiated)
    private var sessions: [String: SessionBuffer] = [:]  // streamQueue-confined
    private weak var webView: WKWebView?

    func pushToken(sessionId: String, token: String) {
        streamQueue.async { [weak self] in
            guard let self else { return }
            let buf = self.sessions[sessionId] ?? SessionBuffer(id: sessionId)
            self.sessions[sessionId] = buf
            buf.add(token)
            if buf.count >= 32 { self.flush(buf) }
            else if !buf.timerArmed {
                buf.timerArmed = true
                self.streamQueue.asyncAfter(deadline: .now() + .milliseconds(60)) { [weak buf] in
                    if let buf, buf.timerArmed { self.flush(buf) }
                }
            }
        }
    }

    private func flush(_ buf: SessionBuffer) {
        guard !buf.isEmpty else { return }
        let (tokens, dropped) = buf.snapshotAndClear()
        let js = buildBatchJs(buf.id, tokens, dropped)
        DispatchQueue.main.async { [weak self] in
            self?.webView?.evaluateJavaScript(js, completionHandler: nil)
        }
    }
}
```

Note: both skeletons keep the buffer mutation on a single thread (streamHandler / streamQueue). The only cross-thread hop is the `evaluateJavascript` call itself, which is fire-and-forget.

---

## 6. Sequence Diagram

```
Rust core            Bridge (stream thread)         UI thread / WebView           JS / Preact
    │                          │                           │                           │
    │── LLMStreamChunk T1 ────►│                           │                           │
    │                          │ buffer.push(T1)            │                           │
    │                          │ timer.arm(60ms)            │                           │
    │── T2 ─────────────────  ►│ buffer.push(T2)            │                           │
    │   ...                    │   ...                      │                           │
    │── T32 ──────────────────►│ buffer.push(T32) → size=32 │                           │
    │                          │ timer.cancel               │                           │
    │                          │ snapshot=[T1..T32]; clear  │                           │
    │                          │── post ──────────────────► │                           │
    │                          │                            │ evaluateJavascript(...)   │
    │                          │                            │── window.__mahoStreamBatch ►│
    │                          │                            │                           │ signal.update
    │── T33 ──────────────────►│ buffer.push(T33)           │                           │ ↳ Preact re-render
    │                          │ timer.arm(60ms)            │                           │
    │   (60ms passes, no T34)  │ TIMER FIRES → flush [T33]  │                           │
    │                          │── post ────────────────────► evaluateJavascript        │
    │                          │                            │── __mahoStreamBatch ──────►│
    │── EndOfStream ──────────►│ flush remaining (if any)   │                           │
    │                          │── post ────────────────────► __mahoStreamComplete ─────►│
    │                          │ free session buffer        │                           │
```

---

## 7. Error Modes

| Failure | Behavior |
|---|---|
| Stream errors mid-flight (network drop, LLM 500, BYOK invalid) | Bridge flushes pending tokens, then calls `__mahoStreamError(sessionId, msg)`. JS transitions to error state. **Pinch-LLM specifically:** UI keeps the extractive bullets already shown and suppresses the LLM bullets section. |
| WebView destroyed mid-stream (user navigated) | `evaluateJavascript` returns/completes with nil. Bridge marks session as orphaned, frees buffer, drops remaining tokens silently. No callback to Rust core (it'll get its own end-of-stream eventually). |
| WebView frozen (debugger pause, system thrash) | Buffer fills to 256 → FIFO drop oldest → next flush carries `droppedCount`. UI renders "..." marker. |
| Session free while buffer non-empty | Flush pending tokens **before** disposing. Never silently swallow on graceful teardown. |
| JS handler missing (window.__mahoStreamBatch undefined) | `evaluateJavascript` no-ops at the call site. Native logs once per session and continues. Implies WebView didn't load the bridge JS yet — a race on cold start. |

---

## 8. Performance Budget

- **Per-batch JS call:** ~5-10ms (one `evaluateJavascript` round-trip + JS function call).
- **Steady state (50 tok/sec):** 32-token cap dominates → ~1.5 batches/sec → **<15ms/sec overhead**.
- **Peak (200 tok/sec):** still 32-token cap → ~6.25 batches/sec → ~50ms/sec overhead. Acceptable.
- **Slow stream (5 tok/sec):** 60ms timer dominates → ~16 batches/sec but each batch is tiny → bridge cost is the floor, ~80ms/sec. Still fine.
- **Per-session memory:** ≤256 tokens × ~5 bytes ≈ 1.3KB. With 3 concurrent sessions: <4KB.
- **JS-side cost:** one signal update per batch. Preact reconciliation amortized across 1-32 tokens, not per-token.

---

## 9. Acceptance Criteria for Wave 4 (B1 / B2)

Both Android (B1) and iOS (B2) implementations must expose the same minimal native surface:

```
startStream(sessionId: String)
pushToken(sessionId: String, token: String)
pushToolDelta(sessionId: String, index: Int, id: String?, name: String?, argumentsDelta: String)
pushImageDelta(sessionId: String, index: Int, mime: String?, b64Chunk: String)
endStream(sessionId: String, finalMessage: String)
errorStream(sessionId: String, message: String)
freeSession(sessionId: String)  // idempotent; flushes-then-disposes
```

**Required tests:**

1. **Concurrent streams.** 3 simultaneous sessions, each streaming 500 tokens in 5 seconds. All 1500 tokens delivered in correct per-session order. No crash. No OOM. JS receives ≤500/32 ≈ ~16-94 batches per session (depending on timing).
2. **GC-pause resilience.** Pause main thread 200ms during streaming. Tokens buffer correctly. Drop count is accurate (matches `pushToken` calls minus delivered tokens). UI catches up after pause.
3. **Buffer overflow.** Force 300 tokens before any flush completes (block UI thread). Confirm exactly 256 retained, oldest 44 dropped, next batch carries `droppedCount: 44`.
4. **WebView teardown.** Kill WebView mid-stream. No crash. Native session freed within 1 frame. No retained buffer (verify with leak detector / Instruments).
5. **Session free flush.** Push 10 tokens, immediately call `freeSession`. Confirm `__mahoStreamBatch` was called with all 10 before disposal.

---

## 10. What NOT to do

- Per-token `evaluateJavascript`. Kills the perf budget. The whole point of this doc.
- String-concatenating tokens across batches into one big payload. Each batch is an independent JS array; assembly is the JS layer's job.
- Holding a lock across `evaluateJavascript`. UI thread can deadlock against the streaming thread. Snapshot first, dispatch second, no lock held over the IPC boundary.
- Adopting RxJava / Combine / `ObservableEmitter` / any reactive lib. One-way push is sufficient. Backpressure is solved by the 256-cap + FIFO drop. Don't reach for machinery you don't need.
- Buffering across `freeSession`. Free flushes-then-disposes. There is no "pending tokens for a freed session" state.
- Coalescing `ToolCallDelta` or `ImageDelta`. Those are structurally distinct events, rare, and the UI cares about each one individually. Pass them through.
- Redefining `LLMStreamChunk` semantics. The bridge wraps the existing enum from `maho-types/src/llm.rs`; it does not extend or reinterpret it.
