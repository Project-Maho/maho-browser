import AppKit
import XCTest

final class CommandBarTests: XCTestCase {
    var app: XCUIApplication!

    private func uiTestState() -> [String: String] {
        let path = "/tmp/maho-uitest-state.json"
        guard let data = FileManager.default.contents(atPath: path),
              let object = try? JSONSerialization.jsonObject(with: data),
              let state = object as? [String: String] else {
            return [:]
        }
        return state
    }

    private func waitForUITestStateValue(_ key: String, timeout: TimeInterval = 4, matching predicate: @escaping (String) -> Bool, allowEmpty: Bool = false) -> String? {
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            if let value = uiTestState()[key], (allowEmpty || !value.isEmpty), predicate(value) {
                return value
            }
            RunLoop.current.run(until: Date().addingTimeInterval(0.05))
        }
        return uiTestState()[key]
    }

    private func waitForUITestStateValue(_ key: String, timeout: TimeInterval = 4) -> String? {
        waitForUITestStateValue(key, timeout: timeout, matching: { _ in true })
    }

    private func mainWindow() -> XCUIElement {
        let window = app.windows["mainWindow"]
        XCTAssertTrue(window.waitForExistence(timeout: 5), "Main window should exist")
        return window
    }

    private func sidebarTabCount(in window: XCUIElement) -> Int {
        let rawValue = uiTestState()["sidebarTabCount"] ?? "0"
        return Int(rawValue) ?? 0
    }

    private func commandBarRowCount() -> Int {
        let rawValue = uiTestState()["commandBarRows"] ?? ""
        if rawValue.isEmpty { return 0 }
        return rawValue.split(separator: "|").count
    }

    private func launchApp(defaultSearchEngine: String? = nil, openSettingsPane: Bool = false) {
        let uiTestStateURL = URL(fileURLWithPath: "/tmp/maho-uitest-state.json")
        try? FileManager.default.removeItem(at: uiTestStateURL)

        app = XCUIApplication()
        app.launchArguments = ["--ui-testing"]
        if openSettingsPane {
            app.launchArguments.append(contentsOf: ["--ui-testing-settings-pane", "general"])
        }
        if let defaultSearchEngine {
            app.launchEnvironment["MAHO_UI_TEST_DEFAULT_SEARCH_ENGINE"] = defaultSearchEngine
        } else {
            app.launchEnvironment.removeValue(forKey: "MAHO_UI_TEST_DEFAULT_SEARCH_ENGINE")
        }
        // Share the test runner's temp directory for file-based text injection
        // (the XCUITest runner may not have write access to /tmp/)
        let injectDir = NSTemporaryDirectory()
        let injectPath = (injectDir as NSString).appendingPathComponent("maho-uitest-inject.txt")
        try? FileManager.default.removeItem(atPath: injectPath)
        app.launchEnvironment["MAHO_UI_TEST_INJECT_PATH"] = injectPath
        app.launch()
    }

    private func setDefaultSearchEngine(_ name: String) {
        app.terminate()
        launchApp(defaultSearchEngine: name, openSettingsPane: true)
        let stateName = waitForUITestStateValue("defaultSearchEngineName", timeout: 5, matching: { $0 == name })
        XCTAssertEqual(stateName, name, "Default search engine should update in UITest state")
        app.terminate()
        launchApp()
    }

    override func setUp() {
        super.setUp()
        continueAfterFailure = false
        launchApp()
    }

    override func tearDown() {
        app.terminate()
        super.tearDown()
    }

    func testOpenCommandBarWithShortcut() {
        let mainWindow = mainWindow()

        let searchPill = mainWindow.buttons["sidebar.search"]
        XCTAssertTrue(searchPill.waitForExistence(timeout: 3), "Search pill should exist")
        searchPill.click()

        let commandBarSearch = app.textFields["commandBar.search"]
        XCTAssertTrue(commandBarSearch.waitForExistence(timeout: 3), "Command bar search field should appear")
    }

    func testFocusURLBarPrefillsCurrentTabURLAndSelectsAll() {
        let mainWindow = mainWindow()
        app.activate()
        mainWindow.click()

        let focusURLBarMenuItem = app.menuBars.menuItems["Focus URL Bar"]
        XCTAssertTrue(focusURLBarMenuItem.waitForExistence(timeout: 3), "Focus URL Bar menu item should exist")
        focusURLBarMenuItem.click()

        let commandBarSearch = app.textFields["commandBar.search"]
        XCTAssertTrue(commandBarSearch.waitForExistence(timeout: 3), "Command bar search field should appear")

        let currentTabURL = waitForUITestStateValue("currentTabURL")
        XCTAssertNotNil(currentTabURL, "Current tab URL should be recorded for UITest assertions")

        let prefilledURL = waitForUITestStateValue("commandBarPrefilledURL")
        XCTAssertNotNil(prefilledURL, "Prefilled command bar URL should be recorded")
        XCTAssertEqual(prefilledURL, currentTabURL, "Current-tab command bar should prefill the active URL")

        let selectionRange = waitForUITestStateValue("commandBarSelectionRange")
        XCTAssertNotNil(selectionRange, "Command bar selection range should be recorded")

        let expectedLength = (currentTabURL ?? "").count
        XCTAssertEqual(selectionRange, "0:\(expectedLength)", "Current-tab command bar should select all text")
    }

    func testOpenCommandBarViaSearchPill() {
        let mainWindow = mainWindow()

        let searchPill = mainWindow.buttons["sidebar.search"]
        XCTAssertTrue(searchPill.waitForExistence(timeout: 3), "Search pill should exist")

        searchPill.click()

        let commandBarSearch = app.textFields["commandBar.search"]
        XCTAssertTrue(commandBarSearch.waitForExistence(timeout: 3), "Command bar should open when search pill is clicked")
    }

    private func openAIPanel() {
        let mainWindow = mainWindow()

        app.activate()
        mainWindow.click()

        let aiButton = mainWindow.buttons["sidebar.nav.ai"]
        XCTAssertTrue(aiButton.waitForExistence(timeout: 3), "AI panel button should exist")
        aiButton.click()

        let aiPanelVisible = waitForUITestStateValue("aiPanelVisible", timeout: 5, matching: { $0 == "1" })
        XCTAssertEqual(aiPanelVisible, "1", "AI panel should report itself visible after opening")
    }

    func testOpenAIPanelShowsVisibleRightSidePanel() {
        openAIPanel()

        let mainWindow = mainWindow()

        let aiPanelVisible = waitForUITestStateValue("aiPanelVisible", timeout: 5, matching: { $0 == "1" })
        XCTAssertEqual(aiPanelVisible, "1", "AI panel should report itself visible after opening")

        let aiPanelMode = waitForUITestStateValue("aiPanelMode", timeout: 5) ?? ""
        XCTAssertFalse(aiPanelMode.isEmpty, "AI panel mode should be recorded for UITest assertions")

        if aiPanelMode == "onboarding" {
            let onboardingCTA = mainWindow.buttons["ai.panel.onboarding.openSettings"]
            XCTAssertTrue(onboardingCTA.waitForExistence(timeout: 5), "Onboarding CTA should be visible")
            XCTAssertGreaterThan(onboardingCTA.frame.minX, mainWindow.frame.midX, "Onboarding CTA should appear on the right side of the main window")
        } else if aiPanelMode == "chat" {
            let composerInput = mainWindow.textViews["ai.panel.composer.input"]
            XCTAssertTrue(composerInput.waitForExistence(timeout: 5), "Composer input should be visible in chat mode")
            XCTAssertGreaterThan(composerInput.frame.minX, mainWindow.frame.midX, "Composer input should appear on the right side of the main window")
        } else {
            XCTFail("Unexpected AI panel mode: \(aiPanelMode)")
        }
    }

    func testAIPanelComposerSendProducesDeterministicAssistantReply() {
        let mainWindow = mainWindow()
        openAIPanel()

        let composerInput = mainWindow.textViews["ai.panel.composer.input"]
        XCTAssertTrue(composerInput.waitForExistence(timeout: 5), "Composer input should be visible in chat mode")
        composerInput.click()
        composerInput.typeText("ping")

        mainWindow.buttons["ai.panel.composer.send"].click()

        let assistantReply = waitForUITestStateValue("aiPanelLatestAssistantText", timeout: 6, matching: { $0.contains("UI test assistant reply to: ping") })
        XCTAssertEqual(assistantReply, "UI test assistant reply to: ping", "AI panel should surface the deterministic ui-testing assistant response")

        let streamingFinished = waitForUITestStateValue("aiPanelStreaming", timeout: 6, matching: { $0 == "0" })
        XCTAssertEqual(streamingFinished, "0", "AI panel should finish streaming deterministically")

        let messageCount = waitForUITestStateValue("aiPanelMessageCount", timeout: 6, matching: { $0 == "2" || $0 == "3" })
        XCTAssertNotNil(messageCount, "AI panel should record message count after the deterministic send")

        let lastAction = waitForUITestStateValue("aiPanelLastAction", timeout: 6, matching: { $0 == "streamFinish" })
        XCTAssertNotNil(lastAction, "AI panel should record the send lifecycle in UITest state")
    }

    func testAIPanelNewChatClearsObservableState() {
        let mainWindow = mainWindow()
        openAIPanel()

        let composerInput = mainWindow.textViews["ai.panel.composer.input"]
        XCTAssertTrue(composerInput.waitForExistence(timeout: 5), "Composer input should be visible in chat mode")
        composerInput.click()
        composerInput.typeText("state reset check")

        app.typeKey(.return, modifierFlags: [])

        XCTAssertNotNil(waitForUITestStateValue("aiPanelLatestAssistantText", timeout: 6, matching: { $0.contains("state reset check") }), "Assistant response should arrive before testing new chat reset")

        let newChatButton = mainWindow.buttons["ai.panel.header.newChat"]
        XCTAssertTrue(newChatButton.waitForExistence(timeout: 3), "New chat button should exist")
        newChatButton.click()

        let lastAction = waitForUITestStateValue("aiPanelLastAction", timeout: 5, matching: { $0 == "newChat" })
        XCTAssertEqual(lastAction, "newChat", "New chat should be recorded in UITest state")

        XCTAssertEqual(waitForUITestStateValue("aiPanelMessageCount", timeout: 5, matching: { $0 == "0" }), "0", "New chat should clear the message count")
        XCTAssertEqual(waitForUITestStateValue("aiPanelAssistantMessageCount", timeout: 5, matching: { $0 == "0" }), "0", "New chat should clear assistant messages")
        XCTAssertEqual(waitForUITestStateValue("aiPanelLatestAssistantText", timeout: 5, matching: { $0.isEmpty }, allowEmpty: true), "", "New chat should clear the latest assistant text")
        XCTAssertEqual(waitForUITestStateValue("aiPanelStreaming", timeout: 5, matching: { $0 == "0" }), "0", "New chat should leave streaming in the idle state")
    }

    func testAIPanelCloseButtonHidesPanel() {
        let mainWindow = mainWindow()
        openAIPanel()

        let closeButton = mainWindow.buttons["ai.panel.header.close"]
        XCTAssertTrue(closeButton.waitForExistence(timeout: 3), "Close button should exist")
        closeButton.click()

        let lastAction = waitForUITestStateValue("aiPanelLastAction", timeout: 5, matching: { $0 == "close" })
        XCTAssertEqual(lastAction, "close", "Close button should be recorded in UITest state")

        let aiPanelVisible = waitForUITestStateValue("aiPanelVisible", timeout: 5, matching: { $0 == "0" })
        XCTAssertEqual(aiPanelVisible, "0", "Close button should hide the AI panel")
    }

    func testAIPanelMicrophoneButtonTriggersPlaceholderState() {
        let mainWindow = mainWindow()

        let microphoneButton = mainWindow.buttons["ai.panel.composer.microphone"]
        XCTAssertTrue(microphoneButton.waitForExistence(timeout: 3), "Microphone button should exist")
        microphoneButton.click()

        let lastAction = waitForUITestStateValue("aiPanelLastAction", timeout: 5, matching: { $0 == "microphone" })
        XCTAssertEqual(lastAction, "microphone", "Microphone button should be recorded in UITest state")

        let microphoneActivated = waitForUITestStateValue("aiPanelMicrophoneActivated", timeout: 5, matching: { $0 == "1" })
        XCTAssertEqual(microphoneActivated, "1", "Microphone button should trigger the observable placeholder state")

        let microphoneSheet = app.sheets.firstMatch
        if microphoneSheet.exists {
            let okButton = microphoneSheet.buttons["OK"]
            if okButton.exists {
                okButton.click()
            }
        }
    }

    func testOpenNewTabCommandBarShowsVisibleRows() {
        let mainWindow = mainWindow()
        app.activate()
        mainWindow.click()

        let newTabRow = mainWindow.otherElements["sidebar.newTab"]
        XCTAssertTrue(newTabRow.waitForExistence(timeout: 3), "New tab row should exist")
        newTabRow.click()

        let commandBarSearch = app.textFields["commandBar.search"]
        XCTAssertTrue(commandBarSearch.waitForExistence(timeout: 3), "Command bar search field should appear")

        let commandBarMode = waitForUITestStateValue("commandBarMode", timeout: 5, matching: { $0.contains("newTabDraft") })
        XCTAssertNotNil(commandBarMode, "Command bar should open in newTabDraft mode")

        let rowsValue = waitForUITestStateValue("commandBarRows", timeout: 5, matching: { !$0.isEmpty })
        XCTAssertNotNil(rowsValue, "Command bar should populate visible rows in newTabDraft mode")
        XCTAssertGreaterThan(commandBarRowCount(), 0, "Command bar row count should be greater than zero")
    }

    /// Regression: selecting a URL-navigation suggestion (e.g. youtube.com) and pressing
    /// Enter must navigate to the URL, not search for display text like "Navigate".
    func testSelectingURLNavigationSuggestionNavigatesToURL() {
        let mainWindow = mainWindow()
        let expectedNavigationRow = "nav:https://xn--uitest-nav.example"

        app.activate()
        mainWindow.click()

        let searchPill = mainWindow.buttons["sidebar.search"]
        XCTAssertTrue(searchPill.waitForExistence(timeout: 3), "Search pill should exist")
        searchPill.click()

        let commandBarSearch = app.textFields["commandBar.search"]
        XCTAssertTrue(commandBarSearch.waitForExistence(timeout: 3), "Command bar search field should appear")

        // Inject text via file to bypass Korean IM keystroke conversion.
        let injectPath = (NSTemporaryDirectory() as NSString).appendingPathComponent("maho-uitest-inject.txt")
        let injectData = "xn--uitest-nav.example".data(using: .utf8)!
        let wrote = FileManager.default.createFile(atPath: injectPath, contents: injectData, attributes: nil)
        XCTAssertTrue(wrote, "File injection write should succeed at \(injectPath)")

        let rowsValue = waitForUITestStateValue("commandBarRows", timeout: 5, matching: { value in
            value.contains(expectedNavigationRow)
        })
        XCTAssertTrue(rowsValue?.contains(expectedNavigationRow) == true, "Command bar should show a navigation suggestion for xn--uitest-nav.example")
        guard let rows = rowsValue else { return }

        let rowKeys = rows.split(separator: "|").map(String.init)
        guard let navIndex = rowKeys.firstIndex(where: { $0.hasPrefix("nav:") }) else {
            XCTFail("Navigation row not found in command bar rows: \(rowKeys)")
            return
        }

        let selectedRowStr = waitForUITestStateValue("commandBarSelectedRow", timeout: 2) ?? "0"
        let currentSelected = Int(selectedRowStr) ?? 0
        for _ in currentSelected..<navIndex {
            commandBarSearch.typeKey(.downArrow, modifierFlags: [])
        }

        commandBarSearch.typeKey(.return, modifierFlags: [])

        let activeURL = waitForUITestStateValue("activeURL", timeout: 5, matching: { value in
            value.contains("xn--uitest-nav.example")
        })
        XCTAssertNotNil(activeURL, "After selecting a URL navigation suggestion, the active page should navigate to the URL, not search for display text")
        if let url = activeURL {
            XCTAssertFalse(url.contains("search?q=Navigate"), "Navigation suggestion must not search for 'Navigate'")
            XCTAssertTrue(url.contains("xn--uitest-nav.example"), "Navigation suggestion should navigate to the actual URL")
        }
    }
}
