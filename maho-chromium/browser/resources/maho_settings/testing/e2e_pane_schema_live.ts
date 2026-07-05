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

async function checkSchemaLivePane(
  cdp: CDP,
  paneId: string,
  headerRegex: RegExp,
  paneLabel: string,
): Promise<void> {
  await cdp.navigate(`${CDP_URL_ROOT}${paneId}`, 3500);

  const state = await cdp.eval<{
    hasHeader: boolean;
    hasControls: boolean;
    controlCount: number;
    text: string;
  }>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var switches = main.querySelectorAll('button[role="switch"]').length;
    var selects = main.querySelectorAll('button[role="combobox"]').length;
    var sliders = main.querySelectorAll('input[type="range"]').length;
    var inputs = main.querySelectorAll('input:not([type="range"])').length;
    var controlCount = switches + selects + sliders + inputs;
    return {
      hasHeader: ${headerRegex}.test(text),
      hasControls: controlCount > 0,
      controlCount: controlCount,
      text: text.slice(0, 200)
    };
  })()`);

  assert(state!.hasHeader === true, `${paneLabel}: header rendered`);
  assert(state!.hasControls === true, `${paneLabel}: at least one control rendered (${state!.controlCount} controls)`);
}

async function main() {
  await checkRelayOrExit();

  const ws = await getSettingsWs();
  const cdp = new CDP(ws);
  await cdp.send("Runtime.enable");
  await cdp.send("Page.enable");
  await new Promise((r) => setTimeout(r, 500));

  step("Schema-driven Live Panes Sample Verification");

  await checkSchemaLivePane(cdp, "general", /General/i, "General");
  await checkSchemaLivePane(cdp, "tabs", /Tabs/i, "Tabs");
  await checkSchemaLivePane(cdp, "privacy", /Privacy/i, "Privacy");
  await checkSchemaLivePane(cdp, "notifications", /Notifications/i, "Notifications");
  await checkSchemaLivePane(cdp, "advanced", /Advanced/i, "Advanced");

  ws.close();
  reportResultsAndExit();
}

await main();
