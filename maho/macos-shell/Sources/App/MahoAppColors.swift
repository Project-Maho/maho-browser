import AppKit

enum MahoAppColors {
    private static func dynamicColor(light: NSColor, dark: NSColor) -> NSColor {
        NSColor(name: nil) { appearance in
            appearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua ? dark : light
        }
    }

    static let favoritesPaletteTransitionDuration: TimeInterval = 0.16

    // MARK: - Window & Surface Colors (Arc dark theme)
    static var windowBackground: NSColor {
        dynamicColor(
            light: NSColor(red: 0.940, green: 0.942, blue: 0.962, alpha: 1.0),
            dark: NSColor(red: 0.061, green: 0.057, blue: 0.082, alpha: 1.0)
        )
    }
    static var sidebarBackground: NSColor {
        dynamicColor(
            light: NSColor(red: 0.893, green: 0.899, blue: 0.932, alpha: 1.0),
            dark: NSColor(red: 0.153, green: 0.137, blue: 0.212, alpha: 1.0)
        )
    }
    static var contentBackground: NSColor {
        dynamicColor(
            light: NSColor(red: 0.965, green: 0.968, blue: 0.985, alpha: 1.0),
            dark: NSColor(red: 0.111, green: 0.106, blue: 0.178, alpha: 1.0)
        )
    }
    static var contentGradientTop: NSColor {
        dynamicColor(
            light: NSColor(red: 0.988, green: 0.989, blue: 0.996, alpha: 1.0),
            dark: NSColor(red: 0.136, green: 0.128, blue: 0.214, alpha: 1.0)
        )
    }
    static var contentGradientBottom: NSColor {
        dynamicColor(
            light: NSColor(red: 0.930, green: 0.936, blue: 0.973, alpha: 1.0),
            dark: NSColor(red: 0.094, green: 0.089, blue: 0.154, alpha: 1.0)
        )
    }
    static var contentBorder: NSColor {
        dynamicColor(
            light: NSColor(red: 0.742, green: 0.766, blue: 0.860, alpha: 0.52),
            dark: NSColor(red: 0.214, green: 0.200, blue: 0.318, alpha: 0.18)
        )
    }
    static var cardBackground: NSColor {
        dynamicColor(
            light: NSColor(red: 0.973, green: 0.976, blue: 0.990, alpha: 1.0),
            dark: NSColor(red: 0.164, green: 0.152, blue: 0.239, alpha: 1.0)
        )
    }
    static var cardBorder: NSColor {
        dynamicColor(
            light: NSColor(red: 0.760, green: 0.782, blue: 0.872, alpha: 0.72),
            dark: NSColor(red: 0.235, green: 0.220, blue: 0.333, alpha: 1.0)
        )
    }
    static var titlebarGlass: NSColor {
        dynamicColor(
            light: NSColor(red: 0.928, green: 0.934, blue: 0.970, alpha: 0.92),
            dark: NSColor(red: 0.132, green: 0.122, blue: 0.198, alpha: 0.82)
        )
    }
    static var titlebarEdge: NSColor {
        dynamicColor(
            light: NSColor(red: 0.738, green: 0.761, blue: 0.855, alpha: 0.58),
            dark: NSColor(white: 1.0, alpha: 0.075)
        )
    }
    static var titlebarControlTint: NSColor {
        dynamicColor(
            light: NSColor(red: 0.262, green: 0.286, blue: 0.390, alpha: 1.0),
            dark: NSColor(red: 0.905, green: 0.905, blue: 0.938, alpha: 1.0)
        )
    }
    static var canvasAccent: NSColor {
        dynamicColor(
            light: NSColor(red: 0.442, green: 0.555, blue: 0.982, alpha: 0.12),
            dark: NSColor(red: 0.405, green: 0.427, blue: 0.741, alpha: 0.22)
        )
    }
    static var canvasInnerGlow: NSColor {
        dynamicColor(
            light: NSColor(red: 0.770, green: 0.820, blue: 0.972, alpha: 0.42),
            dark: NSColor(red: 0.214, green: 0.206, blue: 0.322, alpha: 0.48)
        )
    }
    static let canvasBadgeFill = NSColor(red: 0.170, green: 0.160, blue: 0.255, alpha: 0.78)
    static let canvasBadgeBorder = NSColor(red: 0.259, green: 0.243, blue: 0.365, alpha: 0.44)

    // MARK: - Interactive Surface Colors
    static var sidebarHover: NSColor {
        dynamicColor(
            light: NSColor(red: 0.116, green: 0.160, blue: 0.302, alpha: 0.06),
            dark: NSColor(white: 1.0, alpha: 0.06)
        )
    }
    static var sidebarSelected: NSColor {
        dynamicColor(
            light: NSColor(red: 0.212, green: 0.295, blue: 0.548, alpha: 0.11),
            dark: NSColor(white: 1.0, alpha: 0.105)
        )
    }
    static var spaceStripBorder: NSColor {
        dynamicColor(
            light: NSColor(red: 0.720, green: 0.745, blue: 0.846, alpha: 0.66),
            dark: NSColor(white: 1.0, alpha: 0.038)
        )
    }
    static var spaceControlTint: NSColor {
        dynamicColor(
            light: NSColor(red: 0.346, green: 0.380, blue: 0.506, alpha: 0.50),
            dark: NSColor(red: 0.784, green: 0.790, blue: 0.870, alpha: 0.48)
        )
    }
    static var spaceChipHover: NSColor {
        dynamicColor(
            light: NSColor(red: 0.128, green: 0.170, blue: 0.320, alpha: 0.08),
            dark: NSColor(white: 1.0, alpha: 0.078)
        )
    }
    static var spaceChipSelected: NSColor {
        dynamicColor(
            light: NSColor(red: 0.176, green: 0.245, blue: 0.452, alpha: 0.16),
            dark: NSColor(white: 1.0, alpha: 0.118)
        )
    }
    static var spaceChipIdleBorder: NSColor {
        dynamicColor(
            light: NSColor(red: 0.644, green: 0.684, blue: 0.824, alpha: 0.78),
            dark: NSColor(white: 1.0, alpha: 0.065)
        )
    }
    static let spaceChipCountFill = NSColor(red: 0.185, green: 0.176, blue: 0.279, alpha: 0.92)
    static let spaceChipCountBorder = NSColor(red: 0.278, green: 0.259, blue: 0.388, alpha: 0.68)
    static var searchFieldBackground: NSColor {
        dynamicColor(
            light: NSColor(red: 0.948, green: 0.953, blue: 0.978, alpha: 0.96),
            dark: NSColor(red: 0.167, green: 0.158, blue: 0.245, alpha: 0.90)
        )
    }
    static var searchFieldBorder: NSColor {
        dynamicColor(
            light: NSColor(red: 0.706, green: 0.730, blue: 0.834, alpha: 0.42),
            dark: NSColor(red: 0.255, green: 0.239, blue: 0.349, alpha: 0.30)
        )
    }
    static var newTabStripBackground: NSColor {
        dynamicColor(
            light: NSColor(red: 0.930, green: 0.938, blue: 0.972, alpha: 0.82),
            dark: NSColor(red: 0.156, green: 0.146, blue: 0.232, alpha: 0.68)
        )
    }
    static var newTabStripBorder: NSColor {
        dynamicColor(
            light: NSColor(red: 0.690, green: 0.714, blue: 0.822, alpha: 0.38),
            dark: NSColor(red: 0.248, green: 0.231, blue: 0.352, alpha: 0.26)
        )
    }
    static var favoritesEmptyCardBackground: NSColor {
        dynamicColor(
            light: NSColor(red: 0.963, green: 0.970, blue: 0.993, alpha: 0.96),
            dark: NSColor(red: 0.148, green: 0.139, blue: 0.232, alpha: 0.96)
        )
    }
    static var favoritesEmptyCardBorder: NSColor {
        dynamicColor(
            light: NSColor(red: 0.720, green: 0.758, blue: 0.900, alpha: 0.54),
            dark: NSColor(red: 0.266, green: 0.247, blue: 0.386, alpha: 0.50)
        )
    }
    static var favoritesEmptyCardDash: NSColor {
        dynamicColor(
            light: NSColor(red: 0.425, green: 0.528, blue: 0.930, alpha: 0.48),
            dark: NSColor(red: 0.486, green: 0.561, blue: 0.979, alpha: 0.72)
        )
    }
    static var favoritesEmptyBadgeBackground: NSColor {
        dynamicColor(
            light: NSColor(red: 0.926, green: 0.942, blue: 0.989, alpha: 1.0),
            dark: NSColor(red: 0.188, green: 0.176, blue: 0.289, alpha: 1.0)
        )
    }
    static var favoritesEmptyBadgeBorder: NSColor {
        dynamicColor(
            light: NSColor(red: 0.640, green: 0.708, blue: 0.948, alpha: 0.52),
            dark: NSColor(red: 0.400, green: 0.455, blue: 0.896, alpha: 0.56)
        )
    }
    static var favoritesEmptyBadgeIcon: NSColor {
        dynamicColor(
            light: NSColor(red: 0.308, green: 0.420, blue: 0.846, alpha: 1.0),
            dark: NSColor(red: 0.760, green: 0.808, blue: 1.000, alpha: 1.0)
        )
    }
    static var favoritesEmptyCloseGlyph: NSColor {
        dynamicColor(
            light: NSColor(red: 0.462, green: 0.506, blue: 0.654, alpha: 0.62),
            dark: NSColor(red: 0.768, green: 0.777, blue: 0.885, alpha: 0.62)
        )
    }
    static var archivePanelBackground: NSColor {
        dynamicColor(
            light: NSColor(red: 0.948, green: 0.952, blue: 0.972, alpha: 0.84),
            dark: NSColor(red: 0.194, green: 0.190, blue: 0.204, alpha: 0.92)
        )
    }
    static var archiveControlBackground: NSColor {
        dynamicColor(
            light: NSColor(red: 0.962, green: 0.966, blue: 0.980, alpha: 0.76),
            dark: NSColor(red: 0.242, green: 0.236, blue: 0.252, alpha: 0.76)
        )
    }
    static var archiveControlBorder: NSColor {
        dynamicColor(
            light: NSColor(red: 0.692, green: 0.720, blue: 0.828, alpha: 0.04),
            dark: NSColor(red: 0.365, green: 0.357, blue: 0.384, alpha: 0.12)
        )
    }
    static var archiveControlActiveBackground: NSColor {
        dynamicColor(
            light: NSColor(red: 0.914, green: 0.928, blue: 0.980, alpha: 0.50),
            dark: NSColor(red: 0.272, green: 0.266, blue: 0.286, alpha: 0.76)
        )
    }
    static var archiveRowHover: NSColor {
        dynamicColor(
            light: NSColor(red: 0.188, green: 0.240, blue: 0.382, alpha: 0.013),
            dark: NSColor(white: 1.0, alpha: 0.024)
        )
    }
    static var archiveRowSelected: NSColor {
        dynamicColor(
            light: NSColor(red: 0.214, green: 0.292, blue: 0.504, alpha: 0.030),
            dark: NSColor(white: 1.0, alpha: 0.042)
        )
    }
    static var archiveSectionHeaderText: NSColor {
        dynamicColor(
            light: NSColor(red: 0.352, green: 0.386, blue: 0.486, alpha: 0.84),
            dark: NSColor(red: 0.730, green: 0.730, blue: 0.756, alpha: 0.86)
        )
    }
    static var archiveDivider: NSColor {
        dynamicColor(
            light: NSColor(red: 0.640, green: 0.672, blue: 0.790, alpha: 0.036),
            dark: NSColor(white: 1.0, alpha: 0.020)
        )
    }
    // MARK: - Library Rail Colors
    static var libraryRailBackground: NSColor {
        dynamicColor(
            light: NSColor(red: 0.880, green: 0.886, blue: 0.918, alpha: 1.0),
            dark: NSColor(red: 0.120, green: 0.112, blue: 0.168, alpha: 1.0)
        )
    }
    static var libraryRailItemSelected: NSColor {
        dynamicColor(
            light: NSColor(red: 0.862, green: 0.870, blue: 0.910, alpha: 1.0),
            dark: NSColor(red: 0.200, green: 0.192, blue: 0.252, alpha: 1.0)
        )
    }
    static var libraryRailItemHover: NSColor {
        dynamicColor(
            light: NSColor(white: 0.0, alpha: 0.04),
            dark: NSColor(white: 1.0, alpha: 0.04)
        )
    }
    static var libraryRailItemText: NSColor {
        dynamicColor(
            light: NSColor(red: 0.340, green: 0.360, blue: 0.440, alpha: 1.0),
            dark: NSColor(red: 0.580, green: 0.580, blue: 0.620, alpha: 0.86)
        )
    }
    static var libraryRailItemSelectedText: NSColor {
        dynamicColor(
            light: NSColor(red: 0.180, green: 0.200, blue: 0.290, alpha: 1.0),
            dark: NSColor(red: 0.920, green: 0.920, blue: 0.940, alpha: 1.0)
        )
    }
    static var libraryRailDivider: NSColor {
        dynamicColor(
            light: NSColor(red: 0.700, green: 0.720, blue: 0.800, alpha: 0.20),
            dark: NSColor(white: 1.0, alpha: 0.06)
        )
    }
    static var archiveDetailPanelBackground: NSColor {
        dynamicColor(
            light: NSColor(red: 0.930, green: 0.938, blue: 0.972, alpha: 1.0),
            dark: NSColor(red: 0.082, green: 0.078, blue: 0.118, alpha: 1.0)
        )
    }
    static var archiveDetailPanelInnerBackground: NSColor {
        dynamicColor(
            light: NSColor(red: 0.955, green: 0.962, blue: 0.988, alpha: 0.98),
            dark: NSColor(red: 0.118, green: 0.113, blue: 0.170, alpha: 0.98)
        )
    }
    static var archiveDetailPanelBorder: NSColor {
        dynamicColor(
            light: NSColor(red: 0.716, green: 0.744, blue: 0.846, alpha: 0.48),
            dark: NSColor(red: 0.214, green: 0.206, blue: 0.294, alpha: 0.82)
        )
    }
    static var archiveDetailPanelMutedFill: NSColor {
        dynamicColor(
            light: NSColor(red: 0.882, green: 0.902, blue: 0.972, alpha: 0.64),
            dark: NSColor(red: 0.170, green: 0.165, blue: 0.239, alpha: 0.78)
        )
    }
    static var archiveDetailPanelAccent: NSColor {
        dynamicColor(
            light: NSColor(red: 0.362, green: 0.456, blue: 0.930, alpha: 0.18),
            dark: NSColor(red: 0.462, green: 0.510, blue: 0.952, alpha: 0.24)
        )
    }
    // MARK: - Text Colors
    static var primaryText: NSColor {
        dynamicColor(
            light: NSColor(red: 0.176, green: 0.200, blue: 0.290, alpha: 1.0),
            dark: NSColor(red: 0.953, green: 0.953, blue: 0.969, alpha: 1.0)
        )
    }
    static var secondaryText: NSColor {
        dynamicColor(
            light: NSColor(red: 0.348, green: 0.382, blue: 0.500, alpha: 1.0),
            dark: NSColor(red: 0.736, green: 0.736, blue: 0.792, alpha: 1.0)
        )
    }
    static var tertiaryText: NSColor {
        dynamicColor(
            light: NSColor(red: 0.500, green: 0.534, blue: 0.650, alpha: 1.0),
            dark: NSColor(red: 0.561, green: 0.561, blue: 0.624, alpha: 1.0)
        )
    }

    // MARK: - Legacy aliases (for existing code compatibility)
    static let sidebarText = primaryText
    static let sidebarSecondaryText = secondaryText
    static let toolbarBackground = sidebarBackground
    static let urlBarBackground = searchFieldBackground
    static var urlBarText: NSColor { titlebarControlTint }
    static let tabActive = sidebarSelected
    static let tabInactive = NSColor.clear
    static var divider: NSColor {
        dynamicColor(
            light: NSColor(red: 0.634, green: 0.666, blue: 0.782, alpha: 0.18),
            dark: NSColor(white: 1.0, alpha: 0.014)
        )
    }
    static var footerDivider: NSColor {
        dynamicColor(
            light: NSColor(red: 0.634, green: 0.666, blue: 0.782, alpha: 0.14),
            dark: NSColor(white: 1.0, alpha: 0.010)
        )
    }
    static let commandBarBackground = NSColor(red: 0.071, green: 0.071, blue: 0.075, alpha: 0.96)  // mockup panel
    static let commandBarPanelBorder = NSColor(red: 0.306, green: 0.314, blue: 0.329, alpha: 1.0)
    static let commandBarInputFill = NSColor(red: 0.447, green: 0.455, blue: 0.471, alpha: 1.0)
    static let commandBarInputBorder = NSColor(white: 1.0, alpha: 0.06)
    static var commandBarSelectionFill: NSColor {
        dynamicColor(
            light: NSColor(red: 0.430, green: 0.532, blue: 0.960, alpha: 0.22),
            dark: NSColor(red: 0.321, green: 0.341, blue: 0.905, alpha: 0.92)
        )
    }
    static let commandBarSelectionBorder = NSColor(red: 0.431, green: 0.459, blue: 0.975, alpha: 0.38)
    static let commandBarKeycapBorder = NSColor(white: 1.0, alpha: 0.08)
    static let commandBarInputSelection = NSColor(red: 0.584, green: 0.608, blue: 0.716, alpha: 0.42)

    // MARK: - Accent Colors
    static let accentBlue = NSColor(red: 0.388, green: 0.400, blue: 0.945, alpha: 1.0)             // #6366F1
    static let accentBlueBright = NSColor(red: 0.506, green: 0.549, blue: 0.973, alpha: 1.0)       // #818CF8
    static let selectionHighlight = NSColor(red: 0.388, green: 0.400, blue: 0.945, alpha: 1.0)     // #6366F1
    static let folderIconColor = NSColor(red: 0.376, green: 0.647, blue: 0.980, alpha: 1.0)        // #60A5FA

    // MARK: - Semantic Colors
    static let dangerRed = NSColor(red: 0.937, green: 0.267, blue: 0.267, alpha: 1.0)              // #EF4444
    static let successGreen = NSColor(red: 0.133, green: 0.773, blue: 0.369, alpha: 1.0)           // #22C55E
    static let dropIndicator = accentBlue.withAlphaComponent(0.8)

    // MARK: - Space Colors
    static func spaceColor(hue: Double, saturation: Double, brightness: Double) -> NSColor {
        return NSColor(
            hue: CGFloat(hue),
            saturation: CGFloat(saturation),
            brightness: CGFloat(brightness),
            alpha: 1.0
        )
    }

    // MARK: - Appearance-Aware Command Bar Colors
    static var commandBarEmptyText: NSColor {
        NSColor(name: nil) { appearance in
            appearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
                ? NSColor(white: 1.0, alpha: 0.35)
                : NSColor(white: 0.0, alpha: 0.35)
        }
    }
    static var commandBarShortcutBadge: NSColor {
        NSColor(name: nil) { appearance in
            appearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
                ? NSColor(white: 1.0, alpha: 0.12)
                : NSColor(white: 0.0, alpha: 0.08)
        }
    }
    static var commandBarShortcutText: NSColor {
        NSColor(name: nil) { appearance in
            appearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
                ? NSColor(white: 1.0, alpha: 0.55)
                : NSColor(white: 0.0, alpha: 0.45)
        }
    }
    static var commandBarDivider: NSColor {
        NSColor(name: nil) { appearance in
            appearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
                ? NSColor(white: 1.0, alpha: 0.08)
                : NSColor(white: 0.0, alpha: 0.08)
        }
    }
}

enum SidebarMetrics {
    static let sidebarHorizontalInset: CGFloat = 11
}
