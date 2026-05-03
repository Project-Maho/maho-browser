const { remote } = require('webdriverio');
const fs = require('fs');

(async () => {
  const caps = {
    platformName: 'iOS',
    'appium:automationName': 'XCUITest',
    'appium:deviceName': 'iPhone 17 Pro',
    'appium:platformVersion': '26.4',
    'appium:app': '/Users/indo/Library/Developer/Xcode/DerivedData/Maho-cncqahqhgqnsbaetovgsgoskvekd/Build/Products/Debug-iphonesimulator/Maho.app',
    'appium:udid': '9E69A245-2BA0-4DB2-AB89-2D667ECCEC4B',
    'appium:noReset': false,
    'appium:fullReset': false,
    'appium:isSimulator': true,
  };

  const driver = await remote({
    hostname: '127.0.0.1',
    port: 4723,
    path: '/',
    logLevel: 'warn',
    capabilities: caps,
  });

  const firstExisting = async (sels) => {
    for (const s of sels) {
      const el = await driver.$(s);
      if (el && await el.isExisting()) return el;
    }
    return null;
  };

  const tapAt = async (x, y) => {
    await driver.performActions([{
      type: 'pointer',
      id: 'finger1',
      parameters: { pointerType: 'touch' },
      actions: [
        { type: 'pointerMove', duration: 0, x, y },
        { type: 'pointerDown', button: 0 },
        { type: 'pause', duration: 50 },
        { type: 'pointerUp', button: 0 },
      ],
    }]);
    await driver.releaseActions();
    await driver.pause(400);
  };

  const PAGE_MENU_BUTTON_SELECTORS = [
    '//XCUIElementTypeButton[@name="arcBottomBar" and @label="Page menu"]',
    '//XCUIElementTypeButton[@label="Page menu"]',
    '//XCUIElementTypeButton[@name="Page menu"]',
    '~arcMenuButton',
  ];

  await driver.pause(2500);
  const skip = await driver.$('//XCUIElementTypeButton[@name="Skip"]');
  if (await skip.isExisting()) {
    await skip.click();
    await driver.pause(1500);
  }

  const search = await firstExisting(['~homeSearchField', '~homeSearchBar']);
  await search.clearValue();
  await search.setValue('example.com\n');
  await driver.pause(5000);

  let menu = await firstExisting(PAGE_MENU_BUTTON_SELECTORS);
  await menu.click();
  await driver.pause(1200);
  fs.writeFileSync('/tmp/menu-main-before-view.png', await driver.takeScreenshot(), 'base64');

  let view = await firstExisting([
    '//XCUIElementTypeButton[@label="View"]',
    '//XCUIElementTypeStaticText[@name="View"]',
  ]);
  await view.click();
  await driver.pause(1200);
  fs.writeFileSync('/tmp/menu-view-before-dismiss.png', await driver.takeScreenshot(), 'base64');

  const rect = await driver.getWindowRect();
  await tapAt(Math.round(rect.width / 2), Math.round(rect.height / 3));
  await tapAt(Math.round(rect.width / 2), Math.round(rect.height / 3));
  fs.writeFileSync('/tmp/menu-after-dismiss-taps.png', await driver.takeScreenshot(), 'base64');
  fs.writeFileSync('/tmp/menu-after-dismiss.xml', await driver.getPageSource());

  menu = await firstExisting(PAGE_MENU_BUTTON_SELECTORS);
  console.log('menu button exists after dismiss', !!menu);
  if (menu) {
    await menu.click();
    await driver.pause(1200);
  }

  fs.writeFileSync('/tmp/menu-after-reopen.png', await driver.takeScreenshot(), 'base64');
  const source2 = await driver.getPageSource();
  fs.writeFileSync('/tmp/menu-after-reopen.xml', source2);
  console.log('contains Settings after reopen', source2.includes('Settings'));
  console.log('contains View after reopen', source2.includes('View'));
  console.log('contains Reader after reopen', source2.includes('Reader'));

  await driver.deleteSession();
})().catch(err => {
  console.error(err);
  process.exit(1);
});