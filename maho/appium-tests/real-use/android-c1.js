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
  fs.writeFileSync(path.join(EVIDENCE_DIR, `${name}.png`), Buffer.from(await driver.takeScreenshot(), 'base64'));
}

async function visible(driver, selector) {
  try {
    const el = await driver.$(selector);
    return (await el.isExisting()) && (await el.isDisplayed());
  } catch (_) { return false; }
}

async function until(driver, description, condition, timeout = 30000) {
  return driver.waitUntil(condition, { timeout, interval: 500, timeoutMsg: `timed out waiting for: ${description}` });
}

const tag = (t) => `android=new UiSelector().resourceIdMatches(".*${t}")`;
const desc = (d) => `android=new UiSelector().description("${d}")`;

async function openUrl(driver, url) {
  if (await visible(driver, tag('tabGrid'))) {
    const done = await driver.$('android=new UiSelector().textContains("Done")');
    if (await done.isExisting()) { await done.click(); await driver.pause(1200); }
  }
  const entry = await driver.$(desc('New Tab'));
  await entry.click();
  await until(driver, 'url field', () => visible(driver, 'android=new UiSelector().className("android.widget.EditText")'), 15000);
  const field = await driver.$('android=new UiSelector().className("android.widget.EditText")');
  await field.click();
  await field.clearValue().catch(() => {});
  await field.setValue(url);
  await driver.pressKeyCode(66);
}

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
    await openUrl(driver, 'https://example.com');

    let rendered = false;
    try {
      await until(driver, 'Example Domain rendered', async () => (await driver.getPageSource()).includes('Example Domain'), 45000);
      rendered = true;
    } catch (_) {}
    await shot(driver, 'c1-example-loaded');

    const source = await driver.getPageSource();
    const hasGoogle = source.includes('hplogo') || source.includes('Google 검색');
    record('C1-address-content-match', rendered && !hasGoogle ? 'PASS' : 'FAIL',
      rendered
        ? (hasGoogle ? 'Example Domain rendered BUT Google chrome still present' : 'requested URL rendered its real document on device')
        : 'requested URL did not render Example Domain within 45s');
  } finally {
    fs.mkdirSync(EVIDENCE_DIR, { recursive: true });
    fs.writeFileSync(path.join(EVIDENCE_DIR, 'c1-results.json'), JSON.stringify(results, null, 2));
    await driver.deleteSession().catch(() => {});
  }
}

run().then(() => {
  const bad = results.filter((r) => r.status !== 'PASS');
  console.log(`RESULT ${results.length - bad.length}/${results.length} passed`);
  process.exit(bad.length ? 1 : 0);
}, (e) => { console.error('RUN ERROR', e && e.message ? e.message : e); process.exit(2); });
