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

async function until(driver, description, condition, timeout = 45000) {
  return driver.waitUntil(condition, { timeout, interval: 500, timeoutMsg: `timed out waiting for: ${description}` });
}

const tag = (t) => `android=new UiSelector().resourceIdMatches(".*${t}")`;
const desc = (d) => `android=new UiSelector().description("${d}")`;

async function openUrl(driver, url) {
  if (await visible(driver, tag('tabGrid'))) {
    const done = await driver.$('android=new UiSelector().textContains("Done")');
    if (await done.isExisting()) { await done.click(); await driver.pause(1200); }
  }
  for (const candidate of [desc('New Tab'), tag('arcBottomBarPageButton'), tag('homeSearchBar'), tag('urlBar')]) {
    const el = await driver.$(candidate);
    if (await el.isExisting()) { await el.click(); break; }
  }
  await until(driver, 'url field', () => visible(driver, 'android=new UiSelector().className("android.widget.EditText")'), 15000);
  const field = await driver.$('android=new UiSelector().className("android.widget.EditText")');
  await field.click();
  await field.clearValue().catch(() => {});
  await field.setValue(url);
  await driver.pressKeyCode(66);
}

async function waitForText(driver, needle, timeout = 45000) {
  try {
    await until(driver, `page containing ${needle}`, async () => (await driver.getPageSource()).includes(needle), timeout);
    return true;
  } catch (_) { return false; }
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
    const first = await waitForText(driver, 'Example Domain');
    record('C1-address-content-match', first ? 'PASS' : 'FAIL',
      first ? 'example.com rendered its real document' : 'example.com did not render');
    await shot(driver, 'c3-first-doc');

    const link = await driver.$('android=new UiSelector().textContains("Learn more")');
    if (await link.isExisting()) {
      await link.click();
    }
    const second = await waitForText(driver, 'Example Domains', 60000);
    await shot(driver, 'c3-second-doc');
    if (!second) {
      record('C3-back-restores-previous-document', 'FAIL', 'second document never rendered, back could not be judged');
      return;
    }

    await driver.back();
    const restored = await waitForText(driver, 'Example Domain', 30000);
    await shot(driver, 'c3-after-back');
    record('C3-back-restores-previous-document', restored ? 'PASS' : 'FAIL',
      restored ? 'back navigation restored the first document' : 'back navigation did NOT restore the first document');
  } finally {
    fs.mkdirSync(EVIDENCE_DIR, { recursive: true });
    fs.writeFileSync(path.join(EVIDENCE_DIR, 'c3-results.json'), JSON.stringify(results, null, 2));
    await driver.deleteSession().catch(() => {});
  }
}

run().then(() => {
  const bad = results.filter((r) => r.status !== 'PASS');
  console.log(`RESULT ${results.length - bad.length}/${results.length} passed`);
  process.exit(bad.length ? 1 : 0);
}, (e) => { console.error('RUN ERROR', e && e.message ? e.message : e); process.exit(2); });
