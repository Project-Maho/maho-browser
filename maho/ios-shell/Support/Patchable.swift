// SPDX-License-Identifier: MIT
// ios-shell/Support/Patchable.swift — Three-state patch field for Maho iOS

import Foundation

/// Three-state patch field matching Rust's `Option<Option<T>>`:
/// `.absent` (omit from JSON), `.setNull` (encode null), `.set(value)` (encode value).
enum Patchable<T: Codable>: Equatable where T: Equatable {
    case absent
    case setNull
    case set(T)
}

// MARK: - Codable

extension Patchable: Codable {
    init(from decoder: Decoder) throws {
        let container = try decoder.singleValueContainer()
        if container.decodeNil() {
            self = .setNull
        } else {
            self = .set(try container.decode(T.self))
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.singleValueContainer()
        switch self {
        case .absent:
            try container.encodeNil()
        case .setNull:
            try container.encodeNil()
        case .set(let value):
            try container.encode(value)
        }
    }
}

// MARK: - KeyedEncodingContainer helpers

extension KeyedEncodingContainer {
    /// Encode a `Patchable` field: skip the key when `.absent`, encode `null` when
    /// `.setNull`, encode the value when `.set`.
    mutating func encodePatchable<T>(
        _ value: Patchable<T>,
        forKey key: Key
    ) throws where T: Codable, T: Equatable {
        switch value {
        case .absent:
            break
        case .setNull:
            try encodeNil(forKey: key)
        case .set(let inner):
            try encode(inner, forKey: key)
        }
    }
}

// MARK: - KeyedDecodingContainer helpers

extension KeyedDecodingContainer {
    /// Decode a `Patchable` field: missing key → `.absent`, `null` → `.setNull`,
    /// value → `.set(value)`.
    func decodePatchable<T>(
        _ type: Patchable<T>.Type,
        forKey key: Key
    ) throws -> Patchable<T> where T: Codable, T: Equatable {
        guard contains(key) else {
            return .absent
        }
        if try decodeNil(forKey: key) {
            return .setNull
        }
        return .set(try decode(T.self, forKey: key))
    }
}
