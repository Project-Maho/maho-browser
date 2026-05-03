const { remote } = require('webdriverio');

(async () => {
  const driver = await remote({
    hostname: '127.0.0.1',
    port: 4723,
    path: '/',
    capabilities: {
      platformName: 'iOS',
      'appium:automationName': 'XCUITest',
      'appium:udid': '9E69A245-2BA0-4DB2-AB89-2D667ECCEC4B',
      'appium:bundleId': 'dev.maho.browser',
      'appium:noReset': true,
      'appium:connectionRetryTimeout': 120000,
    },
    connectionRetryTimeout: 120000,
    connectionRetryCount: 5,
  });

  try {
    const { width, height } = await driver.getWindowSize();
    console.log(`RESOLUTION: ${width}x${height}`);
    
    if (width > 350 && height > 700) {
      console.log('✅ Resolution is correct (full screen)');
    } else {
      console.log('❌ Resolution still wrong (legacy mode)');
    }

    await driver.saveScreenshot('/Users/indo/code/project/maho/appium-tests/screenshots/resolution-check.png');
    console.log('Screenshot saved');
  } finally {
    await driver.deleteSession();
  }
})();
