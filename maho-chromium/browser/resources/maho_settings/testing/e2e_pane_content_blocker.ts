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

  step("Content Blocker Pane Verification");
  await cdp.navigate(`${CDP_URL_ROOT}content-blocker`, 4000);

  const cb = await cdp.eval<{hasTitle: boolean; hasEngineStatus: boolean; hasRebuildBtn: boolean; text: string}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var hasTitle = /Content blocker/i.test(text);
    var hasEngineStatus = /Engine status/i.test(text);
    var btns = Array.from(main.querySelectorAll('button'));
    var hasRebuildBtn = btns.some(b => b.textContent.trim() === 'Rebuild');
    return {
      hasTitle: hasTitle,
      hasEngineStatus: hasEngineStatus,
      hasRebuildBtn: hasRebuildBtn,
      text: text.slice(0, 300)
    };
  })()`);

  assert(cb!.hasTitle === true, "Content blocker pane title 'Content blocker' rendered");
  assert(cb!.hasEngineStatus === true, "Engine status row is visible");
  assert(cb!.hasRebuildBtn === true, "Rebuild button is present");
  assert(cb!.text.length > 20, "Content blocker pane loaded successfully");

  // Click Rebuild
  const clickedRebuild = await cdp.eval<boolean>(`(function(){
    var btns = Array.from(document.querySelectorAll('main button'));
    var rebuildBtn = btns.find(b => b.textContent.trim() === 'Rebuild');
    if (rebuildBtn) {
      rebuildBtn.click();
      return true;
    }
    return false;
  })()`);
  assert(clickedRebuild === true, "Clicked Rebuild button successfully");
  await new Promise(r => setTimeout(r, 2000));

  const afterRebuild = await cdp.eval<{hasEngineStatus: boolean}>(`(function(){
    var main = document.querySelector('main');
    return { hasEngineStatus: main.textContent.includes("Engine status") };
  })()`);
  assert(afterRebuild!.hasEngineStatus === true, "Engine status row is still visible after rebuild");

  ws.close();
  reportResultsAndExit();
}

await main();
