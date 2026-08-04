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

  step("Mail Calendar Pane Verification");
  await cdp.navigate(`${CDP_URL_ROOT}mail-calendar`, 4000);

  const initial = await cdp.eval<{hasTitle: boolean; settled: boolean; hasContent: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var hasTitle = /Mail calendar settings/i.test(text);
    var settled = !/Loading calendar settings\\.\\.\\./i.test(text);
    var hasContent = /Calendar Preferences|Failed to load calendar/i.test(text);
    return {
      hasTitle: hasTitle,
      settled: settled,
      hasContent: hasContent
    };
  })()`);

  assert(initial!.hasTitle === true, "Calendar pane title 'Mail calendar settings' rendered");
  assert(initial!.settled === true, "Calendar pane resolved past the loading state");
  assert(initial!.hasContent === true, "Calendar preferences or inline error rendered");

  step("Open the Week Start selector");
  const controlsPresent = await cdp.eval<{hasWeekStart: boolean; hasSwitch: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var hasSwitch = !!main.querySelector('[role="switch"]');
    return {
      hasWeekStart: /Week Start/i.test(text),
      hasSwitch: hasSwitch
    };
  })()`);
  assert(controlsPresent!.hasWeekStart === true, "Week Start preference row is visible");
  assert(controlsPresent!.hasSwitch === true, "At least one preference toggle switch is present");

  const opened = await cdp.eval<boolean>(`(function(){
    var trigger = document.querySelector('main [role="combobox"]');
    if (!trigger) return false;
    trigger.click();
    return true;
  })()`);
  assert(opened === true, "Opened a calendar preference dropdown");
  await new Promise(r => setTimeout(r, 700));

  const options = await cdp.eval<string[]>(`(function(){
    return Array.from(document.querySelectorAll('[role="option"]')).map(function(o){ return (o.textContent||'').trim(); });
  })()`);
  await cdp.send("Input.dispatchKeyEvent", {type: "keyDown", key: "Escape", code: "Escape", windowsVirtualKeyCode: 27, nativeVirtualKeyCode: 27});
  await cdp.send("Input.dispatchKeyEvent", {type: "keyUp", key: "Escape", code: "Escape", windowsVirtualKeyCode: 27, nativeVirtualKeyCode: 27});
  await new Promise(r => setTimeout(r, 400));

  assert((options ?? []).length > 0, "Calendar preference dropdown lists selectable options");

  ws.close();
  reportResultsAndExit();
}

await main();
