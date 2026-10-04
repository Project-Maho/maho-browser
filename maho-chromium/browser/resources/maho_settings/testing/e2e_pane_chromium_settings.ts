#!/usr/bin/env bun
import {
  CDP,
  assert,
  step,
  getSettingsWs,
  checkRelayOrExit,
  clickElementByText,
  reportResultsAndExit,
  waitForPageTargetUrl,
  CDP_URL_ROOT,
} from "./cdp_harness";

async function main() {
  await checkRelayOrExit();

  const ws = await getSettingsWs();
  const cdp = new CDP(ws);
  await cdp.send("Runtime.enable");
  await cdp.send("Page.enable");

  step("Chromium Settings Host Surface Verification");
  await cdp.navigate(`${CDP_URL_ROOT}chromium-settings`);

  const state = await cdp.eval<{
    hasBackBtn: boolean;
    hasHeader: boolean;
    hasOpenAction: boolean;
    iframeCount: number;
    limitation: boolean;
  }>(`(function(){
    var btns = Array.from(document.querySelectorAll('button')).map(function(b){return b.textContent.trim();});
    return {
      hasBackBtn: btns.some(function(t){return /Back to Maho settings|Back/i.test(t);}),
      hasHeader: /Current browser profile settings/i.test(document.body.textContent || ''),
      hasOpenAction: btns.includes('Open Chromium settings'),
      iframeCount: document.querySelectorAll('iframe').length,
      limitation: !!document.querySelector('[data-selected-profile-limitation="true"]'),
    };
  })()`);

  assert(state?.hasHeader === true, "Host-profile Chromium settings surface rendered");
  assert(state?.hasOpenAction === true, "Typed Chromium settings action rendered");
  assert(state?.iframeCount === 0, "Unsupported chrome://settings iframe is absent");
  assert(state?.limitation === false, "Host profile is not shown the non-host limitation");
  assert(state?.hasBackBtn === true, "'Back to Maho settings' button present");

  const settingsTarget = await waitForPageTargetUrl(
    "chrome://settings",
    async () => {
      const clicked = await clickElementByText(cdp, "button", "Open Chromium settings");
      if (!clicked) throw new Error("Open Chromium settings action was not clickable");
    },
    10_000,
  );
  assert(
    settingsTarget.type === "page" && settingsTarget.url.startsWith("chrome://settings"),
    `browser-owned Chromium settings page opened (got ${settingsTarget.type} ${settingsTarget.url})`,
  );

  ws.close();
  reportResultsAndExit();
}

await main();
