import AppKit

final class EaselCanvasView: NSView {
    var items: [CanvasItem] = [] {
        didSet { needsDisplay = true }
    }
    var scale: CGFloat = 1.0 {
        didSet { needsDisplay = true }
    }
    var offset: NSPoint = .zero {
        didSet { needsDisplay = true }
    }
    var selectedItemIndex: Int? = nil {
        didSet { needsDisplay = true }
    }

    private var dragStart: NSPoint = .zero
    private var itemDragOriginalRect: Rect?

    override var isFlipped: Bool { true }

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)

        guard let ctx = NSGraphicsContext.current?.cgContext else { return }
        ctx.saveGState()

        ctx.translateBy(x: offset.x, y: offset.y)
        ctx.scaleBy(x: scale, y: scale)

        for (index, item) in items.enumerated() {
            let isSelected = index == selectedItemIndex
            switch item {
            case .note(let content, let rect):
                drawNote(ctx: ctx, content: content, rect: rect, selected: isSelected)
            case .image(let imageData, let rect):
                drawImage(ctx: ctx, imageData: imageData, rect: rect, selected: isSelected)
            case .webEmbed(let url, let rect):
                drawWebEmbed(ctx: ctx, url: url, rect: rect, selected: isSelected)
            case .drawing(let paths, let rect):
                drawPaths(ctx: ctx, paths: paths, rect: rect, selected: isSelected)
            }
        }

        ctx.restoreGState()
    }

    private func nsRect(from rect: Rect) -> NSRect {
        return NSRect(
            x: rect.origin.x,
            y: rect.origin.y,
            width: rect.size.width,
            height: rect.size.height
        )
    }

    private func drawNote(ctx: CGContext, content: String, rect: Rect, selected: Bool) {
        let r = nsRect(from: rect)
        ctx.setFillColor(NSColor.systemYellow.withAlphaComponent(0.3).cgColor)
        ctx.fill(r)
        if selected {
            ctx.setStrokeColor(NSColor.controlAccentColor.cgColor)
            ctx.setLineWidth(2)
            ctx.stroke(r)
        } else {
            ctx.setStrokeColor(NSColor.systemYellow.cgColor)
            ctx.setLineWidth(1)
            ctx.stroke(r)
        }
        let attrs: [NSAttributedString.Key: Any] = [
            .font: NSFont.systemFont(ofSize: 13),
            .foregroundColor: NSColor.labelColor,
        ]
        let textRect = r.insetBy(dx: 6, dy: 6)
        (content as NSString).draw(in: textRect, withAttributes: attrs)
    }

    private func drawImage(ctx: CGContext, imageData: ImageData, rect: Rect, selected: Bool) {
        let r = nsRect(from: rect)
        let data = Data(imageData.data)
        if let nsImage = NSImage(data: data), let cgImage = nsImage.cgImage(forProposedRect: nil, context: nil, hints: nil) {
            ctx.draw(cgImage, in: r)
        } else {
            ctx.setFillColor(NSColor.systemGray.withAlphaComponent(0.2).cgColor)
            ctx.fill(r)
            let attrs: [NSAttributedString.Key: Any] = [
                .font: NSFont.systemFont(ofSize: 11),
                .foregroundColor: NSColor.secondaryLabelColor,
            ]
            ("Image" as NSString).draw(in: r.insetBy(dx: 4, dy: 4), withAttributes: attrs)
        }
        if selected {
            ctx.setStrokeColor(NSColor.controlAccentColor.cgColor)
            ctx.setLineWidth(2)
            ctx.stroke(r)
        }
    }

    private func drawWebEmbed(ctx: CGContext, url: MahoUrl, rect: Rect, selected: Bool) {
        let r = nsRect(from: rect)
        ctx.setFillColor(NSColor.controlBackgroundColor.cgColor)
        ctx.fill(r)
        let borderColor = selected ? NSColor.controlAccentColor : NSColor.separatorColor
        ctx.setStrokeColor(borderColor.cgColor)
        ctx.setLineWidth(selected ? 2 : 1)
        ctx.stroke(r)
        let attrs: [NSAttributedString.Key: Any] = [
            .font: NSFont.systemFont(ofSize: 11),
            .foregroundColor: NSColor.linkColor,
        ]
        (url.value as NSString).draw(in: r.insetBy(dx: 6, dy: 6), withAttributes: attrs)
    }

    private func drawPaths(ctx: CGContext, paths: [DrawPath], rect: Rect, selected: Bool) {
        let r = nsRect(from: rect)
        if selected {
            ctx.setStrokeColor(NSColor.controlAccentColor.cgColor)
            ctx.setLineWidth(2)
            ctx.stroke(r)
        }
        for path in paths {
            guard path.points.count > 1 else { continue }
            ctx.setStrokeColor(CGColor(
                red: CGFloat(path.strokeColor.r) / 255.0,
                green: CGFloat(path.strokeColor.g) / 255.0,
                blue: CGFloat(path.strokeColor.b) / 255.0,
                alpha: CGFloat(path.strokeColor.a)
            ))
            ctx.setLineWidth(CGFloat(path.strokeWidth))
            ctx.beginPath()
            let first = path.points[0]
            ctx.move(to: CGPoint(x: first.x, y: first.y))
            for point in path.points.dropFirst() {
                ctx.addLine(to: CGPoint(x: point.x, y: point.y))
            }
            ctx.strokePath()
        }
    }

    // MARK: - Interaction

    override func scrollWheel(with event: NSEvent) {
        offset.x += event.scrollingDeltaX
        offset.y += event.scrollingDeltaY
    }

    override func magnify(with event: NSEvent) {
        let newScale = max(0.1, min(5.0, scale + event.magnification))
        scale = newScale
    }

    override func mouseDown(with event: NSEvent) {
        let loc = convertToCanvas(event.locationInWindow)
        dragStart = loc
        selectedItemIndex = nil
        for (index, item) in items.enumerated().reversed() {
            let r = rectForItem(item)
            if nsRect(from: r).contains(loc) {
                selectedItemIndex = index
                itemDragOriginalRect = r
                break
            }
        }
    }

    override func mouseDragged(with event: NSEvent) {
        guard let index = selectedItemIndex, let original = itemDragOriginalRect else { return }
        let loc = convertToCanvas(event.locationInWindow)
        let dx = loc.x - dragStart.x
        let dy = loc.y - dragStart.y
        let newRect = Rect(
            origin: Point(x: original.origin.x + dx, y: original.origin.y + dy),
            size: original.size
        )
        items[index] = replaceRect(in: items[index], with: newRect)
    }

    private func convertToCanvas(_ windowPoint: NSPoint) -> NSPoint {
        let viewPoint = convert(windowPoint, from: nil)
        return NSPoint(
            x: (viewPoint.x - offset.x) / scale,
            y: (viewPoint.y - offset.y) / scale
        )
    }

    private func rectForItem(_ item: CanvasItem) -> Rect {
        switch item {
        case .note(_, let r), .image(_, let r), .webEmbed(_, let r), .drawing(_, let r):
            return r
        }
    }

    private func replaceRect(in item: CanvasItem, with rect: Rect) -> CanvasItem {
        switch item {
        case .note(let content, _):
            return .note(content: content, rect: rect)
        case .image(let data, _):
            return .image(data: data, rect: rect)
        case .webEmbed(let url, _):
            return .webEmbed(url: url, rect: rect)
        case .drawing(let paths, _):
            return .drawing(paths: paths, rect: rect)
        }
    }

    // MARK: - Adding Items

    func addNoteItem(at point: NSPoint) {
        let rect = Rect(
            origin: Point(x: Double(point.x), y: Double(point.y)),
            size: Size(width: 200, height: 150)
        )
        items.append(.note(content: "New note", rect: rect))
        selectedItemIndex = items.count - 1
    }

    func addImageItem(image: NSImage, at point: NSPoint) {
        guard let tiffData = image.tiffRepresentation,
              let bitmap = NSBitmapImageRep(data: tiffData),
              let pngData = bitmap.representation(using: .png, properties: [:]) else { return }
        let imageData = ImageData(
            data: [UInt8](pngData),
            width: UInt32(image.size.width),
            height: UInt32(image.size.height),
            format: .png
        )
        let rect = Rect(
            origin: Point(x: Double(point.x), y: Double(point.y)),
            size: Size(width: Double(image.size.width), height: Double(image.size.height))
        )
        items.append(.image(data: imageData, rect: rect))
        selectedItemIndex = items.count - 1
    }

    func removeSelectedItem() {
        guard let index = selectedItemIndex, index < items.count else { return }
        items.remove(at: index)
        selectedItemIndex = nil
    }
}
