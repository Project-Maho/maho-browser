import SwiftUI
#if canImport(UIKit)
import UIKit
#endif

struct ShellShadowStyle {
    let color: Color
    let radius: CGFloat
    let x: CGFloat
    let y: CGFloat
}

enum ShellTheme {
    enum Palette {
        static let accent = Color.accentColor

        static let success = Color(red: 48.0 / 255.0, green: 209.0 / 255.0, blue: 88.0 / 255.0)
        static let warning = Color(red: 255.0 / 255.0, green: 159.0 / 255.0, blue: 10.0 / 255.0)
        static let error = Color(red: 255.0 / 255.0, green: 69.0 / 255.0, blue: 58.0 / 255.0)

#if canImport(UIKit)
        static func dynamicColor(light: UIColor, dark: UIColor) -> Color {
            Color(uiColor: UIColor { traits in traits.userInterfaceStyle == .dark ? dark : light })
        }
#endif

        static var homeBackground: Color {
#if canImport(UIKit)
            Color(uiColor: .systemBackground)
#else
            Color.white
#endif
        }

        static var homeBackgroundElevated: Color {
#if canImport(UIKit)
            Color(uiColor: .secondarySystemBackground)
#else
            Color.white
#endif
        }

        static var logoStarOutline: Color {
#if canImport(UIKit)
            Color(uiColor: UIColor { traits in
                traits.userInterfaceStyle == .dark
                    ? UIColor.clear
                    : UIColor(red: 30.0 / 255.0, green: 27.0 / 255.0, blue: 75.0 / 255.0, alpha: 1.0)
            })
#else
            Color(red: 30.0 / 255.0, green: 27.0 / 255.0, blue: 75.0 / 255.0)
#endif
        }

        static var sheetSurface: Color {
#if canImport(UIKit)
            Color(uiColor: .systemBackground)
#else
            Color.white
#endif
        }

        static var sheetFieldFill: Color {
#if canImport(UIKit)
            Color(uiColor: .secondarySystemBackground)
#else
            Color(white: 0.92)
#endif
        }

        static var browsingBackground: Color {
#if canImport(UIKit)
            Color(uiColor: .systemBackground)
#else
            Color.white
#endif
        }

        static let incognitoBackground = Color(red: 26.0 / 255.0, green: 35.0 / 255.0, blue: 82.0 / 255.0)
        static let incognitoTint = Color(red: 26.0 / 255.0, green: 35.0 / 255.0, blue: 82.0 / 255.0).opacity(0.82)
        static let incognitoForeground = Color.white
        static let incognitoSurface = Color.white.opacity(0.08)
        static let incognitoSurfaceStrong = Color.white.opacity(0.12)
        static let incognitoBorder = Color.white.opacity(0.14)
        static let incognitoMutedForeground = Color.white.opacity(0.7)
        static let searchOverlayMediumField = Color(red: 58.0 / 255.0, green: 58.0 / 255.0, blue: 60.0 / 255.0)
        static let incognitoSearchCanvas = Color(red: 20.0 / 255.0, green: 20.0 / 255.0, blue: 22.0 / 255.0)
        static let incognitoSearchContainer = Color(red: 42.0 / 255.0, green: 42.0 / 255.0, blue: 44.0 / 255.0)
        static let incognitoSearchField = searchOverlayMediumField

        static let searchOverlayBackdrop = Color.black.opacity(0.58)
        static let searchOverlayNormalCanvas = Color(red: 18.0 / 255.0, green: 18.0 / 255.0, blue: 20.0 / 255.0)
        static let searchOverlayNormalElevatedCanvas = Color(red: 27.0 / 255.0, green: 27.0 / 255.0, blue: 30.0 / 255.0)
        static let searchOverlayNormalField = searchOverlayMediumField
        static let searchOverlayBrowsePillFill = Color.black.opacity(0.44)
        static let searchOverlayBrowsePillForeground = Color.white.opacity(0.92)
        static let searchOverlayNormalAccent = Color(red: 132.0 / 255.0, green: 185.0 / 255.0, blue: 255.0 / 255.0)

        static func searchOverlayForeground(isIncognito: Bool) -> Color {
            Color.primary
        }

        static func searchOverlayMutedForeground(isIncognito: Bool) -> Color {
            Color.secondary
        }

        static func searchOverlaySecondaryForeground(isIncognito: Bool) -> Color {
#if canImport(UIKit)
            Color(uiColor: .tertiaryLabel)
#else
            Color.secondary.opacity(0.7)
#endif
        }

        static func searchOverlayBorder(isIncognito: Bool) -> Color {
#if canImport(UIKit)
            Color(uiColor: .separator)
#else
            Color.primary.opacity(0.12)
#endif
        }

        static func searchOverlayRowPressed(isIncognito: Bool) -> Color {
#if canImport(UIKit)
            Color(uiColor: .secondarySystemBackground)
#else
            Color.primary.opacity(0.06)
#endif
        }

        static func searchOverlayIconFill(isIncognito: Bool) -> Color {
#if canImport(UIKit)
            Color(uiColor: .secondarySystemBackground)
#else
            Color.primary.opacity(0.08)
#endif
        }

        static let searchFieldFill = Color.black.opacity(0.03)
        static let searchFieldBorder = Color.black.opacity(0.035)
        static let topSiteIconFill = Color.black.opacity(0.05)
        static let topSiteCardFill = Color.black.opacity(0.022)
        static let secondaryButtonFill = Color.secondary.opacity(0.15)
        static let homeCanvasSecondaryText = Color.primary.opacity(0.58)

        static var tabCardSurface: Color {
#if canImport(UIKit)
            Color(uiColor: .systemBackground)
#else
            Color.white
#endif
        }

        static var tabPreviewFallback: Color {
#if canImport(UIKit)
            Color(uiColor: .systemGray6)
#else
            Color.gray.opacity(0.12)
#endif
        }

        static var tabPreviewPlaceholder: Color {
#if canImport(UIKit)
            Color(uiColor: .systemGray5)
#else
            Color.gray.opacity(0.18)
#endif
        }

        static let overlayBorder = Color.primary.opacity(0.08)
        static let overlayDim = Color.black.opacity(0.18)
        static let summaryAccent = accent
        static let summaryAccentFill = accent.opacity(0.08)
        static let summarySurface = Color.white.opacity(0.66)
        static let summarySurfaceBorder = Color.white.opacity(0.34)
        static let summaryHeroFill = accent.opacity(0.08)
        static let summaryMutedFill = Color.primary.opacity(0.035)
        static let tabGridBackdropTop = accent.opacity(0.08)
        static let tabGridBackdropBottom = Color.black.opacity(0.02)
        static let tabGridActiveFill = accent.opacity(0.07)
        static let tabGridMetaFill = Color.primary.opacity(0.03)
        static let tabGridFloatingSurface = Color.white.opacity(0.66)

        static var visualSearchBackground: Color { homeBackground }
        static let visualSearchForeground = Color.primary
        static let visualSearchMutedForeground = Color.secondary
        static let visualSearchBorder = Color.primary.opacity(0.1)
        static let visualSearchSurface = Color.secondary.opacity(0.1)
        static let visualSearchSurfaceStrong = Color.secondary.opacity(0.2)

        static let chatAttachmentBadgeFill = Color.black.opacity(0.48)
        static let chatAttachmentStroke = Color.primary.opacity(0.08)
        static let chatAttachmentPlaceholder = Color.secondary.opacity(0.1)

        static func bottomBarFill(tint: Color, isIncognito: Bool) -> Color {
#if canImport(UIKit)
            let base = isIncognito ? incognitoBackground : Color(uiColor: .secondarySystemBackground)
#else
            let base = isIncognito ? incognitoBackground : Color.white
#endif
            return isIncognito ? base.opacity(0.92) : base.opacity(0.84)
        }

        static func homeSurfaceFill(showsUnderlyingPage: Bool) -> Color {
            showsUnderlyingPage ? homeBackground.opacity(0.86) : homeBackground
        }

        static func bottomBarCapsuleFill(tint: Color, isIncognito: Bool) -> Color {
            isIncognito ? Color.white.opacity(0.06) : tint.opacity(0.1)
        }

        static func bottomBarSecondaryFill(isIncognito: Bool) -> Color {
            isIncognito ? Color.white.opacity(0.05) : Color.primary.opacity(0.035)
        }

        static func bottomBarForeground(isIncognito: Bool) -> Color {
            isIncognito ? incognitoForeground : .primary
        }

        static func bottomBarMutedForeground(isIncognito: Bool) -> Color {
            isIncognito ? Color.white.opacity(0.68) : Color.primary.opacity(0.5)
        }

        static func bottomBarBadgeText(isIncognito: Bool) -> Color {
            isIncognito ? incognitoBackground : .white
        }

        static func bottomBarBorder(for foreground: Color) -> Color {
            foreground.opacity(0.08)
        }

        static func bottomBarDragIndicator(isIncognito: Bool) -> Color {
            isIncognito ? Color.white.opacity(0.18) : Color.primary.opacity(0.08)
        }

        static func searchOverlayCanvas(isIncognito: Bool) -> Color {
            if isIncognito { return incognitoBackground }
#if canImport(UIKit)
            return Color(uiColor: .secondarySystemBackground)
#else
            return Color.primary.opacity(0.06)
#endif
        }

        static func searchOverlayElevatedCanvas(isIncognito: Bool) -> Color {
            if isIncognito { return incognitoBackground }
#if canImport(UIKit)
            return Color(uiColor: .secondarySystemBackground)
#else
            return Color.primary.opacity(0.06)
#endif
        }

        static func searchOverlayFieldFill(isIncognito: Bool) -> Color {
            if isIncognito { return incognitoSurfaceStrong }
#if canImport(UIKit)
            return Color(uiColor: .tertiarySystemBackground)
#else
            return Color.primary.opacity(0.1)
#endif
        }

        static func searchOverlayAccent(isIncognito: Bool) -> Color {
            isIncognito ? incognitoForeground : searchOverlayNormalAccent
        }

        static let voiceAssistantGlowBlue = Color(red: 56.0 / 255.0, green: 189.0 / 255.0, blue: 248.0 / 255.0)
        static let voiceAssistantGlowPurple = Color(red: 139.0 / 255.0, green: 92.0 / 255.0, blue: 246.0 / 255.0)
        static let voiceAssistantGlowMagenta = Color(red: 14.0 / 255.0, green: 165.0 / 255.0, blue: 233.0 / 255.0)
        static let voiceAssistantHeadlineStart = Color(red: 224.0 / 255.0, green: 242.0 / 255.0, blue: 254.0 / 255.0)
        static let voiceAssistantHeadlineEnd = Color(red: 237.0 / 255.0, green: 233.0 / 255.0, blue: 254.0 / 255.0)
        static let voiceAssistantFace = Color(red: 248.0 / 255.0, green: 250.0 / 255.0, blue: 252.0 / 255.0)
        static let voiceAssistantScrimDim = Color.black.opacity(0.16)
        static let voiceAssistantPromptHaloCore = Color.black.opacity(0.34)
        static let voiceAssistantPromptHaloMid = Color.black.opacity(0.16)
        static let voiceAssistantPromptHaloClear = Color.black.opacity(0)
        static let voiceAssistantTextShadow = Color.black.opacity(0.46)
        static let voiceAssistantWaveformFill = Color(red: 15.0 / 255.0, green: 23.0 / 255.0, blue: 42.0 / 255.0).opacity(0.38)
        static let voiceAssistantWaveformBorder = voiceAssistantHeadlineStart.opacity(0.26)
        static let voiceAssistantWaveformActive = Color(red: 186.0 / 255.0, green: 230.0 / 255.0, blue: 253.0 / 255.0)
        static let voiceAssistantWaveformIdle = voiceAssistantHeadlineEnd.opacity(0.48)
        static let voiceAssistantSecondaryText = voiceAssistantHeadlineStart.opacity(0.78)
        static let voiceAssistantControlFill = Color(red: 15.0 / 255.0, green: 23.0 / 255.0, blue: 42.0 / 255.0).opacity(0.34)
        static let voiceAssistantErrorFill = voiceAssistantGlowPurple.opacity(0.16)
        static let voiceAssistantErrorBorder = voiceAssistantWaveformActive.opacity(0.28)
    }

    enum ReaderPalette {
        struct Scheme {
            let backgroundHex: String
            let foregroundHex: String
            let mutedForegroundHex: String
            let linkHex: String
        }

        static func scheme(for theme: ReaderTheme) -> Scheme {
            switch theme {
            case .light:
                return Scheme(backgroundHex: "#ffffff", foregroundHex: "#000000", mutedForegroundHex: "#666666", linkHex: "#0066cc")
            case .sepia:
                return Scheme(backgroundHex: "#faf2df", foregroundHex: "#5a402a", mutedForegroundHex: "#666666", linkHex: "#0066cc")
            case .dark:
                return Scheme(backgroundHex: "#1e1e1e", foregroundHex: "#dcdcdc", mutedForegroundHex: "#888888", linkHex: "#88c0d0")
            }
        }

        static func background(for theme: ReaderTheme) -> Color {
            color(fromHex: scheme(for: theme).backgroundHex)
        }

        static func foreground(for theme: ReaderTheme) -> Color {
            color(fromHex: scheme(for: theme).foregroundHex)
        }

        private static func color(fromHex hex: String) -> Color {
            let cleaned = hex.trimmingCharacters(in: CharacterSet(charactersIn: "#"))
            let scanner = Scanner(string: cleaned)
            var value: UInt64 = 0
            scanner.scanHexInt64(&value)
            let red = Double((value & 0xFF0000) >> 16) / 255.0
            let green = Double((value & 0x00FF00) >> 8) / 255.0
            let blue = Double(value & 0x0000FF) / 255.0
            return Color(red: red, green: green, blue: blue)
        }
    }

    enum Materials {
        static let bottomBar: Material = .bar
        static let summaryOverlay: Material = .regularMaterial
        static let tabGridCard: Material = .thinMaterial
        static let voiceAssistantPill: Material = .thinMaterial
    }

    enum Spacing {
        static let xxSmall: CGFloat = 4
        static let badgeVertical: CGFloat = 2
        static let badgeHorizontal: CGFloat = 5
        static let xSmall: CGFloat = 6
        static let small: CGFloat = 8
        static let compact: CGFloat = 9
        static let medium: CGFloat = 12
        static let mediumLarge: CGFloat = 14
        static let large: CGFloat = 16
        static let field: CGFloat = 16
        static let xLarge: CGFloat = 18
        static let page: CGFloat = 20
        static let section: CGFloat = 20
        static let hero: CGFloat = 22
    }

    enum Radius {
        static let button: CGFloat = 10
        static let onboardingButton: CGFloat = 12
        static let searchOverlayIcon: CGFloat = 10
        static let searchOverlayRow: CGFloat = 14
        static let card: CGFloat = 16
        static let overlay: CGFloat = 18
        static let searchField: CGFloat = 20
        static let bottomBarControl: CGFloat = 16
        static let bottomBarCapsule: CGFloat = 18
        static let bottomBar: CGFloat = 22
        static let searchOverlayPanel: CGFloat = 28
    }

    enum Size {
        static let touchTarget: CGFloat = 44
        static let bottomBarProgressWidth: CGFloat = 72
        static let bottomBarGrabberWidth: CGFloat = 24
        static let bottomBarGrabberHeight: CGFloat = 3
        static let bottomBarMinHeight: CGFloat = 56
        static let bottomBarCapsuleHeight: CGFloat = 44
        static let topSiteIcon: CGFloat = 40
        static let onboardingHeroIcon: CGFloat = 80
        static let homeContentMaxWidth: CGFloat = 520
        static let homeAnchorThreshold: CGFloat = 250
        static let homeTopSpacer: CGFloat = 28
        static let homeBottomSpacer: CGFloat = 52
        static let tabPreviewHeight: CGFloat = 132
        static let tabGroupSuggestionPreviewHeight: CGFloat = 80
        static let chatAttachmentThumbnail: CGFloat = 80
        static let tabGridCardMin: CGFloat = 152
        static let tabGridCardMax: CGFloat = 188
        static let tabGridFavicon: CGFloat = 34
        static let tabGridSpotlightPreviewWidth: CGFloat = 112
        static let tabGridHeroPreviewHeight: CGFloat = 96
        static let tabGridEmptyDeckWidth: CGFloat = 88
        static let tabGridEmptyDeckHeight: CGFloat = 112
        static let tabGridEmptyDeckFrontWidth: CGFloat = 92
        static let tabGridEmptyDeckFrontHeight: CGFloat = 116
        static let summaryStateIcon: CGFloat = 36
        static let incognitoBannerMaxWidth: CGFloat = 420
        static let visualSearchPreviewMaxWidth: CGFloat = 420
        static let summaryDetentMaxWidth: CGFloat = 560
        static let bottomInsetWithBar: CGFloat = 78
        static let bottomInsetWithoutBar: CGFloat = 24
        static let searchOverlayMaxWidth: CGFloat = 640
        static let searchOverlayFieldHeight: CGFloat = 54
        static let searchOverlayIconButton: CGFloat = 34
        static let searchOverlayLeadingIcon: CGFloat = 34
        static let searchOverlayBrowsePillHeight: CGFloat = 30
        static let searchOverlayEmptyIcon: CGFloat = 36
        static let voiceAssistantHeadlineMaxWidth: CGFloat = 360
        static let voiceAssistantPromptHaloWidth: CGFloat = 440
        static let voiceAssistantPromptHaloHeight: CGFloat = 220
        static let voiceAssistantPromptHaloRadius: CGFloat = 214
        static let voiceAssistantWaveformMaxWidth: CGFloat = 292
        static let voiceAssistantWaveformHeight: CGFloat = 74
        static let voiceAssistantWaveformBarWidth: CGFloat = 7
        static let voiceAssistantWaveformDot: CGFloat = 7
        static let voiceAssistantWaveformMinBarHeight: CGFloat = 8
        static let voiceAssistantWaveformMaxBarHeight: CGFloat = 44
    }

    enum Stroke {
        static let hairline: CGFloat = 1
        static let activeTab: CGFloat = 2.5
    }

    enum Elevation {
        static let homeSearch = ShellShadowStyle(
            color: .black.opacity(0.035),
            radius: 14,
            x: 0,
            y: 6
        )

        static let tabCard = ShellShadowStyle(
            color: .black.opacity(0.05),
            radius: 3,
            x: 0,
            y: 2
        )

        static let tabDeckCard = ShellShadowStyle(
            color: .black.opacity(0.08),
            radius: 14,
            x: 0,
            y: 8
        )

        static let summaryOverlay = ShellShadowStyle(
            color: .black.opacity(0.08),
            radius: 14,
            x: 0,
            y: 8
        )

        static func bottomBar(isIncognito: Bool) -> ShellShadowStyle {
            ShellShadowStyle(
                color: .black.opacity(isIncognito ? 0.12 : 0.05),
                radius: 12,
                x: 0,
                y: 4
            )
        }
    }
}
