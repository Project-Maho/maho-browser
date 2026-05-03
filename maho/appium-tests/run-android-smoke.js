const { remote } = require('webdriverio');
const fs = require('fs');
const path = require('path');
const assert = require('assert');

const SCREENSHOT_DIR = path.join(__dirname, 'screenshots');
const APP_PACKAGE = 'dev.maho.browser';
const APP_ACTIVITY = '.MainActivity';

function resourceId(tag) {
  return `android=new UiSelector().resourceIdMatches(".*${tag}")`;
}

const capabilities = {
  platformName: 'Android',
  'appium:automationName': 'UiAutomator2',
  'appium:deviceName': process.env.ANDROID_DEVICE_NAME || 'Android Emulator',
  'appium:appPackage': APP_PACKAGE,
  'appium:appActivity': APP_ACTIVITY,
  'appium:appWaitActivity': `${APP_PACKAGE}.*`,
  'appium:noReset': false,
  'appium:fullReset': false,
  'appium:autoGrantPermissions': true,
  'appium:enableMultiWindows': true,
  'appium:newCommandTimeout': 120,
};

if (process.env.ANDROID_PLATFORM_VERSION) {
  capabilities['appium:platformVersion'] = process.env.ANDROID_PLATFORM_VERSION;
}

const tests = [];
let passed = 0;
let failed = 0;

function test(name, fn) {
  tests.push({ name, fn });
}

async function firstExisting(driver, selectors) {
  for (const selector of selectors) {
    try {
      const el = await driver.$(selector);
      if (el && await el.isExisting()) return el;
    } catch (_) {}
  }
  return null;
}

async function firstVisible(driver, selectors) {
  for (const selector of selectors) {
    try {
      const el = await driver.$(selector);
      if (el && await el.isExisting() && await el.isDisplayed()) return el;
    } catch (_) {}
  }
  return null;
}

async function tapAt(driver, x, y) {
  await driver.performActions([{
    type: 'pointer',
    id: 'finger1',
    parameters: { pointerType: 'touch' },
    actions: [
      { type: 'pointerMove', duration: 0, x, y },
      { type: 'pointerDown', button: 0 },
      { type: 'pause', duration: 70 },
      { type: 'pointerUp', button: 0 },
    ],
  }]);
  await driver.releaseActions();
  await driver.pause(300);
}

async function tapFallbackMenuButton(driver) {
  const { width, height } = await driver.getWindowRect();
  await tapAt(driver, Math.round(width * 0.89), Math.round(height * 0.93));
}

async function tapElementCenter(driver, element) {
  const location = await element.getLocation();
  const size = await element.getSize();
  await tapAt(
    driver,
    Math.round(location.x + size.width / 2),
    Math.round(location.y + size.height / 2),
  );
}

async function assertVisible(driver, selectors, context) {
  const el = await firstVisible(driver, selectors);
  assert.ok(el, context);
  return el;
}

async function waitForVisible(driver, selectors, context, timeoutMs = 6000, intervalMs = 250) {
  const startedAt = Date.now();

  while (Date.now() - startedAt < timeoutMs) {
    const el = await firstVisible(driver, selectors);
    if (el) return el;
    await driver.pause(intervalMs);
  }

  try {
    const source = await driver.getPageSource();
    const interesting = source
      .split(/\n/)
      .filter((line) => /Browse for Me|Settings|tabSwitcherShell|tabGrid|Tabs|More|Private|Display|General/.test(line))
      .slice(0, 40)
      .join('\n');
    if (interesting) {
      console.log(`  ℹ️  Source snapshot for "${context}":\n${interesting}`);
    }
  } catch (_) {}

  assert.fail(context);
}

async function waitForHidden(driver, selectors, context, timeoutMs = 6000, intervalMs = 250) {
  const startedAt = Date.now();

  while (Date.now() - startedAt < timeoutMs) {
    let visible = false;

    for (const selector of selectors) {
      try {
        const el = await driver.$(selector);
        if (el && await el.isExisting() && await el.isDisplayed()) {
          visible = true;
          break;
        }
      } catch (_) {}
    }

    if (!visible) return true;
    await driver.pause(intervalMs);
  }

  try {
    const source = await driver.getPageSource();
    const interesting = source
      .split(/\n/)
      .filter((line) => /Browse for Me|Settings|tabSwitcherShell|tabGrid|Tabs|More|Private|Display|General/.test(line))
      .slice(0, 40)
      .join('\n');
    if (interesting) {
      console.log(`  ℹ️  Source snapshot for "${context}":\n${interesting}`);
    }
  } catch (_) {}

  assert.fail(context);
}

async function getElementStateDescription(element) {
  for (const attributeName of ['stateDescription', 'contentDescription', 'content-desc', 'description']) {
    try {
      const value = await element.getAttribute(attributeName);
      if (value) return value;
    } catch (_) {}
  }

  return '';
}

async function ensureOnboardingCleared(driver) {
  const skip = await firstVisible(driver, [
    'android=new UiSelector().text("Skip")',
    'android=new UiSelector().textContains("Skip")',
  ]);

  if (skip) {
    await skip.click();
    await driver.pause(1500);
  }
}

async function ensureHomeSurface(driver) {
  await ensureOnboardingCleared(driver);
  const homeSurface = await firstVisible(driver, [
    resourceId('mainBrowserHomeSurface'),
    resourceId('homeSearchView'),
    resourceId('homeSearchBar'),
    resourceId('homeSearchField'),
    'android=new UiSelector().className("android.widget.EditText")',
    'android=new UiSelector().textContains("Search or enter URL")',
  ]);
  assert.ok(homeSurface, 'Home search surface should be visible');
  return homeSurface;
}

async function ensureBottomChrome(driver) {
  const bottomChrome = await firstVisible(driver, [
    resourceId('mainBrowserBottomBar'),
    resourceId('arcBottomBarContainer'),
    resourceId('arcBottomBarTabsButton'),
    'android=new UiSelector().description("Tabs")',
    'android=new UiSelector().description("Back")',
  ]);
  assert.ok(bottomChrome, 'Bottom chrome should be visible');
  return bottomChrome;
}

async function ensureBrowsingState(driver) {
  const homeSurface = await firstVisible(driver, [
    resourceId('mainBrowserHomeSurface'),
    resourceId('homeSearchView'),
    resourceId('homeSearchBar'),
    resourceId('homeSearchField'),
    'android=new UiSelector().className("android.widget.EditText")',
    'android=new UiSelector().textContains("Search or enter URL")',
  ]);

  if (homeSurface) {
    await goToBrowsingState(driver);
    return;
  }

  const browsingSurface = await firstVisible(driver, [
    resourceId('mainBrowserBrowsingSurface'),
    resourceId('mainBrowserShell'),
    'android=new UiSelector().className("android.webkit.WebView")',
  ]);

  if (browsingSurface) {
    await ensureBottomChrome(driver);
    return;
  }

  await dismissOverlay(driver);

  const homeAfterDismiss = await firstVisible(driver, [
    resourceId('mainBrowserHomeSurface'),
    resourceId('homeSearchView'),
    resourceId('homeSearchBar'),
    resourceId('homeSearchField'),
    'android=new UiSelector().className("android.widget.EditText")',
    'android=new UiSelector().textContains("Search or enter URL")',
  ]);

  if (homeAfterDismiss) {
    await goToBrowsingState(driver);
    return;
  }

  await ensureBottomChrome(driver);
}

async function submitHomeSearch(driver, value) {
  const searchField = await assertVisible(driver, [
    resourceId('homeSearchField'),
    resourceId('homeSearchBar'),
    resourceId('homeSearchView'),
    'android=new UiSelector().className("android.widget.EditText")',
    'android=new UiSelector().textContains("Search or enter URL")',
  ], 'Home search field should be visible');

  await searchField.click();
  await searchField.clearValue();
  await searchField.setValue(value);
  await driver.pause(300);

  const submitButton = await firstVisible(driver, [
    resourceId('homeSearchSubmitButton'),
    'android=new UiSelector().description("Submit search")',
  ]);

  if (submitButton) {
    await submitButton.click();
  } else {
    try {
      await driver.execute('mobile: performEditorAction', { action: 'search' });
    } catch (_) {
      try {
        await driver.execute('mobile: pressKey', [{ keycode: 66 }]);
      } catch (_) {
        try { await driver.keys(['Enter']); } catch (_) {}
      }
    }
  }

  await driver.pause(5000);
}

async function openMenu(driver) {
  const alreadyOpen = await firstVisible(driver, [
    'android=new UiSelector().text("Settings")',
    'android=new UiSelector().textContains("Browse for Me")',
  ]);

  if (alreadyOpen) {
    return;
  }

  const moreButton = await waitForVisible(driver, [
    resourceId('arcBottomBarMoreButton'),
  ], 'More button should be visible');

  await moreButton.click();

  await waitForVisible(driver, [
    'android=new UiSelector().text("Settings")',
    'android=new UiSelector().textContains("Browse for Me")',
  ], 'Menu should expose settings entry', 15000);
}

async function dismissOverlay(driver) {
  const dismissButton = await firstVisible(driver, [
    'android=new UiSelector().description("Close")',
    'android=new UiSelector().text("Done")',
    'android=new UiSelector().text("Back")',
    'android=new UiSelector().description("Back")',
  ]);

  if (dismissButton) {
    await dismissButton.click();
    await driver.pause(800);
    return true;
  }

  try {
    await driver.back();
    await driver.pause(800);
    return true;
  } catch (_) {
    return false;
  }
}

async function goToBrowsingState(driver) {
  await ensureHomeSurface(driver);
  await submitHomeSearch(driver, 'example.com');

  const browsingSurface = await firstVisible(driver, [
    resourceId('mainBrowserBrowsingSurface'),
    resourceId('mainBrowserShell'),
    'android=new UiSelector().className("android.webkit.WebView")',
  ]);

  assert.ok(browsingSurface, 'Browsing surface should appear after search submit');
  await ensureBottomChrome(driver);
}

async function openSettings(driver) {
  await openMenu(driver);

  const settingsItem = await waitForVisible(driver, [
    'android=new UiSelector().text("Settings")',
    'android=new UiSelector().textContains("Settings")',
  ], 'Settings menu item should be visible');

  await settingsItem.click();

  await waitForVisible(driver, [
    'android=new UiSelector().text("Settings")',
    'android=new UiSelector().text("General")',
  ], 'Settings screen should be visible');
}

async function openTabGrid(driver) {
  await ensureBrowsingState(driver);
  await ensureBottomChrome(driver);
  await driver.pause(800);

  const tabsButton = await waitForVisible(driver, [
    resourceId('arcBottomBarTabsButton'),
  ], 'Tabs button should be visible');

  await tapElementCenter(driver, tabsButton);
  await driver.pause(1200);

  let tabGridSurface = await firstVisible(driver, [
    resourceId('tabSwitcherShell'),
    resourceId('tabGrid'),
    resourceId('tabGridEmptyState'),
    resourceId('tabSwitcherTitle'),
    'android=new UiSelector().text("Current space")',
    'android=new UiSelector().textContains("0 tabs")',
    'android=new UiSelector().text("No tabs yet")',
    'android=new UiSelector().text("No private tabs")',
    'android=new UiSelector().textContains("tab switcher")',
    'android=new UiSelector().textContains("tab grid")',
    'android=new UiSelector().textContains("No tabs yet")',
    'android=new UiSelector().textContains("No private tabs")',
  ]);

  if (!tabGridSurface) {
    await dismissOverlay(driver);
    await ensureBrowsingState(driver);
    await ensureBottomChrome(driver);
    await driver.pause(800);

    const refreshedTabsButton = await waitForVisible(driver, [
      resourceId('arcBottomBarTabsButton'),
    ], 'Tabs button should be visible for retry');
    await tapElementCenter(driver, refreshedTabsButton);
    await driver.pause(1200);
    tabGridSurface = await waitForVisible(driver, [
      resourceId('tabSwitcherShell'),
      resourceId('tabGrid'),
      resourceId('tabGridEmptyState'),
      resourceId('tabSwitcherTitle'),
      'android=new UiSelector().text("Current space")',
      'android=new UiSelector().textContains("0 tabs")',
      'android=new UiSelector().text("No tabs yet")',
      'android=new UiSelector().text("No private tabs")',
      'android=new UiSelector().textContains("tab switcher")',
      'android=new UiSelector().textContains("tab grid")',
      'android=new UiSelector().textContains("No tabs yet")',
      'android=new UiSelector().textContains("No private tabs")',
    ], 'Tab grid should be visible', 15000);
  }

  assert.ok(tabGridSurface, 'Tab grid should be visible');
  return tabGridSurface;
}

async function openBrowseForMeSummary(driver) {
  await ensureBrowsingState(driver);

  await openMenu(driver);

  const browseForMeItem = await waitForVisible(driver, [
    'android=new UiSelector().text("Browse for Me")',
    'android=new UiSelector().textContains("Browse for Me")',
  ], 'Browse for Me menu item should be visible');

  await browseForMeItem.click();

  const summarySurface = await waitForVisible(driver, [
    resourceId('summaryShellScrim'),
    resourceId('summaryShellOverlay'),
    resourceId('summaryShellPhase'),
    'android=new UiSelector().textContains("Summary")',
    'android=new UiSelector().textContains("Browse for Me")',
  ], 'Browse for Me / summary surface should be visible', 15000);

  return true;
}

async function runTests() {
  if (!fs.existsSync(SCREENSHOT_DIR)) fs.mkdirSync(SCREENSHOT_DIR, { recursive: true });

  console.log('Connecting to Appium at http://127.0.0.1:4723...');
  let driver;
  try {
    driver = await remote({
      hostname: '127.0.0.1',
      port: 4723,
      path: '/',
      capabilities,
      logLevel: 'warn',
      connectionRetryTimeout: 120000,
      connectionRetryCount: 5,
    });
  } catch (err) {
    console.error('Failed to connect to Appium:', err.message);
    process.exit(1);
  }

  console.log(`\nConnected. Session: ${driver.sessionId}`);
  console.log(`Running ${tests.length} tests...\n`);

  for (const t of tests) {
    try {
      await t.fn(driver);
      console.log(`  ✅ ${t.name}`);
      passed++;
    } catch (err) {
      console.log(`  ❌ ${t.name}`);
      console.log(`     ${err.message}`);
      failed++;
    }
  }

  console.log(`\n${passed} passed, ${failed} failed, ${tests.length} total\n`);

  try { await driver.deleteSession(); } catch (_) {}
  process.exit(failed > 0 ? 1 : 0);
}

test('App launches successfully', async (driver) => {
  await driver.pause(3000);
  const source = await driver.getPageSource();
  assert.ok(source.length > 100, `Page source too short (${source.length} chars)`);
});

test('Home: visible after launch', async (driver) => {
  await ensureHomeSurface(driver);
});

test('Home: bottom chrome is visible', async (driver) => {
  await ensureBottomChrome(driver);
});

test('Search: submit query and reach browsing state', async (driver) => {
  await goToBrowsingState(driver);
});

test('Browsing: bottom chrome stays visible', async (driver) => {
  await ensureBottomChrome(driver);
});

test('Menu: open and expose settings entry', async (driver) => {
  await openMenu(driver);
  await assertVisible(driver, [
    'android=new UiSelector().text("Settings")',
    'android=new UiSelector().textContains("Browse for Me")',
  ], 'Menu should expose settings and Browse for Me items');
});

test('Settings: open from menu', async (driver) => {
  await openSettings(driver);
  await dismissOverlay(driver);
});

test('Tab grid: new tab button returns to browsing shell', async (driver) => {
  try {
    const tabGridSurface = await openTabGrid(driver);
    assert.ok(tabGridSurface, 'Tab grid should be visible');

    await assertVisible(driver, [
      'android=new UiSelector().text("Tab overview")',
      'android=new UiSelector().textContains("Tab overview")',
      'android=new UiSelector().text("New")',
      'android=new UiSelector().text("Done")',
      'android=new UiSelector().descriptionContains("New tab")',
      resourceId('tabGridToolbar'),
    ], 'Tab grid toolbar should be visible');

    const newTabButton = await waitForVisible(driver, [
      resourceId('tabGridNewTabButton'),
      'android=new UiSelector().descriptionContains("New tab")',
      'android=new UiSelector().textContains("New tab")',
    ], 'New tab button should be visible');

    await tapElementCenter(driver, newTabButton);

    await waitForHidden(driver, [
      resourceId('tabSwitcherShell'),
      resourceId('tabGrid'),
      resourceId('tabGridEmptyState'),
      'android=new UiSelector().text("Tab overview")',
      'android=new UiSelector().text("New")',
      'android=new UiSelector().text("Done")',
      'android=new UiSelector().descriptionContains("New tab")',
      resourceId('tabGridToolbar'),
    ], 'Tab grid should dismiss after opening a new tab', 15000);

    await waitForVisible(driver, [
      resourceId('mainBrowserBrowsingSurface'),
      resourceId('mainBrowserShell'),
      'android=new UiSelector().className("android.webkit.WebView")',
    ], 'Browsing surface should be visible after opening a new tab', 15000);

    await assertVisible(driver, [
      resourceId('mainBrowserShell'),
    ], 'Main browser shell should exist');

    await ensureBottomChrome(driver);
  } finally {
    await dismissOverlay(driver);
    await ensureBrowsingState(driver);
  }
});

test('Browse for Me: open summary surface when available', async (driver) => {
  try {
    const opened = await openBrowseForMeSummary(driver);
    if (opened) await dismissOverlay(driver);
  } finally {
    await ensureBrowsingState(driver);
  }
});

runTests();
