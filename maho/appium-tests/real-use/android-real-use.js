/**
 * Real-use verification of the installed Maho app on a pinned physical device.
 * Assertions read rendered device state, so this is not a component test, and
 * `noReset` keeps the existing user profile intact.
 */
const { remote } = require('webdriverio');
const fs = require('fs');
const path = require('path');

const UDID = process.env.ANDROID_UDID || 'R3CN8126R4Y';
const APP_PACKAGE = 'dev.maho.browser';
const APP_ACTIVITY = '.MainActivity';
const EVIDENCE_DIR = path.join(__dirname, '..', '..', '..', '.omo', 'evidence', 'real-use-android');
const ANDROID_KEYCODE_ENTER = 66;
const RUN_TAG = `maho-qa-${Date.now()}`;

const capabilities = {
  platformName: 'Android',
  'appium:automationName': 'UiAutomator2',
  'appium:udid': UDID,
  'appium:appPackage': APP_PACKAGE,
  'appium:appActivity': APP_ACTIVITY,
  'appium:appWaitActivity': `${APP_PACKAGE}.*`,
  'appium:noReset': true,
  'appium:fullReset': false,
  // Permission prompts are themselves under test, so they must not be pre-granted.
  'appium:autoGrantPermissions': false,
  'appium:newCommandTimeout': 300,
  'appium:uiautomator2ServerLaunchTimeout': 120000,
  'appium:adbExecTimeout': 120000,
};

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

async function until(driver, description, condition, timeout = 30000) {
  return driver.waitUntil(condition, {
    timeout,
    interval: 500,
    timeoutMsg: `timed out waiting for: ${description}`,
  });
}

function byTag(tag) {
  return `android=new UiSelector().resourceIdMatches(".*${tag}")`;
}

function byDescription(description) {
  return `android=new UiSelector().description("${description}")`;
}

function byTextContains(text) {
  return `android=new UiSelector().textContains("${text}")`;
}

function byClickableText(text) {
  return `android=new UiSelector().text("${text}").clickable(true)`;
}

function byClickableTextDescendant(text) {
  return `android=new UiSelector().clickable(true).childSelector(new UiSelector().text("${text}"))`;
}

async function visible(driver, selector) {
  try {
    const el = await driver.$(selector);
    return (await el.isExisting()) && (await el.isDisplayed());
  } catch (_) {
    return false;
  }
}

async function firstVisible(driver, selectors) {
  for (const selector of selectors) {
    if (await visible(driver, selector)) return driver.$(selector);
  }
  return null;
}

async function passOnboarding(driver) {
  const onboardingWebView = 'android=new UiSelector().className("android.webkit.WebView")';
  const homeVisible = async () =>
    (await visible(driver, byTag('mainBrowserHomeSurface')))
    || (await visible(driver, byTag('mainBrowserBrowsingSurface')));

  if (await homeVisible()) {
    record('NAV-onboarding', 'PASS', 'app opened straight to the home surface (onboarding already completed)');
    return true;
  }

  if (!(await visible(driver, onboardingWebView))) {
    record('NAV-onboarding', 'FAIL', 'neither home surface nor onboarding WebView was rendered on launch');
    return false;
  }

  await shot(driver, 'onboarding');
  const advanceLabels = ['Dive in!', 'Continue', 'Get Started', 'Start browsing', 'Finish', 'Done', 'Maybe later', 'Skip'];
  for (let step = 0; step < 10; step += 1) {
    if (await homeVisible()) break;
    const selectors = advanceLabels.flatMap((label) => [
      byClickableText(label),
      byClickableTextDescendant(label),
    ]);
    const next = await firstVisible(driver, selectors);
    if (!next) break;
    await next.click();
    await driver.pause(1500);
  }

  if (await homeVisible()) {
    record('NAV-onboarding', 'PASS', 'onboarding completed and the home surface rendered');
    return true;
  }
  record('NAV-onboarding', 'FAIL', 'onboarding did not reach the home surface through its own controls');
  return false;
}

async function openUrl(driver, url) {
  let field = await firstVisible(driver, [
    byTag('searchSheetTextField'),
    byTag('searchSheetField'),
    'android=new UiSelector().className("android.widget.EditText")',
  ]);

  if (!field) {
    const searchEntry = await firstVisible(driver, [
      byTag('homeSearchBar'),
      byDescription('New Tab'),
      byTag('arcBottomBarPageButton'),
    ]);
    if (!searchEntry) return false;
    await searchEntry.click();
    await until(
      driver,
      'URL entry field',
      () => visible(driver, 'android=new UiSelector().className("android.widget.EditText")'),
      15000,
    ).catch(() => {});
    field = await firstVisible(driver, [
      byTag('searchSheetTextField'),
      byTag('searchSheetField'),
      'android=new UiSelector().className("android.widget.EditText")',
    ]);
  }

  if (!field) return false;
  await field.clearValue().catch(() => {});
  await field.setValue(url);
  await driver.pressKeyCode(ANDROID_KEYCODE_ENTER);
  return true;
}

async function pageTextContains(driver, needle, timeout = 45000) {
  try {
    await until(
      driver,
      `page text containing "${needle}"`,
      async () => visible(driver, byTextContains(needle)),
      timeout,
    );
    return true;
  } catch (_) {
    return false;
  }
}

async function run() {
  const driver = await remote({
    hostname: '127.0.0.1',
    port: 4723,
    logLevel: 'error',
    capabilities,
  });

  try {
    await until(driver, 'shell root rendered', () => visible(driver, byTag('androidShellRoot')), 60000);
    await shot(driver, '01-launch');

    const started = await passOnboarding(driver);
    if (!started) {
      await shot(driver, '02-start-blocked');
      return;
    }
    await shot(driver, '02-home');

    if (await openUrl(driver, 'https://example.com')) {
      const loaded = await pageTextContains(driver, 'Example Domain');
      await shot(driver, '03-navigation');
      record(
        'NAV-load',
        loaded ? 'PASS' : 'FAIL',
        loaded
          ? 'https://example.com rendered its real page content on the device'
          : 'example.com did not render expected page content within 45s',
      );
    } else {
      record('NAV-load', 'FAIL', 'could not reach the URL entry field from the home surface');
    }

    if (await openUrl(driver, 'https://www.iana.org/help/example-domains')) {
      const second = await pageTextContains(driver, 'Example', 45000);
      await driver.back();
      const returned = await pageTextContains(driver, 'Example Domain', 30000);
      await shot(driver, '04-history-back');
      record(
        'NAV-back',
        second && returned ? 'PASS' : 'FAIL',
        second
          ? `second document loaded; back navigation ${returned ? 'restored' : 'did NOT restore'} the first page`
          : 'second document never rendered, so back navigation could not be judged',
      );
    } else {
      record('NAV-back', 'FAIL', 'URL entry unavailable for the second navigation');
    }

    if (await openUrl(driver, 'https://expired.badssl.com')) {
      const blocked = !(await pageTextContains(driver, 'expired.badssl.com', 20000));
      await shot(driver, '05-tls');
      record(
        'TLS-block',
        blocked ? 'PASS' : 'FAIL',
        blocked
          ? 'expired-certificate site did not render its page content'
          : 'expired-certificate site rendered content, indicating the interstitial did not hold',
      );
    } else {
      record('TLS-block', 'FAIL', 'URL entry unavailable for the TLS check');
    }

    if (await openUrl(driver, `https://example.com/?${RUN_TAG}`)) {
      await pageTextContains(driver, 'Example Domain', 45000);
      await shot(driver, '06-tagged-tab');
      await driver.execute('mobile: terminateApp', { appId: APP_PACKAGE });
      await driver.execute('mobile: activateApp', { appId: APP_PACKAGE });
      await until(driver, 'shell root after restart', () => visible(driver, byTag('androidShellRoot')), 60000);
      const restored = await pageTextContains(driver, 'Example Domain', 30000);
      await shot(driver, '07-after-restart');
      record(
        'REC-restart',
        restored ? 'PASS' : 'FAIL',
        restored
          ? 'the previously open page was restored after a full app restart'
          : 'the previously open page was not restored after a full app restart',
      );
    } else {
      record('REC-restart', 'FAIL', 'URL entry unavailable for the restart-recovery check');
    }
  } finally {
    fs.mkdirSync(EVIDENCE_DIR, { recursive: true });
    fs.writeFileSync(
      path.join(EVIDENCE_DIR, 'results.json'),
      JSON.stringify({ device: UDID, runTag: RUN_TAG, results }, null, 2),
    );
    await driver.deleteSession().catch(() => {});
  }
}

run().then(
  () => {
    const failures = results.filter((r) => r.status !== 'PASS');
    console.log(`\nRESULT ${results.length - failures.length}/${results.length} scenarios passed`);
    process.exit(failures.length === 0 ? 0 : 1);
  },
  (err) => {
    console.error('RUN ERROR', err && err.message ? err.message : err);
    process.exit(2);
  },
);
