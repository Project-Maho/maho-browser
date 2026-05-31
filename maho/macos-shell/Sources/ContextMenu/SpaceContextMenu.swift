import AppKit
import UniformTypeIdentifiers

enum SpacePresentationPresets {
    static let colors: [(title: String, color: SpaceColor)] = [
        ("Cream", SpaceColor(hue: 0.10, saturation: 0.15, brightness: 0.95)),
        ("Pink", SpaceColor(hue: 0.92, saturation: 0.8, brightness: 0.9)),
        ("Purple", SpaceColor(hue: 0.75, saturation: 0.8, brightness: 0.9)),
        ("Red", SpaceColor(hue: 0.0, saturation: 0.8, brightness: 0.9)),
        ("Orange", SpaceColor(hue: 0.08, saturation: 0.8, brightness: 0.9)),
        ("Yellow", SpaceColor(hue: 0.15, saturation: 0.8, brightness: 0.9)),
        ("Green", SpaceColor(hue: 0.35, saturation: 0.8, brightness: 0.9)),
        ("Blue", SpaceColor(hue: 0.58, saturation: 0.8, brightness: 0.9)),
        ("Indigo", SpaceColor(hue: 0.62, saturation: 0.3, brightness: 0.55)),
    ]

    static let icons: [(title: String, symbolName: String?)] = [
        ("None", nil),
        ("House", "house"),
        ("Briefcase", "briefcase"),
        ("Star", "star"),
        ("Bookmark", "bookmark"),
        ("Bolt", "bolt"),
        ("Globe", "globe"),
        ("Message", "message"),
        ("Game Controller", "gamecontroller"),
        ("Music", "music.note"),
        ("Camera", "camera"),
        ("Leaf", "leaf"),
    ]
}

final class SpaceContextMenu {
    static func presentCreateSpaceSheet(defaultColor: SpaceColor, profileId: ProfileId, from view: NSView?) {
        let controller = SpaceCreateSheetController(defaultColor: defaultColor, profileId: profileId)

        if let vc = view?.window?.contentViewController {
            vc.presentAsSheet(controller)
            return
        }
        if let vc = NSApp.mainWindow?.contentViewController {
            vc.presentAsSheet(controller)
            return
        }
        guard let window = view?.window ?? NSApp.mainWindow ?? NSApp.keyWindow else { return }
        let sheetWindow = NSWindow(contentViewController: controller)
        sheetWindow.styleMask = [.titled]
        window.beginSheet(sheetWindow)
    }

    static func createFolder(in spaceId: SpaceId, isPinned: Bool = false) {
        CoreUpdateDispatcher.shared.sendAndDispatch(.createFolder(spaceId: spaceId, name: "New Folder", isPinned: isPinned))
        DispatchQueue.main.async {
            NotificationCenter.default.post(name: .mahoSidebarRefreshRequested, object: nil)
        }
    }

    static func createTab(in spaceId: SpaceId) {
        (NSApp.delegate as? AppDelegate)?.showCommandBarForNewTab(spaceId: spaceId)
        DispatchQueue.main.async {
            NotificationCenter.default.post(name: .mahoSidebarRefreshRequested, object: nil)
        }
    }

    static func presentThemeColorEditor(for space: SpaceViewModel, from window: NSWindow?) {
        presentSpaceConfigSheet(space: space, initialFocus: .color, from: window)
    }

    private static func presentSpaceConfigSheet(
        space: SpaceViewModel,
        initialFocus: SpaceConfigSheetController.InitialFocus,
        from window: NSWindow?
    ) {
        let editor = SpaceConfigSheetController(space: space, initialFocus: initialFocus)

        if let vc = window?.contentViewController {
            vc.presentAsSheet(editor)
            return
        }
        if let vc = NSApp.mainWindow?.contentViewController {
            vc.presentAsSheet(editor)
            return
        }
        guard let resolvedWindow = window ?? NSApp.mainWindow ?? NSApp.keyWindow else { return }
        let sheetWindow = NSWindow(contentViewController: editor)
        sheetWindow.styleMask = [.titled]
        resolvedWindow.beginSheet(sheetWindow)
    }

    static func menu(for space: SpaceViewModel, isPinned: Bool = false) -> NSMenu {
        let menu = NSMenu()
        addSection(menu, items: editItems(for: space))
        addSection(menu, items: organizeItems(for: space, isPinned: isPinned))

        addSection(menu, items: shareItems(for: space))
        addSection(menu, items: destructiveItems(for: space))
        return menu
    }

    private static func addSection(_ menu: NSMenu, items: [NSMenuItem]) {
        guard !items.isEmpty else { return }
        if menu.numberOfItems > 0 {
            menu.addItem(.separator())
        }
        items.forEach { menu.addItem($0) }
    }

    private static func editItems(for space: SpaceViewModel) -> [NSMenuItem] {
        let exportThemeItem = NSMenuItem(title: "Export Theme…", action: {
            guard let theme = space.theme else {
                NSSound.beep()
                return
            }
            let savePanel = NSSavePanel()
            savePanel.allowedContentTypes = [UTType(filenameExtension: "mahotheme") ?? .json]
            savePanel.nameFieldStringValue = "\(space.name).mahotheme"
            savePanel.title = "Export Theme"
            guard savePanel.runModal() == .OK, let url = savePanel.url else { return }
            do {
                let encoder = JSONEncoder()
                encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
                let data = try encoder.encode(theme)
                try data.write(to: url)
            } catch {
                let alert = NSAlert()
                alert.messageText = "Export Failed"
                alert.informativeText = error.localizedDescription
                alert.runModal()
            }
        })
        exportThemeItem.setAccessibilityIdentifier("space.contextMenu.exportTheme")

        let importThemeItem = NSMenuItem(title: "Import Theme…", action: {
            let openPanel = NSOpenPanel()
            openPanel.allowedContentTypes = [UTType(filenameExtension: "mahotheme") ?? .json, .json]
            openPanel.allowsMultipleSelection = false
            openPanel.canChooseDirectories = false
            openPanel.canChooseFiles = true
            openPanel.title = "Import Theme"
            guard openPanel.runModal() == .OK, let url = openPanel.url else { return }
            do {
                let data = try Data(contentsOf: url)
                let theme = try JSONDecoder().decode(SpaceTheme.self, from: data)
                let changes = SpaceConfigUpdate(spaceId: space.id, name: nil, color: nil, theme: theme, icon: nil, profileId: nil)
                MahoBridge.shared.updateSpaceConfig(changes)
            } catch {
                let alert = NSAlert()
                alert.messageText = "Import Failed"
                alert.informativeText = "The file does not contain a valid theme."
                alert.runModal()
            }
        })
        importThemeItem.setAccessibilityIdentifier("space.contextMenu.importTheme")

        return [
            NSMenuItem(title: "Change Space Icon", action: {
                presentSpaceConfigSheet(space: space, initialFocus: .icon, from: NSApp.mainWindow)
            }),
            NSMenuItem(title: "Rename Space", action: {
                presentSpaceConfigSheet(space: space, initialFocus: .name, from: NSApp.mainWindow)
            }),
            NSMenuItem(title: "Edit Theme Color", action: {
                let window = NSApp.mainWindow
                if let browserWindowController = window?.windowController as? BrowserWindowController {
                    browserWindowController.showCreateThemeSidebarPlaceholder(for: space)
                } else {
                    presentThemeColorEditor(for: space, from: window)
                }
            }),
            exportThemeItem,
            importThemeItem,
            NSMenuItem(title: "Set Profile", action: {
                presentSpaceConfigSheet(space: space, initialFocus: .profile, from: NSApp.mainWindow)
            })
        ]
    }

    private static func organizeItems(for space: SpaceViewModel, isPinned: Bool) -> [NSMenuItem] {
        let exportItem = NSMenuItem(title: "Export \"\(space.name)\" into Folder", action: nil, keyEquivalent: "")
        exportItem.submenu = exportTargetMenu(for: space)

        let liveFoldersItem = NSMenuItem(title: "Live Folders", action: nil, keyEquivalent: "")
        liveFoldersItem.submenu = liveFoldersSubmenu(for: space)

        return [
            NSMenuItem(title: "New Folder", action: {
                createFolder(in: space.id, isPinned: isPinned)
            }),
            liveFoldersItem,
            exportItem,
            NSMenuItem(title: "Manage Spaces", action: {
                guard let window = NSApp.mainWindow else { return }
                let manageVC = ManageSpacesController()
                window.contentViewController?.presentAsSheet(manageVC)
            })
        ]
    }

    private static func liveFoldersSubmenu(for space: SpaceViewModel) -> NSMenu {
        let submenu = NSMenu(title: "Live Folders")
        let existingFolders = LiveFolderManager.shared.getFolders().filter { $0.spaceId == nil || $0.spaceId == space.id }
        for folder in existingFolders {
            let itemCount = LiveFolderManager.shared.getItems(for: folder.id).count
            let title = "\(folder.name) (\(itemCount))"
            let folderItem = NSMenuItem(title: title, action: nil, keyEquivalent: "")

            let folderSubmenu = NSMenu(title: folder.name)
            let items = LiveFolderManager.shared.getItems(for: folder.id)
            if items.isEmpty {
                let emptyItem = NSMenuItem(title: "No items", action: nil, keyEquivalent: "")
                emptyItem.isEnabled = false
                folderSubmenu.addItem(emptyItem)
            } else {
                for item in items.prefix(15) {
                    folderSubmenu.addItem(NSMenuItem(title: item.title, action: {
                        if let url = URL(string: item.url) {
                            NSWorkspace.shared.open(url)
                        }
                    }))
                }
                if items.count > 15 {
                    let moreItem = NSMenuItem(title: "… \(items.count - 15) more", action: nil, keyEquivalent: "")
                    moreItem.isEnabled = false
                    folderSubmenu.addItem(moreItem)
                }
            }
            folderSubmenu.addItem(.separator())
            folderSubmenu.addItem(NSMenuItem(title: "Remove Folder", action: {
                LiveFolderManager.shared.removeFolder(id: folder.id)
                NotificationCenter.default.post(name: .mahoSidebarRefreshRequested, object: nil)
            }))
            folderItem.submenu = folderSubmenu
            submenu.addItem(folderItem)
        }
        if !existingFolders.isEmpty {
            submenu.addItem(.separator())
        }
        submenu.addItem(NSMenuItem(title: "Add Live Folder…", action: {
            guard let window = NSApp.mainWindow else { return }
            let addVC = AddLiveFolderController()
            addVC.spaceId = space.id
            addVC.onAdd = {
                NotificationCenter.default.post(name: .mahoSidebarRefreshRequested, object: nil)
            }
            window.contentViewController?.presentAsSheet(addVC)
        }))
        return submenu
    }


    private static func shareItems(for space: SpaceViewModel) -> [NSMenuItem] {
        [
            NSMenuItem(title: "Share Space…", action: {
                guard let window = NSApp.mainWindow else { return }
                let sharingVC = SharingSheetController(spaceId: space.id, spaceName: space.name)
                window.contentViewController?.presentAsSheet(sharingVC)
            })
        ]
    }

    private static func destructiveItems(for space: SpaceViewModel) -> [NSMenuItem] {
        [
            NSMenuItem(title: "Delete Space", action: {
                let alert = NSAlert()
                alert.messageText = "Delete \"\(space.name)\"?"
                alert.informativeText = "All tabs in this space will be closed."
                alert.alertStyle = .warning
                alert.addButton(withTitle: "Delete")
                alert.addButton(withTitle: "Cancel")
                guard let window = NSApp.mainWindow else { return }
                alert.beginSheetModal(for: window) { response in
                    if response == .alertFirstButtonReturn {
                        CoreUpdateDispatcher.shared.sendAndDispatch(.deleteSpace(spaceId: space.id))
                    }
                }
            })
        ]
    }

    private static func exportTargetMenu(for space: SpaceViewModel) -> NSMenu {
        let submenu = NSMenu(title: "Export Target")
        let targets = MahoBridge.shared.getCanonicalSpaceViewModels().filter { $0.id != space.id }
        for target in targets {
            submenu.addItem(NSMenuItem(title: target.name, action: {
                confirmExport(space: space, targetSpace: target)
            }))
        }
        if targets.isEmpty {
            submenu.addItem(NSMenuItem(title: "No other spaces", action: nil, keyEquivalent: ""))
            submenu.items.first?.isEnabled = false
        }
        return submenu
    }

    private static func presentExportFailure(_ error: MahoError, in window: NSWindow) {
        let alert = NSAlert()
        alert.messageText = "Export Failed"
        alert.informativeText = "\(error.message)\nError code: \(error.code)"
        alert.alertStyle = .warning
        alert.addButton(withTitle: "OK")
        alert.beginSheetModal(for: window)
    }

    private static func confirmExport(space: SpaceViewModel, targetSpace: SpaceViewModel) {
        guard let window = NSApp.mainWindow else { return }
        let alert = NSAlert()
        alert.messageText = "Export \"\(space.name)\" into \"\(targetSpace.name)\"?"
        alert.informativeText = "This will move the space contents into a new folder in the target space. Folder hierarchy will be flattened during export."
        alert.alertStyle = .warning
        alert.addButton(withTitle: "Export")
        alert.addButton(withTitle: "Cancel")
        alert.beginSheetModal(for: window) { response in
            guard response == .alertFirstButtonReturn else { return }
            let updates = MahoBridge.shared.sendEvent(.exportSpaceIntoFolder(spaceId: space.id, targetSpaceId: targetSpace.id))
            let error = updates.compactMap { update -> MahoError? in
                if case .error(_, let error) = update {
                    return error
                }
                return nil
            }.first
            let nonErrorUpdates = updates.filter { update in
                if case .error = update {
                    return false
                }
                return true
            }
            CoreUpdateDispatcher.shared.dispatch(nonErrorUpdates)
            if let error {
                presentExportFailure(error, in: window)
            }
        }
    }
}

final class SpaceCreateSheetController: NSViewController, NSTextFieldDelegate {

    private enum Layout {
        static let width: CGFloat = 980
        static let height: CGFloat = 612
        static let outerInset: CGFloat = 24
        static let columnGap: CGFloat = 22
        static let leftColumnWidth: CGFloat = 344
        static let surfaceCornerRadius: CGFloat = 30
        static let surfaceInnerInset: CGFloat = 26
        static let rowHeight: CGFloat = 76
        static let rowSpacing: CGFloat = 12
        static let illustrationSize: CGFloat = 76
        static let createButtonHeight: CGFloat = 52
    }

    private enum Palette {
        static let shellBackground = MahoAppColors.windowBackground.blended(withFraction: 0.55, of: MahoAppColors.contentBackground) ?? MahoAppColors.contentBackground
        static let formSurfaceFill = MahoAppColors.cardBackground.withAlphaComponent(0.9)
        static let formSurfaceBorder = MahoAppColors.contentBorder.withAlphaComponent(0.72)
        static let previewSurfaceFill = MahoAppColors.cardBackground.withAlphaComponent(0.38)
        static let previewSurfaceBorder = MahoAppColors.contentBorder.withAlphaComponent(0.5)
        static let rowFill = MahoAppColors.cardBackground.withAlphaComponent(0.84)
        static let rowBorder = MahoAppColors.cardBorder.withAlphaComponent(0.92)
        static let profilePillFill = MahoAppColors.searchFieldBackground.withAlphaComponent(0.78)
        static let profilePillBorder = MahoAppColors.searchFieldBorder.withAlphaComponent(0.72)
        static let createButtonFill = NSColor(red: 0.278, green: 0.325, blue: 0.628, alpha: 1.0)
        static let createButtonDisabledText = NSColor(red: 0.64, green: 0.66, blue: 0.82, alpha: 1.0)
    }

    private let defaultColor: SpaceColor
    private let profileId: ProfileId
    private var selectedColor: SpaceColor
    private var isUpdatingPreview = false
    private var selectedModeIndex: Int = 2
    private var selectedTheme: SpaceTheme?

    private let leftPanel = NSView()
    private let rightPanel = NSView()
    private let nameField = NSTextField()
    private let profilePopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private var profileIds: [ProfileId] = []
    private let illustrationContainer = NSView()
    private let illustrationOrb = NSView()
    private let illustrationIcon = NSImageView()
    private let subtitleLabel = NSTextField(labelWithString: "Separate your tabs with calm, focused themes built for work, life, and everything in between.")
    private let nameRow = NSView()
    private let profileRow = NSView()
    private let themeRow = NSView()
    private let themeColorDot = NSView()
    private let themeDescriptionLabel = NSTextField(labelWithString: "Solid · ready to customize")
    private let createButton = NSButton(title: "Create Space", target: nil, action: nil)
    private let cancelButton = NSButton(title: "Cancel", target: nil, action: nil)
    private let previewView = SpaceColorPreviewView(displayStyle: .createSheetZen)

    init(defaultColor: SpaceColor, profileId: ProfileId) {
        self.defaultColor = defaultColor
        self.selectedColor = defaultColor
        self.profileId = profileId
        self.selectedTheme = .solid(color: defaultColor)
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func loadView() {
        let root = NSView(frame: NSRect(x: 0, y: 0, width: Layout.width, height: Layout.height))
        root.translatesAutoresizingMaskIntoConstraints = false
        root.wantsLayer = true
        root.layer?.backgroundColor = Palette.shellBackground.cgColor
        view = root

        root.widthAnchor.constraint(equalToConstant: Layout.width).isActive = true
        root.heightAnchor.constraint(equalToConstant: Layout.height).isActive = true
        preferredContentSize = NSSize(width: Layout.width, height: Layout.height)

        setupPanels()
        setupIllustration()
        setupTitleSection()
        setupNameField()
        setupProfileRow()
        setupThemeRow()
        setupBottomButtons()
        setupRightPanel()
        wirePreviewViewCallbacks()
        loadProfiles()
        updateThemeAppearance()
        updatePreviewAppearance()
        updateCreateButtonState()
    }

    override func viewDidAppear() {
        super.viewDidAppear()
        view.window?.makeFirstResponder(nameField)
        nameField.selectText(nil)
    }

    private func setupPanels() {
        configureSurface(leftPanel, fillColor: Palette.formSurfaceFill, borderColor: Palette.formSurfaceBorder)
        configureSurface(rightPanel, fillColor: Palette.previewSurfaceFill, borderColor: Palette.previewSurfaceBorder)
        view.addSubview(leftPanel)
        view.addSubview(rightPanel)

        NSLayoutConstraint.activate([
            leftPanel.topAnchor.constraint(equalTo: view.topAnchor, constant: Layout.outerInset),
            leftPanel.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: Layout.outerInset),
            leftPanel.bottomAnchor.constraint(equalTo: view.bottomAnchor, constant: -Layout.outerInset),
            leftPanel.widthAnchor.constraint(equalToConstant: Layout.leftColumnWidth),

            rightPanel.topAnchor.constraint(equalTo: view.topAnchor, constant: Layout.outerInset),
            rightPanel.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -Layout.outerInset),
            rightPanel.bottomAnchor.constraint(equalTo: view.bottomAnchor, constant: -Layout.outerInset),
            rightPanel.leadingAnchor.constraint(equalTo: leftPanel.trailingAnchor, constant: Layout.columnGap),
        ])
    }

    private func setupIllustration() {
        illustrationContainer.translatesAutoresizingMaskIntoConstraints = false
        illustrationContainer.wantsLayer = true
        illustrationContainer.layer?.cornerRadius = 24
        illustrationContainer.layer?.masksToBounds = true
        leftPanel.addSubview(illustrationContainer)

        illustrationOrb.translatesAutoresizingMaskIntoConstraints = false
        illustrationOrb.wantsLayer = true
        illustrationOrb.layer?.cornerRadius = Layout.illustrationSize * 0.34
        illustrationOrb.layer?.borderWidth = 1
        illustrationOrb.layer?.borderColor = MahoAppColors.canvasInnerGlow.withAlphaComponent(0.42).cgColor
        illustrationOrb.layer?.masksToBounds = true
        illustrationContainer.addSubview(illustrationOrb)

        illustrationIcon.translatesAutoresizingMaskIntoConstraints = false
        illustrationIcon.image = configuredSymbolImage(named: "square.grid.2x2", pointSize: 30, weight: .regular)
        illustrationIcon.imageScaling = .scaleProportionallyUpOrDown
        illustrationContainer.addSubview(illustrationIcon)

        NSLayoutConstraint.activate([
            illustrationContainer.topAnchor.constraint(equalTo: leftPanel.topAnchor, constant: Layout.surfaceInnerInset),
            illustrationContainer.leadingAnchor.constraint(equalTo: leftPanel.leadingAnchor, constant: Layout.surfaceInnerInset),
            illustrationContainer.widthAnchor.constraint(equalToConstant: Layout.illustrationSize),
            illustrationContainer.heightAnchor.constraint(equalToConstant: Layout.illustrationSize),

            illustrationOrb.centerXAnchor.constraint(equalTo: illustrationContainer.centerXAnchor),
            illustrationOrb.centerYAnchor.constraint(equalTo: illustrationContainer.centerYAnchor),
            illustrationOrb.widthAnchor.constraint(equalToConstant: 52),
            illustrationOrb.heightAnchor.constraint(equalToConstant: 52),

            illustrationIcon.centerXAnchor.constraint(equalTo: illustrationOrb.centerXAnchor),
            illustrationIcon.centerYAnchor.constraint(equalTo: illustrationOrb.centerYAnchor),
            illustrationIcon.widthAnchor.constraint(equalToConstant: 30),
            illustrationIcon.heightAnchor.constraint(equalToConstant: 30),
        ])
    }

    private func setupTitleSection() {
        let eyebrowLabel = NSTextField(labelWithString: "NEW SPACE")
        eyebrowLabel.translatesAutoresizingMaskIntoConstraints = false
        eyebrowLabel.font = .systemFont(ofSize: 11, weight: .semibold)
        eyebrowLabel.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.84)
        leftPanel.addSubview(eyebrowLabel)

        let titleLabel = NSTextField(labelWithString: "Create a Space")
        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.font = .systemFont(ofSize: 30, weight: .semibold)
        titleLabel.textColor = MahoAppColors.primaryText
        leftPanel.addSubview(titleLabel)

        subtitleLabel.translatesAutoresizingMaskIntoConstraints = false
        subtitleLabel.font = .systemFont(ofSize: 13, weight: .regular)
        subtitleLabel.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.92)
        subtitleLabel.maximumNumberOfLines = 3
        subtitleLabel.lineBreakMode = .byWordWrapping
        leftPanel.addSubview(subtitleLabel)

        NSLayoutConstraint.activate([
            eyebrowLabel.topAnchor.constraint(equalTo: illustrationContainer.bottomAnchor, constant: 22),
            eyebrowLabel.leadingAnchor.constraint(equalTo: leftPanel.leadingAnchor, constant: Layout.surfaceInnerInset),
            eyebrowLabel.trailingAnchor.constraint(lessThanOrEqualTo: leftPanel.trailingAnchor, constant: -Layout.surfaceInnerInset),

            titleLabel.topAnchor.constraint(equalTo: eyebrowLabel.bottomAnchor, constant: 8),
            titleLabel.leadingAnchor.constraint(equalTo: eyebrowLabel.leadingAnchor),
            titleLabel.trailingAnchor.constraint(equalTo: leftPanel.trailingAnchor, constant: -Layout.surfaceInnerInset),

            subtitleLabel.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 10),
            subtitleLabel.leadingAnchor.constraint(equalTo: titleLabel.leadingAnchor),
            subtitleLabel.trailingAnchor.constraint(equalTo: leftPanel.trailingAnchor, constant: -Layout.surfaceInnerInset),
        ])
    }

    private func setupNameField() {
        configureCardRow(nameRow, accessibilityIdentifier: "space.create.name.card")

        let titleLabel = NSTextField(labelWithString: "Space name")
        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        titleLabel.font = .systemFont(ofSize: 12, weight: .medium)
        titleLabel.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.9)
        nameRow.addSubview(titleLabel)

        nameField.translatesAutoresizingMaskIntoConstraints = false
        nameField.placeholderString = "Enter a name"
        nameField.font = .systemFont(ofSize: 17, weight: .medium)
        nameField.textColor = MahoAppColors.primaryText
        nameField.isBordered = false
        nameField.drawsBackground = false
        nameField.backgroundColor = .clear
        nameField.focusRingType = .none
        nameField.delegate = self
        nameField.setAccessibilityIdentifier("space.create.name")
        nameRow.addSubview(nameField)

        NSLayoutConstraint.activate([
            nameRow.topAnchor.constraint(equalTo: subtitleLabel.bottomAnchor, constant: 28),
            nameRow.leadingAnchor.constraint(equalTo: leftPanel.leadingAnchor, constant: Layout.surfaceInnerInset),
            nameRow.trailingAnchor.constraint(equalTo: leftPanel.trailingAnchor, constant: -Layout.surfaceInnerInset),
            nameRow.heightAnchor.constraint(equalToConstant: Layout.rowHeight),

            titleLabel.topAnchor.constraint(equalTo: nameRow.topAnchor, constant: 14),
            titleLabel.leadingAnchor.constraint(equalTo: nameRow.leadingAnchor, constant: 18),
            titleLabel.trailingAnchor.constraint(equalTo: nameRow.trailingAnchor, constant: -18),

            nameField.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 6),
            nameField.leadingAnchor.constraint(equalTo: titleLabel.leadingAnchor),
            nameField.trailingAnchor.constraint(equalTo: titleLabel.trailingAnchor),
            nameField.bottomAnchor.constraint(equalTo: nameRow.bottomAnchor, constant: -14),
        ])
    }

    private func setupProfileRow() {
        configureCardRow(profileRow, accessibilityIdentifier: "space.create.profile.card")

        let profileIcon = makeRowIcon(symbolName: "person.crop.circle")
        profileRow.addSubview(profileIcon)

        let profileLabel = NSTextField(labelWithString: "Profile")
        profileLabel.translatesAutoresizingMaskIntoConstraints = false
        profileLabel.font = .systemFont(ofSize: 14, weight: .semibold)
        profileLabel.textColor = MahoAppColors.primaryText
        profileRow.addSubview(profileLabel)

        let profileDescription = NSTextField(labelWithString: "Choose the browser identity for this space")
        profileDescription.translatesAutoresizingMaskIntoConstraints = false
        profileDescription.font = .systemFont(ofSize: 11.5, weight: .regular)
        profileDescription.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.8)
        profileRow.addSubview(profileDescription)

        let profilePill = NSView()
        profilePill.translatesAutoresizingMaskIntoConstraints = false
        profilePill.wantsLayer = true
        profilePill.layer?.backgroundColor = Palette.profilePillFill.cgColor
        profilePill.layer?.borderWidth = 1
        profilePill.layer?.borderColor = Palette.profilePillBorder.cgColor
        profilePill.layer?.cornerRadius = 16
        profilePill.layer?.masksToBounds = true
        profileRow.addSubview(profilePill)

        let profileChevron = NSImageView()
        profileChevron.translatesAutoresizingMaskIntoConstraints = false
        profileChevron.image = configuredSymbolImage(named: "chevron.down", pointSize: 11, weight: .semibold)
        profileChevron.contentTintColor = MahoAppColors.secondaryText
        profileChevron.imageScaling = .scaleProportionallyDown
        profilePill.addSubview(profileChevron)

        profilePopup.translatesAutoresizingMaskIntoConstraints = false
        profilePopup.font = .systemFont(ofSize: 12, weight: .medium)
        profilePopup.isBordered = false
        profilePopup.contentTintColor = MahoAppColors.primaryText
        profilePopup.setAccessibilityIdentifier("space.create.profile")
        if let popupCell = profilePopup.cell as? NSPopUpButtonCell {
            popupCell.arrowPosition = .noArrow
        }
        profilePill.addSubview(profilePopup)

        NSLayoutConstraint.activate([
            profileRow.topAnchor.constraint(equalTo: nameRow.bottomAnchor, constant: Layout.rowSpacing),
            profileRow.leadingAnchor.constraint(equalTo: nameRow.leadingAnchor),
            profileRow.trailingAnchor.constraint(equalTo: nameRow.trailingAnchor),
            profileRow.heightAnchor.constraint(equalToConstant: Layout.rowHeight),

            profileIcon.leadingAnchor.constraint(equalTo: profileRow.leadingAnchor, constant: 18),
            profileIcon.centerYAnchor.constraint(equalTo: profileRow.centerYAnchor),
            profileIcon.widthAnchor.constraint(equalToConstant: 20),
            profileIcon.heightAnchor.constraint(equalToConstant: 20),

            profileLabel.leadingAnchor.constraint(equalTo: profileIcon.trailingAnchor, constant: 14),
            profileLabel.topAnchor.constraint(equalTo: profileRow.topAnchor, constant: 16),

            profileDescription.leadingAnchor.constraint(equalTo: profileLabel.leadingAnchor),
            profileDescription.topAnchor.constraint(equalTo: profileLabel.bottomAnchor, constant: 4),
            profileDescription.trailingAnchor.constraint(lessThanOrEqualTo: profilePill.leadingAnchor, constant: -12),

            profilePill.trailingAnchor.constraint(equalTo: profileRow.trailingAnchor, constant: -16),
            profilePill.centerYAnchor.constraint(equalTo: profileRow.centerYAnchor),
            profilePill.heightAnchor.constraint(equalToConstant: 32),
            profilePill.widthAnchor.constraint(greaterThanOrEqualToConstant: 126),

            profilePopup.leadingAnchor.constraint(equalTo: profilePill.leadingAnchor, constant: 12),
            profilePopup.trailingAnchor.constraint(equalTo: profileChevron.leadingAnchor, constant: -8),
            profilePopup.centerYAnchor.constraint(equalTo: profilePill.centerYAnchor),
            profilePopup.heightAnchor.constraint(equalToConstant: 24),

            profileChevron.trailingAnchor.constraint(equalTo: profilePill.trailingAnchor, constant: -10),
            profileChevron.centerYAnchor.constraint(equalTo: profilePill.centerYAnchor),
            profileChevron.widthAnchor.constraint(equalToConstant: 11),
            profileChevron.heightAnchor.constraint(equalToConstant: 11),
        ])
    }

    private func setupThemeRow() {
        configureCardRow(themeRow, accessibilityIdentifier: "space.create.theme")

        let themeIcon = makeRowIcon(symbolName: "sparkles")
        themeRow.addSubview(themeIcon)

        let themeLabel = NSTextField(labelWithString: "Theme studio")
        themeLabel.translatesAutoresizingMaskIntoConstraints = false
        themeLabel.font = .systemFont(ofSize: 14, weight: .semibold)
        themeLabel.textColor = MahoAppColors.primaryText
        themeRow.addSubview(themeLabel)

        themeDescriptionLabel.translatesAutoresizingMaskIntoConstraints = false
        themeDescriptionLabel.font = .systemFont(ofSize: 11.5, weight: .regular)
        themeDescriptionLabel.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.82)
        themeRow.addSubview(themeDescriptionLabel)

        themeColorDot.translatesAutoresizingMaskIntoConstraints = false
        themeColorDot.wantsLayer = true
        themeColorDot.layer?.cornerRadius = 11
        themeColorDot.layer?.borderWidth = 1
        themeColorDot.layer?.borderColor = NSColor.white.withAlphaComponent(0.14).cgColor
        themeColorDot.layer?.masksToBounds = true
        themeRow.addSubview(themeColorDot)

        NSLayoutConstraint.activate([
            themeRow.topAnchor.constraint(equalTo: profileRow.bottomAnchor, constant: Layout.rowSpacing),
            themeRow.leadingAnchor.constraint(equalTo: nameRow.leadingAnchor),
            themeRow.trailingAnchor.constraint(equalTo: nameRow.trailingAnchor),
            themeRow.heightAnchor.constraint(equalToConstant: Layout.rowHeight),

            themeIcon.leadingAnchor.constraint(equalTo: themeRow.leadingAnchor, constant: 18),
            themeIcon.centerYAnchor.constraint(equalTo: themeRow.centerYAnchor),
            themeIcon.widthAnchor.constraint(equalToConstant: 20),
            themeIcon.heightAnchor.constraint(equalToConstant: 20),

            themeLabel.leadingAnchor.constraint(equalTo: themeIcon.trailingAnchor, constant: 14),
            themeLabel.topAnchor.constraint(equalTo: themeRow.topAnchor, constant: 16),

            themeDescriptionLabel.leadingAnchor.constraint(equalTo: themeLabel.leadingAnchor),
            themeDescriptionLabel.topAnchor.constraint(equalTo: themeLabel.bottomAnchor, constant: 4),
            themeDescriptionLabel.trailingAnchor.constraint(lessThanOrEqualTo: themeColorDot.leadingAnchor, constant: -12),

            themeColorDot.trailingAnchor.constraint(equalTo: themeRow.trailingAnchor, constant: -18),
            themeColorDot.centerYAnchor.constraint(equalTo: themeRow.centerYAnchor),
            themeColorDot.widthAnchor.constraint(equalToConstant: 22),
            themeColorDot.heightAnchor.constraint(equalToConstant: 22),
        ])
    }

    private func setupBottomButtons() {
        createButton.translatesAutoresizingMaskIntoConstraints = false
        createButton.wantsLayer = true
        createButton.isBordered = false
        createButton.layer?.backgroundColor = Palette.createButtonFill.cgColor
        createButton.layer?.cornerRadius = 16
        createButton.layer?.masksToBounds = true
        createButton.font = .systemFont(ofSize: 15, weight: .semibold)
        createButton.target = self
        createButton.action = #selector(savePressed)
        createButton.keyEquivalent = "\r"
        createButton.setAccessibilityIdentifier("space.create.save")
        leftPanel.addSubview(createButton)

        cancelButton.translatesAutoresizingMaskIntoConstraints = false
        cancelButton.isBordered = false
        cancelButton.font = .systemFont(ofSize: 13, weight: .medium)
        cancelButton.contentTintColor = MahoAppColors.secondaryText
        cancelButton.target = self
        cancelButton.action = #selector(cancelPressed)
        cancelButton.keyEquivalent = "\u{1b}"
        cancelButton.setAccessibilityIdentifier("space.create.cancel")
        leftPanel.addSubview(cancelButton)

        NSLayoutConstraint.activate([
            cancelButton.centerXAnchor.constraint(equalTo: leftPanel.centerXAnchor),
            cancelButton.bottomAnchor.constraint(equalTo: leftPanel.bottomAnchor, constant: -22),

            createButton.leadingAnchor.constraint(equalTo: leftPanel.leadingAnchor, constant: Layout.surfaceInnerInset),
            createButton.trailingAnchor.constraint(equalTo: leftPanel.trailingAnchor, constant: -Layout.surfaceInnerInset),
            createButton.bottomAnchor.constraint(equalTo: cancelButton.topAnchor, constant: -12),
            createButton.heightAnchor.constraint(equalToConstant: Layout.createButtonHeight),
        ])
    }

    private func setupRightPanel() {
        previewView.translatesAutoresizingMaskIntoConstraints = false
        previewView.setAccessibilityIdentifier("space.theme.creatorPreview")
        rightPanel.addSubview(previewView)

        NSLayoutConstraint.activate([
            previewView.topAnchor.constraint(equalTo: rightPanel.topAnchor, constant: 14),
            previewView.leadingAnchor.constraint(equalTo: rightPanel.leadingAnchor, constant: 14),
            previewView.trailingAnchor.constraint(equalTo: rightPanel.trailingAnchor, constant: -14),
            previewView.bottomAnchor.constraint(equalTo: rightPanel.bottomAnchor, constant: -14),
        ])
    }

    private func wirePreviewViewCallbacks() {
        previewView.onColorSelected = { [weak self] index in
            guard let self = self, index >= 0, index < SpacePresentationPresets.colors.count else { return }
            self.selectedColor = SpacePresentationPresets.colors[index].color
            self.updateThemeAppearance()
        }

        previewView.onModeSelected = { [weak self] index in
            self?.selectedModeIndex = index
        }

        previewView.onColorPicked = { [weak self] color in
            self?.selectedColor = color
            self?.updateThemeAppearance()
        }

        previewView.onGrainChanged = { [weak self] grain in
            guard let self = self else { return }
            self.selectedColor = SpaceColor(
                hue: self.selectedColor.hue,
                saturation: self.selectedColor.saturation,
                brightness: self.selectedColor.brightness,
                grain: grain
            )
            self.updateThemeAppearance()
        }

        previewView.onBrightnessChanged = { [weak self] brightness in
            guard let self = self else { return }
            self.selectedColor = SpaceColor(
                hue: self.selectedColor.hue,
                saturation: self.selectedColor.saturation,
                brightness: brightness,
                grain: self.selectedColor.grain
            )
            self.updateThemeAppearance()
        }

        previewView.onThemeChanged = { [weak self] theme in
            guard let self = self else { return }
            self.selectedTheme = theme
            switch theme {
            case .solid(let color):
                self.selectedColor = color
            case .gradient(let gradientColors, _, _, let texture):
                if let primary = gradientColors.first(where: { $0.isPrimary }) ?? gradientColors.first {
                    self.selectedColor = SpaceColor(
                        hue: primary.hue,
                        saturation: primary.saturation,
                        brightness: primary.brightness,
                        grain: texture
                    )
                }
            }
            self.updateThemeAppearance()
        }
    }

    private func configureSurface(_ surface: NSView, fillColor: NSColor, borderColor: NSColor) {
        surface.translatesAutoresizingMaskIntoConstraints = false
        surface.wantsLayer = true
        surface.layer?.backgroundColor = fillColor.cgColor
        surface.layer?.borderWidth = 1
        surface.layer?.borderColor = borderColor.cgColor
        surface.layer?.cornerRadius = Layout.surfaceCornerRadius
        surface.layer?.masksToBounds = true
    }

    private func configureCardRow(_ row: NSView, accessibilityIdentifier: String) {
        row.translatesAutoresizingMaskIntoConstraints = false
        row.wantsLayer = true
        row.layer?.backgroundColor = Palette.rowFill.cgColor
        row.layer?.borderWidth = 1
        row.layer?.borderColor = Palette.rowBorder.cgColor
        row.layer?.cornerRadius = 22
        row.layer?.masksToBounds = true
        row.setAccessibilityIdentifier(accessibilityIdentifier)
        if row.superview == nil {
            leftPanel.addSubview(row)
        }
    }

    private func makeRowIcon(symbolName: String) -> NSImageView {
        let imageView = NSImageView()
        imageView.translatesAutoresizingMaskIntoConstraints = false
        imageView.image = configuredSymbolImage(named: symbolName, pointSize: 18, weight: .medium)
        imageView.contentTintColor = MahoAppColors.secondaryText.withAlphaComponent(0.92)
        imageView.imageScaling = .scaleProportionallyDown
        return imageView
    }

    private func configuredSymbolImage(named symbolName: String, pointSize: CGFloat, weight: NSFont.Weight) -> NSImage? {
        return MahoIcon.imageForSFSymbol(symbolName, pointSize: pointSize)
    }

    private func previewName() -> String {
        let trimmedName = nameField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmedName.isEmpty ? "Space" : trimmedName
    }

    private func selectedColorIndexForPreview() -> Int {
        for (index, preset) in SpacePresentationPresets.colors.enumerated() {
            if abs(preset.color.hue - selectedColor.hue) < 0.01,
               abs(preset.color.saturation - selectedColor.saturation) < 0.01,
               abs(preset.color.brightness - selectedColor.brightness) < 0.01 {
                return index
            }
        }
        return 0
    }

    private func loadProfiles() {
        guard let json = MahoBridge.shared.listProfiles(),
              let data = json.data(using: .utf8),
              let profiles = try? JSONDecoder().decode([ProfileConfig].self, from: data) else { return }

        for profile in profiles {
            profilePopup.addItem(withTitle: profile.name)
            profileIds.append(profile.id)
        }
        if let idx = profileIds.firstIndex(of: profileId) {
            profilePopup.selectItem(at: idx)
        }
    }

    func controlTextDidChange(_ obj: Notification) {
        updateCreateButtonState()
        updatePreviewAppearance()
    }

    @objc private func savePressed() {
        let trimmedName = nameField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmedName.isEmpty else {
            NSSound.beep()
            return
        }

        let selectedProfileId: ProfileId
        if !profileIds.isEmpty {
            selectedProfileId = profileIds[profilePopup.indexOfSelectedItem]
        } else {
            selectedProfileId = profileId
        }

        let updates = MahoBridge.shared.sendEvent(.createSpace(name: trimmedName, color: selectedColor, profileId: selectedProfileId))
        CoreUpdateDispatcher.shared.dispatch(updates)
        if let createdSpaceId = updates.compactMap({ update -> SpaceId? in
            if case .spaceCreated(let space) = update {
                return space.id
            }
            return nil
        }).first {
            CoreUpdateDispatcher.shared.sendAndDispatch(.activateSpace(spaceId: createdSpaceId))
            if let theme = selectedTheme {
                let changes = SpaceConfigUpdate(spaceId: createdSpaceId, name: nil, color: nil, theme: theme, icon: nil, profileId: nil)
                MahoBridge.shared.updateSpaceConfig(changes)
            }
        }
        dismissCreateSpaceSheet()
    }

    @objc private func cancelPressed() {
        dismissCreateSpaceSheet()
    }

    private func dismissCreateSpaceSheet() {
        if let sheetWindow = view.window, let parentWindow = sheetWindow.sheetParent {
            parentWindow.endSheet(sheetWindow)
            return
        }
        dismiss(nil)
    }

    private func updateCreateButtonState() {
        let trimmedName = nameField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        let isEnabled = !trimmedName.isEmpty
        createButton.isEnabled = isEnabled
        createButton.alphaValue = isEnabled ? 1.0 : 0.5

        let titleColor = isEnabled ? NSColor.white : Palette.createButtonDisabledText
        let paragraphStyle = NSMutableParagraphStyle()
        paragraphStyle.alignment = .center

        createButton.attributedTitle = NSAttributedString(
            string: createButton.title,
            attributes: [
                .font: NSFont.systemFont(ofSize: 15, weight: .semibold),
                .foregroundColor: titleColor,
                .paragraphStyle: paragraphStyle,
            ]
        )
    }

    private func updateThemeAppearance() {
        let nsColor = NSColor(
            hue: selectedColor.hue,
            saturation: selectedColor.saturation,
            brightness: selectedColor.brightness,
            alpha: 1.0
        )
        themeColorDot.layer?.backgroundColor = nsColor.cgColor
        illustrationContainer.layer?.backgroundColor = nsColor.withAlphaComponent(0.16).cgColor
        illustrationOrb.layer?.backgroundColor = nsColor.withAlphaComponent(0.22).cgColor
        illustrationIcon.contentTintColor = nsColor.withAlphaComponent(0.98)
        themeDescriptionLabel.stringValue = themeSummaryText()
    }

    private func updatePreviewAppearance() {
        guard !isUpdatingPreview else { return }
        isUpdatingPreview = true
        defer { isUpdatingPreview = false }

        let name = previewName()
        if let theme = selectedTheme {
            previewView.configure(theme: theme, name: name, icon: nil)
        } else {
            previewView.configure(color: selectedColor, name: name, icon: nil)
        }
        previewView.setSelectedColorIndex(selectedColorIndexForPreview())
        previewView.setSelectedModeIndex(selectedModeIndex)
        previewView.setGrainLevel(selectedColor.grain)
        previewView.setBrightnessLevel(selectedColor.brightness)
    }

    private func themeSummaryText() -> String {
        guard let theme = selectedTheme else {
            return "Solid · ready to customize"
        }

        switch theme {
        case .solid:
            return "Solid · \(hexString(for: selectedColor))"
        case .gradient(let colors, let harmony, _, _):
            return "Gradient · \(colors.count) tones · \(harmonyTitle(harmony))"
        }
    }

    private func harmonyTitle(_ harmony: ColorHarmony) -> String {
        switch harmony {
        case .complementary:
            return "Complementary"
        case .singleAnalogous, .analogous:
            return "Analogous"
        case .splitComplementary:
            return "Split Complementary"
        case .triadic:
            return "Triadic"
        case .custom:
            return "Custom"
        }
    }

    private func hexString(for color: SpaceColor) -> String {
        let nsColor = NSColor(hue: color.hue, saturation: color.saturation, brightness: color.brightness, alpha: 1.0)
        guard let rgb = nsColor.usingColorSpace(.sRGB) else { return "#000000" }
        let red = Int(round(rgb.redComponent * 255))
        let green = Int(round(rgb.greenComponent * 255))
        let blue = Int(round(rgb.blueComponent * 255))
        return String(format: "#%02X%02X%02X", red, green, blue)
    }
}

private final class SpaceConfigSheetController: NSViewController, NSTextFieldDelegate {
    enum InitialFocus {
        case name
        case icon
        case color
        case profile
    }

    private let space: SpaceViewModel
    private let initialFocus: InitialFocus
    private let customEmojiMarker = "__custom_emoji__"

    private let nameField = NSTextField()
    private let iconPopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let emojiField = NSTextField()
    private let colorPopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private let profilePopup = NSPopUpButton(frame: .zero, pullsDown: false)
    private var profileIds: [ProfileId] = []
    private let cancelButton = NSButton(title: "Cancel", target: nil, action: nil)
    private let saveButton = NSButton(title: "Save", target: nil, action: nil)

    init(space: SpaceViewModel, initialFocus: InitialFocus = .name) {
        self.space = space
        self.initialFocus = initialFocus
        super.init(nibName: nil, bundle: nil)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func loadView() {
        view = NSView(frame: NSRect(x: 0, y: 0, width: 420, height: 280))

        let titleLabel = NSTextField(labelWithString: "Edit Space")
        titleLabel.font = .systemFont(ofSize: 16, weight: .semibold)
        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(titleLabel)

        nameField.translatesAutoresizingMaskIntoConstraints = false
        nameField.placeholderString = "Space name"
        nameField.stringValue = space.name
        nameField.delegate = self
        nameField.setAccessibilityIdentifier("space.config.name")

        iconPopup.translatesAutoresizingMaskIntoConstraints = false
        iconPopup.setAccessibilityIdentifier("space.config.icon")
        iconPopup.target = self
        iconPopup.action = #selector(iconSelectionChanged)
        for icon in SpacePresentationPresets.icons {
            iconPopup.addItem(withTitle: icon.title)
            iconPopup.lastItem?.representedObject = icon.symbolName ?? NSNull()
        }
        iconPopup.addItem(withTitle: "Custom Emoji")
        iconPopup.lastItem?.representedObject = customEmojiMarker

        emojiField.translatesAutoresizingMaskIntoConstraints = false
        emojiField.placeholderString = "😀"
        emojiField.delegate = self
        emojiField.setAccessibilityIdentifier("space.config.emoji")

        colorPopup.translatesAutoresizingMaskIntoConstraints = false
        colorPopup.setAccessibilityIdentifier("space.config.color")
        for color in SpacePresentationPresets.colors {
            colorPopup.addItem(withTitle: color.title)
        }

        profilePopup.translatesAutoresizingMaskIntoConstraints = false
        profilePopup.setAccessibilityIdentifier("space.config.profile")
        if let json = MahoBridge.shared.listProfiles(),
           let data = json.data(using: .utf8),
           let profiles = try? JSONDecoder().decode([ProfileConfig].self, from: data) {
            for profile in profiles {
                profilePopup.addItem(withTitle: profile.name)
                profileIds.append(profile.id)
            }
            if let spaceProfileId = space.profileId,
               let idx = profileIds.firstIndex(of: spaceProfileId) {
                profilePopup.selectItem(at: idx)
            }
        }

        let nameRow = makeRow(label: "Name", control: nameField)
        let iconRow = makeRow(label: "Icon", control: iconPopup)
        let emojiRow = makeRow(label: "Emoji Icon", control: emojiField)
        let colorRow = makeRow(label: "Color", control: colorPopup)
        let profileRow = makeRow(label: "Profile", control: profilePopup)

        let buttonRow = NSStackView(views: [cancelButton, saveButton])
        buttonRow.orientation = .horizontal
        buttonRow.alignment = .centerY
        buttonRow.spacing = 8
        buttonRow.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(buttonRow)

        cancelButton.target = self
        cancelButton.action = #selector(cancelPressed)
        cancelButton.keyEquivalent = "\u{1b}"
        cancelButton.setAccessibilityIdentifier("space.config.cancel")

        saveButton.target = self
        saveButton.action = #selector(savePressed)
        saveButton.keyEquivalent = "\r"
        saveButton.setAccessibilityIdentifier("space.config.save")

        view.addSubview(nameRow)
        view.addSubview(iconRow)
        view.addSubview(emojiRow)
        view.addSubview(colorRow)
        view.addSubview(profileRow)

        NSLayoutConstraint.activate([
            titleLabel.topAnchor.constraint(equalTo: view.topAnchor, constant: 20),
            titleLabel.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 20),

            nameRow.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 18),
            nameRow.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 20),
            nameRow.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -20),

            iconRow.topAnchor.constraint(equalTo: nameRow.bottomAnchor, constant: 12),
            iconRow.leadingAnchor.constraint(equalTo: nameRow.leadingAnchor),
            iconRow.trailingAnchor.constraint(equalTo: nameRow.trailingAnchor),

            emojiRow.topAnchor.constraint(equalTo: iconRow.bottomAnchor, constant: 12),
            emojiRow.leadingAnchor.constraint(equalTo: nameRow.leadingAnchor),
            emojiRow.trailingAnchor.constraint(equalTo: nameRow.trailingAnchor),

            colorRow.topAnchor.constraint(equalTo: emojiRow.bottomAnchor, constant: 12),
            colorRow.leadingAnchor.constraint(equalTo: nameRow.leadingAnchor),
            colorRow.trailingAnchor.constraint(equalTo: nameRow.trailingAnchor),

            profileRow.topAnchor.constraint(equalTo: colorRow.bottomAnchor, constant: 12),
            profileRow.leadingAnchor.constraint(equalTo: nameRow.leadingAnchor),
            profileRow.trailingAnchor.constraint(equalTo: nameRow.trailingAnchor),

            buttonRow.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -20),
            buttonRow.bottomAnchor.constraint(equalTo: view.bottomAnchor, constant: -18),
            buttonRow.topAnchor.constraint(greaterThanOrEqualTo: profileRow.bottomAnchor, constant: 18),
        ])

        if let iconIndex = SpacePresentationPresets.icons.firstIndex(where: { $0.symbolName == space.icon }) {
            iconPopup.selectItem(at: iconIndex)
        } else if let icon = space.icon, !icon.isEmpty {
            emojiField.stringValue = String(icon.prefix(2))
            iconPopup.selectItem(at: SpacePresentationPresets.icons.count)
        } else {
            iconPopup.selectItem(at: 0)
        }

        if let colorIndex = SpacePresentationPresets.colors.firstIndex(where: { $0.color == space.color }) {
            colorPopup.selectItem(at: colorIndex)
        } else {
            colorPopup.selectItem(at: 0)
        }

        updateEmojiFieldState()
        updateSaveButtonState()
    }

    override func viewDidAppear() {
        super.viewDidAppear()
        switch initialFocus {
        case .name:
            view.window?.makeFirstResponder(nameField)
            nameField.selectText(nil)
        case .icon:
            view.window?.makeFirstResponder(iconPopup)
        case .color:
            view.window?.makeFirstResponder(colorPopup)
        case .profile:
            view.window?.makeFirstResponder(profilePopup)
        }
    }

    func controlTextDidChange(_ obj: Notification) {
        if let textField = obj.object as? NSTextField, textField === emojiField {
            let limitedValue = String(emojiField.stringValue.prefix(2))
            if emojiField.stringValue != limitedValue {
                emojiField.stringValue = limitedValue
            }
            if !emojiField.stringValue.isEmpty,
               let customEmojiItem = iconPopup.item(withTitle: "Custom Emoji") {
                iconPopup.select(customEmojiItem)
            }
            updateEmojiFieldState()
        }

        updateSaveButtonState()
    }

    @objc private func cancelPressed() {
        dismiss(nil)
    }

    @objc private func iconSelectionChanged() {
        if !isCustomEmojiSelected() {
            emojiField.stringValue = ""
        }

        updateEmojiFieldState()
    }

    @objc private func savePressed() {
        let trimmedName = nameField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmedName.isEmpty else {
            NSSound.beep()
            updateSaveButtonState()
            return
        }

        let selectedColor = SpacePresentationPresets.colors[colorPopup.indexOfSelectedItem].color
        let selectedProfileId: ProfileId?
        if !profileIds.isEmpty {
            selectedProfileId = profileIds[profilePopup.indexOfSelectedItem]
        } else {
            selectedProfileId = space.profileId
        }

        let trimmedEmoji = emojiField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        let selectedIcon = trimmedEmoji.isEmpty ? selectedIconName() : trimmedEmoji

        MahoBridge.shared.updateSpaceConfig(
            SpaceConfigUpdate(
                spaceId: space.id,
                name: trimmedName,
                color: selectedColor,
                theme: nil,
                icon: selectedIcon,
                profileId: selectedProfileId
            )
        )

        dismiss(nil)
    }

    private func isCustomEmojiSelected() -> Bool {
        iconPopup.selectedItem?.representedObject as? String == customEmojiMarker
    }

    private func selectedIconName() -> String? {
        guard let representedObject = iconPopup.selectedItem?.representedObject else { return nil }
        if representedObject is NSNull {
            return nil
        }
        let iconName = representedObject as? String
        return iconName == customEmojiMarker ? nil : iconName
    }

    private func updateEmojiFieldState() {
        let isEnabled = isCustomEmojiSelected()
        emojiField.isEnabled = isEnabled
        emojiField.alphaValue = isEnabled ? 1.0 : 0.6
    }

    private func updateSaveButtonState() {
        let trimmedName = nameField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        saveButton.isEnabled = !trimmedName.isEmpty
    }

    private func makeRow(label: String, control: NSView) -> NSStackView {
        let labelField = NSTextField(labelWithString: label)
        labelField.font = .systemFont(ofSize: 12, weight: .medium)
        labelField.textColor = MahoAppColors.secondaryText.withAlphaComponent(0.9)
        labelField.setContentHuggingPriority(.required, for: .horizontal)

        let row = NSStackView(views: [labelField, control])
        row.orientation = .horizontal
        row.alignment = .centerY
        row.spacing = 12
        row.translatesAutoresizingMaskIntoConstraints = false

        control.translatesAutoresizingMaskIntoConstraints = false
        control.widthAnchor.constraint(equalToConstant: 260).isActive = true
        return row
    }
}

private final class ManageSpacesController: NSViewController, NSTableViewDataSource, NSTableViewDelegate, CoreUpdateObserver {
    private var spaces: [SpaceViewModel] = []
    private let tableView = NSTableView()
    private let scrollView = NSScrollView()

    override func loadView() {
        view = NSView(frame: NSRect(x: 0, y: 0, width: 460, height: 360))
        refreshSpaces()

        let titleLabel = NSTextField(labelWithString: "Manage Spaces")
        titleLabel.font = .systemFont(ofSize: 16, weight: .semibold)
        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(titleLabel)

        let nameColumn = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("name"))
        nameColumn.title = "Name"
        nameColumn.width = 160
        let profileColumn = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("profile"))
        profileColumn.title = "Profile"
        profileColumn.width = 100
        let tabsColumn = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("tabs"))
        tabsColumn.title = "Tabs"
        tabsColumn.width = 50
        let actionsColumn = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("actions"))
        actionsColumn.title = ""
        actionsColumn.width = 100

        tableView.addTableColumn(nameColumn)
        tableView.addTableColumn(profileColumn)
        tableView.addTableColumn(tabsColumn)
        tableView.addTableColumn(actionsColumn)
        tableView.dataSource = self
        tableView.delegate = self
        tableView.usesAlternatingRowBackgroundColors = true
        tableView.rowHeight = 32
        tableView.headerView = NSTableHeaderView()

        scrollView.documentView = tableView
        scrollView.hasVerticalScroller = true
        scrollView.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(scrollView)

        let doneButton = NSButton(title: "Done", target: self, action: #selector(donePressed))
        doneButton.keyEquivalent = "\r"
        doneButton.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(doneButton)

        NSLayoutConstraint.activate([
            titleLabel.topAnchor.constraint(equalTo: view.topAnchor, constant: 20),
            titleLabel.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 20),

            scrollView.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 14),
            scrollView.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 20),
            scrollView.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -20),
            scrollView.bottomAnchor.constraint(equalTo: doneButton.topAnchor, constant: -14),

            doneButton.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -20),
            doneButton.bottomAnchor.constraint(equalTo: view.bottomAnchor, constant: -18),
        ])

        CoreUpdateDispatcher.shared.register(self)
    }

    deinit {
        CoreUpdateDispatcher.shared.unregister(self)
    }

    @objc private func donePressed() {
        dismiss(nil)
    }

    func handleCoreUpdate(_ update: CoreUpdate) {
        switch update {
        case .spaceCreated, .spaceUpdated, .spaceDeleted, .spaceOrderChanged, .spaceConfigUpdated, .folderCreated, .folderUpdated, .folderDeleted, .folderOrderChanged, .profileCreated, .profileDeleted, .profileUpdated, .activeSpaceChanged, .activeProfileChanged:
            refreshSpaces()
        default:
            break
        }
    }

    private func refreshSpaces() {
        spaces = MahoBridge.shared.getCanonicalSpaceViewModels()
        if Thread.isMainThread {
            tableView.reloadData()
        } else {
            DispatchQueue.main.async { [weak self] in
                self?.tableView.reloadData()
            }
        }
    }

    func numberOfRows(in tableView: NSTableView) -> Int {
        spaces.count
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        guard row < spaces.count else { return nil }
        let space = spaces[row]

        switch tableColumn?.identifier.rawValue {
        case "name":
            let cell = NSTextField(labelWithString: space.name)
            cell.font = .systemFont(ofSize: 13, weight: space.isActive ? .semibold : .regular)
            return cell
        case "profile":
            let cell = NSTextField(labelWithString: space.profileName ?? "Default")
            cell.font = .systemFont(ofSize: 12)
            cell.textColor = .secondaryLabelColor
            return cell
        case "tabs":
            let cell = NSTextField(labelWithString: "\(space.tabCount)")
            cell.font = .systemFont(ofSize: 12)
            cell.alignment = .center
            return cell
        case "actions":
            let editBtn = NSButton(title: "Edit", target: nil, action: nil)
            editBtn.bezelStyle = .inline
            editBtn.font = .systemFont(ofSize: 11)
            let deleteBtn = NSButton(title: "Delete", target: nil, action: nil)
            deleteBtn.bezelStyle = .inline
            deleteBtn.font = .systemFont(ofSize: 11)
            deleteBtn.contentTintColor = .systemRed

            let editTarget = MenuActionTarget {
                guard let window = NSApp.mainWindow else { return }
                let editor = SpaceConfigSheetController(space: space)
                window.contentViewController?.presentAsSheet(editor)
            }
            editBtn.target = editTarget
            editBtn.action = #selector(MenuActionTarget.performAction)

            let deleteTarget = MenuActionTarget { [weak self] in
                guard let self = self, let window = self.view.window else { return }
                let alert = NSAlert()
                alert.messageText = "Delete \"\(space.name)\"?"
                alert.informativeText = "All tabs in this space will be closed."
                alert.alertStyle = .warning
                alert.addButton(withTitle: "Delete")
                alert.addButton(withTitle: "Cancel")
                alert.beginSheetModal(for: window) { response in
                    if response == .alertFirstButtonReturn {
                        CoreUpdateDispatcher.shared.sendAndDispatch(.deleteSpace(spaceId: space.id))
                    }
                }
            }
            deleteBtn.target = deleteTarget
            deleteBtn.action = #selector(MenuActionTarget.performAction)

            let row = NSStackView(views: [editBtn, deleteBtn])
            row.orientation = .horizontal
            row.spacing = 6
            row.setAccessibilityElement(false)
            objc_setAssociatedObject(row, "editTarget", editTarget, .OBJC_ASSOCIATION_RETAIN_NONATOMIC)
            objc_setAssociatedObject(row, "deleteTarget", deleteTarget, .OBJC_ASSOCIATION_RETAIN_NONATOMIC)
            return row
        default:
            return nil
        }
    }
}

private extension NSMenuItem {
    convenience init(title: String, action: @escaping () -> Void) {
        self.init(title: title, action: #selector(MenuActionTarget.performAction), keyEquivalent: "")
        let target = MenuActionTarget(action: action)
        self.target = target
        self.representedObject = target
        objc_setAssociatedObject(self, MenuActionTarget.associationKey, target, .OBJC_ASSOCIATION_RETAIN_NONATOMIC)
    }
}

private extension MenuActionTarget {
    static var associationKey: UnsafeRawPointer {
        UnsafeRawPointer(bitPattern: 0x4d41484f_4d454e55)!
    }
}

private final class MenuActionTarget: NSObject {
    private let action: () -> Void

    init(action: @escaping () -> Void) {
        self.action = action
    }

    @objc func performAction() {
        action()
    }
}
