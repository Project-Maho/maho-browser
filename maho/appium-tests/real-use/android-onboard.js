const { remote } = require('webdriverio');

const UDID = 'R3CN8126R4Y';
const APP_PACKAGE = 'dev.maho.browser';

async function visible(driver, selector) {
  try {
    const el = await driver.$(selector);
    return (await el.isExisting()) && (await el.isDisplayed());
  } catch (_) { return false; }
}

const tag = (t) => `android=new UiSelector().resourceIdMatches(".*${t}")`;
const clickable = (text) => `android=new UiSelector().text("${text}").clickable(true)`;

async function run() {
  const driver = await remote({
    hostname: '127.0.0.1', port: 4723, logLevel: 'error',
    capabilities: {
      platformName: 'Android', 'appium:automationName': 'UiAutomator2', 'appium:udid': UDID,
      'appium:appPackage': APP_PACKAGE, 'appium:appActivity': '.MainActivity',
      'appium:noReset': true, 'appium:fullReset': false,
      'appium:autoGrantPermissions': false, 'appium:newCommandTimeout': 300,
    },
  });

  try {
    const labels = ['Continue', 'Continue without an account', 'Maybe later', 'Dive in!'];
    for (let step = 0; step < 12; step += 1) {
      if (await visible(driver, tag('mainBrowserHomeSurface')) || await visible(driver, tag('mainBrowserBrowsingSurface'))) break;
      let clicked = false;
      for (const label of labels) {
        const el = await driver.$(clickable(label));
        if (await el.isExisting()) {
          await el.click();
          await driver.pause(1500);
          clicked = true;
          break;
        }
      }
      if (!clicked) await driver.pause(1000);
    }
    const done = await visible(driver, tag('mainBrowserHomeSurface')) || await visible(driver, tag('mainBrowserBrowsingSurface'));
    console.log(done ? 'ONBOARDING_DONE' : 'ONBOARDING_STUCK');
  } finally {
    await driver.deleteSession().catch(() => {});
  }
}

run().catch((e) => { console.error('ERROR', e && e.message ? e.message : e); process.exit(2); });
