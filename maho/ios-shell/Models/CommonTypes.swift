import Foundation

struct Url: Codable, Hashable {
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

enum ImageFormat: String, Codable {
    case png
    case jpeg
    case webp
}

struct ImageData: Codable {
    let data: [UInt8]
    let width: UInt32
    let height: UInt32
    let format: ImageFormat
}

struct MahoColor: Codable, Equatable {
    let r: UInt8
    let g: UInt8
    let b: UInt8
    let a: Double
}

struct Point: Codable {
    let x: Double
    let y: Double
}

struct Size: Codable {
    let width: Double
    let height: Double
}

struct Rect: Codable {
    let origin: Point
    let size: Size
}

enum Orientation: String, Codable {
    case horizontal
    case vertical
}

enum MemoryPressureLevel: String, Codable {
    case normal
    case warning
    case critical
    case extreme
}

struct UrlPattern: Codable {
    let pattern: String
    let type: UrlPatternType

    enum CodingKeys: String, CodingKey {
        case pattern
        case type
    }
}

enum UrlPatternType: String, Codable {
    case glob
    case regex
}

struct ScrollPosition: Codable {
    let x: Double
    let y: Double
}

struct TabSnapshot: Codable {
    let url: String
    let title: String
    let scrollPosition: ScrollPosition
    let interactionState: [UInt8]
    let capturedAt: String
}
