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

  step("Extensions Pane Verification");
  await cdp.navigate(`${CDP_URL_ROOT}extensions`, 4000);

  const extensions = await cdp.eval<{hasTitle: boolean; hasEmptyOrExtensions: boolean; text: string}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var hasTitle = /Installed extensions/i.test(text);
    var hasEmptyOrExtensions = /No extensions installed|Remove|Version|Enable/i.test(text);
    return {
      hasTitle: hasTitle,
      hasEmptyOrExtensions: hasEmptyOrExtensions,
      text: text.slice(0, 300)
    };
  })()`);

  assert(extensions!.hasTitle === true, "Extensions pane title 'Installed extensions' rendered");
  assert(extensions!.hasEmptyOrExtensions === true, "Extensions pane content loaded (empty list or extension details visible)");
  assert(extensions!.text.length > 20, "Extensions pane successfully loaded");

  // Verify empty state works correctly
  const hasEmptyDesc = await cdp.eval<boolean>(`(function(){
    var main = document.querySelector('main');
    return main.textContent.includes("No extensions installed") || !!main.querySelector('main button');
  })()`);
  assert(hasEmptyDesc === true, "Extensions empty state or active extension list verified");

  ws.close();
  reportResultsAndExit();
}

await main();
