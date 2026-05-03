import XCTest

final class DebugTests: XCTestCase {
    var app: XCUIApplication!

    override func setUp() {
        super.setUp()
        continueAfterFailure = true
        app = XCUIApplication()
        app.launchArguments = ["--ui-testing"]
        app.launch()
    }

    override func tearDown() {
        app.terminate()
        super.tearDown()
    }

    func testDebugAccessibility() {
        sleep(3)
        
        let allWindows = app.windows.allElementsBoundByIndex
        NSLog("DEBUG: Total windows = %d", allWindows.count)
        for (i, w) in allWindows.enumerated() {
            NSLog("DEBUG: window[%d] id='%@' title='%@' exists=%d", i, w.identifier, w.title, w.exists ? 1 : 0)
        }
        
        let mainWindow = app.windows["mainWindow"]
        NSLog("DEBUG: mainWindow by ID exists = %d", mainWindow.exists ? 1 : 0)
        
        let mahoWindow = app.windows["Maho"]
        NSLog("DEBUG: Maho window by title exists = %d", mahoWindow.exists ? 1 : 0)
        
        NSLog("DEBUG: app debugDescription = %@", app.debugDescription)
        
        let firstWindow = app.windows.firstMatch
        NSLog("DEBUG: firstWindow exists = %d, id='%@', title='%@'", firstWindow.exists ? 1 : 0, firstWindow.identifier, firstWindow.title)
    }
}
