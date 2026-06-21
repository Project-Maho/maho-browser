# A0. Session Registry Design (Mobile WebView Bridge)

**Status:** Design only. No code yet. Awaiting user sign-off before A1 implementation.
**Scope:** Maho mobile web AI migration, Phase A foundation. Applies to Android (`WebViewBridge.kt`) and iOS (`WebViewBridgeController.swift`). Backed by existing `maho-jni` and `maho-ffi` crates.

---

## 1. Why this exists

The native chat session is referenced by a raw pointer:

- iOS: `OpaquePointer` (a 64-bit memory address).
- Android: `jlong` returned from JNI (a 64-bit native pointer cast to `Long`).

JavaScript `number` is IEEE-754 double. The safe-integer ceiling is **2^53 - 1 = 9,007,199,254,740,991**. Modern macOS/iOS/Android process address space routinely places allocations above this. Pass a 64-bit pointer through the WebView bridge as `number` and it silently truncates. The session "works" most of the time on simulator (low addresses), then corrupts in production. The bug is invisible until it isn't.

**Threat model, stated explicitly:** if any developer accidentally returns `Long` via `@JavascriptInterface`, or hands an `OpaquePointer`'s `bitPattern` to `WKScriptMessage`, sessions silently corrupt above 2^53. This design makes that impossible by typed contract: the only thing the JS layer ever sees is an opaque `string`.

WebView JS **must never** see raw pointer values. The native side maintains a registry that maps short opaque string IDs to native pointers. JS holds the string. Native holds the pointer.

---

## 2. Public contract

Three operations, identical semantics on both platforms:

| Operation | Signature | Returns |
|-----------|-----------|---------|
| `register` | `register(ptr: NativePtr) -> String` | UUID-v4 string |
| `get`      | `get(id: String) -> Option<NativePtr>` | pointer or absent |
| `release`  | `release(id: String) -> Unit` | calls native free, removes entry |

**TS bridge contract** (shared, mobile-side):

```ts
export type SessionHandle = string;

export interface ChatBridge {
  chatSessionStart(opts: ChatStartOpts): Promise<SessionHandle>;
  chatSessionSend(id: SessionHandle, msg: string): Promise<void>;
  chatSessionFree(id: SessionHandle): Promise<void>;
}
```

`SessionHandle` is `string`. Not `number`. Not `bigint`. Not `string | number`. The TS type system enforces this at every call site; reviewer rejects any PR that widens it.

---

## 3. Android implementation pattern

**File:** `maho/android-shell/app/src/main/java/dev/maho/browser/bridge/SessionRegistry.kt`

**Storage:** `ConcurrentHashMap<String, Long>`. The `Long` is the raw native pointer returned by JNI (`nativeChatSessionStart`).

**ID generation:** `UUID.randomUUID().toString()` (RFC 4122 v4, ~122 bits of entropy, collision-free in practice).

**Lock strategy:** none beyond `ConcurrentHashMap`. `put`, `get`, `remove` are individually atomic. The compound `release` operation (free + remove) is sequenced: `remove` first to claim ownership of the pointer, then call native free outside the map. This avoids holding any internal bucket lock across a JNI call.

**Lifecycle:** `release(id)` → `remove(id)` returns the `Long?`. If non-null, call `nativeChatSessionFree(ptr)`. If null (already released or unknown), no-op.

**Thread safety:** registry methods callable from any thread (WebView main thread, IO dispatcher, JNI callback thread). Native free goes through the existing Rust core free function, which is documented thread-safe.

```kotlin
package dev.maho.browser.bridge

import java.util.UUID
import java.util.concurrent.ConcurrentHashMap

class SessionRegistry(private val nativeFree: (Long) -> Unit) {
    private val map = ConcurrentHashMap<String, Long>()

    fun register(ptr: Long): String {
        require(ptr != 0L) { "null pointer" }
        val id = UUID.randomUUID().toString()
        map[id] = ptr
        return id
    }

    fun get(id: String): Long? = map[id]

    fun release(id: String) {
        // remove first to claim ownership before crossing JNI
        val ptr = map.remove(id) ?: return
        nativeFree(ptr)
    }
}
```

---

## 4. iOS implementation pattern

**File:** `maho/ios-shell/Bridge/ChatSessionRegistry.swift`

**Storage:** `[String: OpaquePointer]` guarded by `NSLock`. Swift `Dictionary` is **not** thread-safe; concurrent mutation crashes or corrupts.

**ID generation:** `UUID().uuidString`.

**Lock strategy:** single `NSLock` around every read and write. Lock is released before crossing the C FFI boundary (same reasoning as Android: don't hold internal locks across foreign calls).

**Lifecycle:** `release(id)` acquires lock, removes entry, releases lock, then calls `maho_core_chat_session_free(ptr)`.

**Thread safety:** every public method acquires the lock. Lock is non-recursive; methods do not call each other.

```swift
import Foundation

final class ChatSessionRegistry {
    private var map: [String: OpaquePointer] = [:]
    private let lock = NSLock()
    private let nativeFree: (OpaquePointer) -> Void

    init(nativeFree: @escaping (OpaquePointer) -> Void) {
        self.nativeFree = nativeFree
    }

    func register(_ ptr: OpaquePointer) -> String {
        let id = UUID().uuidString
        lock.lock()
        map[id] = ptr
        lock.unlock()
        return id
    }

    func get(_ id: String) -> OpaquePointer? {
        lock.lock()
        defer { lock.unlock() }
        return map[id]
    }

    func release(_ id: String) {
        lock.lock()
        let ptr = map.removeValue(forKey: id)
        lock.unlock()
        // MUST NOT hold lock across FFI; nativeFree may run arbitrary code
        if let ptr = ptr { nativeFree(ptr) }
    }
}
```

---

## 5. Bridge protocol changes

Every chat method that previously took or returned `Long` / `OpaquePointer` now takes or returns the `String` ID.

**Dispatch translation (native side):**

- `chatSessionStart(opts)` → call core, get raw ptr → `registry.register(ptr)` → return `String` to JS.
- `chatSessionSend(id, msg)` → `registry.get(id)` → if `Some(ptr)`, call core; if `None`, reply with typed error `{ kind: "session_invalid", id }`.
- `chatSessionFree(id)` → `registry.release(id)`. Idempotent: releasing an unknown ID is a no-op, not an error (avoids races between user-initiated close and GC).

**Lifecycle:** an ID is valid from the moment `register` returns until `release` is called (or the WebView is destroyed and the registry is dropped, which frees all remaining pointers in `deinit` / equivalent cleanup hook). After release, `get` returns `None` and any subsequent bridge call returns `session_invalid` to JS.

**Sequence (text diagram):**

```
JS                     Native bridge          Registry          Rust core
 |                          |                    |                  |
 |--chatSessionStart()----->|                    |                  |
 |                          |--core_start()-------------------------|
 |                          |<--ptr (Long/Opaque)-------------------|
 |                          |--register(ptr)---->|                  |
 |                          |<--id (String)------|                  |
 |<--id (String)------------|                    |                  |
 |                          |                    |                  |
 |--chatSessionSend(id,msg)>|                    |                  |
 |                          |--get(id)---------->|                  |
 |                          |<--Some(ptr)--------|                  |
 |                          |--core_send(ptr,msg)-------------------|
 |                          |<--ack---------------------------------|
 |<--ack--------------------|                    |                  |
 |                          |                    |                  |
 |--chatSessionFree(id)---->|                    |                  |
 |                          |--release(id)------>|                  |
 |                          |                    |--core_free(ptr)->|
 |                          |<--ok---------------|                  |
 |<--ok---------------------|                    |                  |
```

---

## 6. Acceptance criteria

A reviewer signs off when this document specifies, and only when it specifies:

1. Handle type is **string UUID**, not numeric.
2. Registry data structure: `ConcurrentHashMap<String, Long>` (Android), `[String: OpaquePointer] + NSLock` (iOS).
3. Lock strategy stated for both platforms, with the explicit rule "do not hold internal locks across native calls".
4. Lifecycle hooks: `register` on session start, `release` on explicit free, `release-all` on bridge teardown.
5. The TS bridge contract bans `number` for session handles. `SessionHandle = string`.
6. A "what NOT to do" section (§7) is present and unambiguous.
7. Both platforms covered in the same document, side-by-side, so divergence is visible.

This is a design doc. No implementation lands until the user reviews and approves.

---

## 7. What NOT to do

- **❌ Do not** pass raw `Long` through `@JavascriptInterface` return values. JS will receive `number` and silently truncate above 2^53.
- **❌ Do not** call `evaluateJavascript("foo(" + ptr + ")")` with a pointer argument. Same truncation. If you must inline, stringify the registered ID instead.
- **❌ Do not** type the TS contract as `number` or `string | number`. One type. String. Period.
- **❌ Do not** skip the registry and let the WebView see a pointer "just for debugging". Debugging never gets removed; it ships.
- **❌ Do not** use `HashMap` on Android or a plain `Dictionary` on iOS without a lock. Concurrent mutation is undefined behavior, and the bridge is multi-threaded by definition (UI thread, JNI thread, WebView callback thread).
- **❌ Do not** hold the registry lock across a JNI / C FFI call. Free runs Rust drop logic which may itself acquire locks. Recipe for deadlock.
- **❌ Do not** treat `release` of an unknown ID as an error. It's a no-op. Idempotency removes a class of double-free races between user code and GC-driven cleanup.
- **❌ Do not** add new dependencies. `ConcurrentHashMap`, `UUID`, `NSLock`, `Dictionary` are stdlib on both platforms.

---

## 8. Open questions

1. **Bridge teardown sweep:** when the WebView is destroyed mid-session, who walks the registry and calls `release` on every remaining ID? Proposed: registry exposes `releaseAll()`; bridge controller calls it in `onDestroy` (Android) and `deinit` (iOS). Confirm or specify alternative.
2. **Error shape:** is `{ kind: "session_invalid", id }` the right typed error envelope, or does the existing bridge already define a canonical error type to reuse? Defer to existing convention if present.
3. **Telemetry:** should `get` of an unknown ID emit a metric? Useful for catching bugs where JS retains stale handles. Default: yes, low-volume counter, no PII (the ID itself is opaque random).
