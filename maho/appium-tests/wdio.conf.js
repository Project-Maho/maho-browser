exports.config = {
    runner: 'local',
    port: 4723,
    specs: ['./tests/**/*.test.js'],
    maxInstances: 1,
    capabilities: [{
        platformName: 'iOS',
        'appium:automationName': 'XCUITest',
        'appium:deviceName': 'iPhone 17 Pro',
        'appium:platformVersion': '26.4',
        'appium:app': '/Users/indo/Library/Developer/Xcode/DerivedData/Maho-cncqahqhgqnsbaetovgsgoskvekd/Build/Products/Debug-iphonesimulator/Maho.app',
        'appium:udid': '9E69A245-2BA0-4DB2-AB89-2D667ECCEC4B',
        'appium:noReset': true,
        'appium:isSimulator': true,
    }],
    logLevel: 'info',
    bail: 0,
    waitforTimeout: 10000,
    connectionRetryTimeout: 120000,
    connectionRetryCount: 3,
    services: ['appium'],
    framework: 'mocha',
    reporters: ['spec'],
    mochaOpts: {
        ui: 'bdd',
        timeout: 60000
    }
}
