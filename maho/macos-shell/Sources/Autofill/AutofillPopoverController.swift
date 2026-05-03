import AppKit
import WebKit

fileprivate enum AutofillSuggestionItem {
    case password(SavedPassword)
    case address(AutofillAddress)
    case payment(AutofillPayment)
    case manageSettings

    var icon: String {
        switch self {
        case .password:
            return "🔑"
        case .address:
            return "📍"
        case .payment:
            return "💳"
        case .manageSettings:
            return "⚙️"
        }
    }

    var primaryText: String {
        switch self {
        case .password(let password):
            return password.domain.isEmpty ? "Saved Login" : password.domain
        case .address(let address):
            return address.name.isEmpty ? "Saved Address" : address.name
        case .payment(let payment):
            return payment.cardName.isEmpty ? "Saved Card" : payment.cardName
        case .manageSettings:
            return "Manage in Settings…"
        }
    }

    var secondaryText: String {
        switch self {
        case .password(let password):
            return password.username.isEmpty ? "No username" : password.username
        case .address(let address):
            return [address.street, address.city].filter { !$0.isEmpty }.joined(separator: ", ")
        case .payment(let payment):
            let last4 = payment.lastFour.isEmpty ? "••••" : "•••• \(payment.lastFour)"
            return payment.expiry.isEmpty ? last4 : "\(last4)  Exp \(payment.expiry)"
        case .manageSettings:
            return "Passwords, addresses, and payments"
        }
    }
}

final class AutofillPopoverController: NSObject, NSTableViewDataSource, NSTableViewDelegate, CoreUpdateObserver {
    static let shared = AutofillPopoverController()

    private static let panelWidth: CGFloat = 300
    private static let rowHeight: CGFloat = 36
    private static let maxPanelHeight: CGFloat = 200
    private static let panelInset: CGFloat = 6
    private static let panelGap: CGFloat = 4
    private static let rowIdentifier = NSUserInterfaceItemIdentifier("AutofillSuggestionCell")

    private enum FieldType: String {
        case password
        case address
        case payment
    }

    private struct ActiveFieldContext {
        let fieldType: FieldType
        let fieldId: String
        let rect: NSRect
    }

    private final class AutofillPanel: NSPanel {
        override var canBecomeKey: Bool { false }
        override var canBecomeMain: Bool { false }
    }

    private let panel: AutofillPanel
    private let containerView = NSView()
    private let scrollView = NSScrollView()
    private let tableView = HoverTrackingTableView()

    private weak var currentWebView: WKWebView?
    private var currentContext: ActiveFieldContext?
    private var suggestionItems: [AutofillSuggestionItem] = []
    private var selectedIndex = 0
    private var passwordSearchDomain = ""
    private var passwordSearchFieldId = ""

    private var inactivityTimer: Timer?
    private var blurDismissWorkItem: DispatchWorkItem?
    private var keyboardMonitor: Any?
    private var mouseMonitor: Any?
    private var scrollMonitor: Any?
    private var settingsWindowController: SettingsWindowController?

    private override init() {
        panel = AutofillPanel(
            contentRect: NSRect(x: 0, y: 0, width: Self.panelWidth, height: 120),
            styleMask: [.borderless, .nonactivatingPanel],
            backing: .buffered,
            defer: false
        )

        super.init()

        setupPanel()
        setupTableView()

        NotificationCenter.default.addObserver(
            self,
            selector: #selector(handleFieldDetected(_:)),
            name: AutofillHandler.fieldDetected,
            object: nil
        )
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(handleFieldBlurred(_:)),
            name: AutofillHandler.fieldBlurred,
            object: nil
        )
        CoreUpdateDispatcher.shared.register(self)
    }

    deinit {
        NotificationCenter.default.removeObserver(self)
        CoreUpdateDispatcher.shared.unregister(self)
        removeEventMonitors()
        inactivityTimer?.invalidate()
        blurDismissWorkItem?.cancel()
    }

    func numberOfRows(in tableView: NSTableView) -> Int {
        suggestionItems.count
    }

    func tableView(_ tableView: NSTableView, heightOfRow row: Int) -> CGFloat {
        Self.rowHeight
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        guard row >= 0, row < suggestionItems.count else { return nil }

        let cell: AutofillSuggestionCellView
        if let reused = tableView.makeView(withIdentifier: Self.rowIdentifier, owner: nil) as? AutofillSuggestionCellView {
            cell = reused
        } else {
            cell = AutofillSuggestionCellView()
            cell.identifier = Self.rowIdentifier
        }

        cell.configure(item: suggestionItems[row], selected: row == selectedIndex)
        return cell
    }

    func handleCoreUpdate(_ update: CoreUpdate) {
        switch update {
        case .passwordsSearchResult(let passwords):
            handlePasswordSearchResult(passwords)
        default:
            break
        }
    }

    fileprivate func show(near rect: NSRect, in webView: WKWebView, items: [AutofillSuggestionItem], fieldType: String, fieldId: String) {
        guard let resolvedFieldType = FieldType(rawValue: fieldType) else { return }
        guard !items.isEmpty else {
            dismiss()
            return
        }

        blurDismissWorkItem?.cancel()
        currentWebView = webView
        currentContext = ActiveFieldContext(fieldType: resolvedFieldType, fieldId: fieldId, rect: rect)
        suggestionItems = items + [.manageSettings]
        selectedIndex = 0
        tableView.reloadData()
        tableView.scrollRowToVisible(0)
        updatePanelFrame(anchorRect: rect, in: webView)
        installEventMonitorsIfNeeded()
        resetInactivityTimer()

        if panel.isVisible {
            panel.orderFront(nil)
        } else {
            panel.orderFrontRegardless()
        }
    }

    func dismiss() {
        blurDismissWorkItem?.cancel()
        blurDismissWorkItem = nil
        inactivityTimer?.invalidate()
        inactivityTimer = nil
        removeEventMonitors()
        panel.orderOut(nil)
        suggestionItems.removeAll()
        selectedIndex = 0
        currentContext = nil
        currentWebView = nil
        passwordSearchDomain = ""
        passwordSearchFieldId = ""
        tableView.reloadData()
    }

    @objc private func handleFieldDetected(_ notification: Notification) {
        blurDismissWorkItem?.cancel()

        guard let userInfo = notification.userInfo,
              let fieldType = userInfo["fieldType"] as? String,
              let fieldId = userInfo["fieldId"] as? String,
              let webView = userInfo["webView"] as? WKWebView,
              let rect = extractRect(from: userInfo["rect"], in: webView) else {
            dismiss()
            return
        }

        switch fieldType {
        case FieldType.password.rawValue:
            if let passwords = userInfo["passwords"] as? [SavedPassword], !passwords.isEmpty {
                let domain = (userInfo["domain"] as? String) ?? webView.url?.host ?? ""
                let items = filterPasswords(passwords, domain: domain).map(AutofillSuggestionItem.password)
                show(near: rect, in: webView, items: items, fieldType: fieldType, fieldId: fieldId)
            } else {
                requestPasswords(for: userInfo, rect: rect, webView: webView, fieldId: fieldId)
            }

        case FieldType.address.rawValue:
            let items = ((userInfo["addresses"] as? [AutofillAddress]) ?? []).map(AutofillSuggestionItem.address)
            show(near: rect, in: webView, items: items, fieldType: fieldType, fieldId: fieldId)

        case FieldType.payment.rawValue:
            let items = ((userInfo["payments"] as? [AutofillPayment]) ?? []).map(AutofillSuggestionItem.payment)
            show(near: rect, in: webView, items: items, fieldType: fieldType, fieldId: fieldId)

        default:
            dismiss()
        }
    }

    @objc private func handleFieldBlurred(_ notification: Notification) {
        guard let fieldId = notification.userInfo?["fieldId"] as? String else {
            dismiss()
            return
        }
        guard fieldId == currentContext?.fieldId else { return }

        let workItem = DispatchWorkItem { [weak self] in
            self?.dismiss()
        }
        blurDismissWorkItem?.cancel()
        blurDismissWorkItem = workItem
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.15, execute: workItem)
    }

    private func setupPanel() {
        panel.isFloatingPanel = true
        panel.level = .floating
        panel.isMovableByWindowBackground = false
        panel.hidesOnDeactivate = false
        panel.collectionBehavior = [.moveToActiveSpace, .fullScreenAuxiliary]
        panel.isReleasedWhenClosed = false
        panel.backgroundColor = .clear
        panel.isOpaque = false
        panel.hasShadow = true
        panel.ignoresMouseEvents = false
        panel.titleVisibility = .hidden
        panel.titlebarAppearsTransparent = true

        containerView.wantsLayer = true
        containerView.layer?.cornerRadius = 8
        containerView.layer?.masksToBounds = true
        containerView.layer?.backgroundColor = NSColor.windowBackgroundColor.withAlphaComponent(0.96).cgColor
        containerView.layer?.borderWidth = 1
        containerView.layer?.borderColor = NSColor.separatorColor.withAlphaComponent(0.32).cgColor

        panel.contentView = containerView

        scrollView.translatesAutoresizingMaskIntoConstraints = false
        scrollView.drawsBackground = false
        scrollView.borderType = .noBorder
        scrollView.hasVerticalScroller = false
        scrollView.hasHorizontalScroller = false
        scrollView.autohidesScrollers = true
        containerView.addSubview(scrollView)

        NSLayoutConstraint.activate([
            scrollView.topAnchor.constraint(equalTo: containerView.topAnchor, constant: Self.panelInset),
            scrollView.leadingAnchor.constraint(equalTo: containerView.leadingAnchor),
            scrollView.trailingAnchor.constraint(equalTo: containerView.trailingAnchor),
            scrollView.bottomAnchor.constraint(equalTo: containerView.bottomAnchor, constant: -Self.panelInset),
        ])
    }

    private func setupTableView() {
        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("AutofillColumn"))
        column.resizingMask = .autoresizingMask
        column.width = Self.panelWidth
        tableView.addTableColumn(column)
        tableView.headerView = nil
        tableView.backgroundColor = .clear
        tableView.intercellSpacing = NSSize(width: 0, height: 0)
        tableView.selectionHighlightStyle = .none
        tableView.focusRingType = .none
        tableView.rowHeight = Self.rowHeight
        tableView.dataSource = self
        tableView.delegate = self
        tableView.onHoverRow = { [weak self] row in
            self?.updateSelection(row: row)
        }
        tableView.onClickRow = { [weak self] row in
            self?.updateSelection(row: row)
            self?.activateSelection(at: row)
        }
        scrollView.documentView = tableView
    }

    private func requestPasswords(for userInfo: [AnyHashable: Any], rect: NSRect, webView: WKWebView, fieldId: String) {
        let domain = normalizeDomain((userInfo["domain"] as? String) ?? webView.url?.host ?? "")
        currentWebView = webView
        currentContext = ActiveFieldContext(fieldType: .password, fieldId: fieldId, rect: rect)
        passwordSearchDomain = domain
        passwordSearchFieldId = fieldId
        CoreUpdateDispatcher.shared.sendAndDispatch(.searchPasswords(query: domain))
    }

    private func handlePasswordSearchResult(_ passwords: [SavedPassword]) {
        guard let context = currentContext,
              context.fieldType == .password,
              !passwordSearchFieldId.isEmpty,
              passwordSearchFieldId == context.fieldId,
              let webView = currentWebView else {
            return
        }

        let items = filterPasswords(passwords, domain: passwordSearchDomain).map(AutofillSuggestionItem.password)
        show(near: context.rect, in: webView, items: items, fieldType: context.fieldType.rawValue, fieldId: context.fieldId)
    }

    private func filterPasswords(_ passwords: [SavedPassword], domain: String) -> [SavedPassword] {
        let normalizedTarget = normalizeDomain(domain)
        guard !normalizedTarget.isEmpty else { return passwords }

        let filtered = passwords.filter { password in
            let candidate = normalizeDomain(password.domain)
            return candidate == normalizedTarget || candidate.contains(normalizedTarget) || normalizedTarget.contains(candidate)
        }
        return filtered.isEmpty ? passwords : filtered
    }

    private func normalizeDomain(_ domain: String) -> String {
        let trimmed = domain.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        if trimmed.hasPrefix("www.") {
            return String(trimmed.dropFirst(4))
        }
        return trimmed
    }

    private func extractRect(from rawValue: Any?, in webView: WKWebView) -> NSRect? {
        if let rect = rawValue as? NSRect {
            return rect
        }

        guard let dictionary = rawValue as? [String: Any] else { return nil }
        let x = (dictionary["x"] as? CGFloat) ?? CGFloat(dictionary["x"] as? Double ?? 0)
        let y = (dictionary["y"] as? CGFloat) ?? CGFloat(dictionary["y"] as? Double ?? 0)
        let width = (dictionary["width"] as? CGFloat) ?? CGFloat(dictionary["width"] as? Double ?? 0)
        let height = (dictionary["height"] as? CGFloat) ?? CGFloat(dictionary["height"] as? Double ?? 0)
        return NSRect(x: x, y: y, width: width, height: height)
    }

    private func updatePanelFrame(anchorRect: NSRect, in webView: WKWebView) {
        guard let window = webView.window else { return }

        let totalHeight = min(CGFloat(suggestionItems.count) * Self.rowHeight + Self.panelInset * 2, Self.maxPanelHeight)
        let needsScroller = CGFloat(suggestionItems.count) * Self.rowHeight + Self.panelInset * 2 > Self.maxPanelHeight
        scrollView.hasVerticalScroller = needsScroller

        let windowRect = webView.convert(anchorRect, to: nil)
        let screenRect = window.convertToScreen(windowRect)
        guard let screen = window.screen ?? NSScreen.main else { return }
        let visibleFrame = screen.visibleFrame.insetBy(dx: 4, dy: 4)

        var originX = screenRect.minX
        let belowY = screenRect.minY - totalHeight - Self.panelGap
        let aboveY = screenRect.maxY + Self.panelGap
        let fitsBelow = belowY >= visibleFrame.minY
        var originY = fitsBelow ? belowY : aboveY

        if originX + Self.panelWidth > visibleFrame.maxX {
            originX = visibleFrame.maxX - Self.panelWidth
        }
        if originX < visibleFrame.minX {
            originX = visibleFrame.minX
        }
        if originY + totalHeight > visibleFrame.maxY {
            originY = visibleFrame.maxY - totalHeight
        }
        if originY < visibleFrame.minY {
            originY = visibleFrame.minY
        }

        panel.setContentSize(NSSize(width: Self.panelWidth, height: totalHeight))
        panel.setFrame(NSRect(x: originX, y: originY, width: Self.panelWidth, height: totalHeight), display: true)
        tableView.sizeLastColumnToFit()
    }

    private func installEventMonitorsIfNeeded() {
        guard keyboardMonitor == nil, mouseMonitor == nil, scrollMonitor == nil else { return }

        keyboardMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
            guard let self = self, self.panel.isVisible else { return event }
            return self.handleKeyEvent(event)
        }

        mouseMonitor = NSEvent.addLocalMonitorForEvents(matching: [.leftMouseDown, .rightMouseDown, .otherMouseDown]) { [weak self] event in
            guard let self = self, self.panel.isVisible else { return event }
            self.resetInactivityTimer()
            if event.window !== self.panel {
                self.dismiss()
            }
            return event
        }

        scrollMonitor = NSEvent.addLocalMonitorForEvents(matching: .scrollWheel) { [weak self] event in
            guard let self = self, self.panel.isVisible else { return event }
            if event.window === self.currentWebView?.window || event.window === self.panel {
                self.dismiss()
            }
            return event
        }
    }

    private func removeEventMonitors() {
        if let keyboardMonitor {
            NSEvent.removeMonitor(keyboardMonitor)
            self.keyboardMonitor = nil
        }
        if let mouseMonitor {
            NSEvent.removeMonitor(mouseMonitor)
            self.mouseMonitor = nil
        }
        if let scrollMonitor {
            NSEvent.removeMonitor(scrollMonitor)
            self.scrollMonitor = nil
        }
    }

    private func resetInactivityTimer() {
        inactivityTimer?.invalidate()
        inactivityTimer = Timer.scheduledTimer(withTimeInterval: 5.0, repeats: false) { [weak self] _ in
            self?.dismiss()
        }
    }

    private func handleKeyEvent(_ event: NSEvent) -> NSEvent? {
        switch event.keyCode {
        case 125:
            moveSelectionDown()
            return nil
        case 126:
            moveSelectionUp()
            return nil
        case 36, 76:
            activateSelection(at: selectedIndex)
            return nil
        case 53:
            dismiss()
            return nil
        default:
            return event
        }
    }

    private func moveSelectionDown() {
        guard !suggestionItems.isEmpty else { return }
        updateSelection(row: min(selectedIndex + 1, suggestionItems.count - 1))
    }

    private func moveSelectionUp() {
        guard !suggestionItems.isEmpty else { return }
        updateSelection(row: max(selectedIndex - 1, 0))
    }

    private func updateSelection(row: Int?) {
        guard let row, row >= 0, row < suggestionItems.count else { return }
        selectedIndex = row
        tableView.reloadData()
        tableView.scrollRowToVisible(row)
        resetInactivityTimer()
    }

    private func activateSelection(at row: Int) {
        guard row >= 0, row < suggestionItems.count,
              currentContext != nil,
              let webView = currentWebView else {
            return
        }

        resetInactivityTimer()

        switch suggestionItems[row] {
        case .password(let password):
            var formData: [String: String] = ["username": password.username]
            if let pw = password.password {
                formData["password"] = pw
            }
            AutofillHandler.shared.fillFormByType(webView: webView, semanticData: formData)
            dismiss()

        case .address(let address):
            fillAddress(address, in: webView)
            dismiss()

        case .payment(let payment):
            fillPayment(payment, in: webView)
            dismiss()

        case .manageSettings:
            openAutofillSettings()
            dismiss()
        }
    }

    private func fillAddress(_ address: AutofillAddress, in webView: WKWebView) {
        resolveAddressFieldMap(in: webView) { fieldMap in
            var formData: [String: String] = [:]
            let nameParts = self.splitName(address.name)

            for (semanticKey, fieldId) in fieldMap {
                switch semanticKey {
                case "name":
                    formData[fieldId] = address.name
                case "given-name":
                    formData[fieldId] = nameParts.givenName
                case "family-name":
                    formData[fieldId] = nameParts.familyName
                case "street-address", "address-line1":
                    formData[fieldId] = address.street
                case "address-level2":
                    formData[fieldId] = address.city
                case "address-level1":
                    formData[fieldId] = address.state
                case "postal-code":
                    formData[fieldId] = address.zip
                case "country", "country-name":
                    formData[fieldId] = address.country
                case "tel", "tel-national":
                    formData[fieldId] = address.phone ?? ""
                case "email":
                    formData[fieldId] = address.email ?? ""
                default:
                    break
                }
            }

            guard !formData.isEmpty else { return }
            AutofillHandler.shared.fillForm(webView: webView, formData: formData)
        }
    }

    private func fillPayment(_ payment: AutofillPayment, in webView: WKWebView) {
        resolvePaymentFieldMap(in: webView) { fieldMap in
            var formData: [String: String] = [:]
            let expiryParts = self.splitExpiry(payment.expiry)
            let nameParts = self.splitName(payment.cardName)

            for (semanticKey, fieldId) in fieldMap {
                switch semanticKey {
                case "cc-name":
                    formData[fieldId] = payment.cardName
                case "cc-given-name":
                    formData[fieldId] = nameParts.givenName
                case "cc-family-name":
                    formData[fieldId] = nameParts.familyName
                case "cc-exp":
                    formData[fieldId] = payment.expiry
                case "cc-exp-month":
                    formData[fieldId] = expiryParts.month
                case "cc-exp-year":
                    formData[fieldId] = expiryParts.year
                default:
                    break
                }
            }

            guard !formData.isEmpty else { return }
            AutofillHandler.shared.fillForm(webView: webView, formData: formData)
        }
    }

    private func resolveAddressFieldMap(in webView: WKWebView, completion: @escaping ([String: String]) -> Void) {
        let script = """
        (function() {
            var ATTR_ID = 'data-maho-autofill-id';
            var fields = document.querySelectorAll('input, textarea, select');
            var result = {};
            function assign(key, fieldId) {
                if (fieldId && !result[key]) result[key] = fieldId;
            }
            for (var i = 0; i < fields.length; i++) {
                var el = fields[i];
                var fieldId = el.getAttribute(ATTR_ID);
                if (!fieldId) continue;
                var ac = (el.getAttribute('autocomplete') || '').trim().toLowerCase();
                var tokens = ac.split(/\\s+/);
                var effectiveAc = tokens[tokens.length - 1] || '';
                var descriptor = ((el.name || '') + ' ' + (el.id || '') + ' ' + (el.placeholder || '')).toLowerCase();
                if (effectiveAc) assign(effectiveAc, fieldId);
                if (/^name$|full.?name/.test(descriptor)) assign('name', fieldId);
                if (/first|given/.test(descriptor)) assign('given-name', fieldId);
                if (/last|family|surname/.test(descriptor)) assign('family-name', fieldId);
                if (/street|address|addr/.test(descriptor)) assign('street-address', fieldId);
                if (/city|town/.test(descriptor)) assign('address-level2', fieldId);
                if (/state|province|region/.test(descriptor)) assign('address-level1', fieldId);
                if (/zip|postal/.test(descriptor)) assign('postal-code', fieldId);
                if (/country/.test(descriptor)) assign('country', fieldId);
                if (/phone|tel|mobile/.test(descriptor)) assign('tel', fieldId);
                if (/email/.test(descriptor)) assign('email', fieldId);
            }
            return result;
        })();
        """

        webView.evaluateJavaScript(script) { result, _ in
            completion(result as? [String: String] ?? [:])
        }
    }

    private func resolvePaymentFieldMap(in webView: WKWebView, completion: @escaping ([String: String]) -> Void) {
        let script = """
        (function() {
            var ATTR_ID = 'data-maho-autofill-id';
            var fields = document.querySelectorAll('input, textarea, select');
            var result = {};
            function assign(key, fieldId) {
                if (fieldId && !result[key]) result[key] = fieldId;
            }
            for (var i = 0; i < fields.length; i++) {
                var el = fields[i];
                var fieldId = el.getAttribute(ATTR_ID);
                if (!fieldId) continue;
                var ac = (el.getAttribute('autocomplete') || '').trim().toLowerCase();
                var tokens = ac.split(/\\s+/);
                var effectiveAc = tokens[tokens.length - 1] || '';
                var descriptor = ((el.name || '') + ' ' + (el.id || '') + ' ' + (el.placeholder || '')).toLowerCase();
                if (effectiveAc) assign(effectiveAc, fieldId);
                if (/card.?name|name.?on.?card|holder/.test(descriptor)) assign('cc-name', fieldId);
                if (/first|given/.test(descriptor) && /card/.test(descriptor)) assign('cc-given-name', fieldId);
                if (/last|family|surname/.test(descriptor) && /card/.test(descriptor)) assign('cc-family-name', fieldId);
                if (/exp|expiry/.test(descriptor)) assign('cc-exp', fieldId);
                if (/exp.*month|month.*exp/.test(descriptor)) assign('cc-exp-month', fieldId);
                if (/exp.*year|year.*exp/.test(descriptor)) assign('cc-exp-year', fieldId);
            }
            return result;
        })();
        """

        webView.evaluateJavaScript(script) { result, _ in
            completion(result as? [String: String] ?? [:])
        }
    }

    private func splitName(_ fullName: String) -> (givenName: String, familyName: String) {
        let parts = fullName.split(whereSeparator: { $0.isWhitespace }).map(String.init)
        guard let first = parts.first else { return ("", "") }
        if parts.count == 1 {
            return (first, "")
        }
        return (first, parts.dropFirst().joined(separator: " "))
    }

    private func splitExpiry(_ expiry: String) -> (month: String, year: String) {
        let digits = expiry.split(whereSeparator: { !$0.isNumber }).map(String.init)
        guard let month = digits.first else { return ("", "") }
        let year = digits.dropFirst().first ?? ""
        return (month, year)
    }

    private func openAutofillSettings() {
        settingsWindowController = settingsWindowController ?? SettingsWindowController()
        settingsWindowController?.showWindow(nil)
        settingsWindowController?.window?.makeKeyAndOrderFront(nil)
        settingsWindowController?.selectPane(identifier: "autofill")
        NSApp.activate(ignoringOtherApps: true)
    }
}

private final class HoverTrackingTableView: NSTableView {
    var onHoverRow: ((Int?) -> Void)?
    var onClickRow: ((Int) -> Void)?

    private var trackingAreaRef: NSTrackingArea?

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let trackingAreaRef {
            removeTrackingArea(trackingAreaRef)
        }
        let trackingArea = NSTrackingArea(
            rect: .zero,
            options: [.activeAlways, .inVisibleRect, .mouseMoved, .mouseEnteredAndExited],
            owner: self,
            userInfo: nil
        )
        addTrackingArea(trackingArea)
        trackingAreaRef = trackingArea
    }

    override func mouseMoved(with event: NSEvent) {
        let point = convert(event.locationInWindow, from: nil)
        let row = row(at: point)
        onHoverRow?(row >= 0 ? row : nil)
        super.mouseMoved(with: event)
    }

    override func mouseExited(with event: NSEvent) {
        onHoverRow?(nil)
        super.mouseExited(with: event)
    }

    override func mouseDown(with event: NSEvent) {
        let point = convert(event.locationInWindow, from: nil)
        let row = row(at: point)
        if row >= 0 {
            onClickRow?(row)
        } else {
            super.mouseDown(with: event)
        }
    }
}

private final class AutofillSuggestionCellView: NSTableCellView {
    private let selectionView = NSView()
    private let iconLabel = NSTextField(labelWithString: "")
    private let primaryLabel = NSTextField(labelWithString: "")
    private let secondaryLabel = NSTextField(labelWithString: "")

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setup()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    fileprivate func configure(item: AutofillSuggestionItem, selected: Bool) {
        selectionView.layer?.backgroundColor = selected
            ? NSColor.selectedContentBackgroundColor.cgColor
            : NSColor.clear.cgColor

        let titleColor: NSColor = selected ? .white : .labelColor
        let subtitleColor: NSColor = selected ? NSColor.white.withAlphaComponent(0.8) : .secondaryLabelColor

        iconLabel.stringValue = item.icon
        primaryLabel.stringValue = item.primaryText
        primaryLabel.textColor = titleColor
        secondaryLabel.stringValue = item.secondaryText
        secondaryLabel.textColor = subtitleColor
        secondaryLabel.isHidden = item.secondaryText.isEmpty
    }

    private func setup() {
        wantsLayer = true

        selectionView.translatesAutoresizingMaskIntoConstraints = false
        selectionView.wantsLayer = true
        selectionView.layer?.cornerRadius = 6
        addSubview(selectionView)

        iconLabel.translatesAutoresizingMaskIntoConstraints = false
        iconLabel.font = .systemFont(ofSize: 16, weight: .regular)
        addSubview(iconLabel)

        primaryLabel.translatesAutoresizingMaskIntoConstraints = false
        primaryLabel.font = .systemFont(ofSize: 13, weight: .bold)
        primaryLabel.lineBreakMode = .byTruncatingTail
        primaryLabel.maximumNumberOfLines = 1
        addSubview(primaryLabel)

        secondaryLabel.translatesAutoresizingMaskIntoConstraints = false
        secondaryLabel.font = .systemFont(ofSize: 11, weight: .regular)
        secondaryLabel.lineBreakMode = .byTruncatingTail
        secondaryLabel.maximumNumberOfLines = 1
        addSubview(secondaryLabel)

        NSLayoutConstraint.activate([
            selectionView.topAnchor.constraint(equalTo: topAnchor, constant: 1),
            selectionView.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 4),
            selectionView.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -4),
            selectionView.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -1),

            iconLabel.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12),
            iconLabel.centerYAnchor.constraint(equalTo: centerYAnchor),
            iconLabel.widthAnchor.constraint(equalToConstant: 18),

            primaryLabel.topAnchor.constraint(equalTo: topAnchor, constant: 6),
            primaryLabel.leadingAnchor.constraint(equalTo: iconLabel.trailingAnchor, constant: 10),
            primaryLabel.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -12),

            secondaryLabel.topAnchor.constraint(equalTo: primaryLabel.bottomAnchor, constant: 1),
            secondaryLabel.leadingAnchor.constraint(equalTo: primaryLabel.leadingAnchor),
            secondaryLabel.trailingAnchor.constraint(equalTo: primaryLabel.trailingAnchor),
            secondaryLabel.bottomAnchor.constraint(lessThanOrEqualTo: bottomAnchor, constant: -5),
        ])
    }
}
