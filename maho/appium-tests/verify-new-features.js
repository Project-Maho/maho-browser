const { remote } = require('webdriverio');
const fs = require('fs');
const path = require('path');

const SCREENSHOT_DIR = path.join(__dirname, 'screenshots', 'new-features');
const APP_PATH = '/Users/indo/Library/Developer/Xcode/DerivedData/Maho-cncqahqhgqnsbaetovgsgoskvekd/Build/Products/Debug-iphonesimulator/Maho.app';

const capabilities = {
    platformName: 'iOS',
    'appium:automationName': 'XCUITest',
    'appium:deviceName': 'iPhone 17 Pro',
    'appium:platformVersion': '26.4',
    'appium:app': APP_PATH,
    'appium:udid': '9E69A245-2BA0-4DB2-AB89-2D667ECCEC4B',
    'appium:noReset': false,
    'appium:fullReset': false,
    'appium:isSimulator': true,
};

async function tapAt(driver, x, y) {
    await driver.performActions([{
        type: 'pointer', id: 'finger1', parameters: { pointerType: 'touch' },
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

async function swipeDown(driver) {
    const { width, height } = await driver.getWindowRect();
    await driver.performActions([{
        type: 'pointer', id: 'finger1', parameters: { pointerType: 'touch' },
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
}

function save(name, data) {
    const fp = path.join(SCREENSHOT_DIR, name);
    fs.writeFileSync(fp, data, 'base64');
    console.log(`  📸 ${name} (${Math.round(fs.statSync(fp).size / 1024)}KB)`);
}

(async () => {
    if (!fs.existsSync(SCREENSHOT_DIR)) fs.mkdirSync(SCREENSHOT_DIR, { recursive: true });

    console.log('Connecting...');
    const driver = await remote({
        hostname: '127.0.0.1', port: 4723, path: '/',
        capabilities, logLevel: 'warn',
        connectionRetryTimeout: 120000, connectionRetryCount: 5,
    });
    console.log('Connected.\n');

    try {
        // 1. Skip onboarding
        console.log('=== ONBOARDING ===');
        await driver.pause(3000);
        const skipBtn = await driver.$('//XCUIElementTypeButton[@label="Skip"]');
        if (skipBtn && await skipBtn.isExisting()) {
            await skipBtn.click();
            await driver.pause(1000);
            const defBtn = await driver.$('//XCUIElementTypeButton[contains(@label, "Not Now") or contains(@label, "Skip")]');
            if (defBtn && await defBtn.isExisting()) await defBtn.click();
            await driver.pause(1500);
        }
        console.log('  Onboarding skipped.\n');

        // 2. Voice Search mic button on home
        console.log('=== VOICE SEARCH (Home Screen) ===');
        await driver.pause(1000);
        save('01-home-with-mic.png', await driver.takeScreenshot());

        const source1 = await driver.getPageSource();
        const hasMic = source1.includes('Voice search') || source1.includes('mic');
        console.log(`  Mic button in source: ${hasMic}`);

        const xml1 = path.join(SCREENSHOT_DIR, 'home-source.xml');
        fs.writeFileSync(xml1, source1);
        console.log(`  Source tree saved (${Math.round(source1.length / 1024)}KB)\n`);

        // 3. Navigate to example.com for Browse for Me test
        console.log('=== NAVIGATE TO PAGE ===');
        const searchField = await driver.$('//XCUIElementTypeTextField');
        if (searchField && await searchField.isExisting()) {
            await searchField.click();
            await driver.pause(500);
            await searchField.setValue('https://en.wikipedia.org/wiki/Arc_(web_browser)');
            await driver.pause(300);
            await driver.performActions([{
                type: 'key', id: 'keyboard',
                actions: [
                    { type: 'keyDown', value: '\uE007' },
                    { type: 'keyUp', value: '\uE007' },
                ],
            }]);
            await driver.releaseActions();
            await driver.pause(5000);
        }
        save('02-page-loaded.png', await driver.takeScreenshot());
        console.log();

        // 4. Browse for Me in menu
        console.log('=== BROWSE FOR ME (Menu) ===');
        const moreBtn = await driver.$('//XCUIElementTypeButton[@name="More"]');
        await moreBtn.click();
        await driver.pause(1500);

        const menuSource = await driver.getPageSource();
        const hasBrowse = menuSource.includes('Browse for Me');
        console.log(`  "Browse for Me" in menu: ${hasBrowse}`);
        save('03-menu-browse-for-me.png', await driver.takeScreenshot());

        // Tap Browse for Me if found
        const browseBtn = await driver.$('//XCUIElementTypeButton[@label="Browse for Me"]');
        if (browseBtn && await browseBtn.isExisting()) {
            await browseBtn.click();
            await driver.pause(4000);
            save('04-browse-for-me-result.png', await driver.takeScreenshot());
            console.log('  Browse for Me triggered!\n');

            await driver.pause(2000);
            const doneBtn = await driver.$('//XCUIElementTypeButton[@label="Done"]');
            if (doneBtn && await doneBtn.isExisting()) {
                await doneBtn.click();
                await driver.pause(500);
            }
        } else {
            const { width, height } = await driver.getWindowRect();
            await tapAt(driver, Math.round(width / 2), Math.round(height / 3));
            await driver.pause(500);
            console.log('  Browse for Me button not found in menu, dismissed.\n');
        }

        // 5. Settings > Sync
        console.log('=== SYNC SETTINGS ===');
        const moreBtn2 = await driver.$('//XCUIElementTypeButton[@name="More"]');
        await moreBtn2.click();
        await driver.pause(1500);
        const settingsBtn = await driver.$('//XCUIElementTypeButton[@name="Settings"]');
        if (settingsBtn && await settingsBtn.isExisting()) {
            await settingsBtn.click();
            await driver.pause(2000);
        }
        save('05-settings-top.png', await driver.takeScreenshot());

        const settingsSource = await driver.getPageSource();
        const hasSync = settingsSource.includes('Sync');
        console.log(`  "Sync" in settings source: ${hasSync}`);

        // Try to find and tap Sync
        const syncLink = await driver.$('//XCUIElementTypeCell[.//XCUIElementTypeStaticText[@value="Sync"]]');
        if (syncLink && await syncLink.isExisting()) {
            await syncLink.click();
            await driver.pause(2000);
            save('06-sync-settings-screen.png', await driver.takeScreenshot());
            console.log('  Sync settings screen opened!');

            const syncSource = await driver.getPageSource();
            const hasAccount = syncSource.includes('Account') || syncSource.includes('Sign In');
            const hasToggle = syncSource.includes('Enable Sync') || syncSource.includes('toggle');
            console.log(`  Account section: ${hasAccount}, Toggle: ${hasToggle}`);

            fs.writeFileSync(path.join(SCREENSHOT_DIR, 'sync-source.xml'), syncSource);

            const backBtn = await driver.$('//XCUIElementTypeButton[@name="BackButton"]');
            if (backBtn && await backBtn.isExisting()) {
                await backBtn.click();
                await driver.pause(500);
            }
        } else {
            console.log('  Sync cell not directly visible, looking for any Sync element...');
            const anySync = await driver.$('//*[contains(@label, "Sync")]');
            if (anySync && await anySync.isExisting()) {
                console.log('  Found Sync element via label match');
                await anySync.click();
                await driver.pause(2000);
                save('06-sync-settings-screen.png', await driver.takeScreenshot());
            }
        }
        console.log();

        // Dismiss settings
        await swipeDown(driver);
        await swipeDown(driver);
        await driver.pause(500);

        // 6. Final state dump
        console.log('=== FINAL STATE ===');
        save('07-final-state.png', await driver.takeScreenshot());
        const finalSource = await driver.getPageSource();
        fs.writeFileSync(path.join(SCREENSHOT_DIR, 'final-source.xml'), finalSource);
        console.log(`  Final source: ${Math.round(finalSource.length / 1024)}KB\n`);

        console.log('=== VERIFICATION COMPLETE ===');

    } finally {
        await driver.deleteSession();
    }
})();
