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

  step("Chromium Settings Embed Pane Verification");
  await cdp.navigate(`${CDP_URL_ROOT}chromium-settings`, 4500);

  const state = await cdp.eval<{
    iframeSrc: string | null;
    hasBackBtn: boolean;
    hasHeader: boolean;
  }>(`(function(){
    var iframe = document.querySelector('iframe');
    var iframeSrc = iframe ? iframe.src : null;
    var btns = Array.from(document.querySelectorAll('button')).map(function(b){return b.textContent.trim();});
    var hasBackBtn = btns.some(function(t){return /Back to Maho settings|Back/i.test(t);});
    var hasHeader = /Chromium settings/i.test(document.body.textContent || '');
    return {iframeSrc: iframeSrc, hasBackBtn: hasBackBtn, hasHeader: hasHeader};
  })()`);

  assert(state!.hasHeader === true, "Chromium settings pane header rendered");
  assert(
    typeof state!.iframeSrc === "string" && state!.iframeSrc.startsWith("chrome://settings"),
    `iframe src points at chrome://settings (got ${state!.iframeSrc})`,
  );
  assert(state!.hasBackBtn === true, "'Back to Maho settings' button present");

  ws.close();
  reportResultsAndExit();
}

await main();
