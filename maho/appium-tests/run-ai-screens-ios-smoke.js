/**
 * AI Screens Smoke Test — iOS (XCUITest)
 *
 * Tests 12 AI screens from the W2-W8 feature set:
 * 1. BYOK Settings (Bring Your Own Key)
 * 2. Chat View (AI chat with streaming)
 * 3. Conversation List (saved chat sessions)
 * 4. Camera Capture (visual search camera)
 * 5. Visual Search (image-based search results)
 * 6. Space AI Config (per-space AI settings)
 * 7. Offline Model (local LLM download/manage)
 * 8. Privacy Dashboard (data/AI privacy controls)
 * 9. Agent (cloud-agent task chat)
 *
 * Navigation patterns on iOS (post-wiring):
 * - Settings sheet: menuButton → "Settings" → SettingsRootView
 * - MaxSettingsView: Settings → "AI Settings" section → "Max AI"
 * - BYOK settings: Settings → "AI" section → "API Keys" → shared web-ai #byok
 * - SpaceAIConfigView: Settings → "AI Settings" section → "Space AI Configuration" (greyed without context)
 * - OfflineModelView: Settings → "AI Settings" section → "Offline Model"
 * - AgentView: Settings → "AI Settings" section → "Web Agent"
 * - ChatView: Settings → "AI Screens" section → "AI Chat (Test)" (sheet)
 * - ConversationListView: Settings → "AI Screens" section → "Saved Conversations" (sheet)
 * - VisualSearchView: Settings → "AI Screens" section → "Visual Search" (fullScreenCover)
 * - CameraCaptureView: Settings → "AI Screens" section → "Camera Capture" (fullScreenCover)
 * - PrivacySettingsView: Settings → "Privacy & Security"
 */

const { remote } = require('webdriverio');
const fs = require('fs');
const path = require('path');
const assert = require('assert');
const { execSync } = require('child_process');

const SCREENSHOT_DIR = path.join(__dirname, 'screenshots');
const BUNDLE_ID = 'dev.maho.browser';

// Auto-detect booted simulator UDID or fall back to env/config
function getSimulatorUdid() {
  if (process.env.IOS_UDID) return process.env.IOS_UDID;
  try {
    const output = execSync('xcrun simctl list devices booted -j', { encoding: 'utf8' });
    const data = JSON.parse(output);
    for (const runtime of Object.values(data.devices)) {
      for (const device of runtime) {
        if (device.state === 'Booted') return device.udid;
      }
    }
  } catch (_) {}
  // Fallback from wdio.conf.js
  return '9E69A245-2BA0-4DB2-AB89-2D667ECCEC4B';
}

// Use bundleId for already-installed apps; fall back to APP_PATH if explicitly set
const APP_PATH = process.env.IOS_APP_PATH || null;

const capabilities = {
  platformName: 'iOS',
  'appium:automationName': 'XCUITest',
  'appium:deviceName': process.env.IOS_DEVICE_NAME || 'iPhone 17 Pro Max',
  'appium:platformVersion': process.env.IOS_PLATFORM_VERSION || '26.5',
  'appium:udid': getSimulatorUdid(),
  'appium:noReset': true,
  'appium:fullReset': false,
  'appium:isSimulator': true,
  'appium:newCommandTimeout': 120,
  'appium:autoAcceptAlerts': true,
  ...(APP_PATH
    ? { 'appium:app': APP_PATH }
    : { 'appium:bundleId': BUNDLE_ID }),
};

const IS_IOS_SIMULATOR = process.env.IOS_SIM === '1'
  || (process.env.IOS_SIM !== '0' && capabilities['appium:isSimulator'] === true);

// ─── Helpers ─────────────────────────────────────────────────────────────────

const tests = [];
let passed = 0;
let failed = 0;
let skipped = 0;

function skipTest(reason) {
  return { skipped: true, reason };
}

function test(name, fn) {
  tests.push({ name, fn, skip: false });
}

function testSkip(name, reason) {
  tests.push({ name, fn: null, skip: true, reason });
}

async function firstExisting(driver, selectors) {
  for (const selector of selectors) {
    try {
      const el = await driver.$(selector);
      if (!el || !(await el.isExisting())) continue;

      try {
        if (!(await el.isDisplayed())) continue;
      } catch (_) {
        const visible = await el.getAttribute?.('visible');
        if (visible && `${visible}` !== 'true') continue;
      }

      return el;
    } catch (_) {}
  }
  return null;
}

async function waitForFirstExisting(driver, selectors, timeoutMs = 8000, intervalMs = 300) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const el = await firstExisting(driver, selectors);
    if (el) return el;
    await driver.pause(intervalMs);
  }
  return null;
}

async function waitForAnyLabel(driver, labels, timeoutMs = 8000) {
  const selectors = labels.flatMap((label) => [
    `~${label}`,
    `//*[@name="${label}"]`,
    `//*[@label="${label}"]`,
    `//*[contains(@name, "${label}")]`,
    `//*[contains(@label, "${label}")]`,
  ]);
  return waitForFirstExisting(driver, selectors, timeoutMs);
}

function selectorsForLabels(labels) {
  return labels.flatMap((label) => [
    `~${label}`,
    `//*[@name="${label}"]`,
    `//*[@label="${label}"]`,
    `//*[contains(@name, "${label}")]`,
    `//*[contains(@label, "${label}")]`,
    `-ios predicate string:name == "${label}"`,
    `-ios predicate string:label == "${label}"`,
    `-ios predicate string:name CONTAINS "${label}"`,
    `-ios predicate string:label CONTAINS "${label}"`,
  ]);
}

async function tapAt(driver, x, y) {
  await driver.performActions([{
    type: 'pointer',
    id: 'finger1',
    parameters: { pointerType: 'touch' },
    actions: [
      { type: 'pointerMove', duration: 0, x, y },
      { type: 'pointerDown', button: 0 },
      { type: 'pause', duration: 50 },
      { type: 'pointerUp', button: 0 },
    ],
  }]);
  await driver.releaseActions();
  await driver.pause(300);
}

async function tapElement(driver, element, retrySelectors = []) {
  if (!element) return;
  try {
    await element.click();
    return;
  } catch (_) {}

  let fallbackElement = element;
  if (retrySelectors.length > 0) {
    fallbackElement = await waitForFirstExisting(driver, retrySelectors, 1200) || element;
    try {
      await fallbackElement.click();
      return;
    } catch (_) {}
  }

  const elementId = fallbackElement.elementId
    || fallbackElement.ELEMENT
    || fallbackElement['element-6066-11e4-a52e-4f735466cecf'];
  assert.ok(elementId, 'Element is missing a webdriver element id for tap fallback');

  const rect = typeof driver.getElementRect === 'function'
    ? await driver.getElementRect(elementId)
    : {
        ...(await fallbackElement.getLocation()),
        ...(await fallbackElement.getSize()),
      };

  await tapAt(driver, Math.round(rect.x + rect.width / 2), Math.round(rect.y + rect.height / 2));
}

async function tapElementFresh(driver, selectors, description = selectors[0]) {
  let element = await waitForFirstExisting(driver, selectors, 1500);
  assert.ok(element, `Element not found: ${description}`);

  const getFreshRect = async () => {
    const currentId = element.elementId
      || element.ELEMENT
      || element['element-6066-11e4-a52e-4f735466cecf'];
    assert.ok(currentId, `Element is missing a webdriver element id for ${description}`);

    try {
      if (typeof driver.getElementRect === 'function') {
        return await driver.getElementRect(currentId);
      }
      return {
        ...(await element.getLocation()),
        ...(await element.getSize()),
      };
    } catch (error) {
      if (!/stale|expired|getRect|internal cache|not present/i.test(error.message)) {
        throw error;
      }

      element = await waitForFirstExisting(driver, selectors, 1500);
      assert.ok(element, `Element vanished after refind: ${description}`);

      const refreshedId = element.elementId
        || element.ELEMENT
        || element['element-6066-11e4-a52e-4f735466cecf'];
      assert.ok(refreshedId, `Refound element is missing a webdriver element id for ${description}`);

      if (typeof driver.getElementRect === 'function') {
        return await driver.getElementRect(refreshedId);
      }

      return {
        ...(await element.getLocation()),
        ...(await element.getSize()),
      };
    }
  };

  const rect = await getFreshRect();
  await tapAt(driver, Math.round(rect.x + rect.width / 2), Math.round(rect.y + rect.height / 2));
  await driver.pause(400);
}

async function queryForegroundState(driver) {
  if (typeof driver.queryAppState !== 'function') return 4;

  try {
    return await driver.queryAppState(BUNDLE_ID);
  } catch (_) {
    return 4;
  }
}

async function openLabeledScreen(driver, rowLabels, screenSelectors, options = {}) {
  const {
    maxAttempts = 2,
    scrollAttempts = 5,
    postTapPauseMs = 1500,
    alternateSuccessLabels = [],
    missingRowMessage = `${rowLabels[0]} button should be visible`,
    missingScreenMessage = `${screenSelectors[0]} should be visible`,
  } = options;

  const rowSelectors = selectorsForLabels(rowLabels);

  for (let attempt = 0; attempt < maxAttempts; attempt++) {
    const row = await scrollToLabel(driver, rowLabels, scrollAttempts);
    assert.ok(row, missingRowMessage);
    await tapElementFresh(driver, rowSelectors, rowLabels[0]);
    await driver.pause(postTapPauseMs);

    const screen = await waitForFirstExisting(driver, screenSelectors, 4000);
    if (screen) return screen;

    if (alternateSuccessLabels.length > 0) {
      const alternate = await waitForAnyLabel(driver, alternateSuccessLabels, 4000);
      if (alternate) return alternate;
    }
  }

  assert.fail(missingScreenMessage);
}

async function skipOnboardingIfPresent(driver) {
  const skipBtn = await waitForFirstExisting(driver, [
    '//XCUIElementTypeButton[@name="Skip"]',
  ], 1500);
  if (skipBtn) {
    await tapElement(driver, skipBtn);
    await driver.pause(1200);
  }
}

const HOME_SURFACE_SELECTORS = [
  '~homeSearchField',
  '~homeSearchBar',
  '//XCUIElementTypeTextField',
];

async function ensureAppRunning(driver) {
  try {
    await driver.activateApp(BUNDLE_ID);
    await driver.pause(800);
  } catch (_) {}
}

async function dismissAllSheets(driver) {
  for (let i = 0; i < 5; i++) {
    const dismissButton = await firstExisting(driver, [
      ...selectorsForLabels(['Done', 'Close', 'Cancel', 'Chats']),
      '//*[@label="Close visual search"]',
      '//XCUIElementTypeNavigationBar//XCUIElementTypeButton[1]',
    ]);

    if (!dismissButton) break;

    try {
      await tapElement(driver, dismissButton);
    } catch (_) {
      break;
    }

    await driver.pause(300);
  }
}

async function ensureHomeSurface(driver) {
  await ensureAppRunning(driver);
  await skipOnboardingIfPresent(driver);
  let homeField = await waitForFirstExisting(driver, HOME_SURFACE_SELECTORS, 2500);
  if (!homeField) {
    await returnToHome(driver);
    homeField = await waitForFirstExisting(driver, HOME_SURFACE_SELECTORS, 5000);
  }
  assert.ok(homeField, 'App should be on the home search surface');
  return homeField;
}

async function relaunchApp(driver) {
  await driver.terminateApp(BUNDLE_ID);
  await driver.pause(800);
  await driver.activateApp(BUNDLE_ID);
  await driver.pause(2200);
  await skipOnboardingIfPresent(driver);
}

const PAGE_MENU_BUTTON_SELECTORS = [
  '~menuButton',
  '//XCUIElementTypeButton[@name="menuButton"]',
  '//XCUIElementTypeButton[@label="Page menu"]',
  '//XCUIElementTypeButton[@name="Page menu"]',
  '~arcMenuButton',
];

async function openPageMenu(driver) {
  for (let attempt = 0; attempt < 3; attempt++) {
    const pageMenuButton = await waitForFirstExisting(driver, PAGE_MENU_BUTTON_SELECTORS, 3000);
    if (pageMenuButton) {
      await tapElementFresh(driver, PAGE_MENU_BUTTON_SELECTORS, 'Page menu button');
    } else {
      const { width, height } = await driver.getWindowRect();
      await tapAt(driver, Math.round(width * 0.92), Math.round(height * 0.95));
    }

    await driver.pause(350);
    let menuSignal = await waitForAnyLabel(driver, ['Settings', 'Private', 'Browse for Me', 'Cancel'], 2500);
    if (menuSignal) return menuSignal;

    const { width, height } = await driver.getWindowRect();
    await tapAt(driver, Math.round(width * 0.92), Math.round(height * 0.95));
    await driver.pause(350);
    menuSignal = await waitForAnyLabel(driver, ['Settings', 'Private', 'Browse for Me', 'Cancel'], 2500);
    if (menuSignal) return menuSignal;

    await dismissAllSheets(driver);
    await ensureHomeSurface(driver);
  }
  return false;
}

async function openSettings(driver) {
  let lastError = new Error('Settings button in menu');

  for (let attempt = 1; attempt <= 3; attempt++) {
    try {
      const state = await queryForegroundState(driver);
      if (state !== 4) {
        await driver.activateApp(BUNDLE_ID);
        await driver.pause(800);
      }

      await dismissAllSheets(driver);
      await ensureHomeSurface(driver);

      const menuOpened = await openPageMenu(driver);
      if (!menuOpened) throw new Error('Page menu should become visible');

      const settingsSelectors = selectorsForLabels(['Settings']);
      const settingsBtn = await waitForFirstExisting(driver, settingsSelectors, 5000);
      if (!settingsBtn) throw new Error('Settings button in menu');

      await tapElementFresh(driver, settingsSelectors, 'Settings button in menu');
      await driver.pause(800);

      const settingsScreen = await waitForAnyLabel(driver, ['General', 'AI Settings', 'Privacy', 'Max AI', 'Appearance'], 5000);
      if (settingsScreen) return;

      throw new Error('Settings screen did not open');
    } catch (error) {
      lastError = error;
      console.log(`  Attempt ${attempt} failed: ${error.message}`);
      if (attempt === 3) break;

      try {
        await driver.terminateApp(BUNDLE_ID);
      } catch (_) {}
      await driver.pause(500);
      await ensureAppRunning(driver);
      await driver.pause(1200);
      await skipOnboardingIfPresent(driver);
    }
  }

  throw new Error(`openSettingsRobust: failed after 3 attempts (${lastError.message})`);
}

async function dismissSettings(driver) {
  const doneButton = await waitForFirstExisting(driver, [
    '//XCUIElementTypeButton[@name="Done"]',
    `//*[@label="Done"]`,
  ], 2000);
  if (doneButton) {
    await tapElement(driver, doneButton);
    await driver.pause(800);
  }
}

async function dismissOverlay(driver) {
  // Try Done, Close, Back, then swipe down
  const dismiss = await firstExisting(driver, [
    '//XCUIElementTypeButton[@name="Chats"]',
    '//XCUIElementTypeButton[@name="Done"]',
    '//XCUIElementTypeButton[@name="Cancel"]',
    '//XCUIElementTypeButton[@name="Close"]',
    '//*[@label="Close visual search"]',
    '//XCUIElementTypeButton[@name="Back"]',
    '//XCUIElementTypeNavigationBar//XCUIElementTypeButton[1]',
  ]);
  if (dismiss) {
    await tapElement(driver, dismiss);
    await driver.pause(800);
    return true;
  }
  // Swipe down to dismiss sheet
  const { width, height } = await driver.getWindowRect();
  await driver.performActions([{
    type: 'pointer',
    id: 'finger1',
    parameters: { pointerType: 'touch' },
    actions: [
      { type: 'pointerMove', duration: 0, x: Math.round(width / 2), y: 20 },
      { type: 'pointerDown', button: 0 },
      { type: 'pause', duration: 100 },
      { type: 'pointerMove', duration: 300, x: Math.round(width / 2), y: Math.round(height * 0.8) },
      { type: 'pointerUp', button: 0 },
    ],
  }]);
  await driver.releaseActions();
  await driver.pause(800);
  return true;
}

async function returnToHome(driver) {
  await ensureAppRunning(driver);
  await skipOnboardingIfPresent(driver);
  for (let i = 0; i < 5; i++) {
    const home = await firstExisting(driver, HOME_SURFACE_SELECTORS);
    if (home) return;
    try {
      await dismissOverlay(driver);
    } catch (_) {
      await relaunchApp(driver);
    }
  }
  // Final fallback: relaunch
  await relaunchApp(driver);
  const home = await waitForFirstExisting(driver, HOME_SURFACE_SELECTORS, 5000);
  assert.ok(home, 'App should return to the home search surface after recovery');
}

async function scrollDown(driver) {
  const { width, height } = await driver.getWindowRect();
  await driver.performActions([{
    type: 'pointer',
    id: 'finger1',
    parameters: { pointerType: 'touch' },
    actions: [
      { type: 'pointerMove', duration: 0, x: Math.round(width / 2), y: Math.round(height * 0.7) },
      { type: 'pointerDown', button: 0 },
      { type: 'pause', duration: 100 },
      { type: 'pointerMove', duration: 300, x: Math.round(width / 2), y: Math.round(height * 0.3) },
      { type: 'pointerUp', button: 0 },
    ],
  }]);
  await driver.releaseActions();
  await driver.pause(500);
}

async function scrollToLabel(driver, labels, maxScrolls = 4) {
  for (let i = 0; i < maxScrolls; i++) {
    const el = await waitForAnyLabel(driver, labels, 2000);
    if (el) return el;
    await scrollDown(driver);
  }
  return await waitForAnyLabel(driver, labels, 2000);
}

async function saveScreenshot(driver, name) {
  if (!fs.existsSync(SCREENSHOT_DIR)) fs.mkdirSync(SCREENSHOT_DIR, { recursive: true });
  const filePath = path.join(SCREENSHOT_DIR, `ai-ios-${name}.png`);
  await driver.saveScreenshot(filePath);
  return filePath;
}

// ─── Test Definitions ────────────────────────────────────────────────────────

test('App launches successfully', async (driver) => {
  await driver.pause(3000);
  const source = await driver.getPageSource();
  assert.ok(source.length > 100, `Page source too short (${source.length} chars)`);
});

test('Home: visible after launch', async (driver) => {
  await ensureHomeSurface(driver);
});

test('AI: Chat View — open from AI Screens section', async (driver) => {
  try {
    await ensureHomeSurface(driver);
    await openSettings(driver);

    await openLabeledScreen(
      driver,
      ['AI Chat (Test)', 'AI Chat'],
      [
        '~aiScreenChat',
        '//*[@name="aiScreenChat"]',
      ],
      {
        postTapPauseMs: 2000,
        alternateSuccessLabels: ['Chats'],
        missingRowMessage: '"AI Chat (Test)" button should be visible in AI Screens section',
        missingScreenMessage: 'ChatView should render (aiScreenChat identifier)',
      },
    );
    await saveScreenshot(driver, 'chat-view');
  } finally {
    await dismissOverlay(driver);
    await dismissSettings(driver);
    await returnToHome(driver);
  }
});

test('AI: Conversation List — open from AI Screens section', async (driver) => {
  try {
    await ensureHomeSurface(driver);
    await openSettings(driver);

    await openLabeledScreen(
      driver,
      ['Saved Conversations'],
      [
      '~aiScreenConversationList',
      '//*[@name="aiScreenConversationList"]',
      ],
      {
        postTapPauseMs: 2000,
        alternateSuccessLabels: ['Chats', 'New Chat'],
        missingRowMessage: '"Saved Conversations" button should be visible',
        missingScreenMessage: 'ConversationListView should render (aiScreenConversationList identifier)',
      },
    );
    await saveScreenshot(driver, 'conversation-list');
  } finally {
    await dismissOverlay(driver);
    await dismissSettings(driver);
    await returnToHome(driver);
  }
});

test('AI: Camera Capture — open from AI Screens section', async (driver) => {
  if (IS_IOS_SIMULATOR) {
    return skipTest('Camera Capture is an expected skip on iOS Simulator (no reliable camera hardware)');
  }

  try {
    await ensureHomeSurface(driver);
    await openSettings(driver);

    const camBtn = await scrollToLabel(driver, ['Camera Capture'], 5);
    assert.ok(camBtn, '"Camera Capture" button should be visible');
    await tapElement(driver, camBtn);
    await driver.pause(2000);

    // Verify CameraCaptureView rendered
    const camScreen = await waitForFirstExisting(driver, [
      '~aiScreenCameraCapture',
      '//*[@name="aiScreenCameraCapture"]',
    ], 8000);
    assert.ok(camScreen, 'CameraCaptureView should render (aiScreenCameraCapture identifier)');
    await saveScreenshot(driver, 'camera-capture');
  } finally {
    await dismissOverlay(driver);
    await dismissSettings(driver);
    await returnToHome(driver);
  }
});

test('AI: Visual Search — open from AI Screens section', async (driver) => {
  if (IS_IOS_SIMULATOR) {
    return skipTest('Visual Search is an expected skip on iOS Simulator because it enters camera-backed capture flow');
  }

  try {
    await ensureHomeSurface(driver);
    await openSettings(driver);

    const vsBtn = await scrollToLabel(driver, ['Visual Search'], 5);
    assert.ok(vsBtn, '"Visual Search" button should be visible');
    await tapElement(driver, vsBtn);
    await driver.pause(2000);

    // Verify VisualSearchView rendered
    const vsScreen = await waitForFirstExisting(driver, [
      '~aiScreenVisualSearch',
      '//*[@name="aiScreenVisualSearch"]',
    ], 8000);
    assert.ok(vsScreen, 'VisualSearchView should render (aiScreenVisualSearch identifier)');
    await saveScreenshot(driver, 'visual-search');
  } finally {
    await dismissOverlay(driver);
    await dismissSettings(driver);
    await returnToHome(driver);
  }
});

test('AI: Max AI Settings (AI hub) — open from Settings', async (driver) => {
  try {
    await ensureHomeSurface(driver);
    await openSettings(driver);

    // Navigate to Max AI
    const maxAI = await waitForAnyLabel(driver, ['Max AI'], 5000);
    assert.ok(maxAI, '"Max AI" settings row should be visible');
    await tapElementFresh(driver, selectorsForLabels(['Max AI']), 'Max AI settings row');
    await driver.pause(1500);

    // Verify MaxSettingsView rendered — look for any AI feature toggle
    const maxScreen = await waitForAnyLabel(driver, [
      'Page Previews', 'Tidy Tab Titles', 'Tidy Downloads',
      'AI Command Bar', 'Instant Links', 'Tidy Tabs',
      'Max AI', 'AI Features',
    ], 8000);
    assert.ok(maxScreen, 'Max AI Settings screen should render with feature toggles');
    await saveScreenshot(driver, 'max-ai-settings');
  } finally {
    await dismissOverlay(driver);
    await dismissSettings(driver);
    await returnToHome(driver);
  }
});

test('AI: Offline Model — open from Settings', async (driver) => {
  try {
    await ensureHomeSurface(driver);
    await openSettings(driver);

    // Navigate to Offline Model
    const offlineModel = await waitForAnyLabel(driver, ['Offline Model'], 5000);
    assert.ok(offlineModel, '"Offline Model" settings row should be visible');
    await tapElementFresh(driver, selectorsForLabels(['Offline Model']), 'Offline Model settings row');
    await driver.pause(1000);

    // Verify OfflineModelView rendered
    const modelScreen = await waitForAnyLabel(driver, [
      'Gemma', 'Download', 'Local Model', 'Offline',
      'gemma', 'download', 'model',
    ], 5000);
    assert.ok(modelScreen, 'Offline Model screen rendered');
    await saveScreenshot(driver, 'offline-model');
  } finally {
    await dismissOverlay(driver);
    await dismissSettings(driver);
    await returnToHome(driver);
  }
});

test('AI: Privacy Settings — open from Settings', async (driver) => {
  try {
    await ensureHomeSurface(driver);
    await openSettings(driver);

    // Navigate to Privacy & Security
    const privacy = await waitForAnyLabel(driver, ['Privacy & Security', 'Privacy'], 5000);
    assert.ok(privacy, '"Privacy & Security" settings row should be visible');
    await tapElementFresh(driver, selectorsForLabels(['Privacy & Security', 'Privacy']), 'Privacy settings row');
    await driver.pause(1000);

    // Verify PrivacySettingsView rendered
    const privacyScreen = await waitForAnyLabel(driver, [
      'Do Not Track', 'Safe Browsing', 'Content Blocker',
      'Block Third-Party Cookies', 'Clear Browsing Data',
    ], 5000);
    assert.ok(privacyScreen, 'Privacy Settings screen rendered');
    await saveScreenshot(driver, 'privacy-settings');
  } finally {
    await dismissOverlay(driver);
    await dismissSettings(driver);
    await returnToHome(driver);
  }
});

test('AI: BYOK Settings — open from AI Settings section', async (driver) => {
  try {
    await ensureHomeSurface(driver);
    await openSettings(driver);

    const byokBtn = await scrollToLabel(driver, ['API Keys (BYOK)', 'API Keys'], 5);
    assert.ok(byokBtn, '"API Keys (BYOK)" row should be visible in AI Settings section');
    await tapElementFresh(driver, selectorsForLabels(['API Keys (BYOK)', 'API Keys']), 'BYOK settings row');
    await driver.pause(1500);

    const byokScreen = await waitForFirstExisting(driver, [
      '~aiScreenBYOK',
      '//*[@name="aiScreenBYOK"]',
    ], 8000);
    assert.ok(byokScreen, 'Shared web-ai BYOK screen should render (aiScreenBYOK identifier)');
    await saveScreenshot(driver, 'byok-settings');
  } finally {
    await dismissOverlay(driver);
    await dismissSettings(driver);
    await returnToHome(driver);
  }
});

test('AI: Space AI Config — verify row visible (greyed without context)', async (driver) => {
  try {
    await ensureHomeSurface(driver);
    await openSettings(driver);

    const spaceLabel = await scrollToLabel(driver, ['Space AI Configuration'], 5);
    assert.ok(spaceLabel, '"Space AI Configuration" label should be visible in AI Settings section');
    await saveScreenshot(driver, 'space-ai-config-row');
  } finally {
    await dismissSettings(driver);
    await returnToHome(driver);
  }
});

test('AI: Agent — open Web Agent from AI Settings section', async (driver) => {
  try {
    await ensureHomeSurface(driver);
    await openSettings(driver);

    await openLabeledScreen(
      driver,
      ['Web Agent'],
      [
      '~aiScreenAgent',
      '//*[@name="aiScreenAgent"]',
      ],
      {
        postTapPauseMs: 2000,
        alternateSuccessLabels: ['Run Agent'],
        missingRowMessage: '"Web Agent" row should be visible in AI Settings section',
        missingScreenMessage: 'AgentView should render (aiScreenAgent identifier)',
      },
    );
    await saveScreenshot(driver, 'agent-view');
  } finally {
    await dismissOverlay(driver);
    await dismissSettings(driver);
    await returnToHome(driver);
  }
});

// ─── Runner ──────────────────────────────────────────────────────────────────

async function runTests() {
  if (!fs.existsSync(SCREENSHOT_DIR)) fs.mkdirSync(SCREENSHOT_DIR, { recursive: true });

  console.log('🍎 AI Screens Smoke Test — iOS');
  console.log('═══════════════════════════════════════════════════════');
  const APPIUM_PORT = parseInt(process.env.APPIUM_PORT || '4723', 10);
  console.log(`Connecting to Appium at http://127.0.0.1:${APPIUM_PORT}...`);

  let driver;
  try {
    driver = await remote({
      hostname: '127.0.0.1',
      port: APPIUM_PORT,
      path: '/',
      capabilities,
      logLevel: 'warn',
      connectionRetryTimeout: 120000,
      connectionRetryCount: 5,
    });
  } catch (err) {
    console.error('❌ Failed to connect to Appium:', err.message);
    console.log('\nTo run these tests:');
    console.log('  1. Start Appium: APPIUM_PORT=4725 bunx appium --port 4725');
    console.log('  2. Boot simulator: xcrun simctl boot <UDID>');
    console.log('  3. Ensure app installed on simulator');
    process.exit(1);
  }

  console.log(`Connected. Session: ${driver.sessionId}`);
  console.log(`Running ${tests.length} tests...\n`);

  for (const t of tests) {
    if (t.skip) {
      console.log(`  ⏭️  ${t.name}`);
      console.log(`     Reason: ${t.reason}`);
      skipped++;
      continue;
    }
    try {
      const result = await t.fn(driver);
      if (result?.skipped) {
        console.log(`  ⏭️  ${t.name}`);
        console.log(`     Reason: ${result.reason}`);
        skipped++;
        continue;
      }
      console.log(`  ✅ ${t.name}`);
      passed++;
    } catch (err) {
      console.log(`  ❌ ${t.name}`);
      console.log(`     ${err.message}`);
      failed++;
      try {
        await saveScreenshot(driver, `FAIL-${t.name.replace(/[^a-zA-Z0-9]/g, '-').slice(0, 40)}`);
      } catch (_) {}
    }
  }

  console.log('\n═══════════════════════════════════════════════════════');
  console.log(`Results: ${passed} passed, ${failed} failed, ${skipped} skipped (${tests.length} total)`);
  console.log('═══════════════════════════════════════════════════════\n');

  try { await driver.deleteSession(); } catch (_) {}
  process.exit(failed > 0 ? 1 : 0);
}

runTests();
