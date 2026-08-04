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

  step("Slider Persist and Queueing Verification");
  await cdp.navigate(`${CDP_URL_ROOT}appearance`, 4000);

  const initialVal = await cdp.eval<string>(`(function(){
    var s = document.querySelector('main input[type="range"]');
    return s ? s.value : '';
  })()`);
  assert(initialVal !== '', `Initial slider value is: ${initialVal}`);

  // Perform multiple fast slider changes to verify queueing
  const targetWidths = [200, 250, 300, 220, 280];
  for (const w of targetWidths) {
    await cdp.eval(`(function(){
      var s = document.querySelector('main input[type="range"]');
      if (!s) return;
      var setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set;
      setter.call(s, '${w}');
      s.dispatchEvent(new Event('input', {bubbles: true}));
      s.dispatchEvent(new Event('change', {bubbles: true}));
    })()`);
    // Minimal wait to simulate fast drag
    await new Promise(r => setTimeout(r, 50));
  }

  // Wait a bit for the debounced commits to finish
  await new Promise(r => setTimeout(r, 2000));

  // Navigate away to general pane
  await cdp.navigate(`${CDP_URL_ROOT}general`, 2000);

  // Navigate back to appearance pane
  await cdp.navigate(`${CDP_URL_ROOT}appearance`, 3000);

  // Verify that the final width is persisted correctly (should match the last target width: 280)
  const finalVal = await cdp.eval<string>(`(function(){
    var s = document.querySelector('main input[type="range"]');
    return s ? s.value : '';
  })()`);

  assert(finalVal === '280', `Slider value correctly persisted on navigation. Expected 280, got ${finalVal}`);

  ws.close();
  reportResultsAndExit();
}

await main();
