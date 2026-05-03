import AppKit
import UniformTypeIdentifiers

struct FavoriteTileCustomization: Codable, Equatable {
    var emoji: String?
    var imageData: Data?

    var isEmpty: Bool {
        let trimmedEmoji = emoji?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        return trimmedEmoji.isEmpty && imageData == nil
    }
}

final class FavoriteTileCustomizationStore {
    static let shared = FavoriteTileCustomizationStore()

    private let storageKey = "maho.favoriteTileCustomizations"
    private var customizations: [TabId: FavoriteTileCustomization] = [:]

    private init() {
        load()
    }

    func customization(for tabId: TabId) -> FavoriteTileCustomization? {
        customizations[tabId]
    }

    func image(for tabId: TabId) -> NSImage? {
        guard let data = customizations[tabId]?.imageData else { return nil }
        return NSImage(data: data)
    }

    func emoji(for tabId: TabId) -> String? {
        let trimmedEmoji = customizations[tabId]?.emoji?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        return trimmedEmoji.isEmpty ? nil : trimmedEmoji
    }

    func setCustomization(_ customization: FavoriteTileCustomization?, for tabId: TabId) {
        guard let sanitized = sanitize(customization) else {
            customizations.removeValue(forKey: tabId)
            save()
            return
        }
        customizations[tabId] = sanitized
        save()
    }

    private func sanitize(_ customization: FavoriteTileCustomization?) -> FavoriteTileCustomization? {
        guard var customization else { return nil }
        let trimmedEmoji = customization.emoji?.trimmingCharacters(in: .whitespacesAndNewlines)
        customization.emoji = trimmedEmoji?.isEmpty == false ? trimmedEmoji : nil
        if customization.isEmpty {
            return nil
        }
        return customization
    }

    private func load() {
        guard let data = UserDefaults.standard.data(forKey: storageKey),
              let decoded = try? JSONDecoder().decode([TabId: FavoriteTileCustomization].self, from: data) else {
            return
        }
        customizations = decoded
    }

    private func save() {
        guard let data = try? JSONEncoder().encode(customizations) else { return }
        UserDefaults.standard.set(data, forKey: storageKey)
    }
}

final class FavoritesContextMenu {
    struct Context {
        let tab: TabViewModel
        let spaces: [SpaceViewModel]
        let currentActiveTab: TabViewModel?
        let window: NSWindow?
        let onRename: (String) -> Void
        let onUpdateCustomization: (FavoriteTileCustomization?) -> Void
        let onRefreshRequested: () -> Void
    }

    static func menu(for context: Context) -> NSMenu {
        let menu = NSMenu()
        menu.autoenablesItems = false

        menu.addItem(copyLinkItem(for: context.tab))
        menu.addItem(shareItem(for: context.tab))
        menu.addItem(.separator())
        menu.addItem(changeIconItem(for: context))
        menu.addItem(renameItem(for: context))
        menu.addItem(muteItem(for: context.tab))
        menu.addItem(editPinnedPageItem(for: context))
        menu.addItem(.separator())
        menu.addItem(openInSplitViewItem(for: context.tab))
        menu.addItem(duplicateItem(for: context.tab))
        menu.addItem(moveToItem(for: context.tab, spaces: context.spaces))
        menu.addItem(.separator())
        menu.addItem(removeFavoriteItem(for: context.tab))

        return menu
    }

    private static func copyLinkItem(for tab: TabViewModel) -> NSMenuItem {
        NSMenuItem(title: "Copy Link", action: {
            NSPasteboard.general.clearContents()
            NSPasteboard.general.setString(tab.url, forType: .string)
        })
    }

    private static func shareItem(for tab: TabViewModel) -> NSMenuItem {
        let item = NSMenuItem(title: "Share", action: nil, keyEquivalent: "")
        item.submenu = shareMenu(for: tab)
        return item
    }

    private static func shareMenu(for tab: TabViewModel) -> NSMenu {
        let menu = NSMenu(title: "Share")
        menu.autoenablesItems = false
        guard let url = URL(string: tab.url) else {
            let unavailableItem = NSMenuItem(title: "No Share Options", action: nil, keyEquivalent: "")
            unavailableItem.isEnabled = false
            menu.addItem(unavailableItem)
            return menu
        }

        let items: [Any] = [url as NSURL]
        let services = NSSharingService.sharingServices(forItems: items)
        guard !services.isEmpty else {
            let unavailableItem = NSMenuItem(title: "No Share Options", action: nil, keyEquivalent: "")
            unavailableItem.isEnabled = false
            menu.addItem(unavailableItem)
            return menu
        }

        for service in services {
            let serviceItem = NSMenuItem(title: service.title, action: {
                service.subject = preferredDisplayTitle(for: tab)
                service.perform(withItems: items)
            })
            serviceItem.image = service.image
            menu.addItem(serviceItem)
        }
        return menu
    }

    private static func changeIconItem(for context: Context) -> NSMenuItem {
        NSMenuItem(title: "Change Icon...", action: {
            let controller = FavoriteIconSheetController(
                currentCustomization: FavoriteTileCustomizationStore.shared.customization(for: context.tab.id)
            ) { customization in
                context.onUpdateCustomization(customization)
                context.onRefreshRequested()
            }
            present(sheetController: controller, in: context.window)
        })
    }

    private static func renameItem(for context: Context) -> NSMenuItem {
        NSMenuItem(title: "Rename...", action: {
            guard let newTitle = promptForRename(tab: context.tab) else { return }
            context.onRename(newTitle)
            context.onRefreshRequested()
        })
    }

    private static func muteItem(for tab: TabViewModel) -> NSMenuItem {
        let item = NSMenuItem(title: "Mute", action: {
            if tab.isMuted {
                CoreUpdateDispatcher.shared.sendAndDispatch(.unmuteTab(tabId: tab.id))
            } else {
                CoreUpdateDispatcher.shared.sendAndDispatch(.muteTab(tabId: tab.id))
            }
        })
        item.image = tab.isMuted ? mutedIndicatorImage() : nil
        return item
    }

    private static func editPinnedPageItem(for context: Context) -> NSMenuItem {
        let item = NSMenuItem(title: "Edit Pinned Page", action: nil, keyEquivalent: "")
        item.submenu = editPinnedPageMenu(for: context)
        return item
    }

    private static func editPinnedPageMenu(for context: Context) -> NSMenu {
        let menu = NSMenu(title: "Edit Pinned Page")
        menu.autoenablesItems = false

        let replaceItem = NSMenuItem(title: "Replace Pinned URL with Current", action: {
            guard let currentActiveTab = context.currentActiveTab else { return }
            CoreUpdateDispatcher.shared.sendAndDispatch(.navigateTo(tabId: context.tab.id, url: MahoUrl(currentActiveTab.url)))
        })
        let canReplaceWithCurrent = canReplacePinnedURL(for: context.tab, currentActiveTab: context.currentActiveTab)
        replaceItem.isEnabled = canReplaceWithCurrent
        menu.addItem(replaceItem)

        menu.addItem(NSMenuItem(title: "Edit...", action: {
            promptToEditPinnedPage(tab: context.tab, onRename: { newTitle in
                context.onRename(newTitle)
            })
            context.onRefreshRequested()
        }))

        return menu
    }

    private static func openInSplitViewItem(for tab: TabViewModel) -> NSMenuItem {
        NSMenuItem(title: "Open in Split View", action: {
            CoreUpdateDispatcher.shared.sendAndDispatch(
                .createSplit(tabIds: [tab.id, tab.id], orientation: .horizontal)
            )
        })
    }

    private static func duplicateItem(for tab: TabViewModel) -> NSMenuItem {
        NSMenuItem(title: "Duplicate", action: {
            CoreUpdateDispatcher.shared.sendAndDispatch(.duplicateTab(tabId: tab.id))
        })
    }

    private static func moveToItem(for tab: TabViewModel, spaces: [SpaceViewModel]) -> NSMenuItem {
        let item = NSMenuItem(title: "Move to Space", action: nil, keyEquivalent: "")
        let submenu = NSMenu(title: "Move to Space")
        let candidateSpaces = spaces.filter { $0.id != tab.spaceId }

        if candidateSpaces.isEmpty {
            let unavailableItem = NSMenuItem(title: "No Other Spaces", action: nil, keyEquivalent: "")
            unavailableItem.isEnabled = false
            submenu.addItem(unavailableItem)
        } else {
            for space in candidateSpaces {
                submenu.addItem(NSMenuItem(title: space.name, action: {
                    CoreUpdateDispatcher.shared.sendAndDispatch(.moveTab(tabId: tab.id, targetSpace: space.id, position: -1))
                }))
            }
        }

        item.submenu = submenu
        return item
    }

    private static func removeFavoriteItem(for tab: TabViewModel) -> NSMenuItem {
        NSMenuItem(title: "Remove from Favorites", action: {
            CoreUpdateDispatcher.shared.sendAndDispatch(.unfavoriteTab(tabId: tab.id))
            DispatchQueue.main.async {
                NotificationCenter.default.post(name: .mahoSidebarRefreshRequested, object: nil)
            }
        })
    }

    private static func preferredDisplayTitle(for tab: TabViewModel) -> String {
        let custom = tab.customTitle?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        if !custom.isEmpty { return custom }
        let title = tab.title.trimmingCharacters(in: .whitespacesAndNewlines)
        return title.isEmpty ? tab.url : title
    }

    private static func promptForRename(tab: TabViewModel) -> String? {
        let alert = NSAlert()
        alert.messageText = "Rename Favorite"
        alert.informativeText = "Enter a new name for this favorite."
        alert.addButton(withTitle: "Rename")
        alert.addButton(withTitle: "Cancel")

        let textField = NSTextField(frame: NSRect(x: 0, y: 0, width: 260, height: 24))
        textField.stringValue = preferredDisplayTitle(for: tab)
        alert.accessoryView = textField

        guard alert.runModal() == .alertFirstButtonReturn else { return nil }
        let trimmed = textField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? nil : trimmed
    }

    private static func mutedIndicatorImage() -> NSImage? {
        NSImage(systemSymbolName: "speaker.slash.fill", accessibilityDescription: "Muted")
    }

    private static func canReplacePinnedURL(for tab: TabViewModel, currentActiveTab: TabViewModel?) -> Bool {
        guard let currentActiveTab else { return false }
        return currentActiveTab.id != tab.id && currentActiveTab.url != tab.url
    }

    private static func promptToEditPinnedPage(tab: TabViewModel, onRename: (String) -> Void) {
        guard let renamed = promptForRename(tab: tab) else { return }
        onRename(renamed)
    }

    private static func present(sheetController: NSViewController, in window: NSWindow?) {
        if let presenter = window?.contentViewController {
            presenter.presentAsSheet(sheetController)
            return
        }
        if let presenter = NSApp.mainWindow?.contentViewController {
            presenter.presentAsSheet(sheetController)
            return
        }
        guard let window = window ?? NSApp.mainWindow ?? NSApp.keyWindow else { return }
        let sheetWindow = NSWindow(contentViewController: sheetController)
        sheetWindow.styleMask = [.titled]
        window.beginSheet(sheetWindow)
    }
}

private extension NSMenuItem {
    convenience init(title: String, action: @escaping () -> Void) {
        self.init(title: title, action: #selector(MenuActionTarget.performAction), keyEquivalent: "")
        let target = MenuActionTarget(action: action)
        self.target = target
        self.representedObject = target
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

private final class FavoriteIconSheetController: NSViewController {
    private let currentCustomization: FavoriteTileCustomization?
    private let onSave: (FavoriteTileCustomization?) -> Void

    private let emojiField = NSTextField(frame: .zero)
    private let chooseImageButton = NSButton(title: "Choose Image…", target: nil, action: nil)
    private let clearButton = NSButton(title: "Clear", target: nil, action: nil)
    private let saveButton = NSButton(title: "Save", target: nil, action: nil)
    private var selectedImageData: Data?

    init(currentCustomization: FavoriteTileCustomization?, onSave: @escaping (FavoriteTileCustomization?) -> Void) {
        self.currentCustomization = currentCustomization
        self.onSave = onSave
        self.selectedImageData = currentCustomization?.imageData
        super.init(nibName: nil, bundle: nil)
    }

    required init?(coder: NSCoder) {
        fatalError()
    }

    override func loadView() {
        let root = NSView()
        root.translatesAutoresizingMaskIntoConstraints = false

        let stack = NSStackView()
        stack.translatesAutoresizingMaskIntoConstraints = false
        stack.orientation = .vertical
        stack.spacing = 12
        stack.alignment = .leading
        root.addSubview(stack)

        let titleLabel = NSTextField(labelWithString: "Customize Favorite Icon")
        titleLabel.font = .systemFont(ofSize: 16, weight: .semibold)
        stack.addArrangedSubview(titleLabel)

        let emojiLabel = NSTextField(labelWithString: "Emoji")
        emojiField.placeholderString = "Optional emoji"
        emojiField.stringValue = currentCustomization?.emoji ?? ""
        stack.addArrangedSubview(emojiLabel)
        stack.addArrangedSubview(emojiField)

        chooseImageButton.target = self
        chooseImageButton.action = #selector(chooseImage)
        clearButton.target = self
        clearButton.action = #selector(clearCustomization)
        saveButton.target = self
        saveButton.action = #selector(saveCustomization)

        let buttonRow = NSStackView(views: [chooseImageButton, clearButton, saveButton])
        buttonRow.orientation = .horizontal
        buttonRow.spacing = 8
        stack.addArrangedSubview(buttonRow)

        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: root.topAnchor, constant: 20),
            stack.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 20),
            stack.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -20),
            stack.bottomAnchor.constraint(equalTo: root.bottomAnchor, constant: -20),
            emojiField.widthAnchor.constraint(equalToConstant: 220),
            root.widthAnchor.constraint(equalToConstant: 320)
        ])

        self.view = root
    }

    @objc private func chooseImage() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.allowedContentTypes = [.image]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        selectedImageData = try? Data(contentsOf: url)
    }

    @objc private func clearCustomization() {
        emojiField.stringValue = ""
        selectedImageData = nil
    }

    @objc private func saveCustomization() {
        let trimmedEmoji = emojiField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        let customization = FavoriteTileCustomization(
            emoji: trimmedEmoji.isEmpty ? nil : trimmedEmoji,
            imageData: selectedImageData
        )
        onSave(customization.isEmpty ? nil : customization)
        if let sheetWindow = view.window, let parent = sheetWindow.sheetParent {
            parent.endSheet(sheetWindow)
        } else {
            dismiss(self)
        }
    }
}