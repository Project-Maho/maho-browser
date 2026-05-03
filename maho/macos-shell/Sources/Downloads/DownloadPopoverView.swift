import AppKit

final class DownloadPopoverView: NSViewController, NSTableViewDataSource, NSTableViewDelegate, CoreUpdateObserver {
    private var downloads: [DownloadViewModel] = []
    private let tableView = NSTableView()
    private let scrollView = NSScrollView()
    private let emptyLabel = NSTextField(labelWithString: "No downloads")
    private let columnId = NSUserInterfaceItemIdentifier("DownloadColumn")

    override func loadView() {
        view = NSView(frame: NSRect(x: 0, y: 0, width: 340, height: 400))
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.wantsLayer = true
        view.layer?.backgroundColor = MahoAppColors.cardBackground.cgColor

        setupTableView()
        setupEmptyLabel()
        CoreUpdateDispatcher.shared.register(self)
        reloadDownloads()
    }

    deinit {
        CoreUpdateDispatcher.shared.unregister(self)
    }

    private func setupTableView() {
        let column = NSTableColumn(identifier: columnId)
        column.title = "Downloads"
        column.width = 320
        tableView.addTableColumn(column)
        tableView.headerView = nil
        tableView.dataSource = self
        tableView.delegate = self
        tableView.backgroundColor = .clear
        tableView.rowHeight = 64
        tableView.selectionHighlightStyle = .none
        tableView.usesAlternatingRowBackgroundColors = false
        tableView.setAccessibilityLabel("Downloads List")
        tableView.setAccessibilityRole(.table)

        scrollView.documentView = tableView
        scrollView.hasVerticalScroller = true
        scrollView.drawsBackground = false
        scrollView.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(scrollView)

        NSLayoutConstraint.activate([
            scrollView.topAnchor.constraint(equalTo: view.topAnchor, constant: 8),
            scrollView.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            scrollView.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            scrollView.bottomAnchor.constraint(equalTo: view.bottomAnchor),
        ])
    }

    private func setupEmptyLabel() {
        emptyLabel.textColor = MahoAppColors.secondaryText
        emptyLabel.font = NSFont.systemFont(ofSize: 13)
        emptyLabel.alignment = .center
        emptyLabel.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(emptyLabel)
        NSLayoutConstraint.activate([
            emptyLabel.centerXAnchor.constraint(equalTo: view.centerXAnchor),
            emptyLabel.centerYAnchor.constraint(equalTo: view.centerYAnchor),
        ])
    }

    private func reloadDownloads() {
        downloads = MahoBridge.shared.getDownloadViewModels()
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            self.emptyLabel.isHidden = !self.downloads.isEmpty
            self.scrollView.isHidden = self.downloads.isEmpty
            self.tableView.reloadData()
        }
    }

    // MARK: - CoreUpdateObserver

    func handleCoreUpdate(_ update: CoreUpdate) {
        switch update {
        case .downloadStarted:
            reloadDownloads()
        case .downloadProgress(let downloadId, let progress):
            if let idx = downloads.firstIndex(where: { $0.id == downloadId }) {
                updateProgressInRow(idx, progress: progress)
            }
        case .downloadCompleted:
            reloadDownloads()
        case .downloadRenamed:
            reloadDownloads()
        default:
            break
        }
    }

    private func updateProgressInRow(_ row: Int, progress: Double) {
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            if let cellView = self.tableView.view(atColumn: 0, row: row, makeIfNecessary: false) as? DownloadCellView {
                cellView.updateProgress(progress)
            }
        }
    }

    // MARK: - NSTableViewDataSource

    func numberOfRows(in tableView: NSTableView) -> Int {
        return downloads.count
    }

    // MARK: - NSTableViewDelegate

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let download = downloads[row]
        let cellView = DownloadCellView(download: download)
        cellView.setAccessibilityLabel("\(download.filename) - \(download.state)")
        cellView.onAction = { [weak self] action in
            self?.handleAction(action, downloadId: download.id)
        }
        return cellView
    }

    func tableView(_ tableView: NSTableView, heightOfRow row: Int) -> CGFloat {
        let download = downloads[row]
        if download.state == .completed && download.originalFilename != nil {
            return 78
        }
        return 64
    }

    private func handleAction(_ action: DownloadCellAction, downloadId: String) {
        switch action {
        case .pause:
            CoreUpdateDispatcher.shared.sendAndDispatch(.pauseDownload(downloadId: downloadId))
        case .resume:
            CoreUpdateDispatcher.shared.sendAndDispatch(.resumeDownload(downloadId: downloadId))
        case .cancel:
            CoreUpdateDispatcher.shared.sendAndDispatch(.cancelDownload(downloadId: downloadId))
        case .open:
            if let dl = downloads.first(where: { $0.id == downloadId }), let path = dl.filePath {
                NSWorkspace.shared.open(URL(fileURLWithPath: path))
            }
        case .remove:
            CoreUpdateDispatcher.shared.sendAndDispatch(.removeDownload(downloadId: downloadId))
        case .restoreName:
            CoreUpdateDispatcher.shared.sendAndDispatch(.restoreDownloadName(downloadId: downloadId))
        }
        reloadDownloads()
    }
}

// MARK: - Cell Action

private enum DownloadCellAction {
    case pause, resume, cancel, open, remove, restoreName
}

// MARK: - DownloadCellView

private final class DownloadCellView: NSView {
    var onAction: ((DownloadCellAction) -> Void)?

    private let filenameLabel = NSTextField(labelWithString: "")
    private let statusLabel = NSTextField(labelWithString: "")
    private let progressBar = NSProgressIndicator()
    private let primaryButton = NSButton(title: "", target: nil, action: nil)
    private let secondaryButton = NSButton(title: "", target: nil, action: nil)
    private let restoreLabel = NSTextField(labelWithString: "")
    private let download: DownloadViewModel

    init(download: DownloadViewModel) {
        self.download = download
        super.init(frame: .zero)
        setupViews()
        configure()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) not supported")
    }

    private func setupViews() {
        filenameLabel.font = NSFont.systemFont(ofSize: 12, weight: .medium)
        filenameLabel.textColor = MahoAppColors.primaryText
        filenameLabel.lineBreakMode = .byTruncatingMiddle
        filenameLabel.translatesAutoresizingMaskIntoConstraints = false

        statusLabel.font = NSFont.systemFont(ofSize: 10)
        statusLabel.textColor = MahoAppColors.secondaryText
        statusLabel.translatesAutoresizingMaskIntoConstraints = false

        progressBar.style = .bar
        progressBar.isIndeterminate = false
        progressBar.minValue = 0
        progressBar.maxValue = 1
        progressBar.translatesAutoresizingMaskIntoConstraints = false

        primaryButton.bezelStyle = .rounded
        primaryButton.controlSize = .small
        primaryButton.font = NSFont.systemFont(ofSize: 10)
        primaryButton.target = self
        primaryButton.action = #selector(primaryAction)
        primaryButton.translatesAutoresizingMaskIntoConstraints = false

        secondaryButton.bezelStyle = .rounded
        secondaryButton.controlSize = .small
        secondaryButton.font = NSFont.systemFont(ofSize: 10)
        secondaryButton.target = self
        secondaryButton.action = #selector(secondaryAction)
        secondaryButton.translatesAutoresizingMaskIntoConstraints = false

        restoreLabel.font = NSFont.systemFont(ofSize: 10)
        restoreLabel.textColor = MahoAppColors.accentBlue
        restoreLabel.isSelectable = false
        restoreLabel.translatesAutoresizingMaskIntoConstraints = false
        restoreLabel.isHidden = true

        addSubview(filenameLabel)
        addSubview(statusLabel)
        addSubview(progressBar)
        addSubview(primaryButton)
        addSubview(secondaryButton)
        addSubview(restoreLabel)

        NSLayoutConstraint.activate([
            filenameLabel.topAnchor.constraint(equalTo: topAnchor, constant: 6),
            filenameLabel.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12),
            filenameLabel.trailingAnchor.constraint(lessThanOrEqualTo: primaryButton.leadingAnchor, constant: -8),

            progressBar.topAnchor.constraint(equalTo: filenameLabel.bottomAnchor, constant: 4),
            progressBar.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12),
            progressBar.trailingAnchor.constraint(equalTo: primaryButton.leadingAnchor, constant: -8),
            progressBar.heightAnchor.constraint(equalToConstant: 4),

            statusLabel.topAnchor.constraint(equalTo: progressBar.bottomAnchor, constant: 3),
            statusLabel.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12),

            restoreLabel.topAnchor.constraint(equalTo: statusLabel.bottomAnchor, constant: 1),
            restoreLabel.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12),
            restoreLabel.trailingAnchor.constraint(lessThanOrEqualTo: primaryButton.leadingAnchor, constant: -8),

            primaryButton.centerYAnchor.constraint(equalTo: centerYAnchor),
            primaryButton.trailingAnchor.constraint(equalTo: secondaryButton.leadingAnchor, constant: -4),
            primaryButton.widthAnchor.constraint(greaterThanOrEqualToConstant: 48),

            secondaryButton.centerYAnchor.constraint(equalTo: centerYAnchor),
            secondaryButton.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -12),
            secondaryButton.widthAnchor.constraint(greaterThanOrEqualToConstant: 48),
        ])
    }

    private func configure() {
        filenameLabel.stringValue = download.filename

        switch download.state {
        case .downloading:
            let progress = download.totalBytes > 0
                ? Double(download.receivedBytes) / Double(download.totalBytes)
                : 0
            progressBar.doubleValue = progress
            progressBar.isHidden = false
            statusLabel.stringValue = formatBytes(download.receivedBytes) + " / " + formatBytes(download.totalBytes)
            primaryButton.title = "Pause"
            secondaryButton.title = "Cancel"
            primaryButton.isHidden = false
            secondaryButton.isHidden = false

        case .paused:
            let progress = download.totalBytes > 0
                ? Double(download.receivedBytes) / Double(download.totalBytes)
                : 0
            progressBar.doubleValue = progress
            progressBar.isHidden = false
            statusLabel.stringValue = "Paused"
            primaryButton.title = "Resume"
            secondaryButton.title = "Cancel"
            primaryButton.isHidden = false
            secondaryButton.isHidden = false

        case .completed:
            progressBar.isHidden = true
            statusLabel.stringValue = "Completed"
            statusLabel.textColor = MahoAppColors.successGreen
            primaryButton.title = "Open"
            secondaryButton.title = "Remove"
            primaryButton.isHidden = false
            secondaryButton.isHidden = false
            if let originalName = download.originalFilename {
                restoreLabel.stringValue = "Renamed from \(originalName) — Restore"
                restoreLabel.isHidden = false
                let clickGesture = NSClickGestureRecognizer(target: self, action: #selector(restoreNameAction))
                restoreLabel.addGestureRecognizer(clickGesture)
            }

        case .failed:
            progressBar.isHidden = true
            statusLabel.stringValue = download.error ?? "Failed"
            statusLabel.textColor = MahoAppColors.dangerRed
            primaryButton.isHidden = true
            secondaryButton.title = "Remove"
            secondaryButton.isHidden = false

        case .cancelled:
            progressBar.isHidden = true
            statusLabel.stringValue = "Cancelled"
            primaryButton.isHidden = true
            secondaryButton.title = "Remove"
            secondaryButton.isHidden = false
        }
    }

    func updateProgress(_ progress: Double) {
        progressBar.doubleValue = progress
        let received = UInt64(progress * Double(download.totalBytes))
        statusLabel.stringValue = formatBytes(received) + " / " + formatBytes(download.totalBytes)
    }

    @objc private func primaryAction() {
        switch download.state {
        case .downloading: onAction?(.pause)
        case .paused: onAction?(.resume)
        case .completed: onAction?(.open)
        default: break
        }
    }

    @objc private func secondaryAction() {
        switch download.state {
        case .downloading, .paused: onAction?(.cancel)
        case .completed, .failed, .cancelled: onAction?(.remove)
        }
    }

    @objc private func restoreNameAction() {
        onAction?(.restoreName)
    }

    private func formatBytes(_ bytes: UInt64) -> String {
        let formatter = ByteCountFormatter()
        formatter.countStyle = .file
        return formatter.string(fromByteCount: Int64(bytes))
    }
}
