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

  step("Mail Behavior Pane Verification");
  await cdp.navigate(`${CDP_URL_ROOT}mail-behavior`, 4000);

  const initial = await cdp.eval<{hasTitle: boolean; settled: boolean; hasContent: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var hasTitle = /Mail behavior & layout/i.test(text);
    var settled = !/Loading behavior settings\\.\\.\\./i.test(text);
    var hasContent = /Mail Composition & Layout|Privacy & Images|Failed to load behavior settings/i.test(text);
    return {
      hasTitle: hasTitle,
      settled: settled,
      hasContent: hasContent
    };
  })()`);

  assert(initial!.hasTitle === true, "Behavior pane title 'Mail behavior & layout' rendered");
  assert(initial!.settled === true, "Behavior pane resolved past the loading state");
  assert(initial!.hasContent === true, "Behavior sections or inline error rendered");

  step("Inspect behavior controls and open a selector");
  const controlsPresent = await cdp.eval<{hasUndoSend: boolean; hasSwitch: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var hasSwitch = !!main.querySelector('[role="switch"]');
    return {
      hasUndoSend: /Undo Send Delay/i.test(text),
      hasSwitch: hasSwitch
    };
  })()`);
  assert(controlsPresent!.hasUndoSend === true, "Undo Send Delay preference row is visible");
  assert(controlsPresent!.hasSwitch === true, "At least one behavior toggle switch is present");

  const opened = await cdp.eval<boolean>(`(function(){
    var trigger = document.querySelector('main [role="combobox"]');
    if (!trigger) return false;
    trigger.click();
    return true;
  })()`);
  assert(opened === true, "Opened a behavior preference dropdown");
  await new Promise(r => setTimeout(r, 700));

  const options = await cdp.eval<string[]>(`(function(){
    return Array.from(document.querySelectorAll('[role="option"]')).map(function(o){ return (o.textContent||'').trim(); });
  })()`);
  await cdp.send("Input.dispatchKeyEvent", {type: "keyDown", key: "Escape", code: "Escape", windowsVirtualKeyCode: 27, nativeVirtualKeyCode: 27});
  await cdp.send("Input.dispatchKeyEvent", {type: "keyUp", key: "Escape", code: "Escape", windowsVirtualKeyCode: 27, nativeVirtualKeyCode: 27});
  await new Promise(r => setTimeout(r, 400));

  assert((options ?? []).length > 0, "Behavior preference dropdown lists selectable options");

  ws.close();
  reportResultsAndExit();
}

await main();
