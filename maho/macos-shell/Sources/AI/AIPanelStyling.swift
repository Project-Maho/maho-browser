import AppKit
import QuartzCore

// MARK: - Shared surface/button styling used across AI panel views

final class AIPanelTextureOverlayView: NSView {
    var lineColor: NSColor = NSColor.white.withAlphaComponent(0.012) {
        didSet { needsDisplay = true }
    }

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        translatesAutoresizingMaskIntoConstraints = false
    }

    required init?(coder: NSCoder) { fatalError() }

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)
        guard let context = NSGraphicsContext.current?.cgContext else { return }

        context.saveGState()
        context.setFillColor(NSColor.clear.cgColor)
        context.fill(dirtyRect)

        let resolved = lineColor.usingColorSpace(.deviceRGB) ?? lineColor
        context.setStrokeColor(resolved.cgColor)
        context.setLineWidth(1)

        let spacing: CGFloat = 3
        var x: CGFloat = 0
        while x <= bounds.width {
            context.move(to: CGPoint(x: x, y: 0))
            context.addLine(to: CGPoint(x: x, y: bounds.height))
            x += spacing
        }

        var y: CGFloat = 0
        while y <= bounds.height {
            context.move(to: CGPoint(x: 0, y: y))
            context.addLine(to: CGPoint(x: bounds.width, y: y))
            y += spacing
        }

        context.strokePath()
        context.restoreGState()
    }
}

extension NSView {
    func configureSurface(
        background: NSColor,
        border: NSColor,
        radius: CGFloat,
        borderWidth: CGFloat = 1,
        shadowOpacity: Float = 0,
        shadowRadius: CGFloat = 0,
        shadowOffset: NSSize = .zero
    ) {
        wantsLayer = true
        layer?.backgroundColor = background.cgColor
        layer?.borderWidth = borderWidth
        layer?.borderColor = border.cgColor
        layer?.cornerRadius = radius
        layer?.shadowColor = AIDiaPalette.shadow.cgColor
        layer?.shadowOpacity = shadowOpacity
        layer?.shadowRadius = shadowRadius
        layer?.shadowOffset = shadowOffset
    }

    func aiEnsureGradientLayer(
        named name: String,
        colors: [NSColor],
        startPoint: CGPoint = CGPoint(x: 0.5, y: 1.0),
        endPoint: CGPoint = CGPoint(x: 0.5, y: 0.0),
        locations: [NSNumber]? = nil,
        opacity: Float = 1,
        cornerRadius: CGFloat? = nil,
        insertsAboveBackground: Bool = true
    ) {
        wantsLayer = true
        guard let hostLayer = layer else { return }
        let gradient = aiGradientLayer(named: name, in: hostLayer)
        gradient.frame = bounds
        gradient.colors = colors.map { $0.cgColor }
        gradient.startPoint = startPoint
        gradient.endPoint = endPoint
        gradient.locations = locations
        gradient.opacity = opacity
        gradient.cornerRadius = cornerRadius ?? hostLayer.cornerRadius
        if insertsAboveBackground {
            gradient.zPosition = 1
        }
    }

    func aiEnsureRadialGlowLayer(
        named name: String,
        color: NSColor,
        center: CGPoint,
        radius: CGFloat,
        opacity: Float = 1,
        cornerRadius: CGFloat? = nil
    ) {
        wantsLayer = true
        guard let hostLayer = layer else { return }
        let gradient = aiGradientLayer(named: name, in: hostLayer)
        gradient.type = .radial
        gradient.frame = bounds
        let normalizedRadiusX = bounds.width > 0 ? radius / bounds.width : 0
        let normalizedRadiusY = bounds.height > 0 ? radius / bounds.height : 0
        gradient.startPoint = center
        gradient.endPoint = CGPoint(x: center.x + normalizedRadiusX, y: center.y + normalizedRadiusY)
        gradient.colors = [color.cgColor, color.withAlphaComponent(0).cgColor]
        gradient.locations = [0, 1]
        gradient.opacity = opacity
        gradient.cornerRadius = cornerRadius ?? hostLayer.cornerRadius
        gradient.zPosition = 1
    }

    func aiRemoveLayer(named name: String) {
        layer?.sublayers?.first(where: { $0.name == name })?.removeFromSuperlayer()
    }

    private func aiGradientLayer(named name: String, in hostLayer: CALayer) -> CAGradientLayer {
        if let existing = hostLayer.sublayers?.first(where: { $0.name == name }) as? CAGradientLayer {
            return existing
        }
        let gradient = CAGradientLayer()
        gradient.name = name
        hostLayer.addSublayer(gradient)
        return gradient
    }
}

extension NSButton {
    func styleChromeButton(
        background: NSColor,
        border: NSColor,
        radius: CGFloat,
        borderWidth: CGFloat = 1,
        shadowOpacity: Float = 0
    ) {
        wantsLayer = true
        layer?.backgroundColor = background.cgColor
        layer?.borderWidth = borderWidth
        layer?.borderColor = border.cgColor
        layer?.cornerRadius = radius
        layer?.shadowColor = AIDiaPalette.shadow.cgColor
        layer?.shadowOpacity = shadowOpacity
        layer?.shadowRadius = 4
        layer?.shadowOffset = CGSize(width: 0, height: -1)
    }
}

extension NSTextField {
    func styleInlineBadge(background: NSColor, border: NSColor, radius: CGFloat) {
        wantsLayer = true
        layer?.backgroundColor = background.cgColor
        layer?.borderColor = border.cgColor
        layer?.borderWidth = 1
        layer?.cornerRadius = radius
    }
}
