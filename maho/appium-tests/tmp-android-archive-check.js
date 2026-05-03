
const { remote } = require("webdriverio");
const fs = require("fs");

async function firstExisting(driver, selectors) {
  for (const selector of selectors) {
    const elements = await driver.$$(selector);
    if (elements.length) return elements[0];
  }
  return null;
}

(async () => {
  const driver = await remote({
    hostname: "127.0.0.1",
    port: 4723,
    path: "/",
    capabilities: {
      platformName: "Android",
      "appium:automationName": "UiAutomator2",
      "appium:deviceName": "emulator-5554",
      "appium:appPackage": "dev.maho.browser",
      "appium:appActivity": "dev.maho.browser.MainActivity",
      "appium:noReset": true,
      "appium:newCommandTimeout": 120,
      "appium:autoGrantPermissions": true
    }
  });
  try {
    await driver.pause(2500);
    const more = await firstExisting(driver, [
      '~arcBottomBarMoreButton',
      '//*[@resource-id="arcBottomBarMoreButton"]',
      '//*[@content-desc="More"]'
    ]);
    if (!more) throw new Error('More button not found');
    await more.click();
    await driver.pause(1000);

    const menuSource = await driver.getPageSource();
    fs.writeFileSync('/Users/indo/code/project/maho/appium-tests/android-verification/archive-menu-source.xml', menuSource);

    const archive = await firstExisting(driver, [
      '//*[@text="Archive"]',
      '//*[@content-desc="Archive"]'
    ]);
    const archiveMenuItem = !!archive;
    if (!archiveMenuItem) throw new Error('Archive menu item not found');

    await archive.click();
    await driver.pause(1200);

    const archiveTitle = await firstExisting(driver, [
      '//*[@text="Archive"]',
      '//*[@content-desc="Archive"]'
    ]);
    const emptyState = await firstExisting(driver, [
      '//*[@text="No Archived Tabs"]'
    ]);
    const archiveScreenVisible = !!archiveTitle;
    const archiveEmptyVisible = !!emptyState;

    const source = await driver.getPageSource();
    fs.writeFileSync('/Users/indo/code/project/maho/appium-tests/android-verification/archive-flow-source.xml', source);
    const screenshot = await driver.takeScreenshot();
    fs.writeFileSync('/Users/indo/code/project/maho/appium-tests/android-verification/archive-flow.png', screenshot, 'base64');
    console.log(JSON.stringify({ archiveMenuItem, archiveScreenVisible, archiveEmptyVisible }));
  } finally {
    await driver.deleteSession();
  }
})().catch((err) => {
  console.error(err);
  process.exit(1);
});
