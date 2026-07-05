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

  step("AI Pane Verification");
  await cdp.navigate(`${CDP_URL_ROOT}maho-ai`, 4000);

  const ai = await cdp.eval<{hasTitle: boolean; hasProviderSelect: boolean; hasBaseUrlField: boolean; hasApiKeyField: boolean; text: string}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var hasTitle = /Maho AI configuration/i.test(text);
    
    // Select elements and inputs
    var select = main.querySelector('button[role="combobox"]');
    var inputs = Array.from(main.querySelectorAll('input'));
    var hasBaseUrl = inputs.some(i => i.placeholder && i.placeholder.includes("provider default"));
    var hasApiKey = inputs.some(i => i.placeholder && i.placeholder.includes("API key"));
    
    return {
      hasTitle: hasTitle,
      hasProviderSelect: !!select,
      hasBaseUrlField: hasBaseUrl,
      hasApiKeyField: hasApiKey,
      text: text.slice(0, 300)
    };
  })()`);

  assert(ai!.hasTitle === true, "AI pane title 'Maho AI configuration' rendered");
  assert(ai!.hasProviderSelect === true, "AI Provider select dropdown rendered");
  assert(ai!.hasBaseUrlField === true, "AI Base URL field is present");
  assert(ai!.hasApiKeyField === true, "AI API key field is present");
  assert(ai!.text.length > 20, "AI configuration pane successfully loaded");

  // Open the select trigger
  const changedProvider = await cdp.eval<boolean>(`(function(){
    var trigger = document.querySelector('button[role="combobox"]');
    if (!trigger) return false;
    trigger.click();
    return true;
  })()`);
  assert(changedProvider === true, "Clicked AI Provider SelectTrigger");
  await new Promise(r => setTimeout(r, 1500));

  // Click Google option (with detailed debugging)
  const optionResult = await cdp.eval<{success: boolean; debugInfo: string}>(`(function(){
    var options = Array.from(document.querySelectorAll('[role="option"]'));
    var googleOpt = options.find(o => o.textContent && o.textContent.trim().indexOf("Google") >= 0);
    if (!googleOpt) {
      // Find all divs or spans that might be options
      var allDivs = Array.from(document.querySelectorAll('div, span, button'));
      var texts = allDivs.map(d => d.textContent ? d.textContent.trim() : "").filter(t => t.length > 0 && t.length < 50);
      return { success: false, debugInfo: "Found options count: " + options.length + ", general text count: " + allDivs.length + " Sample texts: " + texts.slice(0, 20).join(" | ") };
    }
    googleOpt.click();
    return { success: true, debugInfo: "Clicked" };
  })()`);
  assert(optionResult!.success === true, `Clicked 'Google (Gemini)' option in dropdown (Debug: ${optionResult?.debugInfo})`);
  await new Promise(r => setTimeout(r, 2000));

  // Verify dropdown text
  const verifyProvider = await cdp.eval<string>(`(function(){
    var trigger = document.querySelector('button[role="combobox"]');
    return trigger ? trigger.textContent.trim() : '';
  })()`);
  assert(verifyProvider.indexOf("Google") >= 0, `Dropdown reflects Google provider successfully (value is: '${verifyProvider}')`);

  ws.close();
  reportResultsAndExit();
}

await main();
