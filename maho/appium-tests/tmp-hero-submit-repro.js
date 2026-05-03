const { remote } = require('webdriverio');
const fs = require('fs');

(async () => {
  const driver = await remote({
    hostname: '127.0.0.1',
    port: 4723,
    path: '/',
    logLevel: 'warn',
    capabilities: {
      platformName: 'iOS',
      'appium:automationName': 'XCUITest',
      'appium:deviceName': 'iPhone 17 Pro',
      'appium:platformVersion': '26.4',
      'appium:app': '/Users/indo/Library/Developer/Xcode/DerivedData/Maho-cncqahqhgqnsbaetovgsgoskvekd/Build/Products/Debug-iphonesimulator/Maho.app',
      'appium:udid': '9E69A245-2BA0-4DB2-AB89-2D667ECCEC4B',
      'appium:noReset': false,
      'appium:fullReset': false,
      'appium:isSimulator': true,
    },
  });

  await driver.pause(2500);
  const skip = await driver.$('//XCUIElementTypeButton[@name="Skip"]');
  if (await skip.isExisting()) {
    await skip.click();
    await driver.pause(1500);
  }

  const search = await driver.$('~homeSearchField');
  console.log('search exists', await search.isExisting());
  await search.click();
  await driver.pause(800);

  const keyboard = await driver.$('//XCUIElementTypeKeyboard');
  console.log('keyboard after click', await keyboard.isExisting(), await keyboard.isDisplayed().catch(() => false));

  await search.clearValue();
  await search.setValue('example.com');
  await driver.pause(500);

  const sourceBefore = await driver.getPageSource();
  console.log('before submit has keyboard', sourceBefore.includes('XCUIElementTypeKeyboard'));

  try {
    await driver.execute('mobile: performEditorAction', { action: 'search' });
  } catch (e) {
    console.log('performEditorAction failed', e.message);
  }

  await driver.pause(2500);
  const sourceAfter = await driver.getPageSource();
  fs.writeFileSync('/tmp/hero-submit-source.xml', sourceAfter);
  fs.writeFileSync('/tmp/hero-submit.png', await driver.takeScreenshot(), 'base64');

  console.log('after submit has webview', sourceAfter.includes('XCUIElementTypeWebView') || sourceAfter.includes('WebView'));
  console.log('after submit has bottom bar', sourceAfter.includes('arcBottomBar') || sourceAfter.includes('mainBrowserBottomBar'));

  await driver.deleteSession();
})().catch(err => {
  console.error(err);
  process.exit(1);
});