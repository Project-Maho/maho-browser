import AppKit

// MARK: - Archive Panel

final class SidebarArchivePanelView: NSView, NSSearchFieldDelegate {
    private enum FilterMode: String, CaseIterable {
        case all
        case today
        case last7Days
        case olderThan7Days

        var title: String {
            switch self {
            case .all:
                return "Any time"
            case .today:
                return "Today"
            case .last7Days:
                return "Past week"
            case .olderThan7Days:
                return "Older"
            }
        }

        func matches(dayOffset: Int?) -> Bool {
            switch self {
            case .all:
                return true
            case .today:
                return dayOffset == 0
            case .last7Days:
                guard let dayOffset else { return false }
                return (1...7).contains(dayOffset)
            case .olderThan7Days:
                guard let dayOffset else { return true }
                return dayOffset > 7
            }
        }
    }

    private struct ArchiveSection {
        let title: String
        let sortKey: Int
        let tabs: [ArchivedTabViewModel]
    }

    private struct ArchivedTabEntry {
        let tab: ArchivedTabViewModel
        let archivedDate: Date?
        let dayOffset: Int?
    }

    fileprivate enum Metrics {
        static let panelHorizontalInset: CGFloat = 8
        static let panelVerticalInset: CGFloat = 0
        static let contentSpacing: CGFloat = 1
        static let headerPadding = NSEdgeInsets(top: 8, left: 8, bottom: 10, right: 8)
        static let headerSpacing: CGFloat = 0
        static let controlsSpacing: CGFloat = 8
        static let controlHeight: CGFloat = 32
        static let controlCornerRadius: CGFloat = 16
        static let filterMinWidth: CGFloat = 96
        static let scrollContentInset: CGFloat = 6
        static let sectionSpacing: CGFloat = 14
        static let sectionHeaderSpacing: CGFloat = 7
        static let sectionHeaderVerticalPadding: CGFloat = 0
        static let sectionHeaderLeadingInset: CGFloat = 4
        static let sectionHeaderTrailingInset: CGFloat = 4
        static let sectionCountGap: CGFloat = 5
        static let rowSpacing: CGFloat = 6
        static let rowHeight: CGFloat = 48
        static let rowCornerRadius: CGFloat = 8
        static let rowHorizontalInset: CGFloat = 0
        static let rowVerticalInset: CGFloat = 0
        static let rowLeadingInset: CGFloat = 12
        static let rowTrailingInset: CGFloat = 12
        static let rowFaviconSize: CGFloat = 14
        static let rowTextGap: CGFloat = 8
        static let rowTitleSubtitleSpacing: CGFloat = 1
        static let rowGlyphSize: CGFloat = 9
    }

    private let contentStackView = NSStackView()
    private let headerSurfaceView = NSView()
    private let headerContentStackView = NSStackView()
    private let controlsStackView = NSStackView()
    private let searchField = NSSearchField()
    private let filterButton = NSButton(frame: .zero)
    private let bodyContainerView = NSView()
    private let scrollView = NSScrollView()
    private let listDocumentView = NSView()
    private let listStackView = NSStackView()
    private let emptyStateView = SidebarArchiveEmptyStateView()
    private var allArchivedTabs: [ArchivedTabViewModel] = []
    private var filterMode: FilterMode = .all
    private var searchDebounceWork: DispatchWorkItem?

    var onRestoreArchivedTab: ((TabId) -> Void)?
    var onDeleteArchivedTab: ((TabId) -> Void)?

    private lazy var iso8601Formatters: [ISO8601DateFormatter] = {
        let fractional = ISO8601DateFormatter()
        fractional.formatOptions = [.withInternetDateTime, .withFractionalSeconds]

        let standard = ISO8601DateFormatter()
        standard.formatOptions = [.withInternetDateTime]

        return [fractional, standard]
    }()

    private lazy var fallbackDateFormatters: [DateFormatter] = {
        let formats = [
            "yyyy-MM-dd'T'HH:mm:ss.SSSXXXXX",
            "yyyy-MM-dd'T'HH:mm:ssXXXXX",
            "yyyy-MM-dd HH:mm:ssXXXXX",
            "yyyy-MM-dd HH:mm:ss",
        ]

        return formats.map { format in
            let formatter = DateFormatter()
            formatter.locale = Locale(identifier: "en_US_POSIX")
            formatter.timeZone = TimeZone(secondsFromGMT: 0)
            formatter.dateFormat = format
            return formatter
        }
    }()

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupViews()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupViews()
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        refreshAppearance()
    }

    // MARK: - Public API

    func reloadArchivedTabs(spaceId: SpaceId?) {
        let archivedTabs: [ArchivedTabViewModel]
        if let spaceId {
            archivedTabs = MahoBridge.shared.getArchivedTabs(spaceId: spaceId)
        } else {
            archivedTabs = []
        }

        if shouldUseDenseArchivePreviewData(liveTabs: archivedTabs) {
            allArchivedTabs = denseArchivePreviewTabs()
        } else {
            allArchivedTabs = archivedTabs
        }
        applyFilters()
    }

    func resetState() {
        searchDebounceWork?.cancel()
        searchDebounceWork = nil
        searchField.stringValue = ""
        filterMode = .all
        updateFilterButtonAppearance()
        applyFilters()
    }

    func focusSearchField() {
        window?.makeFirstResponder(searchField)
    }

    // MARK: - Appearance

    func refreshAppearance() {
        layer?.backgroundColor = MahoAppColors.sidebarBackground.withAlphaComponent(0.56).cgColor
        headerSurfaceView.layer?.backgroundColor = MahoAppColors.archivePanelBackground.withAlphaComponent(0.78).cgColor
        headerSurfaceView.layer?.borderColor = MahoAppColors.archiveControlBorder.withAlphaComponent(0.16).cgColor

        bodyContainerView.layer?.backgroundColor = MahoAppColors.archivePanelBackground.cgColor

        searchField.textColor = MahoAppColors.primaryText.withAlphaComponent(0.94)
        searchField.layer?.backgroundColor = MahoAppColors.archiveControlBackground.withAlphaComponent(0.96).cgColor
        searchField.layer?.borderColor = MahoAppColors.archiveControlBorder.withAlphaComponent(0.18).cgColor

        if let searchCell = searchField.cell as? NSSearchFieldCell {
            searchCell.backgroundStyle = .raised
            searchCell.placeholderAttributedString = NSAttributedString(
                string: "Search Archive...",
                attributes: [
                    .font: NSFont.systemFont(ofSize: 13, weight: .medium),
                    .foregroundColor: MahoAppColors.secondaryText.withAlphaComponent(0.42),
                ]
            )
        }

        updateFilterButtonAppearance()
        emptyStateView.refreshAppearance()

        for subview in listStackView.arrangedSubviews {
            if let sectionHeader = subview as? SidebarArchiveSectionHeaderView {
                sectionHeader.refreshAppearance()
            }
            if let row = subview as? SidebarArchiveRowView {
                row.refreshAppearance()
            }
        }
    }

    // MARK: - Setup

    private func setupViews() {
        translatesAutoresizingMaskIntoConstraints = false
        wantsLayer = true
        layer?.backgroundColor = MahoAppColors.sidebarBackground.cgColor
        layer?.cornerRadius = 0
        layer?.masksToBounds = true

        setAccessibilityElement(true)
        setAccessibilityRole(.group)
        setAccessibilityIdentifier("sidebar.archive.panel")
        setAccessibilityLabel("Archived Tabs Panel")

        configureHeaderView()
        configureSearchField()
        configureFilterButton()

        controlsStackView.translatesAutoresizingMaskIntoConstraints = false
        controlsStackView.orientation = .horizontal
        controlsStackView.alignment = .centerY
        controlsStackView.spacing = Metrics.controlsSpacing
        controlsStackView.addArrangedSubview(searchField)
        controlsStackView.addArrangedSubview(filterButton)
        searchField.setContentHuggingPriority(.defaultLow, for: .horizontal)
        searchField.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        filterButton.setContentHuggingPriority(.required, for: .horizontal)
        filterButton.setContentCompressionResistancePriority(.required, for: .horizontal)

        headerContentStackView.translatesAutoresizingMaskIntoConstraints = false
        headerContentStackView.orientation = .vertical
        headerContentStackView.alignment = .leading
        headerContentStackView.spacing = Metrics.headerSpacing
        headerSurfaceView.addSubview(headerContentStackView)
        headerContentStackView.addArrangedSubview(controlsStackView)

        bodyContainerView.translatesAutoresizingMaskIntoConstraints = false
        bodyContainerView.wantsLayer = true
        bodyContainerView.layer?.backgroundColor = MahoAppColors.archivePanelBackground.cgColor
        bodyContainerView.layer?.cornerRadius = 18
        bodyContainerView.layer?.borderWidth = 0
        bodyContainerView.layer?.masksToBounds = true

        scrollView.translatesAutoresizingMaskIntoConstraints = false
        scrollView.contentInsets = NSEdgeInsets(top: Metrics.scrollContentInset, left: 0, bottom: Metrics.scrollContentInset, right: 0)
        scrollView.drawsBackground = false
        scrollView.hasVerticalScroller = true
        scrollView.hasHorizontalScroller = false
        scrollView.autohidesScrollers = true
        scrollView.borderType = .noBorder
        scrollView.setAccessibilityElement(true)
        scrollView.setAccessibilityRole(.group)
        scrollView.setAccessibilityIdentifier("sidebar.archive.list")
        scrollView.setAccessibilityLabel("Archived Tab Results")

        listDocumentView.translatesAutoresizingMaskIntoConstraints = false
        listDocumentView.wantsLayer = true
        listDocumentView.layer?.backgroundColor = NSColor.clear.cgColor
        scrollView.documentView = listDocumentView

        listStackView.translatesAutoresizingMaskIntoConstraints = false
        listStackView.orientation = .vertical
        listStackView.alignment = .leading
        listStackView.distribution = .fill
        listStackView.spacing = Metrics.rowSpacing
        listDocumentView.addSubview(listStackView)

        emptyStateView.translatesAutoresizingMaskIntoConstraints = false
        emptyStateView.isHidden = true
        bodyContainerView.addSubview(scrollView)
        bodyContainerView.addSubview(emptyStateView)

        contentStackView.translatesAutoresizingMaskIntoConstraints = false
        contentStackView.orientation = .vertical
        contentStackView.alignment = .leading
        contentStackView.distribution = .fill
        contentStackView.spacing = Metrics.contentSpacing
        addSubview(contentStackView)

        contentStackView.addArrangedSubview(headerSurfaceView)
        contentStackView.addArrangedSubview(bodyContainerView)

        NSLayoutConstraint.activate([
            contentStackView.leadingAnchor.constraint(equalTo: leadingAnchor, constant: Metrics.panelHorizontalInset),
            contentStackView.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -Metrics.panelHorizontalInset),
            contentStackView.topAnchor.constraint(equalTo: topAnchor, constant: Metrics.panelVerticalInset),
            contentStackView.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -Metrics.panelVerticalInset),

            headerSurfaceView.widthAnchor.constraint(equalTo: contentStackView.widthAnchor),
            bodyContainerView.widthAnchor.constraint(equalTo: contentStackView.widthAnchor),

            headerContentStackView.leadingAnchor.constraint(equalTo: headerSurfaceView.leadingAnchor, constant: Metrics.headerPadding.left),
            headerContentStackView.trailingAnchor.constraint(equalTo: headerSurfaceView.trailingAnchor, constant: -Metrics.headerPadding.right),
            headerContentStackView.topAnchor.constraint(equalTo: headerSurfaceView.topAnchor, constant: Metrics.headerPadding.top),
            headerContentStackView.bottomAnchor.constraint(equalTo: headerSurfaceView.bottomAnchor, constant: -Metrics.headerPadding.bottom),

            controlsStackView.widthAnchor.constraint(equalTo: headerContentStackView.widthAnchor),
            searchField.heightAnchor.constraint(equalToConstant: Metrics.controlHeight),
            filterButton.heightAnchor.constraint(equalToConstant: Metrics.controlHeight),
            filterButton.widthAnchor.constraint(equalToConstant: Metrics.filterMinWidth),

            scrollView.leadingAnchor.constraint(equalTo: bodyContainerView.leadingAnchor),
            scrollView.trailingAnchor.constraint(equalTo: bodyContainerView.trailingAnchor),
            scrollView.topAnchor.constraint(equalTo: bodyContainerView.topAnchor),
            scrollView.bottomAnchor.constraint(equalTo: bodyContainerView.bottomAnchor),

            emptyStateView.leadingAnchor.constraint(equalTo: bodyContainerView.leadingAnchor),
            emptyStateView.trailingAnchor.constraint(equalTo: bodyContainerView.trailingAnchor),
            emptyStateView.centerYAnchor.constraint(equalTo: bodyContainerView.centerYAnchor),

            listStackView.leadingAnchor.constraint(equalTo: listDocumentView.leadingAnchor),
            listStackView.trailingAnchor.constraint(equalTo: listDocumentView.trailingAnchor),
            listStackView.topAnchor.constraint(equalTo: listDocumentView.topAnchor),
            listStackView.bottomAnchor.constraint(equalTo: listDocumentView.bottomAnchor),
            listStackView.widthAnchor.constraint(equalTo: scrollView.contentView.widthAnchor),
        ])

        refreshAppearance()
        updateHeaderCount(visibleCount: 0, totalCount: 0)
    }

    private func configureHeaderView() {
        headerSurfaceView.translatesAutoresizingMaskIntoConstraints = false
        headerSurfaceView.wantsLayer = true
        headerSurfaceView.layer?.cornerRadius = 18
        headerSurfaceView.layer?.borderWidth = 1
        headerSurfaceView.layer?.masksToBounds = true
    }

    private func configureSearchField() {
        searchField.translatesAutoresizingMaskIntoConstraints = false
        searchField.delegate = self
        searchField.focusRingType = .none
        searchField.font = .systemFont(ofSize: 13, weight: .medium)
        searchField.isBordered = false
        searchField.isBezeled = false
        searchField.drawsBackground = false
        searchField.wantsLayer = true
        searchField.layer?.cornerRadius = Metrics.controlCornerRadius
        searchField.layer?.borderWidth = 1
        searchField.layer?.masksToBounds = true
        searchField.setAccessibilityIdentifier("sidebar.archive.search")
        searchField.setAccessibilityLabel("Search Archive")
    }

    override var acceptsFirstResponder: Bool { true }

    private func configureFilterButton() {
        filterButton.translatesAutoresizingMaskIntoConstraints = false
        filterButton.isBordered = false
        filterButton.bezelStyle = .regularSquare
        filterButton.wantsLayer = true
        filterButton.layer?.cornerRadius = Metrics.controlCornerRadius
        filterButton.layer?.borderWidth = 1
        filterButton.target = self
        filterButton.action = #selector(showFilterMenu(_:))
        filterButton.image = MahoIcon.imageForSFSymbol("line.3.horizontal.decrease", pointSize: 9, accessibilityDescription: "Filter")
        filterButton.imagePosition = .imageLeading
        filterButton.imageHugsTitle = true
        filterButton.imageScaling = .scaleProportionallyDown
        filterButton.setAccessibilityRole(.button)
        filterButton.setAccessibilityIdentifier("sidebar.archive.filter")
        filterButton.setAccessibilityLabel("Filter Archive")
        updateFilterButtonAppearance()
    }

    // MARK: - Header Controls

    private func updateFilterButtonAppearance() {
        let isActive = filterMode != .all
        let title = NSMutableAttributedString(string: "Filter")
        title.addAttributes([
            .font: NSFont.systemFont(ofSize: 12, weight: .semibold),
            .foregroundColor: (isActive ? MahoAppColors.primaryText.withAlphaComponent(0.92) : MahoAppColors.primaryText.withAlphaComponent(0.82)),
        ], range: NSRange(location: 0, length: title.length))
        filterButton.attributedTitle = title
        filterButton.toolTip = isActive ? "Filter: \(filterMode.title)" : "Filter"
        filterButton.layer?.backgroundColor = (isActive
            ? MahoAppColors.archiveControlActiveBackground.withAlphaComponent(0.96).cgColor
            : MahoAppColors.archiveControlBackground.withAlphaComponent(0.96).cgColor)
        filterButton.layer?.borderColor = MahoAppColors.archiveControlBorder.withAlphaComponent(isActive ? 0.22 : 0.16).cgColor
        filterButton.contentTintColor = isActive ? MahoAppColors.primaryText.withAlphaComponent(0.90) : MahoAppColors.secondaryText.withAlphaComponent(0.74)
        filterButton.setAccessibilityValue(isActive ? "Filter Active: \(filterMode.title)" : "Filter Inactive")
    }

    // MARK: - Actions

    @objc private func showFilterMenu(_ sender: NSButton) {
        let menu = NSMenu()
        for mode in FilterMode.allCases {
            let item = NSMenuItem(title: mode.title, action: #selector(selectFilterMode(_:)), keyEquivalent: "")
            item.target = self
            item.representedObject = mode.rawValue
            item.state = mode == filterMode ? .on : .off
            menu.addItem(item)
        }
        menu.popUp(positioning: nil, at: NSPoint(x: 0, y: sender.bounds.height + 4), in: sender)
    }

    @objc private func selectFilterMode(_ sender: NSMenuItem) {
        guard let rawValue = sender.representedObject as? String,
              let mode = FilterMode(rawValue: rawValue) else {
            return
        }

        filterMode = mode
        searchDebounceWork?.cancel()
        searchDebounceWork = nil
        applyFilters()
        updateFilterButtonAppearance()
    }

    override func cancelOperation(_ sender: Any?) {
        let hasSearchText = !searchField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
        if hasSearchText {
            searchDebounceWork?.cancel()
            searchDebounceWork = nil
            searchField.stringValue = ""
            applyFilters()
        }
    }

    func controlTextDidChange(_ obj: Notification) {
        searchDebounceWork?.cancel()
        let workItem = DispatchWorkItem { [weak self] in
            self?.searchDebounceWork = nil
            self?.applyFilters()
        }
        searchDebounceWork = workItem
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2, execute: workItem)
    }

    // MARK: - Data + Filtering

    private func applyFilters() {
        let normalizedQuery = searchField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        let entries = allArchivedTabs.map(makeEntry(for:)).filter { entry in
            let matchesQuery: Bool
            if normalizedQuery.isEmpty {
                matchesQuery = true
            } else {
                let haystack = [entry.tab.title, entry.tab.url].joined(separator: " ").lowercased()
                matchesQuery = haystack.contains(normalizedQuery)
            }

            return matchesQuery && filterMode.matches(dayOffset: entry.dayOffset)
        }

        rebuildList(with: groupedSections(from: entries), visibleCount: entries.count)
    }

    private func shouldUseDenseArchivePreviewData(liveTabs: [ArchivedTabViewModel]) -> Bool {
        let arguments = ProcessInfo.processInfo.arguments
        let requestedDensePreview = arguments.contains("--ui-testing")
            || arguments.contains("--ui-testing-open-archive-panel")
            || arguments.contains("--manual-qa-archive-preview")
        guard requestedDensePreview else { return false }

        let liveSections = Set(liveTabs.map { sectionMetadata(forDayOffset: makeEntry(for: $0).dayOffset).title })
        return liveTabs.count < 12 || liveSections.count < 3
    }

    private func denseArchivePreviewTabs() -> [ArchivedTabViewModel] {
        let calendar = Calendar(identifier: .gregorian)
        let now = Date()

        func archivedAt(daysAgo: Int, hour: Int, minute: Int) -> String {
            let startOfDay = calendar.startOfDay(for: now)
            let baseDate = calendar.date(byAdding: .day, value: -daysAgo, to: startOfDay) ?? now
            let dated = calendar.date(bySettingHour: hour, minute: minute, second: 0, of: baseDate) ?? baseDate
            return iso8601Formatters[1].string(from: dated)
        }

        return [
            ArchivedTabViewModel(id: "qa-archive-01", title: "Figma – Archive Layout Explorations", url: "https://www.figma.com/file/archive-layout-explorations", favicon: nil, archivedAt: archivedAt(daysAgo: 0, hour: 9, minute: 12)),
            ArchivedTabViewModel(id: "qa-archive-02", title: "Arc Archive References", url: "https://linear.app/maho/archive-reference-board", favicon: nil, archivedAt: archivedAt(daysAgo: 0, hour: 8, minute: 44)),
            ArchivedTabViewModel(id: "qa-archive-03", title: "Muted Sidebar Tone Study", url: "https://notes.example.com/sidebar-tone-study", favicon: nil, archivedAt: archivedAt(daysAgo: 0, hour: 7, minute: 55)),
            ArchivedTabViewModel(id: "qa-archive-04", title: "Yesterday Design Crit", url: "https://calendar.google.com/calendar/u/0/r/day/2026/04/24", favicon: nil, archivedAt: archivedAt(daysAgo: 1, hour: 18, minute: 5)),
            ArchivedTabViewModel(id: "qa-archive-05", title: "Navigation Chrome Audit", url: "https://github.com/example/maho/issues/248", favicon: nil, archivedAt: archivedAt(daysAgo: 1, hour: 15, minute: 21)),
            ArchivedTabViewModel(id: "qa-archive-06", title: "Search Field Copy Options", url: "https://docs.example.com/archive-copy-options", favicon: nil, archivedAt: archivedAt(daysAgo: 1, hour: 11, minute: 8)),
            ArchivedTabViewModel(id: "qa-archive-07", title: "Screenshot QA Checklist", url: "https://www.notion.so/maho/Screenshot-QA-Checklist", favicon: nil, archivedAt: archivedAt(daysAgo: 2, hour: 16, minute: 42)),
            ArchivedTabViewModel(id: "qa-archive-08", title: "Dense Archive Content Sample", url: "https://airtable.com/archive-density-sample", favicon: nil, archivedAt: archivedAt(daysAgo: 2, hour: 13, minute: 37)),
            ArchivedTabViewModel(id: "qa-archive-09", title: "Keyboard Shortcut Notes", url: "https://docs.example.com/archive-shortcuts", favicon: nil, archivedAt: archivedAt(daysAgo: 4, hour: 10, minute: 19)),
            ArchivedTabViewModel(id: "qa-archive-10", title: "Tab Restore Edge Cases", url: "https://linear.app/maho/issue/restore-edge-cases", favicon: nil, archivedAt: archivedAt(daysAgo: 4, hour: 9, minute: 2)),
            ArchivedTabViewModel(id: "qa-archive-11", title: "Spec Review Recording", url: "https://drive.google.com/file/d/archive-review-recording", favicon: nil, archivedAt: archivedAt(daysAgo: 7, hour: 14, minute: 48)),
            ArchivedTabViewModel(id: "qa-archive-12", title: "Earlier Browser History Cleanup", url: "https://support.example.com/history-cleanup", favicon: nil, archivedAt: archivedAt(daysAgo: 11, hour: 12, minute: 16)),
            ArchivedTabViewModel(id: "qa-archive-13", title: "Archive Information Architecture", url: "https://www.notion.so/maho/Archive-IA", favicon: nil, archivedAt: archivedAt(daysAgo: 16, hour: 10, minute: 6)),
            ArchivedTabViewModel(id: "qa-archive-14", title: "Release Notes Draft", url: "https://docs.example.com/release-notes/archive-mode", favicon: nil, archivedAt: archivedAt(daysAgo: 23, hour: 8, minute: 30))
        ]
    }

    private func rebuildList(with sections: [ArchiveSection], visibleCount: Int) {
        for subview in listStackView.arrangedSubviews {
            listStackView.removeArrangedSubview(subview)
            subview.removeFromSuperview()
        }

        let hasResults = !sections.isEmpty
        scrollView.isHidden = !hasResults
        emptyStateView.isHidden = hasResults
        emptyStateView.configure(
            hasArchivedTabs: !allArchivedTabs.isEmpty,
            hasActiveFilters: filterMode != .all || !searchField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
        )
        updateHeaderCount(visibleCount: visibleCount, totalCount: allArchivedTabs.count)

        guard hasResults else { return }

        for (sectionIndex, section) in sections.enumerated() {
            let headerView = SidebarArchiveSectionHeaderView()
            headerView.translatesAutoresizingMaskIntoConstraints = false
            headerView.configure(title: section.title, count: section.tabs.count)
            listStackView.addArrangedSubview(headerView)
            headerView.widthAnchor.constraint(equalTo: listStackView.widthAnchor).isActive = true
            listStackView.setCustomSpacing(Metrics.sectionHeaderSpacing, after: headerView)

            for (index, tab) in section.tabs.enumerated() {
                let row = SidebarArchiveRowView()
                row.translatesAutoresizingMaskIntoConstraints = false
                row.configure(with: tab)
                row.showsDivider = index < section.tabs.count - 1
                row.onRestore = { [weak self] tabId in
                    self?.onRestoreArchivedTab?(tabId)
                }
                row.onDelete = { [weak self] tabId in
                    self?.onDeleteArchivedTab?(tabId)
                }
                listStackView.addArrangedSubview(row)
                row.widthAnchor.constraint(equalTo: listStackView.widthAnchor).isActive = true
                if index < section.tabs.count - 1 {
                    listStackView.setCustomSpacing(Metrics.rowSpacing, after: row)
                }
            }

            if sectionIndex < sections.count - 1, let lastRow = listStackView.arrangedSubviews.last {
                listStackView.setCustomSpacing(Metrics.sectionSpacing, after: lastRow)
            }
        }
    }

    private func updateHeaderCount(visibleCount: Int, totalCount: Int) {
        let summary: String
        if totalCount == 0 {
            summary = "No tabs"
        } else if visibleCount == totalCount {
            summary = visibleCount == 1 ? "1 tab" : "\(visibleCount) tabs"
        } else {
            summary = "\(visibleCount) of \(totalCount)"
        }

        scrollView.setAccessibilityValue(summary)
    }

    private func groupedSections(from entries: [ArchivedTabEntry]) -> [ArchiveSection] {
        let sortedEntries = entries.sorted(by: { (lhs: ArchivedTabEntry, rhs: ArchivedTabEntry) in
            switch (lhs.archivedDate, rhs.archivedDate) {
            case let (lhsDate?, rhsDate?):
                return lhsDate > rhsDate
            case (_?, nil):
                return true
            case (nil, _?):
                return false
            case (nil, nil):
                return lhs.tab.title.localizedCaseInsensitiveCompare(rhs.tab.title) == .orderedAscending
            }
        })

        var orderedSections: [ArchiveSection] = []
        var sectionIndexByTitle: [String: Int] = [:]

        for entry in sortedEntries {
            let metadata = sectionMetadata(forDayOffset: entry.dayOffset)

            if let index = sectionIndexByTitle[metadata.title] {
                let existing = orderedSections[index]
                orderedSections[index] = ArchiveSection(title: existing.title, sortKey: existing.sortKey, tabs: existing.tabs + [entry.tab])
            } else {
                sectionIndexByTitle[metadata.title] = orderedSections.count
                orderedSections.append(ArchiveSection(title: metadata.title, sortKey: metadata.sortKey, tabs: [entry.tab]))
            }
        }

        return orderedSections.sorted { lhs, rhs in
            if lhs.sortKey != rhs.sortKey {
                return lhs.sortKey < rhs.sortKey
            }
            return lhs.title.localizedCaseInsensitiveCompare(rhs.title) == .orderedAscending
        }
    }

    // MARK: - Date Helpers

    private func makeEntry(for tab: ArchivedTabViewModel) -> ArchivedTabEntry {
        let archivedDate = parsedDate(from: tab.archivedAt)
        return ArchivedTabEntry(
            tab: tab,
            archivedDate: archivedDate,
            dayOffset: archivedDate.map(dayOffset(from:))
        )
    }

    private func parsedDate(from rawValue: String) -> Date? {
        for formatter in iso8601Formatters {
            if let date = formatter.date(from: rawValue) {
                return date
            }
        }

        for formatter in fallbackDateFormatters {
            if let date = formatter.date(from: rawValue) {
                return date
            }
        }

        return nil
    }

    private func dayOffset(from date: Date) -> Int {
        let calendar = Calendar.current
        let today = calendar.startOfDay(for: Date())
        let archivedDay = calendar.startOfDay(for: date)
        return max(0, calendar.dateComponents([.day], from: archivedDay, to: today).day ?? 0)
    }

    private func sectionMetadata(forDayOffset dayOffset: Int?) -> (title: String, sortKey: Int) {
        guard let dayOffset else { return ("Earlier", Int.max) }

        switch dayOffset {
        case 0:
            return ("Today", 0)
        case 1:
            return ("Yesterday", 1)
        case 2...7:
            return ("\(dayOffset) days ago", dayOffset)
        case 8...14:
            return ("Last 2 weeks", 8)
        case 15...21:
            return ("3 weeks ago", 15)
        case 22...28:
            return ("4 weeks ago", 22)
        default:
            return ("Earlier", Int.max)
        }
    }
}

// MARK: - Archive Section Header

private final class SidebarArchiveSectionHeaderView: NSView {
    private let titleLabel = NSTextField(labelWithString: "")
    private let countLabel = NSTextField(labelWithString: "")
    private let trailingSpacerView = NSView()
    private let rowStackView = NSStackView()

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupViews()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupViews()
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        refreshAppearance()
    }

    func configure(title: String, count: Int) {
        titleLabel.stringValue = title
        countLabel.stringValue = "\(count)"
        setAccessibilityElement(true)
        setAccessibilityRole(.group)
        let tabWord = count == 1 ? "tab" : "tabs"
        setAccessibilityLabel("\(title), \(count) archived \(tabWord)")
    }

    func refreshAppearance() {
        titleLabel.textColor = MahoAppColors.archiveSectionHeaderText.withAlphaComponent(0.94)
        countLabel.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.32)
        trailingSpacerView.layer?.backgroundColor = NSColor.clear.cgColor
    }

    private func setupViews() {
        translatesAutoresizingMaskIntoConstraints = false

        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.font = .systemFont(ofSize: 11.5, weight: .semibold)
        titleLabel.setAccessibilityElement(false)

        countLabel.translatesAutoresizingMaskIntoConstraints = false
        countLabel.font = .systemFont(ofSize: 9.5, weight: .medium)
        countLabel.setAccessibilityElement(false)

        trailingSpacerView.translatesAutoresizingMaskIntoConstraints = false
        trailingSpacerView.wantsLayer = true
        trailingSpacerView.setAccessibilityElement(false)

        rowStackView.translatesAutoresizingMaskIntoConstraints = false
        rowStackView.orientation = .horizontal
        rowStackView.alignment = .firstBaseline
        rowStackView.spacing = SidebarArchivePanelView.Metrics.sectionCountGap
        addSubview(rowStackView)
        rowStackView.addArrangedSubview(titleLabel)
        rowStackView.addArrangedSubview(countLabel)
        rowStackView.addArrangedSubview(trailingSpacerView)

        trailingSpacerView.setContentHuggingPriority(.defaultLow, for: .horizontal)
        trailingSpacerView.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)

        NSLayoutConstraint.activate([
            rowStackView.leadingAnchor.constraint(equalTo: leadingAnchor, constant: SidebarArchivePanelView.Metrics.sectionHeaderLeadingInset),
            rowStackView.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -SidebarArchivePanelView.Metrics.sectionHeaderTrailingInset),
            rowStackView.topAnchor.constraint(equalTo: topAnchor, constant: SidebarArchivePanelView.Metrics.sectionHeaderVerticalPadding),
            rowStackView.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -SidebarArchivePanelView.Metrics.sectionHeaderVerticalPadding),
            trailingSpacerView.heightAnchor.constraint(equalToConstant: 0),
        ])

        refreshAppearance()
    }
}

// MARK: - Archive Row

private final class SidebarArchiveRowView: NSView {
    var showsDivider: Bool = true {
        didSet { dividerView.isHidden = !showsDivider }
    }

    private let backgroundView = NSView()
    private let faviconView = NSImageView()
    private let titleLabel = NSTextField(labelWithString: "")
    private let subtitleLabel = NSTextField(labelWithString: "")
    private let textStackView = NSStackView()
    private let restoreGlyphView = NSImageView()
    private let dividerView = NSView()
    private var trackingArea: NSTrackingArea?
    private var tabId: TabId?
    private var isHovered = false

    var onRestore: ((TabId) -> Void)?
    var onDelete: ((TabId) -> Void)?

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupViews()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupViews()
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        refreshAppearance()
    }

    private var usesFallbackFavicon = false

    func configure(with tab: ArchivedTabViewModel) {
        tabId = tab.id

        let resolvedTitle: String
        if tab.title.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            resolvedTitle = URL(string: tab.url)?.host ?? tab.url
        } else {
            resolvedTitle = tab.title
        }

        titleLabel.stringValue = resolvedTitle
        subtitleLabel.stringValue = compactLocation(from: tab.url)

        if let faviconData = tab.favicon,
           let image = NSImage(data: Data(faviconData.data)) {
            usesFallbackFavicon = false
            faviconView.image = image
            faviconView.contentTintColor = nil
        } else {
            usesFallbackFavicon = true
            faviconView.image = MahoIcon.imageForSFSymbol("globe", pointSize: 13, accessibilityDescription: "Archived tab")
            faviconView.contentTintColor = MahoAppColors.secondaryText.withAlphaComponent(0.72)
        }

        setAccessibilityElement(true)
        setAccessibilityRole(.button)
        setAccessibilityIdentifier("sidebar.archive.row.\(tab.id)")
        setAccessibilityLabel("Restore archived tab \(resolvedTitle)")
        menu = makeContextMenu(for: tab.id)

        refreshAppearance()
    }

    func refreshAppearance() {
        backgroundView.layer?.backgroundColor = currentBackgroundColor().cgColor
        backgroundView.layer?.borderColor = currentBorderColor().cgColor
        if usesFallbackFavicon {
            faviconView.contentTintColor = MahoAppColors.secondaryText.withAlphaComponent(0.70)
        }
        titleLabel.textColor = MahoAppColors.primaryText.withAlphaComponent(0.88)
        subtitleLabel.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.48)
        let glyphAppearance = currentRestoreGlyphAppearance()
        restoreGlyphView.contentTintColor = glyphAppearance.tintColor
        restoreGlyphView.alphaValue = glyphAppearance.alpha
        dividerView.layer?.backgroundColor = MahoAppColors.archiveDivider.withAlphaComponent(0.42).cgColor
        dividerView.isHidden = !showsDivider
        needsDisplay = true
    }

    private func setupViews() {
        translatesAutoresizingMaskIntoConstraints = false
        wantsLayer = true
        focusRingType = .exterior

        backgroundView.translatesAutoresizingMaskIntoConstraints = false
        backgroundView.wantsLayer = true
        backgroundView.layer?.cornerRadius = SidebarArchivePanelView.Metrics.rowCornerRadius
        backgroundView.layer?.borderWidth = 1
        addSubview(backgroundView)

        faviconView.translatesAutoresizingMaskIntoConstraints = false
        faviconView.imageScaling = .scaleProportionallyUpOrDown
        faviconView.setContentHuggingPriority(.required, for: .horizontal)
        addSubview(faviconView)

        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.font = .systemFont(ofSize: 12, weight: .medium)
        titleLabel.lineBreakMode = .byTruncatingTail
        titleLabel.maximumNumberOfLines = 1
        titleLabel.setAccessibilityElement(false)

        subtitleLabel.translatesAutoresizingMaskIntoConstraints = false
        subtitleLabel.font = .systemFont(ofSize: 10, weight: .regular)
        subtitleLabel.lineBreakMode = .byTruncatingTail
        subtitleLabel.maximumNumberOfLines = 1
        subtitleLabel.setAccessibilityElement(false)

        textStackView.translatesAutoresizingMaskIntoConstraints = false
        textStackView.orientation = .vertical
        textStackView.alignment = .leading
        textStackView.spacing = SidebarArchivePanelView.Metrics.rowTitleSubtitleSpacing
        textStackView.addArrangedSubview(titleLabel)
        textStackView.addArrangedSubview(subtitleLabel)
        addSubview(textStackView)

        restoreGlyphView.translatesAutoresizingMaskIntoConstraints = false
        restoreGlyphView.image = MahoIcon.imageForSFSymbol("arrow.up.left", pointSize: SidebarArchivePanelView.Metrics.rowGlyphSize, accessibilityDescription: "Restore")
        restoreGlyphView.setAccessibilityElement(false)
        addSubview(restoreGlyphView)

        dividerView.translatesAutoresizingMaskIntoConstraints = false
        dividerView.wantsLayer = true
        addSubview(dividerView)

        NSLayoutConstraint.activate([
            heightAnchor.constraint(equalToConstant: SidebarArchivePanelView.Metrics.rowHeight),

            backgroundView.leadingAnchor.constraint(equalTo: leadingAnchor, constant: SidebarArchivePanelView.Metrics.rowHorizontalInset),
            backgroundView.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -SidebarArchivePanelView.Metrics.rowHorizontalInset),
            backgroundView.topAnchor.constraint(equalTo: topAnchor, constant: SidebarArchivePanelView.Metrics.rowVerticalInset),
            backgroundView.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -SidebarArchivePanelView.Metrics.rowVerticalInset),

            faviconView.leadingAnchor.constraint(equalTo: backgroundView.leadingAnchor, constant: SidebarArchivePanelView.Metrics.rowLeadingInset),
            faviconView.centerYAnchor.constraint(equalTo: textStackView.centerYAnchor, constant: -1),
            faviconView.widthAnchor.constraint(equalToConstant: SidebarArchivePanelView.Metrics.rowFaviconSize),
            faviconView.heightAnchor.constraint(equalToConstant: SidebarArchivePanelView.Metrics.rowFaviconSize),

            textStackView.leadingAnchor.constraint(equalTo: faviconView.trailingAnchor, constant: SidebarArchivePanelView.Metrics.rowTextGap),
            textStackView.centerYAnchor.constraint(equalTo: backgroundView.centerYAnchor),

            restoreGlyphView.leadingAnchor.constraint(greaterThanOrEqualTo: textStackView.trailingAnchor, constant: 8),
            restoreGlyphView.trailingAnchor.constraint(equalTo: backgroundView.trailingAnchor, constant: -SidebarArchivePanelView.Metrics.rowTrailingInset),
            restoreGlyphView.centerYAnchor.constraint(equalTo: titleLabel.centerYAnchor),
            restoreGlyphView.widthAnchor.constraint(equalToConstant: SidebarArchivePanelView.Metrics.rowGlyphSize),
            restoreGlyphView.heightAnchor.constraint(equalToConstant: SidebarArchivePanelView.Metrics.rowGlyphSize),

            dividerView.leadingAnchor.constraint(equalTo: textStackView.leadingAnchor),
            dividerView.trailingAnchor.constraint(equalTo: backgroundView.trailingAnchor, constant: -SidebarArchivePanelView.Metrics.rowTrailingInset),
            dividerView.bottomAnchor.constraint(equalTo: backgroundView.bottomAnchor),
            dividerView.heightAnchor.constraint(equalToConstant: 0.5),
        ])
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let trackingArea {
            removeTrackingArea(trackingArea)
        }

        trackingArea = NSTrackingArea(
            rect: bounds,
            options: [.activeInActiveApp, .mouseEnteredAndExited, .inVisibleRect],
            owner: self,
            userInfo: nil
        )

        if let trackingArea {
            addTrackingArea(trackingArea)
        }
    }

    override func mouseEntered(with event: NSEvent) {
        isHovered = true
        updateInteractiveAppearance()
    }

    override func mouseExited(with event: NSEvent) {
        isHovered = false
        updateInteractiveAppearance()
    }

    override var acceptsFirstResponder: Bool { true }
    override var canBecomeKeyView: Bool { true }

    override func becomeFirstResponder() -> Bool {
        let accepted = super.becomeFirstResponder()
        if accepted {
            needsDisplay = true
            updateInteractiveAppearance()
        }
        return accepted
    }

    override func resignFirstResponder() -> Bool {
        let resigned = super.resignFirstResponder()
        if resigned {
            needsDisplay = true
            updateInteractiveAppearance()
        }
        return resigned
    }

    override func mouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        super.mouseDown(with: event)
    }

    override func rightMouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        super.rightMouseDown(with: event)
    }

    override func mouseUp(with event: NSEvent) {
        guard bounds.contains(convert(event.locationInWindow, from: nil)), let tabId else {
            return
        }
        onRestore?(tabId)
    }

    override func keyDown(with event: NSEvent) {
        let characters = event.charactersIgnoringModifiers ?? ""
        if event.keyCode == 36 || event.keyCode == 49 || characters == "\r" || characters == " " {
            if let tabId {
                onRestore?(tabId)
                return
            }
        }
        if event.keyCode == 51 {
            if let tabId {
                onDelete?(tabId)
                return
            }
        }
        super.keyDown(with: event)
    }

    override func accessibilityPerformPress() -> Bool {
        guard let tabId else { return false }
        onRestore?(tabId)
        return true
    }

    override func drawFocusRingMask() {
        NSBezierPath(roundedRect: backgroundView.frame.insetBy(dx: 1, dy: 1), xRadius: SidebarArchivePanelView.Metrics.rowCornerRadius, yRadius: SidebarArchivePanelView.Metrics.rowCornerRadius).fill()
    }

    override var focusRingMaskBounds: NSRect {
        bounds
    }

    private func makeContextMenu(for tabId: TabId) -> NSMenu {
        let menu = NSMenu()

        let restoreItem = NSMenuItem(title: "Restore", action: #selector(handleRestoreMenuAction(_:)), keyEquivalent: "")
        restoreItem.target = self
        restoreItem.representedObject = tabId
        menu.addItem(restoreItem)

        let deleteItem = NSMenuItem(title: "Delete", action: #selector(handleDeleteMenuAction(_:)), keyEquivalent: "")
        deleteItem.target = self
        deleteItem.representedObject = tabId
        menu.addItem(deleteItem)

        return menu
    }

    @objc private func handleRestoreMenuAction(_ sender: NSMenuItem) {
        guard let tabId = sender.representedObject as? TabId else { return }
        onRestore?(tabId)
    }

    @objc private func handleDeleteMenuAction(_ sender: NSMenuItem) {
        guard let tabId = sender.representedObject as? TabId else { return }
        onDelete?(tabId)
    }

    private func updateInteractiveAppearance() {
        backgroundView.layer?.backgroundColor = currentBackgroundColor().cgColor
        backgroundView.layer?.borderColor = currentBorderColor().cgColor
        let glyphAppearance = currentRestoreGlyphAppearance()
        restoreGlyphView.alphaValue = glyphAppearance.alpha
        restoreGlyphView.contentTintColor = glyphAppearance.tintColor
    }

    private func currentRestoreGlyphAppearance() -> (alpha: CGFloat, tintColor: NSColor) {
        let isEmphasized = isHovered || window?.firstResponder === self
        return (
            alpha: isEmphasized ? 0.68 : 0.22,
            tintColor: isEmphasized ? MahoAppColors.secondaryText : MahoAppColors.secondaryText.withAlphaComponent(0.22)
        )
    }

    private func currentBackgroundColor() -> NSColor {
        if window?.firstResponder === self {
            return MahoAppColors.archiveRowSelected
        }
        if isHovered {
            return MahoAppColors.archiveRowHover
        }
        return MahoAppColors.archivePanelBackground.withAlphaComponent(0.05)
    }

    private func currentBorderColor() -> NSColor {
        if window?.firstResponder === self {
            return MahoAppColors.archiveControlBorder
        }
        if isHovered {
            return MahoAppColors.archiveControlBorder
        }
        return MahoAppColors.archiveControlBorder.withAlphaComponent(0.04)
    }

    private func compactLocation(from rawUrl: String) -> String {
        guard let url = URL(string: rawUrl), let host = url.host, !host.isEmpty else {
            return rawUrl
        }

        let path = url.path.trimmingCharacters(in: CharacterSet(charactersIn: "/"))
        if path.isEmpty {
            return host
        }

        let compactPath = path.count > 28 ? String(path.prefix(25)) + "…" : path
        return "\(host) / \(compactPath)"
    }
}

// MARK: - Archive Empty State

private final class SidebarArchiveEmptyStateView: NSView {
    private let silhouetteView = NSView()
    private let iconView = NSImageView()
    private let titleLabel = NSTextField(labelWithString: "")
    private let subtitleLabel = NSTextField(labelWithString: "")
    private let stackView = NSStackView()

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupViews()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupViews()
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        refreshAppearance()
    }

    func configure(hasArchivedTabs: Bool, hasActiveFilters: Bool) {
        if hasArchivedTabs && hasActiveFilters {
            titleLabel.stringValue = "No matching tabs"
            subtitleLabel.stringValue = "Clear the search or adjust the filter."
            subtitleLabel.isHidden = false
        } else {
            titleLabel.stringValue = "No archived tabs yet"
            subtitleLabel.stringValue = ""
            subtitleLabel.isHidden = true
        }
    }

    func refreshAppearance() {
        silhouetteView.layer?.backgroundColor = MahoAppColors.archiveControlBackground.withAlphaComponent(0.24).cgColor
        silhouetteView.layer?.borderColor = MahoAppColors.archiveControlBorder.withAlphaComponent(0.30).cgColor
        iconView.contentTintColor = MahoAppColors.secondaryText.withAlphaComponent(0.34)
        titleLabel.textColor = MahoAppColors.primaryText.withAlphaComponent(0.72)
        subtitleLabel.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.48)
    }

    private func setupViews() {
        translatesAutoresizingMaskIntoConstraints = false

        setAccessibilityElement(true)
        setAccessibilityRole(.group)
        setAccessibilityIdentifier("sidebar.archive.empty")
        setAccessibilityLabel("Archive Empty State")

        silhouetteView.translatesAutoresizingMaskIntoConstraints = false
        silhouetteView.wantsLayer = true
        silhouetteView.layer?.cornerRadius = 16
        silhouetteView.layer?.borderWidth = 1
        addSubview(silhouetteView)

        iconView.translatesAutoresizingMaskIntoConstraints = false
        iconView.image = MahoIcon.imageForSFSymbol("archivebox", pointSize: 13, accessibilityDescription: "Archive")
        iconView.setAccessibilityElement(false)

        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.font = .systemFont(ofSize: 11, weight: .semibold)
        titleLabel.alignment = .center
        titleLabel.setAccessibilityElement(false)

        subtitleLabel.translatesAutoresizingMaskIntoConstraints = false
        subtitleLabel.font = .systemFont(ofSize: 10, weight: .regular)
        subtitleLabel.alignment = .center
        subtitleLabel.maximumNumberOfLines = 2
        subtitleLabel.lineBreakMode = .byWordWrapping
        subtitleLabel.setAccessibilityElement(false)

        stackView.translatesAutoresizingMaskIntoConstraints = false
        stackView.orientation = .vertical
        stackView.alignment = .centerX
        stackView.spacing = 4
        stackView.addArrangedSubview(iconView)
        stackView.addArrangedSubview(titleLabel)
        stackView.addArrangedSubview(subtitleLabel)
        addSubview(stackView)

        NSLayoutConstraint.activate([
            silhouetteView.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 16),
            silhouetteView.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -16),
            silhouetteView.centerYAnchor.constraint(equalTo: centerYAnchor),
            silhouetteView.heightAnchor.constraint(equalToConstant: 100),

            stackView.leadingAnchor.constraint(equalTo: silhouetteView.leadingAnchor, constant: 16),
            stackView.trailingAnchor.constraint(equalTo: silhouetteView.trailingAnchor, constant: -16),
            stackView.centerXAnchor.constraint(equalTo: silhouetteView.centerXAnchor),
            stackView.centerYAnchor.constraint(equalTo: silhouetteView.centerYAnchor),
            subtitleLabel.widthAnchor.constraint(lessThanOrEqualToConstant: 220),
        ])

        configure(hasArchivedTabs: false, hasActiveFilters: false)
        refreshAppearance()
    }
}
