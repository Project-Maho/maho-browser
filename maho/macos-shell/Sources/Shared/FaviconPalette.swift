import Foundation
import CoreGraphics
import AppKit

struct FavoriteTilePalette: Equatable {
    let idleBackground: NSColor
    let idleBorder: NSColor
    let hoverBackground: NSColor
    let hoverBorder: NSColor
    let selectedBackground: NSColor
    let selectedBorder: NSColor
    let selectedGlow: NSColor
    let iconTint: NSColor
    let fallbackTint: NSColor

    static func == (lhs: FavoriteTilePalette, rhs: FavoriteTilePalette) -> Bool {
        lhs.idleBackground.isEqual(rhs.idleBackground) &&
        lhs.idleBorder.isEqual(rhs.idleBorder) &&
        lhs.hoverBackground.isEqual(rhs.hoverBackground) &&
        lhs.hoverBorder.isEqual(rhs.hoverBorder) &&
        lhs.selectedBackground.isEqual(rhs.selectedBackground) &&
        lhs.selectedBorder.isEqual(rhs.selectedBorder) &&
        lhs.selectedGlow.isEqual(rhs.selectedGlow) &&
        lhs.iconTint.isEqual(rhs.iconTint) &&
        lhs.fallbackTint.isEqual(rhs.fallbackTint)
    }

    static func fallback(isDarkAppearance: Bool) -> FavoriteTilePalette {
        let idleBackground = isDarkAppearance
            ? NSColor(calibratedRed: 0.187, green: 0.175, blue: 0.240, alpha: 0.96)
            : NSColor(calibratedRed: 0.959, green: 0.964, blue: 0.981, alpha: 0.98)
        let idleBorder = isDarkAppearance
            ? NSColor(calibratedRed: 0.274, green: 0.255, blue: 0.355, alpha: 0.92)
            : NSColor(calibratedRed: 0.738, green: 0.770, blue: 0.866, alpha: 0.70)
        let hoverBackground = isDarkAppearance
            ? NSColor(calibratedRed: 0.221, green: 0.204, blue: 0.282, alpha: 0.98)
            : NSColor(calibratedRed: 0.974, green: 0.978, blue: 0.989, alpha: 0.99)
        let hoverBorder = isDarkAppearance
            ? NSColor(calibratedRed: 0.353, green: 0.330, blue: 0.435, alpha: 0.96)
            : NSColor(calibratedRed: 0.662, green: 0.704, blue: 0.812, alpha: 0.84)
        let selectedBackground = isDarkAppearance
            ? NSColor(calibratedRed: 0.301, green: 0.280, blue: 0.375, alpha: 0.98)
            : NSColor(calibratedRed: 0.923, green: 0.938, blue: 0.979, alpha: 0.99)
        let selectedBorder = isDarkAppearance
            ? NSColor(calibratedRed: 0.558, green: 0.610, blue: 0.955, alpha: 0.82)
            : NSColor(calibratedRed: 0.520, green: 0.612, blue: 0.966, alpha: 0.86)
        let selectedGlow = isDarkAppearance
            ? NSColor(calibratedRed: 0.520, green: 0.612, blue: 0.966, alpha: 0.26)
            : NSColor(calibratedRed: 0.430, green: 0.522, blue: 0.944, alpha: 0.20)
        let iconTint = isDarkAppearance
            ? NSColor(calibratedRed: 0.872, green: 0.885, blue: 0.940, alpha: 1.0)
            : NSColor(calibratedRed: 0.308, green: 0.365, blue: 0.588, alpha: 1.0)
        let fallbackTint = isDarkAppearance
            ? NSColor(calibratedRed: 0.824, green: 0.840, blue: 0.894, alpha: 1.0)
            : NSColor(calibratedRed: 0.382, green: 0.448, blue: 0.692, alpha: 1.0)

        return FavoriteTilePalette(
            idleBackground: idleBackground.stableSRGBColor(),
            idleBorder: idleBorder.stableSRGBColor(),
            hoverBackground: hoverBackground.stableSRGBColor(),
            hoverBorder: hoverBorder.stableSRGBColor(),
            selectedBackground: selectedBackground.stableSRGBColor(),
            selectedBorder: selectedBorder.stableSRGBColor(),
            selectedGlow: selectedGlow.stableSRGBColor(),
            iconTint: iconTint.stableSRGBColor(),
            fallbackTint: fallbackTint.stableSRGBColor()
        )
    }
}

struct FavoriteTilePaletteCacheKey: Hashable {
    let tabId: String
    let faviconSignature: Int
    let isDarkAppearance: Bool
}

enum FavoriteTilePaletteResolver {
    static func cacheKey(for tabId: String, favicon: ImageData?, appearance: NSAppearance) -> FavoriteTilePaletteCacheKey {
        FavoriteTilePaletteCacheKey(
            tabId: tabId,
            faviconSignature: favicon?.favoriteTileSignature ?? 0,
            isDarkAppearance: isDarkAppearance(appearance)
        )
    }

    static func resolve(for favicon: ImageData?, appearance: NSAppearance) -> FavoriteTilePalette {
        let isDark = isDarkAppearance(appearance)
        guard let favicon,
              let image = NSImage(data: Data(favicon.data)),
              let accent = chromaticAccent(from: image) else {
            return FavoriteTilePalette.fallback(isDarkAppearance: isDark)
        }
        return palette(for: accent, isDarkAppearance: isDark)
    }

    private static func palette(for accent: NSColor, isDarkAppearance: Bool) -> FavoriteTilePalette {
        let stableAccent = accent.stableSRGBColor()
        let surfaceBase = isDarkAppearance
            ? NSColor(calibratedRed: 0.183, green: 0.173, blue: 0.232, alpha: 1.0)
            : NSColor(calibratedRed: 0.955, green: 0.962, blue: 0.982, alpha: 1.0)
        let borderBase = isDarkAppearance
            ? NSColor(calibratedRed: 0.290, green: 0.270, blue: 0.360, alpha: 1.0)
            : NSColor(calibratedRed: 0.735, green: 0.762, blue: 0.862, alpha: 1.0)
        let hoverSurfaceBase = isDarkAppearance
            ? NSColor(calibratedRed: 0.214, green: 0.201, blue: 0.271, alpha: 1.0)
            : NSColor(calibratedRed: 0.971, green: 0.976, blue: 0.990, alpha: 1.0)
        let hoverBorderBase = isDarkAppearance
            ? NSColor(calibratedRed: 0.355, green: 0.333, blue: 0.429, alpha: 1.0)
            : NSColor(calibratedRed: 0.650, green: 0.702, blue: 0.816, alpha: 1.0)
        let selectedSurfaceBase = isDarkAppearance
            ? NSColor(calibratedRed: 0.270, green: 0.251, blue: 0.336, alpha: 1.0)
            : NSColor(calibratedRed: 0.926, green: 0.939, blue: 0.979, alpha: 1.0)
        let selectedBorderBase = isDarkAppearance
            ? NSColor(calibratedRed: 0.560, green: 0.618, blue: 0.952, alpha: 1.0)
            : NSColor(calibratedRed: 0.502, green: 0.600, blue: 0.964, alpha: 1.0)
        let glowBase = stableAccent.withAlphaComponent(isDarkAppearance ? 0.30 : 0.20)
        let iconTintBase = isDarkAppearance
            ? NSColor(calibratedRed: 0.902, green: 0.915, blue: 0.955, alpha: 1.0)
            : NSColor(calibratedRed: 0.260, green: 0.335, blue: 0.558, alpha: 1.0)
        let fallbackTintBase = isDarkAppearance
            ? NSColor(calibratedRed: 0.808, green: 0.824, blue: 0.888, alpha: 1.0)
            : NSColor(calibratedRed: 0.360, green: 0.438, blue: 0.670, alpha: 1.0)

        return FavoriteTilePalette(
            idleBackground: surfaceBase.blendedStable(withFraction: isDarkAppearance ? 0.085 : 0.050, of: stableAccent),
            idleBorder: borderBase.blendedStable(withFraction: isDarkAppearance ? 0.150 : 0.100, of: stableAccent),
            hoverBackground: hoverSurfaceBase.blendedStable(withFraction: isDarkAppearance ? 0.175 : 0.110, of: stableAccent),
            hoverBorder: hoverBorderBase.blendedStable(withFraction: isDarkAppearance ? 0.275 : 0.200, of: stableAccent),
            selectedBackground: selectedSurfaceBase.blendedStable(withFraction: isDarkAppearance ? 0.245 : 0.170, of: stableAccent),
            selectedBorder: selectedBorderBase.blendedStable(withFraction: isDarkAppearance ? 0.390 : 0.280, of: stableAccent),
            selectedGlow: glowBase.stableSRGBColor(),
            iconTint: iconTintBase.blendedStable(withFraction: isDarkAppearance ? 0.260 : 0.220, of: stableAccent),
            fallbackTint: fallbackTintBase.blendedStable(withFraction: isDarkAppearance ? 0.340 : 0.260, of: stableAccent)
        )
    }

    private static func chromaticAccent(from image: NSImage) -> NSColor? {
        guard let cgImage = image.cgImage(forProposedRect: nil, context: nil, hints: nil) else {
            return nil
        }

        let sampleDimension = 24
        let bytesPerRow = sampleDimension * 4
        var pixels = [UInt8](repeating: 0, count: bytesPerRow * sampleDimension)
        pixels.withUnsafeMutableBytes { pixelBytes in
            guard let context = CGContext(
                data: pixelBytes.baseAddress,
                width: sampleDimension,
                height: sampleDimension,
                bitsPerComponent: 8,
                bytesPerRow: bytesPerRow,
                space: CGColorSpaceCreateDeviceRGB(),
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue
            ) else {
                return
            }

            context.interpolationQuality = .high
            context.draw(cgImage, in: CGRect(x: 0, y: 0, width: sampleDimension, height: sampleDimension))
        }

        var chromaticAccumulator = ColorAccumulator()
        var chromaticWeight: CGFloat = 0

        for index in stride(from: 0, to: pixels.count, by: 4) {
            let alpha = CGFloat(pixels[index + 3]) / 255.0
            guard alpha > 0.12 else { continue }

            let premultipliedRed = CGFloat(pixels[index]) / 255.0
            let premultipliedGreen = CGFloat(pixels[index + 1]) / 255.0
            let premultipliedBlue = CGFloat(pixels[index + 2]) / 255.0
            let unpremultiply = alpha > 0 ? 1.0 / alpha : 1.0
            let red = clamp(premultipliedRed * unpremultiply, lower: 0, upper: 1)
            let green = clamp(premultipliedGreen * unpremultiply, lower: 0, upper: 1)
            let blue = clamp(premultipliedBlue * unpremultiply, lower: 0, upper: 1)

            let maxComponent = max(red, green, blue)
            let minComponent = min(red, green, blue)
            let chroma = maxComponent - minComponent
            let saturation = maxComponent > 0 ? chroma / maxComponent : 0
            let brightness = maxComponent

            let isNearWhite = brightness > 0.95 && chroma < 0.18
            let isNearBlack = brightness < 0.12
            let isEffectivelyGrayscale = saturation < 0.18 || chroma < 0.10
            guard !isNearWhite, !isNearBlack, !isEffectivelyGrayscale else {
                continue
            }

            let alphaWeight = alpha
            let chromaWeight = max(0.18, chroma)
            let saturationWeight = max(0.20, saturation)
            let brightnessWeight = 0.50 + (1.0 - abs(brightness - 0.55))
            let weight = alphaWeight * chromaWeight * saturationWeight * brightnessWeight
            chromaticAccumulator.add(red: red, green: green, blue: blue, weight: weight)
            chromaticWeight += weight
        }

        guard chromaticWeight > 0 else {
            return nil
        }

        return chromaticAccumulator.color()
    }

    private static func isDarkAppearance(_ appearance: NSAppearance) -> Bool {
        appearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
    }

    private static func clamp(_ value: CGFloat, lower: CGFloat, upper: CGFloat) -> CGFloat {
        min(max(value, lower), upper)
    }

    private struct ColorAccumulator {
        private var red: CGFloat = 0
        private var green: CGFloat = 0
        private var blue: CGFloat = 0
        private var weight: CGFloat = 0

        mutating func add(red: CGFloat, green: CGFloat, blue: CGFloat, weight: CGFloat) {
            self.red += red * weight
            self.green += green * weight
            self.blue += blue * weight
            self.weight += weight
        }

        func color() -> NSColor {
            guard weight > 0 else {
                return NSColor(calibratedRed: 0.408, green: 0.490, blue: 0.738, alpha: 1.0).stableSRGBColor()
            }
            return NSColor(
                calibratedRed: red / weight,
                green: green / weight,
                blue: blue / weight,
                alpha: 1.0
            ).stableSRGBColor()
        }
    }
}
