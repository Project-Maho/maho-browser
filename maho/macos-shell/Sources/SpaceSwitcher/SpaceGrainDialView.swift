import AppKit

final class SpaceGrainDialView: NSView {
    var grainValue: Double = 0.0 {
        didSet {
            let clampedValue = Self.clamp(grainValue)
            if clampedValue != grainValue {
                grainValue = clampedValue
                return
            }
            setAccessibilityValue(NSNumber(value: grainValue))
            needsDisplay = true
        }
    }

    var onGrainChanged: ((Double) -> Void)?

    private static let outerDotCount = 24

    override var intrinsicContentSize: NSSize {
        NSSize(width: 80, height: 80)
    }

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        commonInit()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)

        let dialRect = bounds.insetBy(dx: 6, dy: 6)
        let center = NSPoint(x: dialRect.midX, y: dialRect.midY)
        let outerRadius = min(dialRect.width, dialRect.height) * 0.5 - 2
        let innerRingRadius = outerRadius - 10
        let dotDiameter: CGFloat = 2

        let innerRingRect = NSRect(
            x: center.x - innerRingRadius,
            y: center.y - innerRingRadius,
            width: innerRingRadius * 2,
            height: innerRingRadius * 2
        )
        let innerRingPath = NSBezierPath(ovalIn: innerRingRect)
        innerRingPath.lineWidth = 0.5
        NSColor.white.withAlphaComponent(0.08).setStroke()
        innerRingPath.stroke()

        for dotIndex in 0..<Self.outerDotCount {
            let dotAngle = CGFloat(dotIndex) * (360 / CGFloat(Self.outerDotCount))
            let dotCenter = point(onCircleWithCenter: center, radius: outerRadius, angleDegrees: dotAngle - 90)
            let dotRect = NSRect(
                x: dotCenter.x - (dotDiameter * 0.5),
                y: dotCenter.y - (dotDiameter * 0.5),
                width: dotDiameter,
                height: dotDiameter
            )

            NSColor.white.withAlphaComponent(0.12).setFill()
            NSBezierPath(ovalIn: dotRect).fill()
        }
    }

    override func mouseDown(with event: NSEvent) {
        updateValue(with: event)
    }

    override func mouseDragged(with event: NSEvent) {
        updateValue(with: event)
    }

    private func commonInit() {
        translatesAutoresizingMaskIntoConstraints = false
        wantsLayer = true
        layer?.shadowColor = NSColor.black.cgColor
        layer?.shadowOpacity = 0.3
        layer?.shadowRadius = 4
        layer?.shadowOffset = CGSize(width: 0, height: -1)
        setAccessibilityIdentifier("space.theme.grainDial")
        setAccessibilityRole(.slider)
        setAccessibilityLabel("Grain level")
        setAccessibilityValue(NSNumber(value: grainValue))
    }

    private func updateValue(with event: NSEvent) {
        let point = convert(event.locationInWindow, from: nil)
        let center = NSPoint(x: bounds.midX, y: bounds.midY)
        let deltaX = point.x - center.x
        let deltaY = point.y - center.y
        var degrees = atan2(deltaY, deltaX) * 180 / .pi
        if degrees < 0 {
            degrees += 360
        }

        let clockwiseAngleFromTop = (450 - degrees).truncatingRemainder(dividingBy: 360)
        let nextValue = value(forAngle: clockwiseAngleFromTop)
        guard abs(nextValue - grainValue) > 0.0001 else { return }
        grainValue = nextValue
        onGrainChanged?(nextValue)
    }

    private func value(forAngle angle: CGFloat) -> Double {
        let normalizedAngle = angle.truncatingRemainder(dividingBy: 360)
        let positiveAngle = normalizedAngle >= 0 ? normalizedAngle : normalizedAngle + 360
        return Self.clamp(Double(positiveAngle / 360))
    }

    private func point(onCircleWithCenter center: NSPoint, radius: CGFloat, angleDegrees: CGFloat) -> NSPoint {
        let radians = angleDegrees * .pi / 180
        return NSPoint(
            x: center.x + (cos(radians) * radius),
            y: center.y + (sin(radians) * radius)
        )
    }


    private static func clamp(_ value: Double) -> Double {
        min(max(value, 0.0), 1.0)
    }
}
