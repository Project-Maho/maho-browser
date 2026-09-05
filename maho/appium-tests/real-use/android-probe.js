const { remote } = require('webdriverio');
const fs = require('fs');
const path = require('path');

const UDID = process.env.ANDROID_UDID || 'R3CN8126R4Y';
const APP_PACKAGE = 'dev.maho.browser';
const EVIDENCE_DIR = path.join(__dirname, '..', '..', '..', '.omo', 'evidence', 'real-use-android');

async function shot(driver, name) {
  fs.mkdirSync(EVIDENCE_DIR, { recursive: true });
  fs.writeFileSync(path.join(EVIDENCE_DIR, `${name}.png`), Buffer.from(await driver.takeScreenshot(), 'base64'));
}

async function describe(driver, label) {
  const source = await driver.getPageSource();
  const ids = [...new Set([...source.matchAll(/resource-id="([^"]+)"/g)].map((m) => m[1]))];
  const texts = [...new Set([...source.matchAll(/text="([^"]+)"/g)].map((m) => m[1]))].slice(0, 14);
  const descs = [...new Set([...source.matchAll(/content-desc="([^"]+)"/g)].map((m) => m[1]))].slice(0, 14);
  console.log(`\n== ${label}\n   ids: ${ids.join(', ')}\n   texts: ${texts.join(' | ')}\n   descs: ${descs.join(' | ')}`);
  return { ids, texts, descs };
}

async function tapText(driver, text) {
  const el = await driver.$(`android=new UiSelector().textContains("${text}")`);
  if (!(await el.isExisting())) return false;
  await el.click();
  await driver.pause(2500);
  return true;
}

async function run() {
  const driver = await remote({
    hostname: '127.0.0.1',
    port: 4723,
    logLevel: 'error',
    capabilities: {
      platformName: 'Android',
      'appium:automationName': 'UiAutomator2',
      'appium:udid': UDID,
      'appium:appPackage': APP_PACKAGE,
      'appium:appActivity': '.MainActivity',
      'appium:noReset': true,
      'appium:fullReset': false,
      'appium:autoGrantPermissions': false,
      'appium:newCommandTimeout': 300,
    },
  });

  try {
    await describe(driver, 'initial');

    if (await tapText(driver, 'example.com')) {
      await describe(driver, 'after opening the example.com tab');
      await shot(driver, 'p5-opened-tab');
    } else if (await tapText(driver, 'Done')) {
      await describe(driver, 'after Done');
      await shot(driver, 'p5-after-done');
    }

    const webviews = await driver.$$('android.webkit.WebView');
    console.log(`   WebView count: ${webviews.length}`);
    const source = await driver.getPageSource();
    console.log(`   page mentions Example Domain: ${source.includes('Example Domain')}`);
  } finally {
    await driver.deleteSession().catch(() => {});
  }
}

run().catch((err) => {
  console.error('PROBE ERROR', err && err.message ? err.message : err);
  process.exit(2);
});
