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

  step("Mail Security Pane Verification");
  await cdp.navigate(`${CDP_URL_ROOT}mail-security`, 4000);

  const initial = await cdp.eval<{hasTitle: boolean; settled: boolean; hasContent: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var hasTitle = /PGP & S\\/MIME security/i.test(text);
    var settled = !/Loading security settings\\.\\.\\./i.test(text);
    // Either the no-account empty state, the account selector + key sections,
    // or an inline load error are valid settled outcomes.
    var hasContent = /Connect a mail account to manage security keys|Mail Account|PGP Encryption Keys|Failed to load/i.test(text);
    return {
      hasTitle: hasTitle,
      settled: settled,
      hasContent: hasContent
    };
  })()`);

  assert(initial!.hasTitle === true, "Security pane title 'PGP & S/MIME security' rendered");
  assert(initial!.settled === true, "Security pane resolved past the loading state");
  assert(initial!.hasContent === true, "Account selector, empty-state, or inline error rendered");

  step("Account-scoped security state");
  const state = await cdp.eval<{hasAccountSelector: boolean; hasEmptyState: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var hasAccountSelector = !!main.querySelector('[role="combobox"]') && /Mail Account/i.test(text);
    var hasEmptyState = /Connect a mail account to manage security keys/i.test(text);
    return {
      hasAccountSelector: hasAccountSelector,
      hasEmptyState: hasEmptyState
    };
  })()`);
  assert(
    state!.hasAccountSelector === true || state!.hasEmptyState === true,
    "Security pane shows either the account selector or the connect-account empty state"
  );

  if (state!.hasAccountSelector) {
    // With a selectable account, the key-management sections must render.
    const withAccount = await cdp.eval<{hasPgpSection: boolean; hasSmimeSection: boolean; optionCount: number}>(`(function(){
      var main = document.querySelector('main');
      var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
      var trigger = main.querySelector('[role="combobox"]');
      if (trigger) trigger.click();
      return {
        hasPgpSection: /PGP Encryption Keys/i.test(text),
        hasSmimeSection: /S\\/MIME Identities/i.test(text),
        optionCount: 0
      };
    })()`);
    await new Promise(r => setTimeout(r, 700));
    const options = await cdp.eval<string[]>(`(function(){
      return Array.from(document.querySelectorAll('[role="option"]')).map(function(o){ return (o.textContent||'').trim(); });
    })()`);
    await cdp.send("Input.dispatchKeyEvent", {type: "keyDown", key: "Escape", code: "Escape", windowsVirtualKeyCode: 27, nativeVirtualKeyCode: 27});
    await cdp.send("Input.dispatchKeyEvent", {type: "keyUp", key: "Escape", code: "Escape", windowsVirtualKeyCode: 27, nativeVirtualKeyCode: 27});
    await new Promise(r => setTimeout(r, 400));

    assert(withAccount!.hasPgpSection === true, "PGP Encryption Keys section visible for selected account");
    assert(withAccount!.hasSmimeSection === true, "S/MIME Identities section visible for selected account");
    assert((options ?? []).length > 0, "Account selector lists at least one mail account option");
  } else {
    assert(state!.hasEmptyState === true, "Connect-account empty state guides the user with no accounts");
  }

  ws.close();
  reportResultsAndExit();
}

await main();
