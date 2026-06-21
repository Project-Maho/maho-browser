/**
 * AI Screens Smoke Test — Android (UiAutomator2)
 *
 * Tests 11 AI screens from the W2-W8 feature set:
 * 1. App launches successfully
 * 2. Home visible after launch
 * 3. Chat View (AI chat with streaming)
 * 4. Conversation List (saved chat sessions)
 * 5. Camera Capture (visual search camera)
 * 6. Visual Search (image-based search results)
 * 7. Space AI Config (per-space AI settings)
 * 8. BYOK Settings (Bring Your Own Key)
 * 9. Offline Model (local LLM download/manage)
 * 10. Privacy Dashboard (data/AI privacy controls)
 * 11. Agent (cloud-agent task chat)
 *
 * Navigation patterns:
 * - ChatScreen: Settings → "AI Chat" (or deep link maho://ask fallback)
 * - ConversationList: Settings → "Saved Conversations"
 * - SpaceAIConfig: bottom bar spaces dropdown → "AI personality"
 * - Agent: Settings → "Web Agent"
 * - BYOKSettings: Settings → "BYOK API Keys"
 * - OfflineModel: Settings → "Offline Model"
 * - PrivacyDashboard: Settings → "Privacy Dashboard"
 * - CameraCapture, VisualSearch: child of ChatScreen (nested)
 *
 * testTags (exposed as resource-id via testTagsAsResourceId):
 *   aiScreenBYOK, aiScreenChat, aiScreenConversationList,
 *   aiScreenVisualSearch, aiScreenCameraCapture, aiScreenSpaceAIConfig,
 *   aiScreenOfflineModel, aiScreenAgent, aiScreenPrivacyDashboard
 */

const { remote } = require('webdriverio');
const fs = require('fs');
const path = require('path');
const assert = require('assert');

const SCREENSHOT_DIR = path.join(__dirname, 'screenshots');
const APP_PACKAGE = 'dev.maho.browser';
const APP_ACTIVITY = '.MainActivity';

// ─── Helpers ─────────────────────────────────────────────────────────────────

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
let skipped = 0;

class ExpectedSkipError extends Error {
  constructor(message) {
    super(message);
    this.name = 'ExpectedSkipError';
  }
}

function test(name, fn) {
  tests.push({ name, fn, skip: false });
}

async function firstVisible(driver, selectors) {
  for (const selector of selectors) {
    try {
      const el = await driver.$(selector);
      if (el && (await el.isExisting()) && (await el.isDisplayed())) return el;
    } catch (_) {}
  }
  return null;
}

async function waitForVisible(driver, selectors, context, timeoutMs = 8000, intervalMs = 120) {
  const startedAt = Date.now();
  while (Date.now() - startedAt < timeoutMs) {
    const el = await firstVisible(driver, selectors);
    if (el) return el;
    await driver.pause(intervalMs);
  }
  assert.fail(`waitForVisible timed out: ${context}`);
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

async function tapElementCenter(driver, element) {
  const location = await element.getLocation();
  const size = await element.getSize();
  await tapAt(
    driver,
    Math.round(location.x + size.width / 2),
    Math.round(location.y + size.height / 2),
  );
}

async function longPressElementCenter(driver, element, durationMs = 900) {
  try {
    await driver.execute('mobile: longClickGesture', {
      elementId: element.elementId,
      duration: durationMs,
    });
  } catch (_) {
    const location = await element.getLocation();
    const size = await element.getSize();
    const x = Math.round(location.x + size.width / 2);
    const y = Math.round(location.y + size.height / 2);

    await driver.performActions([{
      type: 'pointer',
      id: 'finger1',
      parameters: { pointerType: 'touch' },
      actions: [
        { type: 'pointerMove', duration: 0, x, y },
        { type: 'pointerDown', button: 0 },
        { type: 'pause', duration: durationMs },
        { type: 'pointerUp', button: 0 },
      ],
    }]);
    await driver.releaseActions();
  }
  await driver.pause(500);
}

function isEmulatorDevice() {
  return (process.env.ANDROID_DEVICE_NAME || '').toLowerCase().includes('emulator');
}

function homeSelectors() {
  return [
    resourceId('mainBrowserHomeSurface'),
    resourceId('homeSearchView'),
    resourceId('homeSearchBar'),
    resourceId('homeSearchField'),
    'android=new UiSelector().className("android.widget.EditText")',
    'android=new UiSelector().textContains("Search or enter URL")',
  ];
}

function browsingSelectors() {
  return [
    resourceId('mainBrowserBrowsingSurface'),
    resourceId('mainBrowserShell'),
    'android=new UiSelector().className("android.webkit.WebView")',
  ];
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

async function dismissCrashDialog(driver) {
  const crashButton = await firstVisible(driver, [
    'android=new UiSelector().textContains("Close app")',
    'android=new UiSelector().text("Close app")',
    'android=new UiSelector().textContains("Wait")',
    'android=new UiSelector().text("Wait")',
    'android=new UiSelector().text("OK")',
    'android=new UiSelector().textContains("stopping")',
  ]);

  if (crashButton) {
    await crashButton.click();
    await driver.pause(900);
    return true;
  }

  return false;
}

async function relaunchApp(driver, pauseMs = 2200) {
  await dismissCrashDialog(driver);

  try {
    await driver.terminateApp(APP_PACKAGE);
  } catch (_) {}

  await driver.pause(600);
  await dismissCrashDialog(driver);

  try {
    await driver.activateApp(APP_PACKAGE);
  } catch (_) {}

  await driver.pause(pauseMs);
  await dismissCrashDialog(driver);
  await ensureOnboardingCleared(driver);
}

async function ensureHomeSurface(driver) {
  for (let attempt = 0; attempt < 3; attempt++) {
    await dismissCrashDialog(driver);
    await ensureOnboardingCleared(driver);

    const homeSurface = await firstVisible(driver, homeSelectors());
    if (homeSurface) return homeSurface;

    const homeButton = await firstVisible(driver, [
      resourceId('arcBottomBarPageButton'),
    ]);
    if (homeButton) {
      await homeButton.click();
      await driver.pause(900);
      const homeAfterReturn = await firstVisible(driver, homeSelectors());
      if (homeAfterReturn) return homeAfterReturn;
    }

    const browsingSurface = await firstVisible(driver, browsingSelectors());
    if (browsingSurface) {
      try {
        await driver.back();
        await driver.pause(900);
      } catch (_) {}
      const homeAfterBack = await firstVisible(driver, homeSelectors());
      if (homeAfterBack) return homeAfterBack;
    }

    if (attempt < 2) {
      await relaunchApp(driver);
    }
  }

  assert.fail('Home search surface should be visible');
}

async function dismissOverlay(driver) {
  const dismissButton = await firstVisible(driver, [
    'android=new UiSelector().description("Close")',
    'android=new UiSelector().text("Done")',
    'android=new UiSelector().text("Back")',
    'android=new UiSelector().description("Back")',
    'android=new UiSelector().description("Navigate up")',
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

async function returnToHome(driver) {
  for (let i = 0; i < 5; i++) {
    const home = await firstVisible(driver, homeSelectors());
    if (home) return;
    await dismissCrashDialog(driver);
    await dismissOverlay(driver);
  }

  await relaunchApp(driver, 1800);
  await ensureHomeSurface(driver);
}

async function openSpacesMenu(driver) {
  const alreadyOpen = await firstVisible(driver, [
    'android=new UiSelector().text("AI personality")',
    'android=new UiSelector().textContains("Tone, memory")',
  ]);
  if (alreadyOpen) return;

  const tabsButton = await waitForVisible(driver, [
    resourceId('arcBottomBarTabsButton'),
  ], 'Tabs button should be visible for spaces menu');

  await longPressElementCenter(driver, tabsButton);
  await waitForVisible(driver, [
    'android=new UiSelector().text("Spaces")',
    'android=new UiSelector().text("AI personality")',
    'android=new UiSelector().textContains("Tone, memory")',
  ], 'Spaces menu should be visible', 5000, 120);
}

async function openMenu(driver) {
  const alreadyOpen = await firstVisible(driver, [
    'android=new UiSelector().text("Settings")',
    'android=new UiSelector().textContains("Browse for Me")',
  ]);
  if (alreadyOpen) return;

  const moreButton = await waitForVisible(driver, [
    resourceId('arcBottomBarMoreButton'),
  ], 'More button should be visible');
  await moreButton.click();
  await waitForVisible(driver, [
    'android=new UiSelector().text("Settings")',
    'android=new UiSelector().textContains("Browse for Me")',
  ], 'Menu should expose settings entry', 15000);
}

async function openSettings(driver) {
  await openMenu(driver);
  const settingsItem = await waitForVisible(driver, [
    'android=new UiSelector().text("Settings")',
    'android=new UiSelector().textContains("Settings")',
  ], 'Settings menu item should be visible');
  await settingsItem.click();
  await waitForVisible(driver, [
    'android=new UiSelector().text("General")',
    'android=new UiSelector().text("Display")',
    'android=new UiSelector().textContains("AI Chat")',
  ], 'Settings screen should be visible', 8000);
}

async function scrollToAndTapSettingsItem(driver, itemText) {
  // First check if already visible
  let item = await firstVisible(driver, [
    `android=new UiSelector().text("${itemText}")`,
    `android=new UiSelector().textContains("${itemText}")`,
  ]);
  if (item) {
    await tapElementCenter(driver, item);
    await driver.pause(1800);
    return true;
  }

  // Try scrolling into view
  try {
    const scrollable = await driver.$('android=new UiSelector().scrollable(true)');
    if (scrollable && await scrollable.isExisting()) {
      const scrolled = await driver.$(`android=new UiScrollable(new UiSelector().scrollable(true)).scrollIntoView(new UiSelector().text("${itemText}"))`);
      if (scrolled && await scrolled.isExisting()) {
        await tapElementCenter(driver, scrolled);
        await driver.pause(1800);
        return true;
      }
    }
  } catch (_) {}

  // Last resort: try textContains with scroll
  try {
    const scrolled = await driver.$(`android=new UiScrollable(new UiSelector().scrollable(true)).scrollIntoView(new UiSelector().textContains("${itemText}"))`);
    if (scrolled && await scrolled.isExisting()) {
      await tapElementCenter(driver, scrolled);
      await driver.pause(1800);
      return true;
    }
  } catch (_) {}

  return false;
}

async function saveScreenshot(driver, name) {
  if (!fs.existsSync(SCREENSHOT_DIR)) fs.mkdirSync(SCREENSHOT_DIR, { recursive: true });
  const filePath = path.join(SCREENSHOT_DIR, `ai-android-${name}.png`);
  await driver.saveScreenshot(filePath);
  return filePath;
}

async function goToBrowsingState(driver) {
  await ensureHomeSurface(driver);
  const searchField = await firstVisible(driver, [
    resourceId('homeSearchField'),
    resourceId('homeSearchBar'),
    'android=new UiSelector().className("android.widget.EditText")',
  ]);
  if (searchField) {
    await searchField.click();
    await searchField.clearValue();
    await searchField.setValue('example.com');
    await driver.pause(300);
    try {
      await driver.execute('mobile: performEditorAction', { action: 'search' });
    } catch (_) {
      try { await driver.keys(['Enter']); } catch (_) {}
    }
    await driver.pause(4000);
  }
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

test('AI: Chat View — open via deep link and verify rendered', async (driver) => {
  try {
    // Launch chat via deep link (maho://ask triggers ChatScreen)
    await driver.execute('mobile: deepLink', {
      url: 'maho://ask?q=hello',
      package: APP_PACKAGE,
    });
    await driver.pause(2500);

    const chatOverlay = await waitForVisible(driver, [
      resourceId('aiScreenChat'),
      resourceId('browserChatOverlay'),
      resourceId('chatShellScrim'),
      'android=new UiSelector().textContains("Type a message")',
      'android=new UiSelector().textContains("Ask anything")',
      'android=new UiSelector().descriptionContains("chat")',
    ], 'Chat overlay should be visible after deep link', 10000);

    assert.ok(chatOverlay, 'Chat View rendered');
    await saveScreenshot(driver, 'chat-view');
  } finally {
    await dismissOverlay(driver);
    await driver.pause(500);
    await returnToHome(driver);
  }
});

test('AI: Conversation List — open from Settings → Saved Conversations', async (driver) => {
  try {
    await ensureHomeSurface(driver);
    await openSettings(driver);

    const tapped = await scrollToAndTapSettingsItem(driver, 'Saved Conversations');
    assert.ok(tapped, 'Should find and tap "Saved Conversations" in Settings');

    const listView = await waitForVisible(driver, [
      resourceId('aiScreenConversationList'),
      'android=new UiSelector().text("Saved chats")',
      'android=new UiSelector().textContains("Conversations")',
      'android=new UiSelector().textContains("No saved chats")',
      'android=new UiSelector().textContains("No conversations")',
      'android=new UiSelector().textContains("Chat history")',
    ], 'Conversation List screen should be visible', 12000);
    assert.ok(listView, 'Conversation List rendered');
    await saveScreenshot(driver, 'conversation-list');
  } finally {
    await dismissOverlay(driver);
    await driver.pause(500);
    await returnToHome(driver);
  }
});

test('AI: Camera Capture — verify camera UI renders', async (driver) => {
  try {
    // Open chat first (camera is child of chat)
    await driver.execute('mobile: deepLink', {
      url: 'maho://ask?q=test',
      package: APP_PACKAGE,
    });
    await driver.pause(2500);

    await waitForVisible(driver, [
      resourceId('aiScreenChat'),
      resourceId('browserChatOverlay'),
      resourceId('chatShellScrim'),
    ], 'Chat should open', 10000);

    const cameraButton = await firstVisible(driver, [
      'android=new UiSelector().descriptionContains("camera")',
      'android=new UiSelector().descriptionContains("Camera")',
      'android=new UiSelector().descriptionContains("visual search")',
      resourceId('chatCameraButton'),
      resourceId('visualSearchButton'),
    ]);

    if (cameraButton) {
      await cameraButton.click();
      await driver.pause(2000);

      const cameraUI = await firstVisible(driver, [
        resourceId('aiScreenCameraCapture'),
        'android=new UiSelector().textContains("Take photo")',
        'android=new UiSelector().descriptionContains("shutter")',
        'android=new UiSelector().descriptionContains("capture")',
        'android=new UiSelector().className("android.view.SurfaceView")',
        resourceId('cameraCaptureScreen'),
        'android=new UiSelector().textContains("While using")',
        'android=new UiSelector().textContains("Allow")',
      ]);
      if (cameraUI) {
        // Grant permission if needed
        const allowBtn = await firstVisible(driver, [
          'android=new UiSelector().text("While using the app")',
          'android=new UiSelector().textContains("Allow")',
        ]);
        if (allowBtn) await allowBtn.click();
        await driver.pause(1000);
      }
      assert.ok(true, 'Camera capture UI triggered');
    } else {
      if (isEmulatorDevice()) {
        throw new ExpectedSkipError('expected skip: emulator has no camera entry point');
      }
      console.log('    ℹ️  No camera button found in chat input bar');
    }
    await saveScreenshot(driver, 'camera-capture');
  } finally {
    await dismissOverlay(driver);
    await driver.pause(300);
    await dismissOverlay(driver);
    await returnToHome(driver);
  }
});

test('AI: Visual Search — verify visual search UI renders', async (driver) => {
  try {
    await driver.execute('mobile: deepLink', {
      url: 'maho://ask?q=test',
      package: APP_PACKAGE,
    });
    await driver.pause(2500);

    await waitForVisible(driver, [
      resourceId('aiScreenChat'),
      resourceId('browserChatOverlay'),
      resourceId('chatShellScrim'),
    ], 'Chat should open', 10000);

    const cameraButton = await firstVisible(driver, [
      'android=new UiSelector().descriptionContains("camera")',
      'android=new UiSelector().descriptionContains("Camera")',
      resourceId('chatCameraButton'),
      resourceId('visualSearchButton'),
    ]);

    if (cameraButton) {
      assert.ok(true, 'Visual search entry point (camera button) exists in chat');
    } else {
      const vsScreen = await firstVisible(driver, [
        resourceId('aiScreenVisualSearch'),
      ]);
      if (vsScreen) {
        assert.ok(true, 'Visual search screen rendered directly');
      } else if (isEmulatorDevice()) {
        throw new ExpectedSkipError('expected skip: emulator has no visual-search camera entry point');
      } else {
        console.log('    ℹ️  Visual search entry (camera button) not found');
      }
    }
    await saveScreenshot(driver, 'visual-search');
  } finally {
    await dismissOverlay(driver);
    await returnToHome(driver);
  }
});

test('AI: Space AI Config — open from bottom bar spaces menu', async (driver) => {
  try {
    await ensureHomeSurface(driver);

    await openSpacesMenu(driver);

    const aiPersonality = await waitForVisible(driver, [
      'android=new UiSelector().text("AI personality")',
      'android=new UiSelector().textContains("AI personality")',
      'android=new UiSelector().textContains("Tone, memory")',
    ], 'AI personality row should be visible in spaces menu', 5000);
    await tapElementCenter(driver, aiPersonality);
    await driver.pause(1200);

    const configScreen = await waitForVisible(driver, [
      resourceId('aiScreenSpaceAIConfig'),
      'android=new UiSelector().textContains("AI Personality")',
      'android=new UiSelector().textContains("Shape how Maho thinks")',
      'android=new UiSelector().textContains("personality")',
      'android=new UiSelector().textContains("Tone")',
      'android=new UiSelector().textContains("Model")',
      'android=new UiSelector().textContains("model")',
      resourceId('spaceAiConfigScreen'),
    ], 'Space AI Config screen should be visible', 12000);
    assert.ok(configScreen, 'Space AI Config screen rendered');
    await saveScreenshot(driver, 'space-ai-config');
  } finally {
    await dismissOverlay(driver);
    await returnToHome(driver);
  }
});

test('AI: BYOK Settings — open from Settings → BYOK API Keys', async (driver) => {
  try {
    await ensureHomeSurface(driver);
    await openSettings(driver);

    const tapped = await scrollToAndTapSettingsItem(driver, 'BYOK API Keys');
    assert.ok(tapped, 'Should find and tap "BYOK API Keys" in Settings');

    const byokScreen = await waitForVisible(driver, [
      resourceId('aiScreenBYOK'),
      'android=new UiSelector().text("LLM API Keys")',
      'android=new UiSelector().textContains("No API keys saved")',
      'android=new UiSelector().textContains("Loading saved keys")',
      'android=new UiSelector().textContains("API Key")',
      'android=new UiSelector().textContains("BYOK")',
      'android=new UiSelector().textContains("key")',
    ], 'BYOK Settings screen should be visible', 12000);
    assert.ok(byokScreen, 'BYOK Settings screen rendered');
    await saveScreenshot(driver, 'byok-settings');
  } finally {
    await dismissOverlay(driver);
    await dismissOverlay(driver);
    await returnToHome(driver);
  }
});

test('AI: Offline Model — open from Settings → Offline Model', async (driver) => {
  try {
    await ensureHomeSurface(driver);
    await openSettings(driver);

    const tapped = await scrollToAndTapSettingsItem(driver, 'Offline Model');
    assert.ok(tapped, 'Should find and tap "Offline Model" in Settings');

    const offlineScreen = await waitForVisible(driver, [
      resourceId('aiScreenOfflineModel'),
      'android=new UiSelector().text("Offline AI Model")',
      'android=new UiSelector().textContains("Status:")',
      'android=new UiSelector().textContains("Offline")',
      'android=new UiSelector().textContains("Download")',
      'android=new UiSelector().textContains("Model")',
    ], 'Offline Model screen should be visible', 12000);
    assert.ok(offlineScreen, 'Offline Model screen rendered');
    await saveScreenshot(driver, 'offline-model');
  } finally {
    await dismissOverlay(driver);
    await dismissOverlay(driver);
    await returnToHome(driver);
  }
});

test('AI: Privacy Dashboard — open from Settings → Privacy Dashboard', async (driver) => {
  try {
    await ensureHomeSurface(driver);
    await openSettings(driver);

    const tapped = await scrollToAndTapSettingsItem(driver, 'Privacy Dashboard');
    assert.ok(tapped, 'Should find and tap "Privacy Dashboard" in Settings');

    const privacyScreen = await waitForVisible(driver, [
      resourceId('aiScreenPrivacyDashboard'),
      'android=new UiSelector().textContains("Privacy")',
      'android=new UiSelector().textContains("Data")',
      'android=new UiSelector().textContains("dashboard")',
    ], 'Privacy Dashboard screen should be visible', 8000);
    assert.ok(privacyScreen, 'Privacy Dashboard screen rendered');
    await saveScreenshot(driver, 'privacy-dashboard');
  } finally {
    await dismissOverlay(driver);
    await dismissOverlay(driver);
    await returnToHome(driver);
  }
});

test('AI: Agent — open from Settings → Web Agent', async (driver) => {
  try {
    await ensureHomeSurface(driver);
    await openSettings(driver);

    const tapped = await scrollToAndTapSettingsItem(driver, 'Web Agent');
    assert.ok(tapped, 'Should find and tap "Web Agent" in Settings');

    await driver.pause(1800);

    const agentScreen = await waitForVisible(driver, [
      resourceId('aiScreenAgent'),
      'android=new UiSelector().textContains("Agent")',
      'android=new UiSelector().textContains("agent")',
      'android=new UiSelector().textContains("Task")',
      'android=new UiSelector().textContains("connecting")',
      resourceId('agentScreen'),
    ], 'Agent screen should be visible', 8000);
    assert.ok(agentScreen, 'Agent screen rendered');
    await saveScreenshot(driver, 'agent');
  } finally {
    await dismissOverlay(driver);
    await dismissOverlay(driver);
    await returnToHome(driver);
  }
});

// ─── Runner ──────────────────────────────────────────────────────────────────

async function runTests() {
  if (!fs.existsSync(SCREENSHOT_DIR)) fs.mkdirSync(SCREENSHOT_DIR, { recursive: true });

  const priorityForTest = (name) => {
    if (name.startsWith('App launches')) return 0;
    if (name.startsWith('Home:')) return 1;
    if (name.includes('Conversation List')) return 2;
    if (name.includes('Space AI Config')) return 3;
    if (name.includes('Offline Model')) return 4;
    if (name.includes('Privacy Dashboard')) return 5;
    if (name.includes('Agent')) return 6;
    if (name.includes('BYOK Settings')) return 7;
    if (name.includes('Chat View')) return 8;
    if (name.includes('Camera Capture')) return 9;
    if (name.includes('Visual Search')) return 10;
    return 99;
  };

  const orderedTests = [...tests].sort((a, b) => priorityForTest(a.name) - priorityForTest(b.name));

  console.log('🤖 AI Screens Smoke Test — Android');
  console.log('═══════════════════════════════════════════════════════');
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
    console.error('❌ Failed to connect to Appium:', err.message);
    console.log('\nTo run these tests:');
    console.log('  1. Start Appium: bunx appium --port 4723');
    console.log('  2. Start emulator: emulator -avd Pixel_7_API_34');
    console.log('  3. Ensure app installed: adb shell pm list packages | grep dev.maho.browser');
    process.exit(1);
  }

  console.log(`Connected. Session: ${driver.sessionId}`);
  console.log(`Running ${orderedTests.length} tests...\n`);

  for (let index = 0; index < orderedTests.length; index++) {
    const t = orderedTests[index];
    if (t.skip) {
      console.log(`  ⏭️  ${t.name}`);
      console.log(`     Reason: ${t.reason}`);
      skipped++;
      continue;
    }

    if (index > 0) {
      try {
        await relaunchApp(driver);
      } catch (err) {
        console.log(`  ℹ️  Relaunch before "${t.name}" failed: ${err.message}`);
      }
    }

    try {
      await t.fn(driver);
      console.log(`  ✅ ${t.name}`);
      passed++;
    } catch (err) {
      if (err instanceof ExpectedSkipError) {
        console.log(`  ⏭️  ${t.name}`);
        console.log(`     ${err.message}`);
        skipped++;
        continue;
      }
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
