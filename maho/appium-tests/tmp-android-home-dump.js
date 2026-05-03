
const { remote } = require("webdriverio");
const fs = require("fs");
(async () => {
  const driver = await remote({ hostname:"127.0.0.1", port:4723, path:"/", capabilities:{ platformName:"Android", "appium:automationName":"UiAutomator2", "appium:deviceName":"emulator-5554", "appium:appPackage":"dev.maho.browser", "appium:appActivity":"dev.maho.browser.MainActivity", "appium:noReset":true, "appium:newCommandTimeout":120, "appium:autoGrantPermissions":true }});
  try {
    await driver.pause(2500);
    const source = await driver.getPageSource();
    fs.writeFileSync('/Users/indo/code/project/maho/appium-tests/android-verification/home-shell-source.xml', source);
    const screenshot = await driver.takeScreenshot();
    fs.writeFileSync('/Users/indo/code/project/maho/appium-tests/android-verification/home-shell.png', screenshot, 'base64');
  } finally { await driver.deleteSession(); }
})().catch((err) => { console.error(err); process.exit(1); });
