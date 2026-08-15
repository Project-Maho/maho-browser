// SPDX-License-Identifier: MIT
// ios-shell/Support/RelayCoding.swift — JSON coding for the relay HTTP + keychain boundary

import Foundation

// MARK: - Relay Coder

/// Pre-configured coders for the relay server boundary (HTTP request/response
/// and keychain persistence).
///
/// This is intentionally separate from the FFI-oriented `MahoJSON` coder. The
/// relay server (`maho/relay/src/auth/models.rs`) is canonical and emits:
/// - i64 Unix-second timestamps for `expires_at` / `refresh_expires_at`
/// - i32 integers for `account.id` / `device.id` / `session.id`
///
/// So this coder uses `.secondsSince1970` (not `.iso8601`) and pairs with
/// `RelayID` to accept integer-or-string ids. Do NOT reuse `MahoJSON` here and
/// do NOT change `MahoJSON`; the FFI paths must keep `.iso8601`.
enum RelayCoding {
    /// Decoder for relay HTTP responses and keychain reads.
    static let decoder: JSONDecoder = {
        let d = JSONDecoder()
        d.keyDecodingStrategy = .convertFromSnakeCase
        d.dateDecodingStrategy = .secondsSince1970
        return d
    }()

    /// Encoder for relay HTTP requests and keychain writes.
    static let encoder: JSONEncoder = {
        let e = JSONEncoder()
        e.keyEncodingStrategy = .convertToSnakeCase
        e.dateEncodingStrategy = .secondsSince1970
        return e
    }()
}

// MARK: - Relay Identifier

/// A relay entity identifier normalized to `String`.
///
/// The relay server encodes ids as JSON integers (the canonical i32 contract),
/// but iOS exposes them downstream as `String?` (fed to
/// `signIn(userId:deviceId:)` and encoded as string `user_id`/`device_id` in
/// `ShellEvent`, matching Android). This wrapper accepts either a JSON integer
/// or a JSON string on decode and always encodes back as a `String` so keychain
/// persistence round-trips. Mirrors Desktop C++ (`FindInt` → `NumberToString`)
/// and Android (`contentOrNull`) id handling.
struct RelayID: Codable, Equatable {
    let value: String

    init(_ value: String) {
        self.value = value
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.singleValueContainer()
        if let intValue = try? container.decode(Int.self) {
            value = String(intValue)
        } else {
            value = try container.decode(String.self)
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.singleValueContainer()
        try container.encode(value)
    }
}
