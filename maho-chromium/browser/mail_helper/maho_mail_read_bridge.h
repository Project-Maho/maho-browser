// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_READ_BRIDGE_H_
#define MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_READ_BRIDGE_H_

#include <string>

#include "base/functional/callback.h"
#include "maho/third_party/maho/maho_mail_ffi.h"

namespace maho {

// Adapts one asynchronous maho-mail-ffi read call into a single sequenced C++
// reply. The mail backend invokes `MahoMailReadCallback` exactly once with a
// Rust-owned C string that is valid ONLY for the callback's duration, and it
// may fire that callback on an arbitrary Rust runtime thread -- potentially
// before the FFI export itself returns `true`. This bridge makes that safe.
//
// Thread / lifetime invariants:
//   * Start() must run on a sequence. It captures the current
//     base::SequencedTaskRunner and the `reply` is always posted back to that
//     sequence; the reply never runs inline.
//   * The Rust C string is copied into a std::string synchronously inside the
//     extern-C trampoline, before the trampoline returns, honouring the
//     callback-duration ownership contract.
//   * The shared reply state is reference counted and thread-safe. Exactly one
//     reference is handed to the FFI as `user_data`; whichever path takes
//     ownership of that reference -- the trampoline (callback fired) or Start()
//     (immediate rejection) -- releases it. The bridge never captures a raw
//     `this`, so a callback that races ahead of `invoke.Run()` returning stays
//     memory-safe.
//   * The reply resolves exactly once. If the FFI export returns false its
//     callback contract guarantees it will not fire, so Start() resolves with
//     (false, deterministic error). A reply may itself be WeakPtr-bound and
//     become a no-op after teardown; the reference is still released cleanly.
class MahoMailReadBridge {
 public:
  // Invokes the concrete maho-mail-ffi export. Callers bind the leading FFI
  // arguments (if any) and leave the trailing (callback, user_data) pair to be
  // supplied here, e.g. base::BindOnce(&MahoMailListFolders, account_id).
  // Returns the FFI acceptance bool: true means the callback will fire later,
  // false means it will not.
  using FfiInvoker = base::OnceCallback<bool(MahoMailReadCallback, void*)>;

  // Sequenced reply: (ok, json_or_error). Posted to the sequence that called
  // Start().
  using ReplyCallback = base::OnceCallback<void(bool, std::string)>;

  MahoMailReadBridge() = delete;
  MahoMailReadBridge(const MahoMailReadBridge&) = delete;
  MahoMailReadBridge& operator=(const MahoMailReadBridge&) = delete;

  // Runs `invoker` now and arranges for `reply` to be invoked exactly once on
  // the calling sequence.
  static void Start(FfiInvoker invoker, ReplyCallback reply);
};

}  // namespace maho

#endif  // MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_READ_BRIDGE_H_
