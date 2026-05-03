import AppKit

extension Notification.Name {
    static let miniPlayerSkipBack = Notification.Name("miniPlayerSkipBack")
    static let miniPlayerSkipForward = Notification.Name("miniPlayerSkipForward")
    static let miniPlayerPlayPause = Notification.Name("miniPlayerPlayPause")
}

final class MiniPlayerView: NSView {
    private let titleLabel = NSTextField(labelWithString: "")
    private let playPauseButton = NSButton(title: "Pause", target: nil, action: nil)
    private let closeButton = NSButton(title: "\u{00D7}", target: nil, action: nil)
    private let skipBackButton = NSButton(title: "\u{23EA}", target: nil, action: nil)
    private let skipForwardButton = NSButton(title: "\u{23E9}", target: nil, action: nil)
    private let progressBar = NSProgressIndicator()
    private let artPlaceholder = NSView(frame: .zero)

    private var tabId: TabId?
    private var isPlaying: Bool = false

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupUI()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupUI()
    }

    private func setupUI() {
        wantsLayer = true
        layer?.backgroundColor = MahoAppColors.toolbarBackground.cgColor
        layer?.cornerRadius = 8

        let width: CGFloat = 280
        let height: CGFloat = 80
        frame = NSRect(x: 0, y: 0, width: width, height: height)

        artPlaceholder.translatesAutoresizingMaskIntoConstraints = false
        artPlaceholder.wantsLayer = true
        artPlaceholder.layer?.backgroundColor = MahoAppColors.secondaryText.withAlphaComponent(0.2).cgColor
        artPlaceholder.layer?.cornerRadius = 6
        addSubview(artPlaceholder)

        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.textColor = MahoAppColors.sidebarText
        titleLabel.maximumNumberOfLines = 1
        titleLabel.lineBreakMode = .byTruncatingTail
        addSubview(titleLabel)

        progressBar.translatesAutoresizingMaskIntoConstraints = false
        progressBar.style = .bar
        progressBar.isIndeterminate = false
        progressBar.minValue = 0
        progressBar.maxValue = 100
        progressBar.doubleValue = 0
        addSubview(progressBar)

        skipBackButton.translatesAutoresizingMaskIntoConstraints = false
        skipBackButton.bezelStyle = .rounded
        skipBackButton.setButtonType(.momentaryPushIn)
        skipBackButton.font = .systemFont(ofSize: 10)
        skipBackButton.target = self
        skipBackButton.action = #selector(skipBackClicked(_:))
        addSubview(skipBackButton)

        playPauseButton.translatesAutoresizingMaskIntoConstraints = false
        playPauseButton.bezelStyle = .rounded
        playPauseButton.setButtonType(.momentaryPushIn)
        playPauseButton.target = self
        playPauseButton.action = #selector(playPauseClicked(_:))
        addSubview(playPauseButton)

        skipForwardButton.translatesAutoresizingMaskIntoConstraints = false
        skipForwardButton.bezelStyle = .rounded
        skipForwardButton.setButtonType(.momentaryPushIn)
        skipForwardButton.font = .systemFont(ofSize: 10)
        skipForwardButton.target = self
        skipForwardButton.action = #selector(skipForwardClicked(_:))
        addSubview(skipForwardButton)

        closeButton.translatesAutoresizingMaskIntoConstraints = false
        closeButton.bezelStyle = .rounded
        closeButton.setButtonType(.momentaryPushIn)
        closeButton.font = NSFont.systemFont(ofSize: 14, weight: .semibold)
        closeButton.target = self
        closeButton.action = #selector(closeClicked(_:))
        addSubview(closeButton)

        NSLayoutConstraint.activate([
            artPlaceholder.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 10),
            artPlaceholder.centerYAnchor.constraint(equalTo: centerYAnchor, constant: -8),
            artPlaceholder.widthAnchor.constraint(equalToConstant: 40),
            artPlaceholder.heightAnchor.constraint(equalToConstant: 40),

            titleLabel.leadingAnchor.constraint(equalTo: artPlaceholder.trailingAnchor, constant: 8),
            titleLabel.topAnchor.constraint(equalTo: topAnchor, constant: 10),
            titleLabel.trailingAnchor.constraint(lessThanOrEqualTo: closeButton.leadingAnchor, constant: -8),

            skipBackButton.leadingAnchor.constraint(equalTo: artPlaceholder.trailingAnchor, constant: 8),
            skipBackButton.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 4),

            playPauseButton.leadingAnchor.constraint(equalTo: skipBackButton.trailingAnchor, constant: 4),
            playPauseButton.centerYAnchor.constraint(equalTo: skipBackButton.centerYAnchor),

            skipForwardButton.leadingAnchor.constraint(equalTo: playPauseButton.trailingAnchor, constant: 4),
            skipForwardButton.centerYAnchor.constraint(equalTo: skipBackButton.centerYAnchor),

            closeButton.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -10),
            closeButton.topAnchor.constraint(equalTo: topAnchor, constant: 10),

            progressBar.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 10),
            progressBar.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -10),
            progressBar.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -6),
            progressBar.heightAnchor.constraint(equalToConstant: 4),

            widthAnchor.constraint(equalToConstant: width),
            heightAnchor.constraint(equalToConstant: height),
        ])
    }

    func configure(tabId: TabId, title: String, isPlaying: Bool) {
        self.tabId = tabId
        self.isPlaying = isPlaying
        titleLabel.stringValue = title
        updatePlayPauseTitle()
    }

    private func updatePlayPauseTitle() {
        playPauseButton.title = isPlaying ? "Pause" : "Play"
    }

    @objc private func playPauseClicked(_ sender: Any) {
        guard let tabId = tabId else { return }
        NotificationCenter.default.post(name: .miniPlayerPlayPause, object: nil, userInfo: ["tabId": tabId])
    }

    @objc private func skipBackClicked(_ sender: Any) {
        guard let tabId = tabId else { return }
        NotificationCenter.default.post(name: .miniPlayerSkipBack, object: nil, userInfo: ["tabId": tabId])
    }

    @objc private func skipForwardClicked(_ sender: Any) {
        guard let tabId = tabId else { return }
        NotificationCenter.default.post(name: .miniPlayerSkipForward, object: nil, userInfo: ["tabId": tabId])
    }

    @objc private func closeClicked(_ sender: NSButton) {
        guard let tabId = tabId else { return }
        CoreUpdateDispatcher.shared.sendAndDispatch(.togglePiP(tabId: tabId))
    }
}
