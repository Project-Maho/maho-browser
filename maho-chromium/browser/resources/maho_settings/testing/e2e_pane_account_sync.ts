#!/usr/bin/env bun
import {
  CDP,
  assert,
  step,
  getSettingsWs,
  checkRelayOrExit,
  reportResultsAndExit,
  CDP_URL_ROOT,
} from "./cdp_harness";

async function main() {
  await checkRelayOrExit();

  const ws = await getSettingsWs();
  const cdp = new CDP(ws);
  await cdp.send("Runtime.enable");
  await cdp.send("Page.enable");
  await new Promise((r) => setTimeout(r, 500));

  step("Account & Sync Pane Verification");
  await cdp.navigate(`${CDP_URL_ROOT}account-sync`, 4000);

  const state = await cdp.eval<{
    hasGenerateBtn: boolean;
    hasJoinBtn: boolean;
    hasStatusText: boolean;
    hasHeader: boolean;
    text: string;
  }>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var btns = Array.from(main.querySelectorAll('button')).map(function(b){return b.textContent.trim();});
    var hasHeader = /Account & sync/i.test(text);
    var hasGenerateBtn = btns.some(function(t){return /Generate recovery/i.test(t);});
    var hasJoinBtn = btns.some(function(t){return /^Join/i.test(t);});
    var hasStatusText = /(Not connected|Connected|offline|online|Status)/i.test(text);
    return {hasGenerateBtn: hasGenerateBtn, hasJoinBtn: hasJoinBtn, hasStatusText: hasStatusText, hasHeader: hasHeader, text: text.slice(0, 300)};
  })()`);

  assert(state!.hasHeader === true, "Account & sync pane header rendered");
  assert(state!.hasStatusText === true, `Sync status text visible: ${state!.text.slice(0, 80)}`);
  assert(state!.hasGenerateBtn === true, "'Generate recovery phrase' button present");
  assert(state!.hasJoinBtn === true, "'Join' button present");

  ws.close();
  reportResultsAndExit();
}

await main();
