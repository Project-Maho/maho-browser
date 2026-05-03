import AppKit

final class BoostMarketplaceController: NSWindowController, NSTableViewDataSource, NSTableViewDelegate, NSSearchFieldDelegate {
    private let categoryTable = NSTableView()
    private let boostTable = NSTableView()
    private let searchField = NSSearchField()
    private let previewTextView = NSTextView()

    private let categories = ["All", "Productivity", "Appearance", "Privacy", "Social", "Developer"]
    private var selectedCategory = "All"

    private var allBoosts: [MarketplaceBoost] = []
    private var filteredBoosts: [MarketplaceBoost] = []

    struct MarketplaceBoost {
        let id: String
        let name: String
        let description: String
        let author: String
        let category: String
        let urlPattern: String
        let customCss: String
        let customJs: String
        let installCount: Int
    }

    init() {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 700, height: 500),
            styleMask: [.titled, .closable, .resizable, .miniaturizable],
            backing: .buffered,
            defer: false
        )
        window.title = "Boost Marketplace"
        window.center()
        window.minSize = NSSize(width: 600, height: 400)
        super.init(window: window)
        setupUI()
        loadCatalog()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
    }

    private func setupUI() {
        guard let contentView = window?.contentView else { return }

        let mainSplit = NSSplitView()
        mainSplit.translatesAutoresizingMaskIntoConstraints = false
        mainSplit.dividerStyle = .thin
        mainSplit.isVertical = true
        contentView.addSubview(mainSplit)

        NSLayoutConstraint.activate([
            mainSplit.topAnchor.constraint(equalTo: contentView.topAnchor),
            mainSplit.leadingAnchor.constraint(equalTo: contentView.leadingAnchor),
            mainSplit.trailingAnchor.constraint(equalTo: contentView.trailingAnchor),
            mainSplit.bottomAnchor.constraint(equalTo: contentView.bottomAnchor),
        ])

        let sidebarView = NSView()
        sidebarView.translatesAutoresizingMaskIntoConstraints = false

        let catColumn = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("CategoryColumn"))
        catColumn.title = "Categories"
        categoryTable.addTableColumn(catColumn)
        categoryTable.headerView = nil
        categoryTable.delegate = self
        categoryTable.dataSource = self

        let catScroll = NSScrollView()
        catScroll.translatesAutoresizingMaskIntoConstraints = false
        catScroll.documentView = categoryTable
        catScroll.hasVerticalScroller = true
        sidebarView.addSubview(catScroll)

        NSLayoutConstraint.activate([
            catScroll.topAnchor.constraint(equalTo: sidebarView.topAnchor),
            catScroll.leadingAnchor.constraint(equalTo: sidebarView.leadingAnchor),
            catScroll.trailingAnchor.constraint(equalTo: sidebarView.trailingAnchor),
            catScroll.bottomAnchor.constraint(equalTo: sidebarView.bottomAnchor),
            sidebarView.widthAnchor.constraint(greaterThanOrEqualToConstant: 130),
        ])

        let rightView = NSView()
        rightView.translatesAutoresizingMaskIntoConstraints = false

        searchField.translatesAutoresizingMaskIntoConstraints = false
        searchField.placeholderString = "Search boosts..."
        searchField.delegate = self
        rightView.addSubview(searchField)

        let nameCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("NameCol"))
        nameCol.title = "Name"
        nameCol.width = 150
        boostTable.addTableColumn(nameCol)

        let catCol2 = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("CatCol"))
        catCol2.title = "Category"
        catCol2.width = 80
        boostTable.addTableColumn(catCol2)

        let installsCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("InstallsCol"))
        installsCol.title = "Installs"
        installsCol.width = 60
        boostTable.addTableColumn(installsCol)

        let actionsCol = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("ActionsCol"))
        actionsCol.title = ""
        actionsCol.width = 70
        boostTable.addTableColumn(actionsCol)

        boostTable.delegate = self
        boostTable.dataSource = self

        let boostScroll = NSScrollView()
        boostScroll.translatesAutoresizingMaskIntoConstraints = false
        boostScroll.documentView = boostTable
        boostScroll.hasVerticalScroller = true
        rightView.addSubview(boostScroll)

        let previewLabel = NSTextField(labelWithString: "Preview")
        previewLabel.translatesAutoresizingMaskIntoConstraints = false
        previewLabel.font = NSFont.boldSystemFont(ofSize: 11)
        rightView.addSubview(previewLabel)

        let previewScroll = NSScrollView()
        previewScroll.translatesAutoresizingMaskIntoConstraints = false
        previewScroll.hasVerticalScroller = true
        previewScroll.documentView = previewTextView
        previewTextView.isEditable = false
        previewTextView.isRichText = false
        previewTextView.font = NSFont.userFixedPitchFont(ofSize: 11)
        rightView.addSubview(previewScroll)

        NSLayoutConstraint.activate([
            searchField.topAnchor.constraint(equalTo: rightView.topAnchor, constant: 8),
            searchField.leadingAnchor.constraint(equalTo: rightView.leadingAnchor, constant: 8),
            searchField.trailingAnchor.constraint(equalTo: rightView.trailingAnchor, constant: -8),

            boostScroll.topAnchor.constraint(equalTo: searchField.bottomAnchor, constant: 8),
            boostScroll.leadingAnchor.constraint(equalTo: rightView.leadingAnchor),
            boostScroll.trailingAnchor.constraint(equalTo: rightView.trailingAnchor),

            previewLabel.topAnchor.constraint(equalTo: boostScroll.bottomAnchor, constant: 4),
            previewLabel.leadingAnchor.constraint(equalTo: rightView.leadingAnchor, constant: 8),

            previewScroll.topAnchor.constraint(equalTo: previewLabel.bottomAnchor, constant: 2),
            previewScroll.leadingAnchor.constraint(equalTo: rightView.leadingAnchor),
            previewScroll.trailingAnchor.constraint(equalTo: rightView.trailingAnchor),
            previewScroll.bottomAnchor.constraint(equalTo: rightView.bottomAnchor),
            previewScroll.heightAnchor.constraint(equalToConstant: 120),
        ])

        mainSplit.addArrangedSubview(sidebarView)
        mainSplit.addArrangedSubview(rightView)
        mainSplit.setHoldingPriority(.defaultLow, forSubviewAt: 1)
    }

    private func loadCatalog() {
        allBoosts = [
            MarketplaceBoost(id: "dark-reader", name: "Dark Reader", description: "Dark mode for every website", author: "Community", category: "Appearance", urlPattern: "*", customCss: "html { filter: invert(0.9) hue-rotate(180deg); } img { filter: invert(1) hue-rotate(180deg); }", customJs: "", installCount: 15000),
            MarketplaceBoost(id: "youtube-enhancer", name: "YouTube Enhancer", description: "Hide distractions on YouTube", author: "Community", category: "Productivity", urlPattern: "*.youtube.com/*", customCss: "#secondary, #comments { display: none !important; }", customJs: "", installCount: 8500),
            MarketplaceBoost(id: "twitter-declutter", name: "Twitter/X Declutter", description: "Clean Twitter interface", author: "Community", category: "Social", urlPattern: "*.twitter.com/*,*.x.com/*", customCss: "[data-testid='trend'] { display: none !important; }", customJs: "", installCount: 6200),
            MarketplaceBoost(id: "github-enhanced", name: "GitHub Enhanced", description: "Better GitHub UI", author: "Community", category: "Developer", urlPattern: "*.github.com/*", customCss: ".js-header-wrapper { background: #1a1a2e !important; }", customJs: "", installCount: 9100),
            MarketplaceBoost(id: "privacy-guard", name: "Privacy Guard", description: "Block tracking scripts", author: "Community", category: "Privacy", urlPattern: "*", customCss: "", customJs: "document.querySelectorAll('iframe[src*=\"analytics\"]').forEach(el => el.remove());", installCount: 4300),
            MarketplaceBoost(id: "speed-reader", name: "Speed Reader", description: "Highlight text for speed reading", author: "Community", category: "Productivity", urlPattern: "*", customCss: "", customJs: "", installCount: 3200),
            MarketplaceBoost(id: "minimal-medium", name: "Minimal Medium", description: "Clean Medium reading", author: "Community", category: "Appearance", urlPattern: "*.medium.com/*", customCss: ".metabar, .postActionsBar { display: none !important; }", customJs: "", installCount: 5100),
            MarketplaceBoost(id: "reddit-old", name: "Old Reddit Style", description: "Classic Reddit look", author: "Community", category: "Appearance", urlPattern: "*.reddit.com/*", customCss: "", customJs: "", installCount: 7800),
            MarketplaceBoost(id: "focus-mode", name: "Focus Mode", description: "Hide all sidebars and distractions", author: "Community", category: "Productivity", urlPattern: "*", customCss: "aside, [role='complementary'] { display: none !important; }", customJs: "", installCount: 2900),
            MarketplaceBoost(id: "cookie-cleaner", name: "Cookie Consent Hider", description: "Auto-hide cookie banners", author: "Community", category: "Privacy", urlPattern: "*", customCss: "[class*='cookie'], [id*='cookie'], [class*='consent'] { display: none !important; }", customJs: "", installCount: 11000),
        ]
        filterBoosts()
    }

    private func filterBoosts() {
        let query = searchField.stringValue.lowercased()
        filteredBoosts = allBoosts.filter { boost in
            let matchesCategory = selectedCategory == "All" || boost.category == selectedCategory
            let matchesSearch = query.isEmpty || boost.name.lowercased().contains(query) || boost.description.lowercased().contains(query)
            return matchesCategory && matchesSearch
        }
        boostTable.reloadData()
    }

    func numberOfRows(in tableView: NSTableView) -> Int {
        if tableView === categoryTable { return categories.count }
        return filteredBoosts.count
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        if tableView === categoryTable {
            return makeCategoryCell(row: row)
        }
        guard row < filteredBoosts.count, let colId = tableColumn?.identifier.rawValue else { return nil }
        let boost = filteredBoosts[row]

        switch colId {
        case "NameCol":
            return makeLabel("\(boost.name)\n\(boost.description)", row: row)
        case "CatCol":
            return makeLabel(boost.category, row: row)
        case "InstallsCol":
            return makeLabel(formatCount(boost.installCount), row: row)
        case "ActionsCol":
            return makeInstallButton(row: row)
        default:
            return nil
        }
    }

    func tableView(_ tableView: NSTableView, heightOfRow row: Int) -> CGFloat {
        if tableView === categoryTable { return 24 }
        return 40
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        guard let tableView = notification.object as? NSTableView else { return }
        if tableView === categoryTable {
            let row = categoryTable.selectedRow
            selectedCategory = row >= 0 ? categories[row] : "All"
            filterBoosts()
        } else if tableView === boostTable {
            let row = boostTable.selectedRow
            if row >= 0, row < filteredBoosts.count {
                showPreview(filteredBoosts[row])
            }
        }
    }

    func controlTextDidChange(_ obj: Notification) {
        if (obj.object as? NSSearchField) === searchField {
            filterBoosts()
        }
    }

    @objc private func installClicked(_ sender: NSButton) {
        let button = sender
        let row = boostTable.row(for: button)
        guard row >= 0, row < filteredBoosts.count else { return }
        let boost = filteredBoosts[row]

        let pattern = UrlPattern(pattern: boost.urlPattern, patternType: .glob)
        CoreUpdateDispatcher.shared.sendAndDispatch(.createBoost(urlPattern: pattern))

        let boosts = MahoBridge.shared.getBoostViewModels()
        if let created = boosts.last {
            let css = boost.customCss.isEmpty ? nil : boost.customCss
            let js = boost.customJs.isEmpty ? nil : boost.customJs
            let changes = BoostUpdate(
                urlPattern: pattern,
                customCss: css,
                customJs: js,
                colorTheme: nil,
                fontOverride: nil,
                enabled: true,
                spaceScope: nil
            )
            CoreUpdateDispatcher.shared.sendAndDispatch(.updateBoost(boostId: created.id, changes: changes))
        }

        sender.title = "Installed"
        sender.isEnabled = false
    }

    private func showPreview(_ boost: MarketplaceBoost) {
        var text = "-- \(boost.name) by \(boost.author) --\n"
        if !boost.customCss.isEmpty {
            text += "\nCSS:\n\(boost.customCss)\n"
        }
        if !boost.customJs.isEmpty {
            text += "\nJS:\n\(boost.customJs)\n"
        }
        if boost.customCss.isEmpty && boost.customJs.isEmpty {
            text += "\n(No CSS or JS content)"
        }
        previewTextView.string = text
    }

    private func makeCategoryCell(row: Int) -> NSView {
        let cell = NSTableCellView()
        let tf = NSTextField(labelWithString: categories[row])
        tf.translatesAutoresizingMaskIntoConstraints = false
        cell.textField = tf
        cell.addSubview(tf)
        NSLayoutConstraint.activate([
            tf.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 8),
            tf.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
        ])
        return cell
    }

    private func makeLabel(_ text: String, row: Int) -> NSView {
        let cell = NSTableCellView()
        let tf = NSTextField(labelWithString: text)
        tf.translatesAutoresizingMaskIntoConstraints = false
        tf.maximumNumberOfLines = 2
        tf.cell?.lineBreakMode = .byTruncatingTail
        cell.textField = tf
        cell.addSubview(tf)
        NSLayoutConstraint.activate([
            tf.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 4),
            tf.trailingAnchor.constraint(equalTo: cell.trailingAnchor, constant: -4),
            tf.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
        ])
        return cell
    }

    private func makeInstallButton(row: Int) -> NSView {
        let container = NSView()
        let btn = NSButton(title: "Install", target: self, action: #selector(installClicked(_:)))
        btn.bezelStyle = .rounded
        btn.controlSize = .small
        btn.tag = row
        btn.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(btn)
        NSLayoutConstraint.activate([
            btn.centerXAnchor.constraint(equalTo: container.centerXAnchor),
            btn.centerYAnchor.constraint(equalTo: container.centerYAnchor),
        ])
        return container
    }

    private func formatCount(_ n: Int) -> String {
        if n >= 1000 {
            return String(format: "%.1fk", Double(n) / 1000.0)
        }
        return "\(n)"
    }
}
