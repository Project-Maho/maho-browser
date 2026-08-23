// FFI ABI Layout Audit Test
//
// This file asserts the exact byte size and alignment of all structures
// and enums that cross the FFI boundary between Rust (maho-core) and C++ (Chromium) / Swift (macOS Shell).
//
// A comprehensive audit of maho_ffi/src/lib.rs was conducted. The only types passed
// by value or by non-opaque reference across the FFI boundary are:
//   - MahoChatConfig (struct)
//   - MahoAgentPermissionDecision (enum)
//   - MahoAgentSecureKey (struct)
//   - MahoAgentToolResult (struct)
//
// Opaque types (e.g., MahoCore, MahoAgentSession, MahoImportSession) are always
// passed as opaque pointers (*mut / *const) and their layouts are not exposed
// to C++/Swift clients, and thus do not require static layout assertions.

use maho_ffi::{
    MahoAgentPermissionDecision, MahoAgentSecureKey, MahoAgentToolResult, MahoChatConfig,
};
use std::mem;

const _: () = {
    // Assert size and alignment of MahoChatConfig
    assert!(mem::size_of::<MahoChatConfig>() == 32);
    assert!(mem::align_of::<MahoChatConfig>() == 8);

    // Assert size and alignment of MahoAgentPermissionDecision
    assert!(mem::size_of::<MahoAgentPermissionDecision>() == 4);
    assert!(mem::align_of::<MahoAgentPermissionDecision>() == 4);

    // Assert size and alignment of MahoAgentSecureKey
    assert!(mem::size_of::<MahoAgentSecureKey>() == 48);
    assert!(mem::align_of::<MahoAgentSecureKey>() == 8);

    // Assert size and alignment of MahoAgentToolResult
    assert!(mem::size_of::<MahoAgentToolResult>() == 40);
    assert!(mem::align_of::<MahoAgentToolResult>() == 8);
};

#[test]
fn test_abi_layouts() {
    // Satisfy cargo test framework
}
