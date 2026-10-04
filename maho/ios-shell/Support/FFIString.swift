// SPDX-License-Identifier: MIT
// ios-shell/Support/FFIString.swift — C string ↔ Swift String helpers for Maho FFI

import Foundation

// MARK: - FFI String Helpers

enum FFIString {
    /// Converts a `*mut c_char` returned by Rust into a Swift `String`,
    /// then frees the Rust-allocated memory via `maho_string_free`.
    ///
    /// Returns `nil` if the pointer is `NULL`.
    static func consume(_ ptr: UnsafeMutablePointer<CChar>?) -> String? {
        guard let ptr = ptr else { return nil }
        let string = String(cString: ptr)
        maho_string_free(ptr)
        return string
    }

    /// Converts a `*mut c_char` JSON payload from Rust into decoded `T`,
    /// freeing the Rust string after copying.
    ///
    /// Returns `nil` if the pointer is `NULL` or decoding fails.
    static func consumeJSON<T: Decodable>(_ ptr: UnsafeMutablePointer<CChar>?, as type: T.Type = T.self) -> T? {
        guard let json = consume(ptr) else { return nil }
        guard let data = json.data(using: .utf8) else { return nil }
        return try? data.mahoDecoded(as: type)
    }

    /// Calls `body` with a temporary C string pointer. The pointer is only valid
    /// for the duration of the closure.
    ///
    /// Use this to pass Swift strings to Rust FFI functions that take `*const c_char`.
    static func withCString<R>(_ string: String, _ body: (UnsafePointer<CChar>) -> R) -> R {
        string.withCString(body)
    }
}
