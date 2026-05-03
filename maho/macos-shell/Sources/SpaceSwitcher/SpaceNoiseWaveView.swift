import AppKit

final class SpaceNoiseWaveView: NSView {
    var grainLevel: Double = 0.0 {
        didSet {
            guard !isAnimating else { return }
            let clampedLevel = Self.clamp(grainLevel)
            if clampedLevel != grainLevel {
                grainLevel = clampedLevel
                return
            }
            presentationLevel = clampedLevel
            stopAnimationTimer()
            setAccessibilityValue(NSNumber(value: grainLevel))
            needsDisplay = true
        }
    }

    private var isAnimating = false
    private var animationTimer: Timer?
    private var animationStartDate: Date?
    private var animationDuration: TimeInterval = 0.24
    private var animationFromLevel: Double = 0.0
    private var animationToLevel: Double = 0.0
    private var presentationLevel: Double = 0.0

    override var intrinsicContentSize: NSSize {
        NSSize(width: 200, height: 50)
    }

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        commonInit()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    deinit {
        stopAnimationTimer()
    }

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)

        let drawingRect = bounds.insetBy(dx: 1, dy: 4)
        guard drawingRect.width > 0, drawingRect.height > 0 else { return }

        let pillPath = NSBezierPath(roundedRect: drawingRect, xRadius: drawingRect.height * 0.5, yRadius: drawingRect.height * 0.5)
        NSColor.white.withAlphaComponent(0.02).setFill()
        pillPath.fill()

        let level = CGFloat(presentationLevel)
        let amplitude = 18 * level
        let baseline = drawingRect.midY
        let cycles: CGFloat = 3.5
        let path = NSBezierPath()
        path.lineWidth = 4.5
        path.lineCapStyle = .round
        path.lineJoinStyle = .round

        let sampleCount = max(Int(drawingRect.width / 3), 36)
        for sampleIndex in 0...sampleCount {
            let progress = CGFloat(sampleIndex) / CGFloat(sampleCount)
            let x = drawingRect.minX + (drawingRect.width * progress)
            let angle = progress * cycles * 2 * .pi
            let y = baseline + (sin(angle) * amplitude)
            let point = NSPoint(x: x, y: y)
            if sampleIndex == 0 {
                path.move(to: point)
            } else {
                path.line(to: point)
            }
        }

        guard let highlightPath = path.copy() as? NSBezierPath else { return }
        let highlightTransform = NSAffineTransform()
        highlightTransform.translateX(by: 0, yBy: 0.5)
        highlightPath.transform(using: highlightTransform as AffineTransform)
        NSColor(white: 0.4, alpha: 0.3).setStroke()
        highlightPath.stroke()

        NSColor(white: 0.25, alpha: 0.6).setStroke()
        path.stroke()
    }

    func animateToLevel(_ level: Double) {
        let clampedLevel = Self.clamp(level)
        animationFromLevel = presentationLevel
        animationToLevel = clampedLevel
        isAnimating = true
        grainLevel = clampedLevel
        isAnimating = false
        animationStartDate = Date()
        startAnimationTimer()
    }

    private func commonInit() {
        translatesAutoresizingMaskIntoConstraints = false
        wantsLayer = true
        setAccessibilityIdentifier("space.theme.noiseWave")
        setAccessibilityRole(.progressIndicator)
        setAccessibilityLabel("Noise wave level")
        setAccessibilityValue(NSNumber(value: grainLevel))
    }

    private func startAnimationTimer() {
        stopAnimationTimer()
        let timer = Timer(timeInterval: 1.0 / 60.0, repeats: true) { [weak self] timer in
            self?.handleAnimationTick(timer)
        }
        animationTimer = timer
        RunLoop.main.add(timer, forMode: .common)
    }

    private func stopAnimationTimer() {
        animationTimer?.invalidate()
        animationTimer = nil
        animationStartDate = nil
    }

    private func handleAnimationTick(_ timer: Timer) {
        guard let animationStartDate else {
            stopAnimationTimer()
            return
        }

        let elapsed = Date().timeIntervalSince(animationStartDate)
        let rawProgress = min(max(elapsed / animationDuration, 0), 1)
        let easedProgress = 1 - pow(1 - rawProgress, 3)
        presentationLevel = animationFromLevel + ((animationToLevel - animationFromLevel) * easedProgress)
        needsDisplay = true

        if rawProgress >= 1 {
            presentationLevel = animationToLevel
            stopAnimationTimer()
            setAccessibilityValue(NSNumber(value: grainLevel))
            needsDisplay = true
        }
    }

    private static func clamp(_ value: Double) -> Double {
        min(max(value, 0.0), 1.0)
    }
}
