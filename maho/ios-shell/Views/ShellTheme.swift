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
        static var homeBackground: Color {
#if canImport(UIKit)
            Color(uiColor: .systemBackground)
#else
            Color.white
#endif
        }

        static var browsingBackground: Color {
#if canImport(UIKit)
            Color(uiColor: .systemBackground)
#else
            Color.white
#endif
        }

        static let incognitoBackground = Color.black
        static let incognitoTint = Color.black.opacity(0.82)
        static let incognitoForeground = Color.white
        static let incognitoSurface = Color.white.opacity(0.08)
        static let incognitoSurfaceStrong = Color.white.opacity(0.12)
        static let incognitoBorder = Color.white.opacity(0.14)
        static let incognitoMutedForeground = Color.white.opacity(0.7)

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

        static func bottomBarFill(tint: Color, isIncognito: Bool) -> Color {
#if canImport(UIKit)
            let base = Color(uiColor: isIncognito ? .black : .secondarySystemBackground)
#else
            let base = isIncognito ? Color.black : Color.white
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
    }

    enum Materials {
        static let bottomBar: Material = .bar
        static let summaryOverlay: Material = .regularMaterial
        static let tabGridCard: Material = .thinMaterial
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
        static let card: CGFloat = 16
        static let overlay: CGFloat = 18
        static let searchField: CGFloat = 20
        static let bottomBarControl: CGFloat = 16
        static let bottomBarCapsule: CGFloat = 18
        static let bottomBar: CGFloat = 22
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
        static let summaryDetentMaxWidth: CGFloat = 560
        static let bottomInsetWithBar: CGFloat = 78
        static let bottomInsetWithoutBar: CGFloat = 24
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
