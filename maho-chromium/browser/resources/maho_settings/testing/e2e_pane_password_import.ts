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

type ImportCardsSnapshot = {
  readonly buttonLabels: readonly string[];
  readonly hasAppleCard: boolean;
  readonly hasBitwardenCard: boolean;
  readonly hasKeePassCard: boolean;
  readonly hasOnePasswordCard: boolean;
  readonly mainText: string;
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
  if (vaultGate.needsRecovery && !await setInputByLabel(cdp, 'Vault recovery secret', 'recovery key')) {
    return false;
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
  return waitForMainText(cdp, 'Import passwords', 6000);
}

async function waitForImportSources(cdp: CDP): Promise<boolean> {
  return waitForCondition(cdp, `(function(){
    var pane = document.querySelector('main [data-pane="saved-passwords"]');
    var text = ((pane && pane.textContent) || '').replace(/\\s+/g, ' ');
    var buttonLabels = Array.from(pane ? pane.querySelectorAll('button') : []).map(function(button){
      return ((button.textContent || '').replace(/\\s+/g, ' ')).trim();
    });
    return /1Password CSV or 1PUX/.test(text) &&
        /Bitwarden CSV or JSON/.test(text) &&
        /Apple Passwords CSV\\/file export/.test(text) &&
        /KeePass \\/ KeePassXC CSV/.test(text) &&
        [
          'Choose 1Password CSV',
          'Choose 1PUX',
          'Choose Bitwarden individual CSV',
          'Choose Bitwarden organization CSV',
          'Choose Bitwarden JSON',
          'Choose Apple Passwords CSV',
          'Choose KeePassXC CSV',
          'Choose KeePass classic CSV',
        ].every(function(label){ return buttonLabels.indexOf(label) !== -1; });
  })()`, 7000);
}

async function main(): Promise<void> {
  await checkBrowserOrExit();

  const ws = await getSettingsWs();
  const cdp = new CDP(ws);
  await cdp.send("Runtime.enable");
  await cdp.send("Page.enable");
  await Bun.sleep(500);

  step("Given Maho Native password management is enabled");
  await cdp.navigate(`${CDP_URL_ROOT}passwords`, 3000);
  assert(await waitForSettingsPane(cdp, 'passwords'), "Passwords pane route is active");
  assert(await ensureNativeProviderAndEnabled(cdp), "Maho Native provider is selected and passwords are enabled");

  step("When the Saved Passwords import flow is opened");
  await cdp.navigate(`${CDP_URL_ROOT}saved-passwords`, 3000);
  assert(await waitForSettingsPane(cdp, 'saved-passwords'), "Saved Passwords pane route is active");
  assert(await unlockVaultIfNeeded(cdp), "Saved Passwords pane shows guided import flow after Vault unlock");
  assert(await waitForImportSources(cdp), "Import source cards and format buttons are visible");

  const snapshot = await cdp.eval<ImportCardsSnapshot>(`(function(){
    var pane = document.querySelector('main [data-pane="saved-passwords"]');
    var text = ((pane && pane.textContent) || '').replace(/\\s+/g, ' ');
    var buttonLabels = Array.from(pane ? pane.querySelectorAll('button') : []).map(function(button){
      return ((button.textContent || '').replace(/\\s+/g, ' ')).trim();
    });
    return {
      buttonLabels: buttonLabels,
      hasAppleCard: /Apple Passwords CSV\\/file export/.test(text),
      hasBitwardenCard: /Bitwarden CSV or JSON/.test(text),
      hasKeePassCard: /KeePass \\/ KeePassXC CSV/.test(text),
      hasOnePasswordCard: /1Password CSV or 1PUX/.test(text),
      mainText: text
    };
  })()`);

  assert(snapshot?.hasOnePasswordCard === true, "1Password CSV/1PUX source card is visible");
  assert(snapshot?.hasBitwardenCard === true, "Bitwarden CSV/JSON source card is visible");
  assert(snapshot?.hasAppleCard === true, "Apple Passwords CSV/file export source card is visible");
  assert(snapshot?.hasKeePassCard === true, "KeePass/KeePassXC CSV source card is visible");
  for (const expectedButton of [
    'Choose 1Password CSV',
    'Choose 1PUX',
    'Choose Bitwarden individual CSV',
    'Choose Bitwarden organization CSV',
    'Choose Bitwarden JSON',
    'Choose Apple Passwords CSV',
    'Choose KeePassXC CSV',
    'Choose KeePass classic CSV',
  ]) {
    assert(snapshot?.buttonLabels.includes(expectedButton) === true, `${expectedButton} button is visible`);
  }
  assert(!/Export All Items to App|native receiver|app-to-app|iCloud Passwords for Windows/i.test(snapshot?.mainText ?? ''),
      "Apple card remains CSV/file-only without native receiver or Windows export copy");
  const legacyRevealPattern = new RegExp(`${"Reveal"}${"Password"}|${"reveal"}${"Password"}|raw password|TOTP seed`, 'i');
  assert(!legacyRevealPattern.test(snapshot?.mainText ?? ''),
      "Import flow does not expose reveal or raw secret preview copy");

  ws.close();
  reportResultsAndExit();
}

await main();
