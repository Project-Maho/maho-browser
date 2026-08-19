#!/usr/bin/env bun
import {
  CDP,
  CDP_URL_ROOT,
  assert,
  checkBrowserOrExit,
  getSettingsWs,
  reportResultsAndExit,
  step,
  waitForCondition,
  waitForSettingsPane,
} from "./cdp_harness";

const SET_INPUT_HELPER = `
  function __setInput(el, val){
    var setter = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, 'value').set;
    setter.call(el, val);
    el.dispatchEvent(new Event('input', {bubbles: true}));
    el.dispatchEvent(new Event('change', {bubbles: true}));
  }
`;

const TEST_ORIGIN = "https://e2e-saved.example";
const TEST_UPDATED_ORIGIN = "https://e2e-saved-updated.example";
const TEST_USERNAME = "e2e-user";
const TEST_USERNAME_HINT = "e***";
const TEST_UPDATED_USERNAME = "z2e-user-updated";
const TEST_UPDATED_USERNAME_HINT = "z***";
const TEST_SECRET = "e2e-secret-value-do-not-log";

type PasswordSettingsSnapshot = {
  readonly hasManagementControls: boolean;
  readonly hasProviderText: boolean;
  readonly hasSecretUseCopy: boolean;
  readonly hasSettingsTitle: boolean;
  readonly mainText: string;
  readonly providerCombobox: boolean;
};

async function setInputByLabel(cdp: CDP, label: string, value: string): Promise<boolean> {
  return await cdp.eval<boolean>(`(function(){
    ${SET_INPUT_HELPER}
    var input = Array.from(document.querySelectorAll('input')).find(function(candidate){
      return (candidate.getAttribute('aria-label') || '') === ${JSON.stringify(label)};
    });
    if (!input) return false;
    __setInput(input, ${JSON.stringify(value)});
    return true;
  })()`) === true;
}

async function clickButton(cdp: CDP, label: string, rowText?: string): Promise<boolean> {
  return await cdp.eval<boolean>(`(function(){
    function normalizedText(element) {
      return ((element && element.textContent) || '').replace(/\\s+/g, ' ').trim();
    }
    var main = document.querySelector('main');
    var root = document;
    if (${JSON.stringify(rowText ?? '')}) {
      root = Array.from(main ? main.querySelectorAll('section') : []).find(function(section){
        return normalizedText(section).indexOf(${JSON.stringify(rowText ?? '')}) !== -1;
      });
    }
    var button = Array.from(root ? root.querySelectorAll('button') : []).find(function(candidate){
      return normalizedText(candidate) === ${JSON.stringify(label)};
    });
    if (!button || button.disabled) return false;
    button.click();
    return true;
  })()`) === true;
}

async function assertNoPlaintextSurface(cdp: CDP, label: string): Promise<void> {
  const leaked = await cdp.eval<boolean>(`(function(){
    var main = document.querySelector('main');
    var text = ((main && main.textContent) || '').replace(/\\s+/g, ' ');
    var hasForbiddenCopy = /\\b(Reveal|Hide|Password:)\\b/i.test(text);
    var hasSecret = text.indexOf(${JSON.stringify(TEST_SECRET)}) !== -1;
    var hasEyeIcon = !!(main && main.querySelector('[data-lucide="eye"], [data-lucide="eye-off"]'));
    return hasForbiddenCopy || hasSecret || hasEyeIcon;
  })()`);
  assert(leaked === false, label);
}

async function waitForMainText(cdp: CDP, text: string, timeoutMs = 5000): Promise<boolean> {
  return waitForCondition(cdp, `(function(){
    var main = document.querySelector('main');
    return !!main && ((main.textContent || '').indexOf(${JSON.stringify(text)}) !== -1);
  })()`, timeoutMs);
}

async function ensureNativeProviderAndEnabled(cdp: CDP): Promise<boolean> {
  const providerReady = await cdp.eval<boolean>(`(function(){
    var trigger = document.querySelector('main button[role="combobox"][aria-label="Password provider"]');
    if (trigger && !/maho/i.test(trigger.textContent || '')) {
      trigger.click();
      return false;
    }
    var sw = document.querySelector('main button[role="switch"][aria-label="Passwords"]');
    var on = sw ? (sw.getAttribute('aria-checked') === 'true' || sw.getAttribute('data-state') === 'checked') : true;
    if (sw && !on) sw.click();
    return !!trigger && /maho/i.test(trigger.textContent || '');
  })()`);
  if (providerReady) {
    await Bun.sleep(900);
    return true;
  }

  await Bun.sleep(500);
  const selected = await cdp.eval<boolean>(`(function(){
    var option = Array.from(document.querySelectorAll('[role="option"]')).find(function(candidate){
      return /maho/i.test(candidate.textContent || '') && candidate.getAttribute('aria-disabled') !== 'true' && !candidate.hasAttribute('data-disabled');
    });
    if (!option) return false;
    option.click();
    return true;
  })()`);
  await Bun.sleep(1200);
  return selected === true;
}

async function unlockVaultIfNeeded(cdp: CDP): Promise<boolean> {
  const vaultGate = await cdp.eval<{readonly needsUnlock: boolean; readonly needsRecovery: boolean}>(`(function(){
    var main = document.querySelector('main');
    function byLabel(label) {
      return Array.from(main ? main.querySelectorAll('input') : []).find(function(input){
        return (input.getAttribute('aria-label') || '') === label;
      });
    }
    var master = byLabel('Vault master passphrase');
    var recovery = byLabel('Vault recovery secret');
    return {needsUnlock: !!master, needsRecovery: !!recovery};
  })()`);
  if (vaultGate?.needsUnlock !== true) {
    return true;
  }

  if (!await setInputByLabel(cdp, 'Vault master passphrase', 'correct passphrase')) {
    return false;
  }
  if (vaultGate.needsRecovery) {
    if (!await setInputByLabel(cdp, 'Confirm Vault master passphrase', 'correct passphrase')) {
      return false;
    }
    if (!await setInputByLabel(cdp, 'Vault recovery secret', 'recovery key')) {
      return false;
    }
    if (!await setInputByLabel(cdp, 'Confirm Vault recovery secret', 'recovery key')) {
      return false;
    }
    await cdp.eval(`(function(){
      var main = document.querySelector('main');
      var cb = main ? main.querySelector('input[type="checkbox"][id="vault-recovery-kit-acknowledged"]') : null;
      if (cb && !cb.checked) { cb.click(); }
    })()`);
  }

  const label = vaultGate.needsRecovery ? 'Initialize Vault' : 'Unlock Vault';
  const canSubmit = await waitForCondition(cdp, `(function(){
    var main = document.querySelector('main');
    var button = Array.from(main.querySelectorAll('button')).find(function(candidate){
      return (candidate.textContent || '').trim() === ${JSON.stringify(label)};
    });
    return !!button && !button.disabled;
  })()`, 3000);
  if (!canSubmit) {
    return false;
  }

  const clicked = await cdp.eval<boolean>(`(function(){
    var main = document.querySelector('main');
    var button = Array.from(main.querySelectorAll('button')).find(function(candidate){
      return (candidate.textContent || '').trim() === ${JSON.stringify(label)};
    });
    if (!button || button.disabled) return false;
    button.click();
    return true;
  })()`);
  if (clicked !== true) {
    return false;
  }
  return waitForMainText(cdp, 'Add password', 6000);
}

async function main(): Promise<void> {
  await checkBrowserOrExit();

  const ws = await getSettingsWs();
  const cdp = new CDP(ws);
  await cdp.send("Runtime.enable");
  await cdp.send("Page.enable");
  await Bun.sleep(500);

  step("Given the Passwords settings pane");
  await cdp.navigate(`${CDP_URL_ROOT}passwords`, 3000);
  assert(await waitForSettingsPane(cdp, 'passwords'), "Passwords pane route is active");
  const settingsSnapshot = await cdp.eval<PasswordSettingsSnapshot>(`(function(){
    var pane = document.querySelector('main [data-pane="passwords"]');
    var text = ((pane && pane.textContent) || '').replace(/\\s+/g, ' ');
    var labels = Array.from(pane ? pane.querySelectorAll('button') : []).map(function(button){
      return ((button.textContent || '').replace(/\\s+/g, ' ')).trim();
    });
    return {
      hasManagementControls: labels.some(function(label){ return /^(Add password|Copy|Fill|Edit|Delete|Reveal|Hide)$/i.test(label); }),
      hasProviderText: /Current provider:/i.test(text),
      hasSecretUseCopy: /\\b(Reveal|Hide|Copy|Fill|Password:)\\b/i.test(text),
      hasSettingsTitle: /Password settings/i.test(text),
      mainText: text.slice(0, 360),
      providerCombobox: !!pane && !!pane.querySelector('button[role="combobox"][aria-label="Password provider"]')
    };
  })()`);
  assert(settingsSnapshot?.hasSettingsTitle === true, "Passwords pane renders settings title");
  assert(settingsSnapshot?.hasProviderText === true, "Passwords pane renders provider settings copy");
  assert(settingsSnapshot?.providerCombobox === true, "Passwords pane keeps provider selector");
  assert(settingsSnapshot?.hasManagementControls === false, `Passwords pane has no credential management buttons (${settingsSnapshot?.mainText ?? '<missing>'})`);
  assert(settingsSnapshot?.hasSecretUseCopy === false, `Passwords pane has no secret-use/reveal copy (${settingsSnapshot?.mainText ?? '<missing>'})`);

  step("When Maho Native password management is enabled");
  assert(await ensureNativeProviderAndEnabled(cdp), "Maho Native provider is selected and passwords are enabled");

  step("When the Saved Passwords pane is opened");
  await cdp.navigate(`${CDP_URL_ROOT}saved-passwords`, 3000);
  assert(await waitForSettingsPane(cdp, 'saved-passwords'), "Saved Passwords pane route is active");
  assert(await unlockVaultIfNeeded(cdp), "Saved Passwords pane is unlocked or unlock flow completed");
  assert(await waitForMainText(cdp, 'Saved Passwords'), "Saved Passwords management heading rendered");
  await assertNoPlaintextSurface(cdp, "Saved Passwords initially has no plaintext/reveal surface");

  step("When a saved password is added");
  assert(await clickButton(cdp, 'Add password'), "Add password drawer opened");
  assert(await setInputByLabel(cdp, 'Saved password origin', TEST_ORIGIN), "Origin entered");
  assert(await setInputByLabel(cdp, 'Saved password username', TEST_USERNAME), "Username entered");
  assert(await setInputByLabel(cdp, 'Saved password value', TEST_SECRET), "Password entered without reading it back");
  assert(await clickButton(cdp, 'Save password'), "Save password submitted");
  assert(await waitForMainText(cdp, TEST_ORIGIN, 7000), "Added saved password row rendered");
  await assertNoPlaintextSurface(cdp, "Adding does not display plaintext or reveal controls");

  step("Then search isolates the saved password");
  assert(await setInputByLabel(cdp, 'Search saved passwords', TEST_ORIGIN), "Search query entered");
  await Bun.sleep(800);
  assert(await waitForMainText(cdp, TEST_USERNAME_HINT), "Search result contains the masked username hint");
  await assertNoPlaintextSurface(cdp, "Search result remains metadata-only");

  step("When copy and fill actions run through status-only path");
  assert(await clickButton(cdp, 'Copy', TEST_ORIGIN), "Copy action clicked");
  assert(await waitForMainText(cdp, 'No password was returned', 7000), "Copy reports status-only completion");
  assert(await clickButton(cdp, 'Fill', TEST_ORIGIN), "Fill action clicked");
  assert(await waitForMainText(cdp, 'status-only path', 7000), "Fill reports status-only completion");
  await assertNoPlaintextSurface(cdp, "Copy/fill never renders the secret");

  step("When username and origin are edited in the drawer");
  assert(await clickButton(cdp, 'Edit', TEST_ORIGIN), "Edit drawer opened");
  assert(await setInputByLabel(cdp, 'Edit saved password origin', TEST_UPDATED_ORIGIN), "Updated origin entered");
  assert(await setInputByLabel(cdp, 'Edit saved password username', TEST_UPDATED_USERNAME), "Updated username entered");
  assert(await clickButton(cdp, 'Save changes'), "Metadata changes submitted");
  assert(await setInputByLabel(cdp, 'Search saved passwords', TEST_UPDATED_ORIGIN), "Search updated origin entered");
  assert(await waitForMainText(cdp, TEST_UPDATED_USERNAME_HINT, 7000), "Updated masked username hint rendered");
  await assertNoPlaintextSurface(cdp, "Edit drawer does not reveal plaintext");

  step("Cleanup removes the saved password");
  assert(await clickButton(cdp, 'Delete', TEST_UPDATED_ORIGIN), "Delete action clicked");
  const deleted = await waitForCondition(cdp, `(function(){
    var main = document.querySelector('main');
    return !!main && ((main.textContent || '').indexOf(${JSON.stringify(TEST_UPDATED_ORIGIN)}) === -1);
  })()`, 7000);
  assert(deleted, "Saved password row removed after delete");
  await assertNoPlaintextSurface(cdp, "Cleanup leaves no plaintext/reveal surface");

  ws.close();
  reportResultsAndExit();
}

await main();
