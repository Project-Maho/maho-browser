import AppKit

private final class SpaceDottedGridView: NSView {
    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        translatesAutoresizingMaskIntoConstraints = false
        wantsLayer = false
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)

        let dotColor = NSColor.white.withAlphaComponent(0.08)
        dotColor.setFill()

        let spacing: CGFloat = 20
        let dotDiameter: CGFloat = 1.8
        let radius = dotDiameter / 2

        let minX = Int(floor(dirtyRect.minX / spacing))
        let maxX = Int(ceil(dirtyRect.maxX / spacing))
        let minY = Int(floor(dirtyRect.minY / spacing))
        let maxY = Int(ceil(dirtyRect.maxY / spacing))

        for xIndex in minX...maxX {
            let x = CGFloat(xIndex) * spacing
            for yIndex in minY...maxY {
                let y = CGFloat(yIndex) * spacing
                let dotRect = NSRect(x: x - radius, y: y - radius, width: dotDiameter, height: dotDiameter)
                NSBezierPath(ovalIn: dotRect).fill()
            }
        }
    }
}

private final class SpaceInteractivePreviewCardView: NSView {
    var brightness: Double = 0.9 {
        didSet {
            let clampedBrightness = min(max(brightness, 0.3), 1.0)
            if abs(clampedBrightness - brightness) > 0.0001 {
                brightness = clampedBrightness
                return
            }
            cachedFieldImage = nil
            needsDisplay = true
        }
    }

    var grain: Double = 0.0 {
        didSet {
            let clampedGrain = min(max(grain, 0.0), 1.0)
            if abs(clampedGrain - grain) > 0.0001 {
                grain = clampedGrain
                return
            }
            needsDisplay = true
        }
    }

    var colorFieldRect: NSRect = .zero {
        didSet {
            if oldValue.integral != colorFieldRect.integral {
                needsDisplay = true
            }
        }
    }

    var pickerCursorPoint: NSPoint? {
        didSet {
            needsDisplay = true
        }
    }

    var gradientColors: [ThemeColor] = [] {
        didSet {
            updateGradientLayer()
            needsDisplay = true
        }
    }

    var gradientOpacity: Double = 1.0 {
        didSet {
            let clampedOpacity = min(max(gradientOpacity, 0.0), 1.0)
            if abs(clampedOpacity - gradientOpacity) > 0.0001 {
                gradientOpacity = clampedOpacity
                return
            }
            updateGradientLayerOpacity()
        }
    }

    var onColorPointChanged: ((Double, Double, NSPoint) -> Void)?

    private let gradientLayer = CAGradientLayer()
    private var cachedFieldImage: NSImage?
    private var cachedImageSize: NSSize = .zero
    private var cachedBrightness: Double = -1
    private var cachedNoiseImage: NSImage?
    private var cachedNoiseSize: NSSize = .zero

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        translatesAutoresizingMaskIntoConstraints = false
        wantsLayer = true
        configureGradientLayer()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func layout() {
        super.layout()
        gradientLayer.frame = bounds
        gradientLayer.cornerRadius = 16
    }

    override func viewDidChangeBackingProperties() {
        super.viewDidChangeBackingProperties()
        updateGradientLayerContentsScale()
    }

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)

        NSGraphicsContext.current?.saveGraphicsState()
        let roundedBounds = NSBezierPath(roundedRect: bounds, xRadius: 16, yRadius: 16)
        roundedBounds.addClip()

        updateCachedFieldImageIfNeeded()
        let fieldAlpha: CGFloat = gradientColors.isEmpty ? 1.0 : 0.42
        cachedFieldImage?.draw(in: bounds, from: .zero, operation: .sourceOver, fraction: fieldAlpha)

        if grain > 0.001 {
            let noiseImage = noiseImageForSize(bounds.size)
            noiseImage.draw(in: bounds, from: .zero, operation: .sourceOver, fraction: CGFloat(grain) * 0.25)
        }

        if let pickerCursorPoint {
            let cursorRect = NSRect(x: pickerCursorPoint.x - 6, y: pickerCursorPoint.y - 6, width: 12, height: 12)
            let cursorPath = NSBezierPath(ovalIn: cursorRect)
            cursorPath.lineWidth = 2
            NSColor.white.withAlphaComponent(0.92).setStroke()
            cursorPath.stroke()
        }
        NSGraphicsContext.current?.restoreGraphicsState()
    }

    override func mouseDown(with event: NSEvent) {
        handlePointerEvent(event)
    }

    override func mouseDragged(with event: NSEvent) {
        handlePointerEvent(event)
    }

    private func handlePointerEvent(_ event: NSEvent) {
        guard bounds.width > 0, bounds.height > 0 else { return }
        let location = convert(event.locationInWindow, from: nil)
        let interactiveRect = colorFieldRect.isEmpty ? bounds : colorFieldRect
        let clampedPoint = NSPoint(
            x: min(max(location.x, interactiveRect.minX), interactiveRect.maxX),
            y: min(max(location.y, interactiveRect.minY), interactiveRect.maxY)
        )

        let normalizedX = interactiveRect.width > 0 ? (clampedPoint.x - interactiveRect.minX) / interactiveRect.width : 0
        let normalizedY = interactiveRect.height > 0 ? (clampedPoint.y - interactiveRect.minY) / interactiveRect.height : 0
        let hue = min(max(Double(normalizedX), 0.0), 1.0)
        let saturation = 0.2 + (min(max(Double(normalizedY), 0.0), 1.0) * 0.8)

        pickerCursorPoint = clampedPoint
        onColorPointChanged?(hue, saturation, clampedPoint)
    }

    private func configureGradientLayer() {
        gradientLayer.cornerRadius = 16
        gradientLayer.masksToBounds = true
        gradientLayer.startPoint = CGPoint(x: 0.0, y: 0.0)
        gradientLayer.endPoint = CGPoint(x: 1.0, y: 1.0)
        gradientLayer.zPosition = -1
        updateGradientLayerContentsScale()
    }

    private func updateGradientLayer() {
        gradientLayer.frame = bounds
        gradientLayer.cornerRadius = 16

        guard !gradientColors.isEmpty else {
            gradientLayer.colors = nil
            gradientLayer.locations = nil
            gradientLayer.opacity = 0
            gradientLayer.removeFromSuperlayer()
            return
        }

        let resolvedColors = gradientColors.map { Self.nsColor(for: $0).cgColor }
        gradientLayer.colors = resolvedColors
        gradientLayer.locations = gradientLocations(for: resolvedColors.count)
        gradientLayer.opacity = Float(gradientOpacity)
        gradientLayer.isHidden = false

        if gradientLayer.superlayer == nil {
            layer?.insertSublayer(gradientLayer, at: 0)
        }
    }

    private func updateGradientLayerOpacity() {
        gradientLayer.opacity = Float(gradientOpacity)
    }

    private func gradientLocations(for count: Int) -> [NSNumber]? {
        guard count > 1 else { return [0.0, 1.0] }
        let step = 1.0 / Double(count - 1)
        return (0..<count).map { NSNumber(value: Double($0) * step) }
    }

    private func updateGradientLayerContentsScale() {
        gradientLayer.contentsScale = window?.backingScaleFactor ?? NSScreen.main?.backingScaleFactor ?? 2.0
    }

    private static func nsColor(for themeColor: ThemeColor) -> NSColor {
        NSColor(
            hue: CGFloat(themeColor.hue),
            saturation: CGFloat(themeColor.saturation),
            brightness: CGFloat(themeColor.brightness),
            alpha: 1.0
        )
    }

    private func updateCachedFieldImageIfNeeded() {
        let targetSize = bounds.integral.size
        guard targetSize.width > 0, targetSize.height > 0 else {
            cachedFieldImage = nil
            return
        }

        if let cachedFieldImage,
           cachedImageSize == targetSize,
           abs(cachedBrightness - brightness) < 0.0001 {
            self.cachedFieldImage = cachedFieldImage
            return
        }

        let width = max(Int(targetSize.width), 1)
        let height = max(Int(targetSize.height), 1)
        guard let bitmap = NSBitmapImageRep(
            bitmapDataPlanes: nil,
            pixelsWide: width,
            pixelsHigh: height,
            bitsPerSample: 8,
            samplesPerPixel: 4,
            hasAlpha: true,
            isPlanar: false,
            colorSpaceName: .deviceRGB,
            bytesPerRow: 0,
            bitsPerPixel: 0
        ) else {
            return
        }

        guard let data = bitmap.bitmapData else { return }
        let bytesPerRow = bitmap.bytesPerRow
        let br = CGFloat(brightness)

        for x in 0..<width {
            let hue = CGFloat(x) / CGFloat(max(width - 1, 1))
            for y in 0..<height {
                let saturationProgress = CGFloat(y) / CGFloat(max(height - 1, 1))
                let saturation = 0.2 + (saturationProgress * 0.8)
                let (r, g, b) = Self.hsbToRGB(hue, saturation, br)
                let offset = y * bytesPerRow + x * 4
                data[offset]     = UInt8(min(max(r * 255, 0), 255))
                data[offset + 1] = UInt8(min(max(g * 255, 0), 255))
                data[offset + 2] = UInt8(min(max(b * 255, 0), 255))
                data[offset + 3] = 255
            }
        }

        let image = NSImage(size: targetSize)
        image.addRepresentation(bitmap)
        cachedFieldImage = image
        cachedImageSize = targetSize
        cachedBrightness = brightness
    }

    private func noiseImageForSize(_ size: NSSize) -> NSImage {
        let targetSize = NSSize(width: ceil(size.width), height: ceil(size.height))
        if let cachedNoiseImage, cachedNoiseSize == targetSize {
            return cachedNoiseImage
        }

        let width = max(Int(targetSize.width), 1)
        let height = max(Int(targetSize.height), 1)
        guard let bitmap = NSBitmapImageRep(
            bitmapDataPlanes: nil,
            pixelsWide: width,
            pixelsHigh: height,
            bitsPerSample: 8,
            samplesPerPixel: 4,
            hasAlpha: true,
            isPlanar: false,
            colorSpaceName: .deviceRGB,
            bytesPerRow: 0,
            bitsPerPixel: 0
        ), let data = bitmap.bitmapData else {
            return NSImage(size: targetSize)
        }

        let bytesPerRow = bitmap.bytesPerRow
        for y in 0..<height {
            for x in 0..<width {
                let randomByte = UInt8(arc4random_uniform(256))
                let offset = y * bytesPerRow + x * 4
                data[offset] = randomByte
                data[offset + 1] = randomByte
                data[offset + 2] = randomByte
                data[offset + 3] = 255
            }
        }

        let image = NSImage(size: targetSize)
        image.addRepresentation(bitmap)
        cachedNoiseImage = image
        cachedNoiseSize = targetSize
        return image
    }

    private static func hsbToRGB(_ h: CGFloat, _ s: CGFloat, _ b: CGFloat) -> (CGFloat, CGFloat, CGFloat) {
        guard s > 0 else { return (b, b, b) }
        let hWrapped = h - floor(h)
        let h6 = hWrapped * 6
        let sector = Int(h6) % 6
        let f = h6 - floor(h6)
        let p = b * (1 - s)
        let q = b * (1 - s * f)
        let t = b * (1 - s * (1 - f))
        switch sector {
        case 0: return (b, t, p)
        case 1: return (q, b, p)
        case 2: return (p, b, t)
        case 3: return (p, q, b)
        case 4: return (t, p, b)
        default: return (b, p, q)
        }
    }
}

final class SpaceColorPreviewView: NSView, NSTextFieldDelegate {

    enum DisplayStyle {
        case standard
        case createSheetZen
    }

    // MARK: - Public API

    var onColorSelected: ((Int) -> Void)?
    var onModeSelected: ((Int) -> Void)?
    var onColorPicked: ((SpaceColor) -> Void)?
    var onGrainChanged: ((Double) -> Void)?
    var onBrightnessChanged: ((Double) -> Void)?
    var onThemeChanged: ((SpaceTheme) -> Void)?

    func configure(color: SpaceColor, name: String, icon: String?) {
        currentBrightness = Self.clampBrightness(color.brightness)
        currentGrain = Self.clampUnit(color.grain)
        currentTexture = currentGrain
        currentColor = SpaceColor(
            hue: Self.clampUnit(color.hue),
            saturation: Self.clampSaturation(color.saturation),
            brightness: currentBrightness,
            grain: currentGrain
        )

        nameLabel.stringValue = name
        iconLabel.stringValue = icon ?? ""
        iconLabel.isHidden = (icon == nil || icon?.isEmpty == true)

        previewCard.brightness = currentBrightness
        previewCard.grain = currentGrain
        previewCard.gradientOpacity = currentOpacity
        grainDialView.grainValue = currentGrain
        noiseWaveView.grainLevel = currentGrain
        updateHexField()
        updateIconCircleColor(for: currentColor)
        updatePickerCursorFromCurrentColor()
        updateGradientModeVisibility()
        updateGradientDots()
    }

    func configure(theme: SpaceTheme, name: String, icon: String?) {
        switch theme {
        case .solid(let color):
            isGradientMode = false
            configure(color: color, name: name, icon: icon)
        case .gradient(let colors, let harmony, let opacity, let texture):
            isGradientMode = true
            currentHarmony = harmony
            currentOpacity = Self.clampUnit(opacity)
            currentTexture = Self.clampUnit(texture)
            if let primary = colors.first(where: { $0.isPrimary }) ?? colors.first {
                configure(
                    color: SpaceColor(
                        hue: primary.hue,
                        saturation: primary.saturation,
                        brightness: primary.brightness,
                        grain: texture
                    ),
                    name: name,
                    icon: icon
                )
            }
        }
        updateGradientModeVisibility()
        updateGradientDots()
    }

    func setSelectedColorIndex(_ index: Int) {
        guard index >= 0, index < Self.presetColors.count else { return }
        selectedColorIndex = index
        updateSwatchSelection()
    }

    func setSelectedModeIndex(_ index: Int) {
        guard index >= 0, index < modeButtons.count else { return }
        selectedModeIndex = index
        updateModeSelection()
    }

    func setGrainLevel(_ grain: Double) {
        currentGrain = Self.clampUnit(grain)
        currentTexture = currentGrain
        previewCard.grain = currentGrain
        grainDialView.grainValue = currentGrain
        noiseWaveView.animateToLevel(currentGrain)
        updateGradientDots()
        emitCurrentTheme()
    }

    func setBrightnessLevel(_ brightness: Double) {
        currentBrightness = Self.clampBrightness(brightness)
        previewCard.brightness = currentBrightness
        updateHexField()
        updateIconCircleColor(for: currentColor)
        updateGradientDots()
        emitCurrentTheme()
    }

    // MARK: - Preset Colors

    private static var presetColors: [(title: String, color: SpaceColor)] {
        SpacePresentationPresets.colors
    }

    private static let harmonyOptions: [(harmony: ColorHarmony, title: String, accessibilityIdentifier: String)] = [
        (.complementary, "Comp", "space.theme.harmony.complementary"),
        (.analogous, "Analog", "space.theme.harmony.analogous"),
        (.splitComplementary, "Split", "space.theme.harmony.splitComplementary"),
        (.triadic, "Triad", "space.theme.harmony.triadic"),
        (.custom, "Custom", "space.theme.harmony.custom"),
    ]

    // MARK: - Private State

    private var currentColor = SpaceColor(hue: 0.58, saturation: 0.8, brightness: 0.9)
    private var currentBrightness: Double {
        get { currentColor.brightness }
        set {
            currentColor = SpaceColor(
                hue: currentColor.hue,
                saturation: currentColor.saturation,
                brightness: Self.clampBrightness(newValue),
                grain: currentColor.grain
            )
        }
    }

    private var currentGrain: Double {
        get { currentColor.grain }
        set {
            currentColor = SpaceColor(
                hue: currentColor.hue,
                saturation: currentColor.saturation,
                brightness: currentColor.brightness,
                grain: Self.clampUnit(newValue)
            )
        }
    }

    private var isGradientMode = false
    private var currentHarmony: ColorHarmony = .analogous
    private var currentOpacity: Double = 1.0
    private var currentTexture: Double = 0.0
    private var gradientDotLayers: [CALayer] = []
    private var selectedColorIndex = 0
    private var selectedModeIndex = 2
    private var modeButtons: [NSButton] = []
    private var harmonyButtons: [NSButton] = []
    private var swatchViews: [NSView] = []
    private var hasUserPickedColor = false
    private let displayStyle: DisplayStyle

    // MARK: - Subviews

    private let previewCard = SpaceInteractivePreviewCardView()
    private let dottedGridView = SpaceDottedGridView()

    private let modeStack = NSStackView()
    private let iconCircle = NSView()
    private let iconLabel = NSTextField(labelWithString: "")
    private let nameLabel = NSTextField(labelWithString: "")
    private let tabsCaption = NSTextField(labelWithString: "0 tabs")
    private let instructionLabel = NSTextField(labelWithString: "Tap to pick a color\nfor this space")
    private let swatchStack = NSStackView()
    private let harmonyStack = NSStackView()
    private let leftChevron = NSButton()
    private let rightChevron = NSButton()
    private let gradientToggleButton = NSButton()
    private let minusButton = NSButton()
    private let plusButton = NSButton()
    private let hexField = NSTextField()
    private let opacityLabel = NSTextField(labelWithString: "Opacity")
    private let opacitySlider = NSSlider(value: 1.0, minValue: 0.0, maxValue: 1.0, target: nil, action: nil)
    private let noiseWaveView = SpaceNoiseWaveView()
    private let grainDialView = SpaceGrainDialView()

    // MARK: - Init

    init(displayStyle: DisplayStyle = .standard) {
        self.displayStyle = displayStyle
        super.init(frame: .zero)
        setupUI()
    }

    override init(frame frameRect: NSRect) {
        self.displayStyle = .standard
        super.init(frame: frameRect)
        setupUI()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func layout() {
        super.layout()
        previewCard.colorFieldRect = colorFieldRectForCurrentLayout()
        updatePickerCursorFromCurrentColor()
        updateGradientDots()
        dottedGridView.needsDisplay = true
    }

    // MARK: - Setup

    private func setupUI() {
        translatesAutoresizingMaskIntoConstraints = false
        wantsLayer = true
        setAccessibilityIdentifier("space.theme.preview")

        applyViewAppearance()
        setupPreviewCard()
        setupModeChips()
        setupCenterContent()
        setupBrightnessButtons()
        setupHexInput()
        setupBottomControls()
        setupSwatchRow()
        setupGradientControls()
        updateGradientModeVisibility()
    }

    private func applyViewAppearance() {
        switch displayStyle {
        case .standard:
            layer?.backgroundColor = NSColor.clear.cgColor
        case .createSheetZen:
            layer?.backgroundColor = MahoAppColors.cardBackground.withAlphaComponent(0.08).cgColor
            layer?.cornerRadius = 26
            layer?.borderWidth = 1
            layer?.borderColor = MahoAppColors.contentBorder.withAlphaComponent(0.22).cgColor
            layer?.masksToBounds = true
        }
    }

    private func setupPreviewCard() {
        previewCard.translatesAutoresizingMaskIntoConstraints = false
        previewCard.wantsLayer = true
        previewCard.layer?.cornerRadius = displayStyle == .createSheetZen ? 28 : 16
        previewCard.layer?.masksToBounds = true
        previewCard.layer?.borderWidth = 1
        previewCard.layer?.borderColor = displayStyle == .createSheetZen
            ? MahoAppColors.contentBorder.withAlphaComponent(0.32).cgColor
            : MahoAppColors.cardBorder.cgColor
        previewCard.layer?.backgroundColor = displayStyle == .createSheetZen
            ? MahoAppColors.archivePanelBackground.withAlphaComponent(0.34).cgColor
            : NSColor.clear.cgColor
        previewCard.brightness = currentBrightness
        previewCard.grain = currentGrain
        previewCard.onColorPointChanged = { [weak self] hue, saturation, point in
            self?.handleColorPicked(hue: hue, saturation: saturation, cursorPoint: point)
        }
        addSubview(previewCard)

        dottedGridView.alphaValue = displayStyle == .createSheetZen ? 0.48 : 0.75
        previewCard.addSubview(dottedGridView)

        NSLayoutConstraint.activate([
            dottedGridView.topAnchor.constraint(equalTo: previewCard.topAnchor),
            dottedGridView.leadingAnchor.constraint(equalTo: previewCard.leadingAnchor),
            dottedGridView.trailingAnchor.constraint(equalTo: previewCard.trailingAnchor),
            dottedGridView.bottomAnchor.constraint(equalTo: previewCard.bottomAnchor),
            previewCard.topAnchor.constraint(equalTo: topAnchor, constant: displayStyle == .createSheetZen ? 10 : 0),
            previewCard.leadingAnchor.constraint(equalTo: leadingAnchor, constant: displayStyle == .createSheetZen ? 10 : 0),
            previewCard.trailingAnchor.constraint(equalTo: trailingAnchor, constant: displayStyle == .createSheetZen ? -10 : 0),
            previewCard.bottomAnchor.constraint(equalTo: bottomAnchor, constant: displayStyle == .createSheetZen ? -10 : 0),
        ])
    }

    private func setupModeChips() {
        modeStack.translatesAutoresizingMaskIntoConstraints = false
        modeStack.orientation = .horizontal
        modeStack.spacing = displayStyle == .createSheetZen ? 10 : 6
        modeStack.distribution = .fillEqually
        previewCard.addSubview(modeStack)

        let modes: [(String, String)] = [
            ("sun.max.fill", "Light"),
            ("moon.fill", "Dark"),
            ("sparkles", "Auto"),
        ]

        for (i, mode) in modes.enumerated() {
            let button = NSButton()
            button.translatesAutoresizingMaskIntoConstraints = false
            button.isBordered = false
            button.wantsLayer = true
            button.layer?.cornerRadius = displayStyle == .createSheetZen ? 12 : 8
            button.layer?.borderWidth = 1
            button.bezelStyle = .inline
            button.image = MahoIcon.imageForSFSymbol(mode.0, accessibilityDescription: mode.1)
            button.imagePosition = .imageLeading
            button.imageScaling = .scaleProportionallyDown
            button.title = displayStyle == .createSheetZen ? mode.1 : " \(mode.1)"
            button.font = .systemFont(ofSize: displayStyle == .createSheetZen ? 11.5 : 11, weight: .medium)
            button.contentTintColor = displayStyle == .createSheetZen
                ? MahoAppColors.primaryText.withAlphaComponent(0.94)
                : MahoAppColors.primaryText
            button.tag = i
            button.target = self
            button.action = #selector(modeChipTapped(_:))
            button.setAccessibilityIdentifier("space.theme.mode.\(i)")

            button.heightAnchor.constraint(equalToConstant: displayStyle == .createSheetZen ? 32 : 26).isActive = true
            modeButtons.append(button)
            modeStack.addArrangedSubview(button)
        }

        updateModeSelection()

        NSLayoutConstraint.activate([
            modeStack.topAnchor.constraint(equalTo: previewCard.topAnchor, constant: displayStyle == .createSheetZen ? 20 : 16),
            modeStack.leadingAnchor.constraint(equalTo: previewCard.leadingAnchor, constant: displayStyle == .createSheetZen ? 20 : 16),
            modeStack.trailingAnchor.constraint(equalTo: previewCard.trailingAnchor, constant: displayStyle == .createSheetZen ? -20 : -16),
        ])
    }

    private func setupCenterContent() {
        iconCircle.translatesAutoresizingMaskIntoConstraints = false
        iconCircle.wantsLayer = true
        iconCircle.layer?.cornerRadius = displayStyle == .createSheetZen ? 30 : 24
        iconCircle.layer?.borderWidth = displayStyle == .createSheetZen ? 1 : 0
        iconCircle.layer?.borderColor = displayStyle == .createSheetZen ? NSColor.white.withAlphaComponent(0.12).cgColor : nil
        iconCircle.layer?.masksToBounds = true
        previewCard.addSubview(iconCircle)
        updateIconCircleColor(for: currentColor)

        iconLabel.translatesAutoresizingMaskIntoConstraints = false
        iconLabel.font = .systemFont(ofSize: displayStyle == .createSheetZen ? 26 : 22)
        iconLabel.alignment = .center
        iconLabel.backgroundColor = .clear
        iconLabel.isBezeled = false
        iconLabel.isEditable = false
        iconLabel.isSelectable = false
        iconCircle.addSubview(iconLabel)

        nameLabel.translatesAutoresizingMaskIntoConstraints = false
        nameLabel.font = .systemFont(ofSize: displayStyle == .createSheetZen ? 18 : 15, weight: .semibold)
        nameLabel.textColor = MahoAppColors.primaryText
        nameLabel.alignment = .center
        nameLabel.lineBreakMode = .byTruncatingTail
        nameLabel.maximumNumberOfLines = 1
        previewCard.addSubview(nameLabel)

        tabsCaption.translatesAutoresizingMaskIntoConstraints = false
        tabsCaption.font = .systemFont(ofSize: displayStyle == .createSheetZen ? 12 : 11, weight: .medium)
        tabsCaption.textColor = MahoAppColors.secondaryText.withAlphaComponent(displayStyle == .createSheetZen ? 0.74 : 1.0)
        tabsCaption.alignment = .center
        previewCard.addSubview(tabsCaption)

        instructionLabel.translatesAutoresizingMaskIntoConstraints = false
        instructionLabel.font = .systemFont(ofSize: displayStyle == .createSheetZen ? 13 : 12, weight: .medium)
        instructionLabel.textColor = NSColor.white.withAlphaComponent(displayStyle == .createSheetZen ? 0.54 : 0.5)
        instructionLabel.alignment = .center
        instructionLabel.maximumNumberOfLines = 2
        instructionLabel.lineBreakMode = .byWordWrapping
        instructionLabel.stringValue = displayStyle == .createSheetZen
            ? "Tap the canvas to shape a muted tone\nthat feels right for this space"
            : "Tap to pick a color\nfor this space"
        previewCard.addSubview(instructionLabel)

        NSLayoutConstraint.activate([
            iconCircle.centerXAnchor.constraint(equalTo: previewCard.centerXAnchor),
            iconCircle.centerYAnchor.constraint(equalTo: previewCard.centerYAnchor, constant: displayStyle == .createSheetZen ? -52 : -42),
            iconCircle.widthAnchor.constraint(equalToConstant: displayStyle == .createSheetZen ? 60 : 48),
            iconCircle.heightAnchor.constraint(equalToConstant: displayStyle == .createSheetZen ? 60 : 48),

            iconLabel.centerXAnchor.constraint(equalTo: iconCircle.centerXAnchor),
            iconLabel.centerYAnchor.constraint(equalTo: iconCircle.centerYAnchor),

            nameLabel.topAnchor.constraint(equalTo: iconCircle.bottomAnchor, constant: displayStyle == .createSheetZen ? 14 : 10),
            nameLabel.centerXAnchor.constraint(equalTo: previewCard.centerXAnchor),
            nameLabel.leadingAnchor.constraint(greaterThanOrEqualTo: previewCard.leadingAnchor, constant: 16),
            nameLabel.trailingAnchor.constraint(lessThanOrEqualTo: previewCard.trailingAnchor, constant: -16),

            tabsCaption.topAnchor.constraint(equalTo: nameLabel.bottomAnchor, constant: 4),
            tabsCaption.centerXAnchor.constraint(equalTo: previewCard.centerXAnchor),

            instructionLabel.topAnchor.constraint(equalTo: tabsCaption.bottomAnchor, constant: displayStyle == .createSheetZen ? 22 : 18),
            instructionLabel.centerXAnchor.constraint(equalTo: previewCard.centerXAnchor),
            instructionLabel.leadingAnchor.constraint(greaterThanOrEqualTo: previewCard.leadingAnchor, constant: 32),
            instructionLabel.trailingAnchor.constraint(lessThanOrEqualTo: previewCard.trailingAnchor, constant: -32),
        ])
    }

    private func setupBrightnessButtons() {
        configureBrightnessButton(minusButton, symbolName: "minus", accessibilityIdentifier: "space.theme.luminosity.minus")
        minusButton.tag = -1
        previewCard.addSubview(minusButton)

        configureBrightnessButton(plusButton, symbolName: "plus", accessibilityIdentifier: "space.theme.luminosity.plus")
        plusButton.tag = 1
        previewCard.addSubview(plusButton)

        NSLayoutConstraint.activate([
            minusButton.centerXAnchor.constraint(equalTo: previewCard.centerXAnchor, constant: displayStyle == .createSheetZen ? -62 : -50),
            minusButton.centerYAnchor.constraint(equalTo: previewCard.centerYAnchor, constant: displayStyle == .createSheetZen ? 74 : 54),
            plusButton.centerXAnchor.constraint(equalTo: previewCard.centerXAnchor, constant: displayStyle == .createSheetZen ? 62 : 50),
            plusButton.centerYAnchor.constraint(equalTo: minusButton.centerYAnchor),
        ])
    }

    private func setupHexInput() {
        hexField.translatesAutoresizingMaskIntoConstraints = false
        hexField.font = .monospacedSystemFont(ofSize: displayStyle == .createSheetZen ? 12.5 : 12, weight: .medium)
        hexField.alignment = .center
        hexField.isBordered = false
        hexField.drawsBackground = false
        hexField.textColor = NSColor.white.withAlphaComponent(displayStyle == .createSheetZen ? 0.88 : 0.8)
        hexField.placeholderString = "#FFFFFF"
        hexField.focusRingType = .none
        hexField.delegate = self
        hexField.setAccessibilityIdentifier("space.theme.hexInput")
        previewCard.addSubview(hexField)
        updateHexField()

        NSLayoutConstraint.activate([
            hexField.centerXAnchor.constraint(equalTo: previewCard.centerXAnchor),
            hexField.topAnchor.constraint(equalTo: minusButton.bottomAnchor, constant: displayStyle == .createSheetZen ? 12 : 10),
            hexField.widthAnchor.constraint(equalToConstant: displayStyle == .createSheetZen ? 102 : 90),
            hexField.heightAnchor.constraint(equalToConstant: 24),
        ])
    }

    private func setupBottomControls() {
        previewCard.addSubview(noiseWaveView)
        previewCard.addSubview(grainDialView)
        previewCard.addSubview(opacityLabel)
        previewCard.addSubview(opacitySlider)
        grainDialView.setAccessibilityIdentifier("space.theme.grainDial")
        noiseWaveView.setAccessibilityIdentifier("space.theme.noiseWave")

        opacityLabel.translatesAutoresizingMaskIntoConstraints = false
        opacityLabel.font = .systemFont(ofSize: displayStyle == .createSheetZen ? 11 : 10.5, weight: .medium)
        opacityLabel.textColor = MahoAppColors.secondaryText.withAlphaComponent(displayStyle == .createSheetZen ? 0.92 : 1.0)

        opacitySlider.translatesAutoresizingMaskIntoConstraints = false
        opacitySlider.minValue = 0.0
        opacitySlider.maxValue = 1.0
        opacitySlider.doubleValue = currentOpacity
        opacitySlider.controlSize = .small
        opacitySlider.isContinuous = true
        opacitySlider.target = self
        opacitySlider.action = #selector(opacitySliderChanged(_:))
        opacitySlider.setAccessibilityIdentifier("space.theme.opacitySlider")

        grainDialView.onGrainChanged = { [weak self] grain in
            guard let self = self else { return }
            self.currentGrain = Self.clampUnit(grain)
            self.currentTexture = self.currentGrain
            self.previewCard.grain = self.currentGrain
            self.noiseWaveView.animateToLevel(self.currentGrain)
            self.onGrainChanged?(self.currentGrain)
            self.emitCurrentTheme()
        }

        NSLayoutConstraint.activate([
            grainDialView.trailingAnchor.constraint(equalTo: previewCard.trailingAnchor, constant: displayStyle == .createSheetZen ? -24 : -16),
            grainDialView.bottomAnchor.constraint(equalTo: previewCard.bottomAnchor, constant: displayStyle == .createSheetZen ? -86 : -60),
            grainDialView.widthAnchor.constraint(equalToConstant: 80),
            grainDialView.heightAnchor.constraint(equalToConstant: 80),

            noiseWaveView.leadingAnchor.constraint(equalTo: previewCard.leadingAnchor, constant: displayStyle == .createSheetZen ? 24 : 16),
            noiseWaveView.trailingAnchor.constraint(equalTo: grainDialView.leadingAnchor, constant: -16),
            noiseWaveView.centerYAnchor.constraint(equalTo: grainDialView.centerYAnchor),
            noiseWaveView.heightAnchor.constraint(equalToConstant: displayStyle == .createSheetZen ? 56 : 50),

            opacityLabel.leadingAnchor.constraint(equalTo: previewCard.leadingAnchor, constant: displayStyle == .createSheetZen ? 24 : 16),
            opacityLabel.centerYAnchor.constraint(equalTo: opacitySlider.centerYAnchor),

            opacitySlider.leadingAnchor.constraint(equalTo: opacityLabel.trailingAnchor, constant: 10),
            opacitySlider.trailingAnchor.constraint(equalTo: grainDialView.leadingAnchor, constant: -16),
            opacitySlider.bottomAnchor.constraint(equalTo: previewCard.bottomAnchor, constant: displayStyle == .createSheetZen ? -104 : -78),
        ])
    }

    private func setupSwatchRow() {
        configureChevron(leftChevron, symbolName: "chevron.left", tag: -1)
        previewCard.addSubview(leftChevron)
        leftChevron.setAccessibilityIdentifier("space.theme.chevron.left")

        configureChevron(rightChevron, symbolName: "chevron.right", tag: 1)
        previewCard.addSubview(rightChevron)
        rightChevron.setAccessibilityIdentifier("space.theme.chevron.right")

        swatchStack.translatesAutoresizingMaskIntoConstraints = false
        swatchStack.orientation = .horizontal
        swatchStack.spacing = displayStyle == .createSheetZen ? 10 : 8
        swatchStack.alignment = .centerY
        previewCard.addSubview(swatchStack)

        configureGradientToggleButton()
        previewCard.addSubview(gradientToggleButton)

        for (i, preset) in Self.presetColors.enumerated() {
            let swatch = NSView()
            swatch.translatesAutoresizingMaskIntoConstraints = false
            swatch.wantsLayer = true
            swatch.layer?.cornerRadius = displayStyle == .createSheetZen ? 11 : 10
            swatch.layer?.masksToBounds = true
            swatch.setAccessibilityIdentifier("space.theme.swatch.\(i)")

            swatch.layer?.backgroundColor = nsColor(for: preset.color).cgColor

            let clickGesture = NSClickGestureRecognizer(target: self, action: #selector(swatchTapped(_:)))
            swatch.addGestureRecognizer(clickGesture)

            NSLayoutConstraint.activate([
                swatch.widthAnchor.constraint(equalToConstant: displayStyle == .createSheetZen ? 22 : 20),
                swatch.heightAnchor.constraint(equalToConstant: displayStyle == .createSheetZen ? 22 : 20),
            ])

            swatchViews.append(swatch)
            swatchStack.addArrangedSubview(swatch)
        }

        updateSwatchSelection()

        NSLayoutConstraint.activate([
            swatchStack.centerXAnchor.constraint(equalTo: previewCard.centerXAnchor),
            swatchStack.bottomAnchor.constraint(equalTo: previewCard.bottomAnchor, constant: displayStyle == .createSheetZen ? -28 : -20),

            leftChevron.centerYAnchor.constraint(equalTo: swatchStack.centerYAnchor),
            leftChevron.trailingAnchor.constraint(equalTo: swatchStack.leadingAnchor, constant: -8),

            rightChevron.centerYAnchor.constraint(equalTo: swatchStack.centerYAnchor),
            rightChevron.leadingAnchor.constraint(equalTo: swatchStack.trailingAnchor, constant: 8),

            gradientToggleButton.centerYAnchor.constraint(equalTo: swatchStack.centerYAnchor),
            gradientToggleButton.trailingAnchor.constraint(equalTo: previewCard.trailingAnchor, constant: displayStyle == .createSheetZen ? -24 : -16),
            gradientToggleButton.heightAnchor.constraint(equalToConstant: displayStyle == .createSheetZen ? 28 : 24),
        ])
    }

    private func setupGradientControls() {
        harmonyStack.translatesAutoresizingMaskIntoConstraints = false
        harmonyStack.orientation = .horizontal
        harmonyStack.spacing = displayStyle == .createSheetZen ? 8 : 6
        harmonyStack.distribution = .fillEqually
        harmonyStack.alignment = .centerY
        previewCard.addSubview(harmonyStack)

        for (index, option) in Self.harmonyOptions.enumerated() {
            let button = NSButton(title: option.title, target: self, action: #selector(harmonyButtonTapped(_:)))
            button.translatesAutoresizingMaskIntoConstraints = false
            button.isBordered = false
            button.wantsLayer = true
            button.layer?.cornerRadius = displayStyle == .createSheetZen ? 10 : 7
            button.layer?.borderWidth = 1
            button.bezelStyle = .inline
            button.font = .systemFont(ofSize: displayStyle == .createSheetZen ? 11 : 10.5, weight: .medium)
            button.contentTintColor = MahoAppColors.primaryText
            button.tag = index
            button.setAccessibilityIdentifier(option.accessibilityIdentifier)
            button.heightAnchor.constraint(equalToConstant: displayStyle == .createSheetZen ? 28 : 24).isActive = true
            harmonyButtons.append(button)
            harmonyStack.addArrangedSubview(button)
        }

        NSLayoutConstraint.activate([
            harmonyStack.leadingAnchor.constraint(equalTo: previewCard.leadingAnchor, constant: displayStyle == .createSheetZen ? 24 : 16),
            harmonyStack.trailingAnchor.constraint(equalTo: grainDialView.leadingAnchor, constant: -16),
            harmonyStack.bottomAnchor.constraint(equalTo: previewCard.bottomAnchor, constant: displayStyle == .createSheetZen ? -66 : -48),
        ])

        updateHarmonySelection()
    }

    private func configureChevron(_ button: NSButton, symbolName: String, tag: Int) {
        button.translatesAutoresizingMaskIntoConstraints = false
        button.isBordered = false
        button.bezelStyle = .inline
        button.image = MahoIcon.imageForSFSymbol(symbolName)
        button.imageScaling = .scaleProportionallyDown
        button.contentTintColor = MahoAppColors.secondaryText
        button.tag = tag
        button.target = self
        button.action = #selector(chevronTapped(_:))
        NSLayoutConstraint.activate([
            button.widthAnchor.constraint(equalToConstant: displayStyle == .createSheetZen ? 22 : 20),
            button.heightAnchor.constraint(equalToConstant: displayStyle == .createSheetZen ? 22 : 20),
        ])
    }

    private func configureBrightnessButton(_ button: NSButton, symbolName: String, accessibilityIdentifier: String) {
        button.translatesAutoresizingMaskIntoConstraints = false
        button.isBordered = false
        button.bezelStyle = .inline
        let pointSize: CGFloat = displayStyle == .createSheetZen ? 15 : 14
        button.image = MahoIcon.imageForSFSymbol(symbolName, pointSize: pointSize)
        button.imageScaling = .scaleProportionallyDown
        button.contentTintColor = NSColor.white.withAlphaComponent(displayStyle == .createSheetZen ? 0.62 : 0.5)
        button.target = self
        button.action = #selector(brightnessButtonTapped(_:))
        button.setAccessibilityIdentifier(accessibilityIdentifier)

        NSLayoutConstraint.activate([
            button.widthAnchor.constraint(equalToConstant: displayStyle == .createSheetZen ? 24 : 20),
            button.heightAnchor.constraint(equalToConstant: displayStyle == .createSheetZen ? 24 : 20),
        ])
    }

    private func configureGradientToggleButton() {
        gradientToggleButton.translatesAutoresizingMaskIntoConstraints = false
        gradientToggleButton.setButtonType(.toggle)
        gradientToggleButton.isBordered = false
        gradientToggleButton.bezelStyle = .inline
        gradientToggleButton.wantsLayer = true
        gradientToggleButton.layer?.cornerRadius = displayStyle == .createSheetZen ? 10 : 7
        gradientToggleButton.layer?.borderWidth = 1
        gradientToggleButton.title = "Gradient"
        gradientToggleButton.font = .systemFont(ofSize: displayStyle == .createSheetZen ? 11 : 10.5, weight: .medium)
        gradientToggleButton.contentTintColor = MahoAppColors.primaryText
        gradientToggleButton.image = MahoIcon.imageForSFSymbol("circle.lefthalf.filled", accessibilityDescription: "Gradient")
        gradientToggleButton.imagePosition = .imageLeading
        gradientToggleButton.imageScaling = .scaleProportionallyDown
        gradientToggleButton.target = self
        gradientToggleButton.action = #selector(gradientToggleTapped(_:))
        gradientToggleButton.setAccessibilityIdentifier("space.theme.gradientToggle")
        updateGradientToggleSelection()
    }

    // MARK: - Actions

    @objc private func modeChipTapped(_ sender: NSButton) {
        selectedModeIndex = sender.tag
        updateModeSelection()
        onModeSelected?(sender.tag)
    }

    @objc private func swatchTapped(_ sender: NSClickGestureRecognizer) {
        guard let view = sender.view,
              let index = swatchViews.firstIndex(of: view),
              index < Self.presetColors.count else {
            return
        }

        selectedColorIndex = index
        updateSwatchSelection()
        applyPreset(Self.presetColors[index].color, selectedIndex: index)
    }

    @objc private func chevronTapped(_ sender: NSButton) {
        let direction = sender.tag
        var newIndex = selectedColorIndex + direction
        if newIndex < 0 { newIndex = Self.presetColors.count - 1 }
        if newIndex >= Self.presetColors.count { newIndex = 0 }

        selectedColorIndex = newIndex
        updateSwatchSelection()
        applyPreset(Self.presetColors[newIndex].color, selectedIndex: newIndex)
    }

    @objc private func brightnessButtonTapped(_ sender: NSButton) {
        let delta = Double(sender.tag) * 0.05
        let nextBrightness = Self.clampBrightness(currentBrightness + delta)
        guard abs(nextBrightness - currentBrightness) > 0.0001 else { return }

        fadeInstructionLabelIfNeeded()
        currentBrightness = nextBrightness
        previewCard.brightness = currentBrightness
        updateHexField()
        updateIconCircleColor(for: currentColor)
        updateGradientDots()
        onBrightnessChanged?(currentBrightness)
        onColorPicked?(currentColor)
        emitCurrentTheme()
    }

    @objc private func gradientToggleTapped(_ sender: NSButton) {
        isGradientMode = sender.state == .on
        updateGradientModeVisibility()
        emitCurrentTheme()
    }

    @objc private func harmonyButtonTapped(_ sender: NSButton) {
        guard sender.tag >= 0, sender.tag < Self.harmonyOptions.count else { return }
        currentHarmony = Self.harmonyOptions[sender.tag].harmony
        updateHarmonySelection()
        updateGradientDots()
        emitCurrentTheme()
    }

    @objc private func opacitySliderChanged(_ sender: NSSlider) {
        currentOpacity = Self.clampUnit(sender.doubleValue)
        sender.doubleValue = currentOpacity
        previewCard.gradientOpacity = currentOpacity
        emitCurrentTheme()
    }

    func controlTextDidEndEditing(_ obj: Notification) {
        guard let textField = obj.object as? NSTextField, textField === hexField else { return }

        let hex = textField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let (h, s, b) = Self.hexToHSB(hex) else {
            updateHexField()
            return
        }

        fadeInstructionLabelIfNeeded()
        currentColor = SpaceColor(
            hue: Self.clampUnit(h),
            saturation: Self.clampSaturation(s),
            brightness: Self.clampBrightness(b),
            grain: currentGrain
        )
        selectedColorIndex = -1
        updateSwatchSelection()
        updatePickerCursorFromCurrentColor()
        previewCard.brightness = currentBrightness
        updateHexField()
        updateIconCircleColor(for: currentColor)
        updateGradientDots()
        onColorPicked?(currentColor)
        emitCurrentTheme()
    }

    // MARK: - Updates

    private func handleColorPicked(hue: Double, saturation: Double, cursorPoint: NSPoint) {
        fadeInstructionLabelIfNeeded()
        currentColor = SpaceColor(
            hue: Self.clampUnit(hue),
            saturation: Self.clampSaturation(saturation),
            brightness: currentBrightness,
            grain: currentGrain
        )
        selectedColorIndex = -1
        updateSwatchSelection()
        previewCard.pickerCursorPoint = cursorPoint
        updateHexField()
        updateIconCircleColor(for: currentColor)
        updateGradientDots()
        onColorPicked?(currentColor)
        emitCurrentTheme()
    }

    private func applyPreset(_ presetColor: SpaceColor, selectedIndex: Int) {
        fadeInstructionLabelIfNeeded()
        currentBrightness = Self.clampBrightness(presetColor.brightness)
        currentColor = SpaceColor(
            hue: Self.clampUnit(presetColor.hue),
            saturation: Self.clampSaturation(presetColor.saturation),
            brightness: currentBrightness,
            grain: currentGrain
        )
        previewCard.brightness = currentBrightness
        previewCard.grain = currentGrain
        updateHexField()
        updateIconCircleColor(for: currentColor)
        updatePickerCursorFromCurrentColor()
        updateGradientDots()
        onColorSelected?(selectedIndex)
        onColorPicked?(currentColor)
        emitCurrentTheme()
    }

    private func updateIconCircleColor(for color: SpaceColor) {
        let circleColor = NSColor(
            hue: CGFloat(color.hue),
            saturation: CGFloat(color.saturation * 0.55),
            brightness: CGFloat(min(color.brightness + 0.08, 1.0)),
            alpha: 0.38
        )
        iconCircle.layer?.backgroundColor = circleColor.cgColor
    }

    private func updateSwatchSelection() {
        for (i, swatch) in swatchViews.enumerated() {
            if i == selectedColorIndex {
                swatch.layer?.borderWidth = displayStyle == .createSheetZen ? 2.5 : 2
                swatch.layer?.borderColor = NSColor.white.withAlphaComponent(displayStyle == .createSheetZen ? 0.95 : 1.0).cgColor
            } else {
                swatch.layer?.borderWidth = displayStyle == .createSheetZen ? 1 : 0
                swatch.layer?.borderColor = displayStyle == .createSheetZen
                    ? NSColor.white.withAlphaComponent(0.08).cgColor
                    : nil
            }
        }
    }

    private func updateModeSelection() {
        for (i, button) in modeButtons.enumerated() {
            let isSelected = i == selectedModeIndex
            button.layer?.backgroundColor = isSelected
                ? MahoAppColors.spaceChipSelected.withAlphaComponent(displayStyle == .createSheetZen ? 0.92 : 1.0).cgColor
                : (displayStyle == .createSheetZen
                    ? MahoAppColors.searchFieldBackground.withAlphaComponent(0.16).cgColor
                    : NSColor.clear.cgColor)
            button.layer?.borderColor = isSelected
                ? MahoAppColors.spaceChipSelected.withAlphaComponent(0.9).cgColor
                : MahoAppColors.spaceChipIdleBorder.cgColor
        }
    }

    private func updateGradientToggleSelection() {
        gradientToggleButton.state = isGradientMode ? .on : .off
        gradientToggleButton.layer?.backgroundColor = isGradientMode
            ? MahoAppColors.spaceChipSelected.cgColor
            : (displayStyle == .createSheetZen
                ? MahoAppColors.searchFieldBackground.withAlphaComponent(0.16).cgColor
                : NSColor.clear.cgColor)
        gradientToggleButton.layer?.borderColor = isGradientMode
            ? MahoAppColors.spaceChipSelected.withAlphaComponent(0.9).cgColor
            : MahoAppColors.spaceChipIdleBorder.cgColor
    }

    private func updateHarmonySelection() {
        for (index, button) in harmonyButtons.enumerated() {
            let harmony = Self.harmonyOptions[index].harmony
            let isSelected = harmony == currentHarmony || (harmony == .analogous && currentHarmony == .singleAnalogous)
            button.layer?.backgroundColor = isSelected
                ? MahoAppColors.spaceChipSelected.cgColor
                : (displayStyle == .createSheetZen
                    ? MahoAppColors.searchFieldBackground.withAlphaComponent(0.14).cgColor
                    : NSColor.clear.cgColor)
            button.layer?.borderColor = isSelected
                ? MahoAppColors.spaceChipSelected.withAlphaComponent(0.9).cgColor
                : MahoAppColors.spaceChipIdleBorder.cgColor
        }
    }

    private func updatePickerCursorFromCurrentColor() {
        let fieldRect = previewCard.colorFieldRect
        guard !fieldRect.isEmpty else { return }
        previewCard.pickerCursorPoint = pointForColorField(hue: currentColor.hue, saturation: currentColor.saturation, in: fieldRect)
    }

    private func updateGradientModeVisibility() {
        updateGradientToggleSelection()
        updateHarmonySelection()
        harmonyStack.isHidden = !isGradientMode
        opacityLabel.isHidden = !isGradientMode
        opacitySlider.isHidden = !isGradientMode
        noiseWaveView.isHidden = isGradientMode
        opacitySlider.doubleValue = currentOpacity
        previewCard.colorFieldRect = colorFieldRectForCurrentLayout()
        updatePickerCursorFromCurrentColor()
        updateGradientDots()
    }

    private func updateGradientDots() {
        gradientDotLayers.forEach { $0.removeFromSuperlayer() }
        gradientDotLayers.removeAll()

        guard isGradientMode,
              let hostLayer = previewCard.layer else {
            previewCard.gradientColors = []
            return
        }

        let colors = computeHarmonyColors(primary: currentColor)
        previewCard.gradientColors = colors
        previewCard.gradientOpacity = currentOpacity

        let fieldRect = previewCard.colorFieldRect
        guard !fieldRect.isEmpty else { return }

        for color in colors {
            let point = pointForColorField(hue: color.hue, saturation: color.saturation, in: fieldRect)
            let diameter: CGFloat = color.isPrimary ? 8 : 6
            let borderWidth: CGFloat = color.isPrimary ? 2 : 1
            let layer = CALayer()
            layer.frame = NSRect(x: point.x - (diameter / 2), y: point.y - (diameter / 2), width: diameter, height: diameter)
            layer.cornerRadius = diameter / 2
            layer.backgroundColor = NSColor(
                hue: CGFloat(color.hue),
                saturation: CGFloat(color.saturation),
                brightness: CGFloat(color.brightness),
                alpha: 1.0
            ).cgColor
            layer.borderColor = NSColor.white.withAlphaComponent(0.96).cgColor
            layer.borderWidth = borderWidth
            layer.shadowColor = NSColor.black.withAlphaComponent(0.28).cgColor
            layer.shadowOpacity = 1.0
            layer.shadowRadius = 2
            layer.shadowOffset = .zero
            layer.zPosition = 8
            hostLayer.addSublayer(layer)
            gradientDotLayers.append(layer)
        }
    }

    private func emitCurrentTheme() {
        if isGradientMode {
            let colors = computeHarmonyColors(primary: currentColor)
            let theme = SpaceTheme.gradient(
                gradientColors: colors,
                harmony: currentHarmony,
                opacity: currentOpacity,
                texture: currentTexture
            )
            onThemeChanged?(theme)
            return
        }

        onThemeChanged?(.solid(color: currentColor))
    }

    private func computeHarmonyColors(primary: SpaceColor) -> [ThemeColor] {
        let baseHue = primary.hue
        let offsets: [Double]
        switch currentHarmony {
        case .complementary:
            offsets = [180.0 / 360.0]
        case .singleAnalogous:
            offsets = [310.0 / 360.0]
        case .splitComplementary:
            offsets = [150.0 / 360.0, 210.0 / 360.0]
        case .analogous:
            offsets = [50.0 / 360.0, 310.0 / 360.0]
        case .triadic:
            offsets = [120.0 / 360.0, 240.0 / 360.0]
        case .custom:
            offsets = []
        }

        var colors = [ThemeColor(
            hue: baseHue,
            saturation: primary.saturation,
            brightness: primary.brightness,
            isPrimary: true
        )]

        for offset in offsets {
            let derivedHue = (baseHue + offset).truncatingRemainder(dividingBy: 1.0)
            colors.append(
                ThemeColor(
                    hue: derivedHue < 0 ? derivedHue + 1.0 : derivedHue,
                    saturation: primary.saturation,
                    brightness: primary.brightness
                )
            )
        }
        return colors
    }

    private func pointForColorField(hue: Double, saturation: Double, in fieldRect: NSRect) -> NSPoint {
        let clampedHue = Self.clampUnit(hue)
        let saturationProgress = (Self.clampSaturation(saturation) - 0.2) / 0.8
        return NSPoint(
            x: fieldRect.minX + (fieldRect.width * CGFloat(clampedHue)),
            y: fieldRect.minY + (fieldRect.height * CGFloat(min(max(saturationProgress, 0.0), 1.0)))
        )
    }

    private func updateHexField() {
        hexField.stringValue = Self.hsbToHex(h: currentColor.hue, s: currentColor.saturation, b: currentBrightness)
    }

    private func fadeInstructionLabelIfNeeded() {
        guard !hasUserPickedColor else { return }
        hasUserPickedColor = true

        if window != nil {
            NSAnimationContext.runAnimationGroup { context in
                context.duration = 0.18
                instructionLabel.animator().alphaValue = 0.0
            } completionHandler: { [weak self] in
                self?.instructionLabel.isHidden = true
            }
        } else {
            instructionLabel.alphaValue = 0.0
            instructionLabel.isHidden = true
        }
    }

    private func colorFieldRectForCurrentLayout() -> NSRect {
        let horizontalInset: CGFloat = displayStyle == .createSheetZen ? 18 : 14
        let topInset: CGFloat = displayStyle == .createSheetZen ? 66 : 54
        let bottomInset: CGFloat
        if displayStyle == .createSheetZen {
            bottomInset = isGradientMode ? 184 : 136
        } else {
            bottomInset = isGradientMode ? 158 : 110
        }
        return NSRect(
            x: horizontalInset,
            y: bottomInset,
            width: max(previewCard.bounds.width - (horizontalInset * 2), 1),
            height: max(previewCard.bounds.height - topInset - bottomInset, 1)
        )
    }

    private func nsColor(for color: SpaceColor) -> NSColor {
        NSColor(
            hue: CGFloat(color.hue),
            saturation: CGFloat(color.saturation),
            brightness: CGFloat(color.brightness),
            alpha: 1.0
        )
    }

    private static func clampUnit(_ value: Double) -> Double {
        min(max(value, 0.0), 1.0)
    }

    private static func clampBrightness(_ value: Double) -> Double {
        min(max(value, 0.3), 1.0)
    }

    private static func clampSaturation(_ value: Double) -> Double {
        min(max(value, 0.2), 1.0)
    }

    private static func hsbToHex(h: Double, s: Double, b: Double) -> String {
        let nsColor = NSColor(hue: h, saturation: s, brightness: b, alpha: 1.0)
        guard let rgb = nsColor.usingColorSpace(.sRGB) else { return "#000000" }
        let r = Int(round(rgb.redComponent * 255))
        let g = Int(round(rgb.greenComponent * 255))
        let blue = Int(round(rgb.blueComponent * 255))
        return String(format: "#%02X%02X%02X", r, g, blue)
    }

    private static func hexToHSB(_ hex: String) -> (h: Double, s: Double, b: Double)? {
        var str = hex.trimmingCharacters(in: .whitespacesAndNewlines)
        if str.hasPrefix("#") { str.removeFirst() }
        guard str.count == 6, let val = UInt64(str, radix: 16) else { return nil }
        let r = CGFloat((val >> 16) & 0xFF) / 255.0
        let g = CGFloat((val >> 8) & 0xFF) / 255.0
        let b = CGFloat(val & 0xFF) / 255.0
        let nsColor = NSColor(srgbRed: r, green: g, blue: b, alpha: 1.0)
        return (
            Double(nsColor.hueComponent),
            Double(nsColor.saturationComponent),
            Double(nsColor.brightnessComponent)
        )
    }
}
