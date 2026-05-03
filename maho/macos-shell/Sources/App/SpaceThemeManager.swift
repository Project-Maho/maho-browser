import AppKit

struct SpaceItemAppearance {
    let badgeFillColor: NSColor
    let badgeBorderColor: NSColor
    let badgeInnerGlowColor: NSColor
    let badgeScale: CGFloat
    let alpha: CGFloat
}

final class SpaceThemeManager {
    static let shared = SpaceThemeManager()

    /// The theme of the currently active space.
    private(set) var activeSpaceColor: SpaceColor?
    private(set) var activeSpaceTheme: SpaceTheme?

    private init() {}

    /// Update the active space's theme. Posts notification so chrome elements can react.
    func setActiveSpace(color: SpaceColor?, theme: SpaceTheme?) {
        activeSpaceColor = color
        activeSpaceTheme = theme
        NotificationCenter.default.post(name: .mahoActiveSpaceThemeChanged, object: nil)
    }

    /// Returns a very subtle tint for chrome elements based on the active space theme.
    /// Alpha should be very low (0.04-0.08) to avoid overpowering the dark chrome.
    func chromeTint(alpha: CGFloat = 0.06) -> NSColor? {
        guard let color = activeSpaceTheme?.baseColor ?? activeSpaceColor else { return nil }
        return tintColor(for: color, alpha: alpha)
    }

    func colorForSpace(_ color: SpaceColor) -> NSColor {
        return NSColor(name: nil) { appearance in
            let isDark = appearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
            let saturation = isDark ? color.saturation : min(color.saturation * 0.85, 0.9)
            let brightness = isDark ? color.brightness : min(color.brightness * 1.1, 1.0)
            return NSColor(
                hue: CGFloat(color.hue),
                saturation: CGFloat(saturation),
                brightness: CGFloat(brightness),
                alpha: 1.0
            )
        }
    }

    func tintColor(for color: SpaceColor, alpha: CGFloat) -> NSColor {
        return NSColor(name: nil) { appearance in
            let isDark = appearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
            let saturation = isDark ? color.saturation : min(color.saturation * 0.85, 0.9)
            let brightness = isDark ? color.brightness : min(color.brightness * 1.1, 1.0)
            return NSColor(
                hue: CGFloat(color.hue),
                saturation: CGFloat(saturation),
                brightness: CGFloat(brightness),
                alpha: alpha
            )
        }
    }

    func itemAppearance(color: SpaceColor, isActive: Bool, isSelected: Bool, isHovered: Bool) -> SpaceItemAppearance {
        let baseColor = colorForSpace(color)

        if isActive {
            let fillColor = baseColor.blended(withFraction: 0.25, of: .white) ?? baseColor
            return SpaceItemAppearance(
                badgeFillColor: fillColor,
                badgeBorderColor: .clear,
                badgeInnerGlowColor: .clear,
                badgeScale: 1.0,
                alpha: 1.0
            )
        } else if isSelected {
            return SpaceItemAppearance(
                badgeFillColor: baseColor,
                badgeBorderColor: .clear,
                badgeInnerGlowColor: .clear,
                badgeScale: 1.0,
                alpha: 0.85
            )
        } else if isHovered {
            let fillColor = baseColor.blended(withFraction: 0.15, of: .white) ?? baseColor
            return SpaceItemAppearance(
                badgeFillColor: fillColor,
                badgeBorderColor: .clear,
                badgeInnerGlowColor: .clear,
                badgeScale: 1.0,
                alpha: 0.90
            )
        } else {
            return SpaceItemAppearance(
                badgeFillColor: baseColor,
                badgeBorderColor: .clear,
                badgeInnerGlowColor: .clear,
                badgeScale: 1.0,
                alpha: 0.75
            )
        }
    }

    /// Overloaded itemAppearance that accepts optional theme.
    func itemAppearance(color: SpaceColor, theme: SpaceTheme?, isActive: Bool, isSelected: Bool, isHovered: Bool) -> SpaceItemAppearance {
        let effectiveColor = theme?.baseColor ?? color
        return itemAppearance(color: effectiveColor, isActive: isActive, isSelected: isSelected, isHovered: isHovered)
    }
}

extension Notification.Name {
    static let mahoActiveSpaceThemeChanged = Notification.Name("mahoActiveSpaceThemeChanged")
}
