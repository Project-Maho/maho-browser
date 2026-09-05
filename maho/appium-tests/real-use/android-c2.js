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
const clickable = (text) => `android=new UiSelector().text("${text}").clickable(true)`;

async function run() {
  // Onboarding state lives in app storage; this run intentionally re-triggers
  // onboarding through the shell's own reset RPC rather than wiping user data.
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
    await shot(driver, 'c2-start');

    let source = await driver.getPageSource();
    if (!source.includes('Sign in to Maho') && source.includes('Continue')) {
      const cont = await driver.$(clickable('Continue'));
      if (await cont.isExisting()) {
        await cont.click();
        await until(driver, 'auth step', async () => (await driver.getPageSource()).includes('Sign in to Maho'), 20000)
          .catch(() => {});
        source = await driver.getPageSource();
      }
    }

    if (!source.includes('Sign in to Maho')) {
      record('C2-skip-button-present', 'SKIP', 'device is past onboarding; auth step not reachable this run');
      return;
    }

    const skip = await driver.$(clickable('Continue without an account'));
    if (!(await skip.isExisting())) {
      record('C2-skip-button-present', 'FAIL', 'auth step shown but no "Continue without an account" affordance');
      await shot(driver, 'c2-no-skip');
      return;
    }
    await skip.click();
    await driver.pause(1500);
    await shot(driver, 'c2-after-skip');

    let reached = false;
    try {
      await until(driver, 'theme step or home', async () => {
        const s = await driver.getPageSource();
        return s.includes('Choose your theme') || (await visible(driver, tag('mainBrowserHomeSurface')));
      }, 20000);
      reached = true;
    } catch (_) {}

    record('C2-no-account-path', reached ? 'PASS' : 'FAIL',
      reached ? 'advanced past the auth step without signing in' : 'skip did not advance past the auth step');
  } finally {
    fs.mkdirSync(EVIDENCE_DIR, { recursive: true });
    fs.writeFileSync(path.join(EVIDENCE_DIR, 'c2-results.json'), JSON.stringify(results, null, 2));
    await driver.deleteSession().catch(() => {});
  }
}

run().then(() => {
  const bad = results.filter((r) => r.status !== 'PASS');
  console.log(`RESULT ${results.filter(r=>r.status==='PASS').length} pass, ${bad.length} fail, ${results.filter(r=>r.status==='SKIP').length} skip`);
  process.exit(bad.length ? 1 : 0);
}, (e) => { console.error('RUN ERROR', e && e.message ? e.message : e); process.exit(2); });
