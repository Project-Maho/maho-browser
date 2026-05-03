import Foundation

struct MahoUrl: Codable, Equatable, Hashable {
    let value: String

    init(_ value: String) { self.value = value }

    init(from decoder: Decoder) throws {
        let container = try decoder.singleValueContainer()
        value = try container.decode(String.self)
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.singleValueContainer()
        try container.encode(value)
    }
}

struct MahoDateTime: Codable, Equatable, Hashable {
    let value: String

    init(_ value: String) { self.value = value }

    init(from decoder: Decoder) throws {
        let container = try decoder.singleValueContainer()
        value = try container.decode(String.self)
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.singleValueContainer()
        try container.encode(value)
    }
}

// MARK: - Geometric Types

struct Point: Codable, Equatable {
    let x: Double
    let y: Double
}

struct Size: Codable, Equatable {
    let width: Double
    let height: Double
}

struct Rect: Codable, Equatable {
    let origin: Point
    let size: Size
}

struct Viewport: Codable, Equatable {
    let offset: Point
    let zoom: Double
}

struct ScrollPosition: Codable, Equatable {
    let x: Double
    let y: Double

    init(x: Double = 0.0, y: Double = 0.0) {
        self.x = x
        self.y = y
    }
}

// MARK: - Media Types

enum ImageFormat: String, Codable, Equatable {
    case png
    case jpeg
    case webp
}

struct ImageData: Codable, Equatable {
    let data: [UInt8]
    let width: UInt32
    let height: UInt32
    let format: ImageFormat
}

// MARK: - URL Pattern

struct UrlPattern: Codable, Equatable {
    let pattern: String
    let patternType: UrlPatternType

    private enum CodingKeys: String, CodingKey {
        case pattern
        case patternType = "type"
    }
}

enum UrlPatternType: String, Codable, Equatable {
    case glob
    case regex
}

// MARK: - Cookie

struct Cookie: Codable, Equatable {
    let name: String
    let value: String
    let domain: String
    let path: String
    let expires: String?
    let httpOnly: Bool
    let secure: Bool
    let sameSite: SameSite

    private enum CodingKeys: String, CodingKey {
        case name, value, domain, path, expires
        case httpOnly, secure, sameSite
    }
}

enum SameSite: String, Codable, Equatable {
    case strict
    case lax
    case none
}

// MARK: - Data Types

struct DataTypes: Codable, Equatable {
    let cookies: Bool
    let cache: Bool
    let localStorage: Bool
    let sessionStorage: Bool
    let indexedDb: Bool
    let serviceWorkers: Bool

    private enum CodingKeys: String, CodingKey {
        case cookies, cache
        case localStorage, sessionStorage, indexedDb, serviceWorkers
    }
}

// MARK: - JS Interop

enum JsValue: Codable, Equatable {
    case null
    case bool(Bool)
    case number(Double)
    case string(String)
    case array([JsValue])
    case object([String: JsValue])

    init(from decoder: Decoder) throws {
        let container = try decoder.singleValueContainer()
        if container.decodeNil() {
            self = .null
            return
        }
        if let v = try? container.decode(Bool.self) {
            self = .bool(v)
            return
        }
        if let v = try? container.decode(Double.self) {
            self = .number(v)
            return
        }
        if let v = try? container.decode(String.self) {
            self = .string(v)
            return
        }
        if let v = try? container.decode([JsValue].self) {
            self = .array(v)
            return
        }
        if let v = try? container.decode([String: JsValue].self) {
            self = .object(v)
            return
        }
        throw DecodingError.dataCorruptedError(
            in: container,
            debugDescription: "JsValue could not decode any variant"
        )
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.singleValueContainer()
        switch self {
        case .null: try container.encodeNil()
        case .bool(let v): try container.encode(v)
        case .number(let v): try container.encode(v)
        case .string(let v): try container.encode(v)
        case .array(let v): try container.encode(v)
        case .object(let v): try container.encode(v)
        }
    }
}

// MARK: - Script Injection

enum ScriptWorld: String, Codable, Equatable {
    case main
    case isolated
}

// MARK: - Find in Page

struct FindOptions: Codable, Equatable {
    let caseSensitive: Bool
    let wholeWord: Bool
    let backwards: Bool

    private enum CodingKeys: String, CodingKey {
        case caseSensitive, wholeWord, backwards
    }
}

struct FindResult: Codable, Equatable {
    let matchCount: UInt32
    let activeMatchIndex: UInt32

    private enum CodingKeys: String, CodingKey {
        case matchCount, activeMatchIndex
    }
}


// MARK: - Color

struct Color: Codable, Equatable {
    let r: UInt8
    let g: UInt8
    let b: UInt8
    let a: Double
}

// MARK: - Orientation

enum Orientation: String, Codable, Equatable {
    case horizontal
    case vertical
}

// MARK: - Memory Pressure

enum MemoryPressureLevel: String, Codable, Equatable {
    case normal
    case warning
    case critical
    case extreme
}

// MARK: - Drag & Drop

struct DropTarget: Codable, Equatable {
    let spaceId: SpaceId
    let position: Int
    let parentTabId: TabId?

    private enum CodingKeys: String, CodingKey {
        case spaceId, position, parentTabId
    }
}

// MARK: - Tab Snapshot

struct TabSnapshot: Codable, Equatable {
    let url: String
    let title: String
    let scrollPosition: ScrollPosition
    let interactionState: [UInt8]
    let capturedAt: MahoDateTime

    private enum CodingKeys: String, CodingKey {
        case url, title, scrollPosition, interactionState, capturedAt
    }
}

// MARK: - Easel Drawing

struct DrawPath: Codable, Equatable {
    let points: [Point]
    let strokeColor: Color
    let strokeWidth: Double

    private enum CodingKeys: String, CodingKey {
        case points, strokeColor, strokeWidth
    }
}

// MARK: - Keyboard

enum KeyModifier: String, Codable, Equatable, Hashable {
    case ctrl
    case shift
    case alt
    case meta
}

struct KeyCombo: Codable, Equatable, Hashable {
    let key: String
    let modifiers: [KeyModifier]

    private enum CodingKeys: String, CodingKey {
        case key, modifiers
    }
}

struct KeyboardShortcut: Codable, Equatable {
    let action: String
    let key: String
    let modifiers: [KeyModifier]
}

// MARK: - Animation

struct SpringConfig: Codable, Equatable {
    let dampingRatio: Double
    let stiffness: Double
    let mass: Double
    let initialVelocity: Double

    private enum CodingKeys: String, CodingKey {
        case dampingRatio, stiffness, mass, initialVelocity
    }
}

enum SpringPresetName: String, Codable, Equatable {
    case snappy
    case smooth
    case bouncy
}

enum SplitSide: String, Equatable {
    case left
    case right
    case top
    case bottom
}
