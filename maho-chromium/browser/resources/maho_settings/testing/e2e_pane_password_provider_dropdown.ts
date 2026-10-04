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

type ProviderOptionState = {
  readonly isDisabled: boolean;
  readonly text: string;
};

type ProviderOptionsSnapshot = {
  readonly bitwarden: ProviderOptionState | null;
  readonly onePassword: ProviderOptionState | null;
};

async function openPasswordProviderDropdown(cdp: CDP): Promise<boolean> {
  const clicked = await cdp.eval<boolean>(`(function(){
    var trigger = document.querySelector('main button[role="combobox"][aria-label="Password provider"]');
    if (!trigger || trigger.disabled) return false;
    trigger.click();
    return true;
  })()`) === true;
  if (!clicked) {
    return false;
  }

  return waitForCondition(cdp, `(function(){
    var options = Array.from(document.querySelectorAll('[role="option"]'));
    return options.some(function(candidate){ return /Bitwarden/i.test(candidate.textContent || ''); }) &&
        options.some(function(candidate){ return /1Password/i.test(candidate.textContent || ''); });
  })()`, 3000);
}

async function getProviderOptionsSnapshot(cdp: CDP): Promise<ProviderOptionsSnapshot> {
  return await cdp.eval<ProviderOptionsSnapshot>(`(function(){
    function describeOption(pattern) {
      var option = Array.from(document.querySelectorAll('[role="option"]')).find(function(candidate){
        return pattern.test(candidate.textContent || '');
      });
      if (!option) return null;
      return {
        isDisabled: option.getAttribute('aria-disabled') === 'true' || option.hasAttribute('data-disabled'),
        text: ((option.textContent || '').replace(/\\s+/g, ' ')).trim()
      };
    }
    return {
      bitwarden: describeOption(/Bitwarden/i),
      onePassword: describeOption(/1Password/i)
    };
  })()`);
}

async function clickProviderOption(cdp: CDP, label: string): Promise<boolean> {
  const clicked = await cdp.eval<boolean>(`(function(){
    var option = Array.from(document.querySelectorAll('[role="option"]')).find(function(candidate){
      return (candidate.textContent || '').indexOf(${JSON.stringify(label)}) !== -1;
    });
    if (!option || option.getAttribute('aria-disabled') === 'true' || option.hasAttribute('data-disabled')) {
      return false;
    }
    option.click();
    return true;
  })()`) === true;
  if (!clicked) {
    return false;
  }

  return waitForCondition(cdp, `(function(){
    var trigger = document.querySelector('main button[role="combobox"][aria-label="Password provider"]');
    return !!trigger && (trigger.textContent || '').indexOf(${JSON.stringify(label)}) !== -1;
  })()`, 5000);
}

async function selectPasswordProvider(cdp: CDP, label: string): Promise<boolean> {
  if (!await openPasswordProviderDropdown(cdp)) {
    return false;
  }
  return clickProviderOption(cdp, label);
}

async function waitForProviderUnavailableWarning(cdp: CDP, label: string): Promise<boolean> {
  return waitForCondition(cdp, `(function(){
    var pane = document.querySelector('main [data-pane="passwords"]');
    var text = ((pane && pane.textContent) || '').replace(/\\s+/g, ' ');
    var buttons = Array.from(pane ? pane.querySelectorAll('button') : []).map(function(button){
      return ((button.textContent || '').replace(/\\s+/g, ' ')).trim();
    });
    return text.indexOf(${JSON.stringify(`Selected provider extension (${label}) is not installed or disabled.`)}) !== -1 &&
        buttons.indexOf('Manage Extensions') !== -1;
  })()`, 7000);
}

async function main(): Promise<void> {
  await checkBrowserOrExit();

  const ws = await getSettingsWs();
  const cdp = new CDP(ws);
  await cdp.send("Runtime.enable");
  await cdp.send("Page.enable");
  await Bun.sleep(500);

  step("Given the Passwords settings provider dropdown");
  await cdp.navigate(`${CDP_URL_ROOT}passwords`, 3000);
  assert(await waitForSettingsPane(cdp, 'passwords'), "Passwords pane route is active");
  assert(await openPasswordProviderDropdown(cdp), "Password provider dropdown opens");

  const providerOptions = await getProviderOptionsSnapshot(cdp);
  assert(providerOptions.bitwarden?.isDisabled === false,
      `Unavailable Bitwarden provider option remains selectable (${providerOptions.bitwarden?.text ?? '<missing>'})`);
  assert(providerOptions.onePassword?.isDisabled === false,
      `Unavailable 1Password provider option remains selectable (${providerOptions.onePassword?.text ?? '<missing>'})`);
  assert(/not installed or disabled/i.test(providerOptions.bitwarden?.text ?? ''),
      "Unavailable Bitwarden provider option explains its extension state");
  assert(/not installed or disabled/i.test(providerOptions.onePassword?.text ?? ''),
      "Unavailable 1Password provider option explains its extension state");

  step("When Bitwarden is selected without its extension installed");
  assert(await clickProviderOption(cdp, 'Bitwarden'), "Bitwarden provider option can be selected");
  assert(await waitForProviderUnavailableWarning(cdp, 'Bitwarden'),
      "Manage Extensions action is shown for Bitwarden");

  step("When 1Password is selected without its extension installed");
  assert(await selectPasswordProvider(cdp, '1Password'), "1Password provider option can be selected");
  assert(await waitForProviderUnavailableWarning(cdp, '1Password'),
      "Manage Extensions action is shown for 1Password");

  step("Cleanup returns the provider to Maho Native");
  assert(await selectPasswordProvider(cdp, 'Maho'), "Maho Native provider can be restored");

  ws.close();
  reportResultsAndExit();
}

await main();
