const { remote } = require('webdriverio');
const fs = require('fs');
const path = require('path');

const UDID = 'R3CN8126R4Y';
const APP_PACKAGE = 'dev.maho.browser';
const EVIDENCE_DIR = path.join(__dirname, '..', '..', '..', '.omo', 'evidence', 'real-use-android');
const results = [];

function record(id, status, detail) {
  results.push({ id, status, detail });
  console.log(`[${status}] ${id} :: ${detail}`);
}

async function shot(driver, name) {
  fs.mkdirSync(EVIDENCE_DIR, { recursive: true });
  const file = path.join(EVIDENCE_DIR, `${name}.png`);
  fs.writeFileSync(file, Buffer.from(await driver.takeScreenshot(), 'base64'));
  return file;
}

async function visible(driver, selector) {
  try {
    const el = await driver.$(selector);
    return (await el.isExisting()) && (await el.isDisplayed());
  } catch (_) { return false; }
}

async function until(driver, description, condition, timeout = 45000) {
  return driver.waitUntil(condition, { timeout, interval: 500, timeoutMsg: `timed out waiting for: ${description}` });
}

const tag = (t) => `android=new UiSelector().resourceIdMatches(".*${t}")`;
const desc = (d) => `android=new UiSelector().description("${d}")`;

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
    await until(driver, 'shell root', () => visible(driver, tag('androidShellRoot')), 60000);

    for (const candidate of [desc('New Tab'), tag('arcBottomBarPageButton')]) {
      const el = await driver.$(candidate);
      if (await el.isExisting()) { await el.click(); break; }
    }
    await until(driver, 'url field', () => visible(driver, 'android=new UiSelector().className("android.widget.EditText")'), 15000);
    const field = await driver.$('android=new UiSelector().className("android.widget.EditText")');
    await field.click();
    await field.setValue('https://example.com');
    await driver.pressKeyCode(66);
    await until(driver, 'page rendered', async () => (await driver.getPageSource()).includes('Example Domain'), 45000);

    const tabsButton = await driver.$(desc('Tabs'));
    await tabsButton.click();
    await until(driver, 'tab grid', async () => (await driver.getPageSource()).includes('Tab deck'), 20000);
    await driver.pause(1200);
    const file = await shot(driver, 'c5-tab-grid-preview');

    record('C5-tab-preview-rendered', 'CAPTURED', `tab grid captured for visual check at ${file}`);
  } finally {
    fs.mkdirSync(EVIDENCE_DIR, { recursive: true });
    fs.writeFileSync(path.join(EVIDENCE_DIR, 'c5-results.json'), JSON.stringify(results, null, 2));
    await driver.deleteSession().catch(() => {});
  }
}

run().then(() => { console.log('RESULT captured'); process.exit(0); },
  (e) => { console.error('RUN ERROR', e && e.message ? e.message : e); process.exit(2); });
