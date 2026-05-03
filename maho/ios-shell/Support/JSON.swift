// SPDX-License-Identifier: MIT
// ios-shell/Support/JSON.swift — Shared JSON coding helpers for Maho iOS

import Foundation

// MARK: - Configured Coders

/// Pre-configured JSONDecoder matching Rust serde output:
/// - `convertFromSnakeCase` for struct field names (Rust uses `rename_all = "camelCase"` on some,
///   but the FFI JSON surface is snake_case for enum discriminators).
/// - `.iso8601` date strategy for DateTime strings.
enum MahoJSON {
    /// Decoder configured for Maho FFI JSON payloads.
    static let decoder: JSONDecoder = {
        let d = JSONDecoder()
        d.keyDecodingStrategy = .convertFromSnakeCase
        d.dateDecodingStrategy = .iso8601
        return d
    }()

    /// Encoder configured to produce Maho FFI JSON payloads.
    static let encoder: JSONEncoder = {
        let e = JSONEncoder()
        e.keyEncodingStrategy = .convertToSnakeCase
        e.dateEncodingStrategy = .iso8601
        return e
    }()
}

// MARK: - Data Convenience

extension Data {
    /// Decode this JSON data into `T` using the shared Maho decoder.
    func mahoDecoded<T: Decodable>(as type: T.Type = T.self) throws -> T {
        try MahoJSON.decoder.decode(type, from: self)
    }
}

extension Encodable {
    /// Encode this value to JSON `Data` using the shared Maho encoder.
    func mahoEncoded() throws -> Data {
        try MahoJSON.encoder.encode(self)
    }

    /// Encode this value to a UTF-8 JSON string using the shared Maho encoder.
    func mahoJSONString() throws -> String {
        let data = try mahoEncoded()
        guard let string = String(data: data, encoding: .utf8) else {
            throw MahoJSONError.utf8EncodingFailed
        }
        return string
    }
}

// MARK: - Errors

enum MahoJSONError: Error, CustomStringConvertible {
    case utf8EncodingFailed
    case decodingFailed(String)

    var description: String {
        switch self {
        case .utf8EncodingFailed:
            return "MahoJSON: Failed to encode Data as UTF-8 string"
        case .decodingFailed(let detail):
            return "MahoJSON: Decoding failed — \(detail)"
        }
    }
}
