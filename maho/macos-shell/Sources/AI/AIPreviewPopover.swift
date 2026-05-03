import AppKit

final class AIPreviewViewController: NSViewController {
    private let titleLabel = NSTextField(labelWithString: "")
    private let divider = NSView()
    private let summaryLabel = NSTextField(labelWithString: "")
    private let urlLabel = NSTextField(labelWithString: "")
    private let spinner = NSProgressIndicator()

    private(set) var previewUrl: String?
    private var isLoading = true

    override func loadView() {
        let container = NSView(frame: NSRect(x: 0, y: 0, width: 280, height: 100))
        container.wantsLayer = true

        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.font = .systemFont(ofSize: 13, weight: .semibold)
        titleLabel.textColor = MahoAppColors.primaryText
        titleLabel.lineBreakMode = .byTruncatingTail
        titleLabel.maximumNumberOfLines = 2
        container.addSubview(titleLabel)

        divider.translatesAutoresizingMaskIntoConstraints = false
        divider.wantsLayer = true
        divider.layer?.backgroundColor = MahoAppColors.commandBarDivider.cgColor
        container.addSubview(divider)

        summaryLabel.translatesAutoresizingMaskIntoConstraints = false
        summaryLabel.font = .systemFont(ofSize: 13, weight: .regular)
        summaryLabel.textColor = MahoAppColors.primaryText
        summaryLabel.lineBreakMode = .byTruncatingTail
        summaryLabel.maximumNumberOfLines = 5
        container.addSubview(summaryLabel)

        urlLabel.translatesAutoresizingMaskIntoConstraints = false
        urlLabel.font = .systemFont(ofSize: 10, weight: .regular)
        urlLabel.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.7)
        urlLabel.lineBreakMode = .byTruncatingTail
        urlLabel.maximumNumberOfLines = 1
        container.addSubview(urlLabel)

        spinner.translatesAutoresizingMaskIntoConstraints = false
        spinner.style = .spinning
        spinner.controlSize = .small
        spinner.isDisplayedWhenStopped = false
        container.addSubview(spinner)

        NSLayoutConstraint.activate([
            titleLabel.topAnchor.constraint(equalTo: container.topAnchor, constant: 10),
            titleLabel.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 12),
            titleLabel.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -12),

            divider.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 8),
            divider.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 12),
            divider.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -12),
            divider.heightAnchor.constraint(equalToConstant: 1),

            summaryLabel.topAnchor.constraint(equalTo: divider.bottomAnchor, constant: 8),
            summaryLabel.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 12),
            summaryLabel.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -12),

            urlLabel.topAnchor.constraint(equalTo: summaryLabel.bottomAnchor, constant: 6),
            urlLabel.leadingAnchor.constraint(equalTo: container.leadingAnchor, constant: 12),
            urlLabel.trailingAnchor.constraint(equalTo: container.trailingAnchor, constant: -12),
            urlLabel.bottomAnchor.constraint(lessThanOrEqualTo: container.bottomAnchor, constant: -10),

            spinner.centerXAnchor.constraint(equalTo: container.centerXAnchor),
            spinner.centerYAnchor.constraint(equalTo: container.centerYAnchor),
            spinner.widthAnchor.constraint(equalToConstant: 16),
            spinner.heightAnchor.constraint(equalToConstant: 16),
        ])

        self.view = container
        showLoading()
    }

    func configureLoading(url: String) {
        self.previewUrl = url
        urlLabel.stringValue = url
        showLoading()
    }

    func configureResult(title: String, summary: String, url: String) {
        self.previewUrl = url
        isLoading = false
        spinner.stopAnimation(nil)
        spinner.isHidden = true

        titleLabel.stringValue = title
        titleLabel.isHidden = title.isEmpty
        divider.isHidden = title.isEmpty
        summaryLabel.stringValue = summary
        summaryLabel.isHidden = false
        urlLabel.stringValue = url
    }

    private func showLoading() {
        isLoading = true
        titleLabel.stringValue = ""
        titleLabel.isHidden = true
        divider.isHidden = true
        summaryLabel.isHidden = true
        spinner.isHidden = false
        spinner.startAnimation(nil)
    }
}

final class AIPreviewPopover {
    private var popover: NSPopover?
    private var currentUrl: String?

    func show(url: String, relativeTo rect: NSRect, of view: NSView) {
        dismiss()
        currentUrl = url

        let vc = AIPreviewViewController()
        _ = vc.view
        vc.configureLoading(url: url)

        let pop = NSPopover()
        pop.contentViewController = vc
        pop.contentSize = NSSize(width: 280, height: 100)
        pop.behavior = .semitransient
        pop.animates = true
        pop.show(relativeTo: rect, of: view, preferredEdge: .maxY)

        popover = pop
    }

    func updateWithResult(url: String, title: String, summary: String) {
        guard currentUrl == url,
              let pop = popover,
              pop.isShown,
              let vc = pop.contentViewController as? AIPreviewViewController else { return }

        vc.configureResult(title: title, summary: summary, url: url)

        let fittingSize = vc.view.fittingSize
        let height = min(max(fittingSize.height, 100), 180)
        pop.contentSize = NSSize(width: 280, height: height)
    }

    func dismiss() {
        popover?.close()
        popover = nil
        currentUrl = nil
    }

    var isShown: Bool {
        return popover?.isShown ?? false
    }

    var activeUrl: String? {
        return currentUrl
    }
}
