const { remote } = require('webdriverio');
const fs = require('fs');
const path = require('path');
const assert = require('assert');

const SCREENSHOT_DIR = path.join(__dirname, 'screenshots');
const APP_BUNDLE_ID = 'dev.maho.browser';

const driverOptions = {
  hostname: '127.0.0.1',
  port: 4723,
  path: '/',
  capabilities: {
    platformName: 'iOS',
    'appium:automationName': 'XCUITest',
    'appium:deviceName': 'iPhone 17 Pro',
    'appium:platformVersion': '26.4',
    'appium:udid': '9E69A245-2BA0-4DB2-AB89-2D667ECCEC4B',
    'appium:bundleId': 'dev.maho.browser',
    'appium:noReset': false,
    'appium:fullReset': false,
    'appium:isSimulator': true,
  },
  connectionRetryTimeout: 120000,
  connectionRetryCount: 3,
  logLevel: 'warn',
};

const MENU_BUTTON_SELECTORS = [
  '//XCUIElementTypeButton[@label="Page menu"]',
  '//XCUIElementTypeButton[@name="Page menu"]',
  '~arcMenuButton',
];

const MENU_ITEM_SELECTORS = {
  private: [
    '//XCUIElementTypeButton[@label="Private"]',
    '//XCUIElementTypeButton[@name="Private"]',
    '//XCUIElementTypeStaticText[@name="Private"]',
    '//XCUIElementTypeButton[@label="Private Mode"]',
  ],
  browseForMe: [
    '//XCUIElementTypeButton[@name="Browse for Me"]',
    '//XCUIElementTypeButton[@label="Browse for Me"]',
    '//XCUIElementTypeStaticText[@name="Browse for Me"]',
  ],
  settings: [
    '//XCUIElementTypeButton[@label="Settings"]',
    '//XCUIElementTypeButton[@name="Settings"]',
    '//XCUIElementTypeStaticText[@name="Settings"]',
  ],
};

const HOME_SEARCH_SELECTORS = [
  '~homeSearchField',
  '~homeSearchBar',
  '~homeSearchSubmitButton',
  '//XCUIElementTypeTextField',
  '~arcAddressPill',
];

const HOME_DISMISS_BUTTON_SELECTORS = [
  '//XCUIElementTypeButton[@label="Done"]',
  '//XCUIElementTypeButton[@name="Done"]',
  '//XCUIElementTypeButton[@label="Close"]',
  '//XCUIElementTypeButton[@name="Close"]',
  '//XCUIElementTypeButton[@label="Back"]',
  '//XCUIElementTypeButton[@name="Back"]',
  '//XCUIElementTypeButton[@name="BackButton"]',
  '~arcBackButton',
  '//XCUIElementTypeNavigationBar//XCUIElementTypeButton[1]',
];

async function firstExisting(driver, selectors) {
  for (const selector of selectors) {
    const el = await driver.$(selector);
    if (el && await el.isExisting()) {
      return el;
    }
  }
  return null;
}

async function firstVisible(driver, selectors) {
  for (const selector of selectors) {
    const el = await driver.$(selector);
    if (el && await el.isExisting() && await el.isDisplayed()) {
      return el;
    }
  }
  return null;
}

async function assertAppAlive(driver, context) {
  const app = await driver.$('//XCUIElementTypeApplication[@name="Maho"]');
  assert.ok(await app.isExisting(), `${context}: app should remain alive`);
}

async function ensureHomeSurface(driver, context) {
  const searchField = await recoverToHomeSurface(driver, context);
  assert.ok(searchField, `${context}: home search surface should be visible`);
}

async function tapAwayFromOverlays(driver) {
  const { width, height } = await driver.getWindowRect();
  await driver.performActions([{
    type: 'pointer',
    id: 'finger1',
    parameters: { pointerType: 'touch' },
    actions: [
      { type: 'pointerMove', duration: 0, x: Math.round(width / 2), y: Math.round(height / 3) },
      { type: 'pointerDown', button: 0 },
      { type: 'pause', duration: 50 },
      { type: 'pointerUp', button: 0 },
    ],
  }]);
  await driver.releaseActions();
  await driver.pause(300);
}

async function swipeBackFromLeftEdge(driver) {
  const { height } = await driver.getWindowRect();
  await driver.performActions([{
    type: 'pointer',
    id: 'finger1',
    parameters: { pointerType: 'touch' },
    actions: [
      { type: 'pointerMove', duration: 0, x: 5, y: Math.round(height / 2) },
      { type: 'pointerDown', button: 0 },
      { type: 'pause', duration: 50 },
      { type: 'pointerMove', duration: 300, x: 220, y: Math.round(height / 2) },
      { type: 'pointerUp', button: 0 },
    ],
  }]);
  await driver.releaseActions();
  await driver.pause(400);
}

async function dismissVisibleControl(driver) {
  const button = await firstVisible(driver, HOME_DISMISS_BUTTON_SELECTORS);
  if (button) {
    await button.click();
    await driver.pause(400);
    return true;
  }

  return false;
}

async function recoverToHomeSurface(driver, context) {
  for (let attempt = 0; attempt < 6; attempt++) {
    const searchField = await firstVisible(driver, HOME_SEARCH_SELECTORS);
    if (searchField) {
      return searchField;
    }

    const dismissed = await dismissVisibleControl(driver);
    if (dismissed) {
      continue;
    }

    try {
      await driver.back();
      await driver.pause(400);
      continue;
    } catch (_) {
      // Fall through to gestures when the driver back action is unavailable.
    }

    await tapAwayFromOverlays(driver);
    await swipeBackFromLeftEdge(driver);
  }

  await driver.terminateApp(APP_BUNDLE_ID);
  await driver.pause(1000);
  await driver.activateApp(APP_BUNDLE_ID);
  await driver.pause(2500);

  const skipButton = await firstVisible(driver, ['//XCUIElementTypeButton[@name="Skip"]']);
  if (skipButton) {
    await skipButton.click();
    await driver.pause(1800);
  }

  const searchField = await firstVisible(driver, HOME_SEARCH_SELECTORS);
  assert.ok(searchField, `${context}: home search surface should be visible`);
  return searchField;
}

async function submitFromHome(driver, value) {
  const searchField = await recoverToHomeSurface(driver, 'submitFromHome');
  assert.ok(searchField, 'Home search surface not found');
  await searchField.click();
  await driver.pause(200);
  await searchField.clearValue();
  if (value.length > 0) {
    await searchField.setValue(value);
  }

  const submitButton = await firstExisting(driver, ['~homeSearchSubmitButton']);
  if (submitButton && await submitButton.isExisting()) {
    await submitButton.click();
  } else {
    await searchField.setValue('\n');
  }

  await driver.pause(2500);
}

async function openMenu(driver) {
  const menuButton = await firstExisting(driver, MENU_BUTTON_SELECTORS);
  assert.ok(menuButton, 'Menu button not found');
  await menuButton.click();
  await driver.pause(800);
}

async function closeMenu(driver) {
  if (await dismissVisibleControl(driver)) {
    return;
  }

  await tapAwayFromOverlays(driver);
  await swipeBackFromLeftEdge(driver);
}

async function togglePrivateMode(driver) {
  await openMenu(driver);
  const privateBtn = await firstExisting(driver, MENU_ITEM_SELECTORS.private);
  assert.ok(privateBtn, 'Private mode item not found');
  await privateBtn.click();
  await driver.pause(900);
}

(async () => {
  const driver = await remote(driverOptions);

  let passed = 0;
  let failed = 0;

  async function test(name, fn) {
    try {
      await fn();
      console.log(`  ✅ ${name}`);
      passed++;
    } catch (e) {
      console.log(`  ❌ ${name}: ${e.message}`);
      failed++;
    }
  }

  try {
    await driver.pause(2500);

    console.log('\n=== Edge Case Tests ===\n');

    await test('Home screen renders without crash on fresh launch', async () => {
      await ensureHomeSurface(driver, 'Fresh launch');
      await assertAppAlive(driver, 'Fresh launch');
    });

    await test('Empty query submits without crashing', async () => {
      await submitFromHome(driver, '');
      await assertAppAlive(driver, 'Empty query submit');
      await ensureHomeSurface(driver, 'Empty query submit');
    });

    await test('Malformed URL input does not crash', async () => {
      await submitFromHome(driver, 'http://%zz');
      await assertAppAlive(driver, 'Malformed URL input');
    });

    await test('Handoff-style invalid URL text does not crash', async () => {
      await submitFromHome(driver, 'javascript:alert(1)');
      await assertAppAlive(driver, 'Invalid URL text');
    });

    await test('Very long URL input does not crash', async () => {
      await submitFromHome(driver, 'https://example.com/' + 'a'.repeat(200));
      await assertAppAlive(driver, 'Long URL input');
    });

    await test('Repeated menu open and dismiss keeps the app stable', async () => {
      for (let i = 0; i < 3; i++) {
        await openMenu(driver);
        const browseForMe = await firstExisting(driver, MENU_ITEM_SELECTORS.browseForMe);
        assert.ok(browseForMe, `Browse for Me should stay available on menu open ${i + 1}`);
        await closeMenu(driver);
        await assertAppAlive(driver, `Menu cycle ${i + 1}`);
      }
    });

    await test('Browse for Me on blank state shows graceful handling', async () => {
      await openMenu(driver);
      const browseForMe = await firstExisting(driver, MENU_ITEM_SELECTORS.browseForMe);
      assert.ok(browseForMe, 'Browse for Me button not found');
      await browseForMe.click();
      await driver.pause(1800);
      await assertAppAlive(driver, 'Browse for Me on blank state');

      if (!fs.existsSync(SCREENSHOT_DIR)) {
        fs.mkdirSync(SCREENSHOT_DIR, { recursive: true });
      }
      const screenshot = await driver.takeScreenshot();
      fs.writeFileSync(path.join(SCREENSHOT_DIR, 'edge-browse-for-me-blank.png'), screenshot, 'base64');
    });

    await test('Repeated incognito toggles stay stable', async () => {
      for (let i = 0; i < 3; i++) {
        await togglePrivateMode(driver);
        await assertAppAlive(driver, `Incognito on cycle ${i + 1}`);
        await togglePrivateMode(driver);
        await assertAppAlive(driver, `Incognito off cycle ${i + 1}`);
      }
    });

    await test('Navigate back to home after edge cases', async () => {
      await recoverToHomeSurface(driver, 'Return to home');
      await assertAppAlive(driver, 'Return to home');
    });

    console.log(`\n=== Results: ${passed} passed, ${failed} failed ===\n`);
  } finally {
    await driver.deleteSession();
  }
})();
