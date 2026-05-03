import AppKit

extension NSColor {
    static func fromProfileAvatarString(_ color: String) -> NSColor {
        let trimmed = color.trimmingCharacters(in: .whitespacesAndNewlines)
        let normalized = trimmed.lowercased()

        switch normalized {
        case "red": return .systemRed
        case "blue": return .systemBlue
        case "green": return .systemGreen
        case "orange": return .systemOrange
        case "purple": return .systemPurple
        case "pink": return .systemPink
        case "yellow": return .systemYellow
        default:
            if let hexColor = fromHexString(trimmed) {
                return hexColor
            }
            return MahoAppColors.accentBlue
        }
    }

    static func fromHexString(_ value: String) -> NSColor? {
        var hex = value.trimmingCharacters(in: .whitespacesAndNewlines)
        if hex.hasPrefix("#") {
            hex.removeFirst()
        }

        guard hex.count == 6 || hex.count == 8,
              let rawValue = UInt64(hex, radix: 16) else {
            return nil
        }

        let red: CGFloat
        let green: CGFloat
        let blue: CGFloat
        let alpha: CGFloat

        if hex.count == 8 {
            red = CGFloat((rawValue & 0xFF00_0000) >> 24) / 255
            green = CGFloat((rawValue & 0x00FF_0000) >> 16) / 255
            blue = CGFloat((rawValue & 0x0000_FF00) >> 8) / 255
            alpha = CGFloat(rawValue & 0x0000_00FF) / 255
        } else {
            red = CGFloat((rawValue & 0xFF0000) >> 16) / 255
            green = CGFloat((rawValue & 0x00FF00) >> 8) / 255
            blue = CGFloat(rawValue & 0x0000FF) / 255
            alpha = 1
        }

        return NSColor(calibratedRed: red, green: green, blue: blue, alpha: alpha)
    }

    func stableSRGBColor() -> NSColor {
        usingColorSpace(.sRGB) ?? usingColorSpace(.deviceRGB) ?? self
    }

    func blendedStable(withFraction fraction: CGFloat, of color: NSColor) -> NSColor {
        let base = stableSRGBColor()
        let target = color.stableSRGBColor()
        return base.blended(withFraction: fraction, of: target)?.stableSRGBColor() ?? base
    }
}

extension ImageData {
    var favoriteTileSignature: Int {
        var hasher = Hasher()
        hasher.combine(width)
        hasher.combine(height)
        hasher.combine(format.rawValue)
        hasher.combine(data.count)
        for byte in data.prefix(64) {
            hasher.combine(byte)
        }
        return hasher.finalize()
    }
}
