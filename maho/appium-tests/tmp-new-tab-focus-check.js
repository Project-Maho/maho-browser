
const { remote } = require("webdriverio");
const fs = require("fs");
(async () => {
  const driver = await remote({ hostname:"127.0.0.1", port:4723, path:"/", capabilities:{ platformName:"iOS", "appium:automationName":"XCUITest", "appium:deviceName":"iPhone 17 Pro", "appium:platformVersion":"26.4", "appium:app":"/Users/indo/Library/Developer/Xcode/DerivedData/Maho-cncqahqhgqnsbaetovgsgoskvekd/Build/Products/Debug-iphonesimulator/Maho.app", "appium:udid":"9E69A245-2BA0-4DB2-AB89-2D667ECCEC4B", "appium:noReset":false, "appium:fullReset":false, "appium:isSimulator":true }});
  try {
    const pause = (ms) => driver.pause(ms);
    const skip = await driver.$('//XCUIElementTypeButton[@name="Skip"]');
    if (await skip.isExisting()) { await skip.click(); await pause(1200); }
    await (await driver.$('~tabsButton')).click();
    await pause(1200);
    const plus = await driver.$('~tabGridNewTabButton');
    const plusExists = await plus.isExisting();
    if (!plusExists) throw new Error('tabGridNewTabButton not exposed');
    await plus.click();
    await pause(1800);
    const field = await driver.$('~homeSearchField');
    const fieldExists = await field.isExisting();
    const value = fieldExists ? await field.getAttribute('value') : 'missing';
    const screenshot = await driver.takeScreenshot();
    fs.writeFileSync('/Users/indo/code/project/maho/appium-tests/screenshots/new-tab-focus-check.png', screenshot, 'base64');
    const source = await driver.getPageSource();
    fs.writeFileSync('/Users/indo/code/project/maho/appium-tests/screenshots/new-tab-focus-source.xml', source);
    console.log(JSON.stringify({ plusExists, fieldExists, value }));
  } finally { await driver.deleteSession(); }
})().catch((err) => { console.error(err); process.exit(1); });
