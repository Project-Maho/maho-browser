const { remote } = require('webdriverio');
const fs = require('fs');
const path = require('path');

const UDID = '00008101-001E3DE93A86001E';
const BUNDLE_ID = 'com.maho.browser';
const EVIDENCE_DIR = path.join(__dirname, '..', '..', '..', '.omo', 'evidence', 'real-use-ios');
const results = [];

function record(id, status, detail) {
  results.push({ id, status, detail });
  console.log(`[${status}] ${id} :: ${detail}`);
}

async function shot(driver, name) {
  fs.mkdirSync(EVIDENCE_DIR, { recursive: true });
  fs.writeFileSync(path.join(EVIDENCE_DIR, `${name}.png`), Buffer.from(await driver.takeScreenshot(), 'base64'));
}

async function until(driver, description, condition, timeout = 45000) {
  return driver.waitUntil(condition, { timeout, interval: 500, timeoutMsg: `timed out waiting for: ${description}` });
}

async function sourceHas(driver, needle) {
  try {
    return (await driver.getPageSource()).includes(needle);
  } catch (_) {
    return false;
  }
}

async function run() {
  const driver = await remote({
    hostname: '127.0.0.1',
    port: 4723,
    logLevel: 'error',
    capabilities: {
      platformName: 'iOS',
      'appium:automationName': 'XCUITest',
      'appium:udid': UDID,
      'appium:bundleId': BUNDLE_ID,
      'appium:noReset': true,
      'appium:fullReset': false,
      'appium:newCommandTimeout': 300,
      'appium:wdaLaunchTimeout': 240000,
      'appium:usePrebuiltWDA': false,
    },
  });

  try {
    await driver.pause(4000);
    await shot(driver, 'ios-01-launch');

    const labels = ['Continue', 'Continue without an account', 'Maybe later', 'Dive in!'];
    for (let step = 0; step < 12; step += 1) {
      let clicked = false;
      for (const label of labels) {
        const el = await driver.$(`~${label}`);
        if (await el.isExisting()) {
          await el.click();
          await driver.pause(1500);
          clicked = true;
          break;
        }
      }
      if (!clicked) break;
    }
    await shot(driver, 'ios-02-after-onboarding');

    const fields = await driver.$$('//XCUIElementTypeTextField | //XCUIElementTypeSearchField');
    if (fields.length === 0) {
      record('C4-ios-address-content-match', 'BLOCKED', 'no address entry field exposed on the iOS surface');
      await shot(driver, 'ios-03-no-field');
      return;
    }

    await fields[0].click();
    await driver.pause(600);
    await fields[0].setValue('https://example.com\n');

    let rendered = false;
    try {
      await until(driver, 'Example Domain rendered', () => sourceHas(driver, 'Example Domain'), 45000);
      rendered = true;
    } catch (_) {}
    await shot(driver, 'ios-04-example');

    record('C4-ios-address-content-match', rendered ? 'PASS' : 'FAIL',
      rendered
        ? 'submitted URL rendered its real document on iPhone 12 Pro'
        : 'submitted URL did not render Example Domain within 45s');
  } finally {
    fs.mkdirSync(EVIDENCE_DIR, { recursive: true });
    fs.writeFileSync(path.join(EVIDENCE_DIR, 'c4-results.json'), JSON.stringify(results, null, 2));
    await driver.deleteSession().catch(() => {});
  }
}

run().then(() => {
  const bad = results.filter((r) => r.status === 'FAIL');
  console.log(`RESULT ${results.filter((r) => r.status === 'PASS').length} pass, ${bad.length} fail`);
  process.exit(bad.length ? 1 : 0);
}, (e) => { console.error('RUN ERROR', e && e.message ? e.message : e); process.exit(2); });
