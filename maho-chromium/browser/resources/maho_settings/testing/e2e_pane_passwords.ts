#!/usr/bin/env bun
import {
  CDP,
  assert,
  step,
  getSettingsWs,
  checkRelayOrExit,
  reportResultsAndExit,
  CDP_URL_ROOT
} from "./cdp_harness";

async function main() {
  await checkRelayOrExit();

  const ws = await getSettingsWs();
  const cdp = new CDP(ws);
  await cdp.send("Runtime.enable");
  await cdp.send("Page.enable");
  await new Promise(r => setTimeout(r, 500));

  step("Passwords Pane Verification");
  await cdp.navigate(`${CDP_URL_ROOT}passwords`, 4000);

  const passwords = await cdp.eval<{hasTitle: boolean; hasProviderText: boolean; hasToggle: boolean; text: string}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var hasTitle = /Saved passwords/i.test(text);
    var hasProviderText = /Passwords provider/i.test(text);
    var toggle = main.querySelector('button[role="switch"]') || main.querySelector('input[type="checkbox"]');
    return {
      hasTitle: hasTitle,
      hasProviderText: hasProviderText,
      hasToggle: !!toggle,
      text: text.slice(0, 300)
    };
  })()`);

  assert(passwords!.hasTitle === true, "Passwords pane title 'Saved passwords' rendered");
  assert(passwords!.hasProviderText === true, "Password provider description is visible");
  assert(passwords!.hasToggle === true, "Passwords toggle button is present");
  assert(passwords!.text.length > 50, "Passwords page content successfully loaded");

  // Toggle switch
  const toggled = await cdp.eval<boolean>(`(function(){
    var toggle = document.querySelector('main button[role="switch"]') || document.querySelector('main input[type="checkbox"]');
    if (!toggle) return false;
    toggle.click();
    return true;
  })()`);
  assert(toggled === true, "Toggled passwords setting successfully");
  await new Promise(r => setTimeout(r, 1500));

  ws.close();
  reportResultsAndExit();
}

await main();
