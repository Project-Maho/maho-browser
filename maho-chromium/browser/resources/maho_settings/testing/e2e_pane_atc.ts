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

  step("Air Traffic Control Pane Verification");
  await cdp.navigate(`${CDP_URL_ROOT}atc`, 4000);

  const state = await cdp.eval<{
    hasUrlPatternInput: boolean;
    hasSpaceIdInput: boolean;
    hasAddRuleBtn: boolean;
    hasHeader: boolean;
  }>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var hasHeader = /Air traffic control/i.test(text);
    var inputs = Array.from(main.querySelectorAll('input'));
    var labels = inputs.map(function(i){return (i.getAttribute('aria-label')||i.placeholder||'').toLowerCase();});
    var hasUrlPatternInput = labels.some(function(l){return /url pattern/i.test(l);});
    var hasSpaceIdInput = labels.some(function(l){return /(target space id|space id)/i.test(l);});
    var btns = Array.from(main.querySelectorAll('button')).map(function(b){return b.textContent.trim();});
    var hasAddRuleBtn = btns.some(function(t){return /Add rule|^Add$/i.test(t);});
    return {hasUrlPatternInput: hasUrlPatternInput, hasSpaceIdInput: hasSpaceIdInput, hasAddRuleBtn: hasAddRuleBtn, hasHeader: hasHeader};
  })()`);

  assert(state!.hasHeader === true, "ATC pane header rendered");
  assert(state!.hasUrlPatternInput === true, "URL pattern input present");
  assert(state!.hasSpaceIdInput === true, "Target space ID input present");
  assert(state!.hasAddRuleBtn === true, "'Add rule' button present");

  ws.close();
  reportResultsAndExit();
}

await main();
