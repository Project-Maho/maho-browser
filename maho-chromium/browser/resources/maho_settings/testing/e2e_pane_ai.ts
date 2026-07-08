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

  // 1. Verify title and default Managed Mode
  const initCheck = await cdp.eval<{hasTitle: boolean; hasRadioManaged: boolean; hasRadioByok: boolean; hasTestBtn: boolean; byokVisible: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = main ? main.textContent : '';
    var hasTitle = /Maho AI configuration/i.test(text);

    var radios = Array.from(document.querySelectorAll('input[type="radio"]'));
    var hasRadioManaged = radios.some(r => r.nextSibling && r.nextSibling.textContent.includes("Maho Managed"));
    var hasRadioByok = radios.some(r => r.nextSibling && r.nextSibling.textContent.includes("Use my own"));

    var testBtn = Array.from(document.querySelectorAll('button')).find(b => b.textContent && b.textContent.includes("Test connection"));

    // BYOK elements shouldn't be visible by default (if managed is checked)
    var select = document.querySelector('button[role="combobox"]');

    return {
      hasTitle: hasTitle,
      hasRadioManaged: hasRadioManaged,
      hasRadioByok: hasRadioByok,
      hasTestBtn: !!testBtn,
      byokVisible: !!select
    };
  })()`);

  assert(initCheck!.hasTitle === true, "AI pane title 'Maho AI configuration' rendered");
  assert(initCheck!.hasRadioManaged === true, "Maho Managed radio option present");
  assert(initCheck!.hasRadioByok === true, "BYOK radio option present");
  assert(initCheck!.hasTestBtn === true, "Test connection button present in Managed mode");

  // 2. Switch to BYOK mode
  step("Switching to BYOK (Bring Your Own Key) Mode");
  const switchSuccess = await cdp.eval<boolean>(`(function(){
    var radios = Array.from(document.querySelectorAll('input[type="radio"]'));
    var byokRadio = radios.find(r => r.nextSibling && r.nextSibling.textContent.includes("Use my own"));
    if (byokRadio) {
      byokRadio.click();
      return true;
    }
    return false;
  })()`);
  assert(switchSuccess === true, "Clicked 'Use my own API key' radio");
  await new Promise(r => setTimeout(r, 1000));

  // 3. Verify BYOK dropdown provider select is now visible
  const byokCheck = await cdp.eval<{hasSelect: boolean}>(`(function(){
    var select = document.querySelector('button[role="combobox"]');
    return { hasSelect: !!select };
  })()`);
  assert(byokCheck!.hasSelect === true, "Provider dropdown select combobox visible in BYOK mode");

  // Click provider dropdown trigger to open options
  await cdp.eval(`(function(){
    var trigger = document.querySelector('button[role="combobox"]');
    if (trigger) trigger.click();
  })()`);
  await new Promise(r => setTimeout(r, 600));

  // Verify Google option is NOT present in the dropdown
  const hasGoogle = await cdp.eval<boolean>(`(function(){
    var options = Array.from(document.querySelectorAll('[role="option"]'));
    return options.some(o => o.textContent && o.textContent.includes("Google"));
  })()`);
  assert(hasGoogle === false, "Google provider option is excluded from BYOK dropdown");

  // Close the dropdown trigger
  await cdp.eval(`(function(){
    var trigger = document.querySelector('button[role="combobox"]');
    if (trigger) trigger.click();
  })()`);
  await new Promise(r => setTimeout(r, 600));

  // Helper function to select provider in combobox
  async function selectProvider(providerLabelSubstr: string): Promise<boolean> {
    // Click trigger
    await cdp.eval(`(function(){
      var trigger = document.querySelector('button[role="combobox"]');
      if (trigger) trigger.click();
    })()`);
    await new Promise(r => setTimeout(r, 600));

    // Click option
    const clicked = await cdp.eval<boolean>(`(function(){
      var options = Array.from(document.querySelectorAll('[role="option"]'));
      var opt = options.find(o => o.textContent && o.textContent.includes("${providerLabelSubstr}"));
      if (opt) {
        opt.click();
        return true;
      }
      return false;
    })()`);
    await new Promise(r => setTimeout(r, 1000));
    return clicked;
  }

  // 4. Select OpenAI provider and verify key field visibility
  step("Selecting OpenAI provider");
  const openAiSelected = await selectProvider("OpenAI");
  assert(openAiSelected === true, "Selected OpenAI provider");
  const openAiFields = await cdp.eval<{hasKeyField: boolean; hasUrlField: boolean}>(`(function(){
    var labels = Array.from(document.querySelectorAll('label'));
    var hasKey = labels.some(l => l.textContent.includes("OpenAI BYOK Key"));
    var hasUrl = labels.some(l => l.textContent.includes("Base URL"));
    return { hasKeyField: hasKey, hasUrlField: hasUrl };
  })()`);
  assert(openAiFields!.hasKeyField === true, "OpenAI BYOK Key input field is visible");
  assert(openAiFields!.hasUrlField === false, "Base URL field is hidden for OpenAI provider");

  // 5. Select Anthropic provider and verify key field visibility
  step("Selecting Anthropic provider");
  const anthropicSelected = await selectProvider("Anthropic");
  assert(anthropicSelected === true, "Selected Anthropic provider");
  const anthropicFields = await cdp.eval<{hasKeyField: boolean; hasUrlField: boolean}>(`(function(){
    var labels = Array.from(document.querySelectorAll('label'));
    var hasKey = labels.some(l => l.textContent.includes("Anthropic BYOK Key"));
    var hasUrl = labels.some(l => l.textContent.includes("Base URL"));
    return { hasKeyField: hasKey, hasUrlField: hasUrl };
  })()`);
  assert(anthropicFields!.hasKeyField === true, "Anthropic BYOK Key input field is visible");
  assert(anthropicFields!.hasUrlField === false, "Base URL field is hidden for Anthropic provider");

  // 6. Select OpenAI-compatible provider and verify fields
  step("Selecting OpenAI-compatible provider");
  const compatibleSelected = await selectProvider("OpenAI-compatible");
  assert(compatibleSelected === true, "Selected OpenAI-compatible provider");
  const compatibleFields = await cdp.eval<{hasKeyField: boolean; hasUrlField: boolean}>(`(function(){
    var labels = Array.from(document.querySelectorAll('label'));
    var hasKey = labels.some(l => l.textContent.includes("API key"));
    var hasUrl = labels.some(l => l.textContent.includes("Base URL"));
    return { hasKeyField: hasKey, hasUrlField: hasUrl };
  })()`);
  assert(compatibleFields!.hasKeyField === true, "API key field is visible for OpenAI-compatible provider");
  assert(compatibleFields!.hasUrlField === true, "Base URL field is visible for OpenAI-compatible provider");

  // 7. Select Local Server (Ollama) provider and verify fields
  step("Selecting Local Server (Ollama) provider");
  const ollamaSelected = await selectProvider("Local Server");
  assert(ollamaSelected === true, "Selected Local Server (Ollama) provider");
  const ollamaFields = await cdp.eval<{hasKeyField: boolean}>(`(function(){
    var labels = Array.from(document.querySelectorAll('label'));
    var hasKey = labels.some(l => l.textContent.includes("API key") || l.textContent.includes("BYOK Key"));
    return { hasKeyField: hasKey };
  })()`);
  assert(ollamaFields!.hasKeyField === false, "API Key field is hidden for Local Server (Ollama)");

  // 7b. Verify approval-policy selection options (Ask, Auto, Block)
  step("Verifying Approval Policy options");
  await cdp.eval(`(function(){
    var trigger = document.querySelector('button[aria-label=\"Approval policy\"]');
    if (trigger) trigger.click();
  })()`);
  await new Promise(r => setTimeout(r, 600));

  const approvalPolicyOptions = await cdp.eval<string[]>(`(function(){
    var options = Array.from(document.querySelectorAll('[role=\"option\"]'));
    return options.map(o => o.textContent ? o.textContent.trim() : \"\");
  })()`);

  // Close the dropdown trigger
  await cdp.eval(`(function(){
    var trigger = document.querySelector('button[aria-label=\"Approval policy\"]');
    if (trigger) trigger.click();
  })()`);
  await new Promise(r => setTimeout(r, 600));

  assert(approvalPolicyOptions.length === 3, "Approval policy has exactly 3 options");
  assert(approvalPolicyOptions.some(t => t.includes("Ask")), "Ask option present");
  assert(approvalPolicyOptions.some(t => t.includes("Auto-approve")), "Auto-approve option present");
  assert(approvalPolicyOptions.some(t => t.includes("Block")), "Block option present");

  // 7c. Verify Max Model picker is hidden by default (free/pro tier)
  step("Verifying Max-tier model picker visibility");
  const hasMaxModelDefault = await cdp.eval<boolean>(`(function(){
    var select = document.querySelector('button[aria-label=\"Max Model\"]');
    return !!select;
  })()`);
  assert(hasMaxModelDefault === false, "Max-tier model picker is hidden for free tier");

  // 8. Switch back to Managed Mode and run connection test
  step("Switching back to Managed Mode");
  const switchBack = await cdp.eval<boolean>(`(function(){
    var radios = Array.from(document.querySelectorAll('input[type="radio"]'));
    var managedRadio = radios.find(r => r.nextSibling && r.nextSibling.textContent.includes("Maho Managed"));
    if (managedRadio) {
      managedRadio.click();
      return true;
    }
    return false;
  })()`);
  assert(switchBack === true, "Switched back to Maho Managed Mode");
  await new Promise(r => setTimeout(r, 1000));

  step("Running Test Connection");
  const clickTest = await cdp.eval<boolean>(`(function(){
    var testBtn = Array.from(document.querySelectorAll('button')).find(b => b.textContent && b.textContent.includes("Test connection"));
    if (testBtn) {
      testBtn.click();
      return true;
    }
    return false;
  })()`);
  assert(clickTest === true, "Clicked 'Test connection' button");

  // Wait for result message to appear (max 5s)
  let testResultMessage = "";
  for (let i = 0; i < 10; i++) {
    await new Promise(r => setTimeout(r, 500));
    testResultMessage = await cdp.eval<string>(`(function(){
      var spans = Array.from(document.querySelectorAll('span'));
      var resultSpan = spans.find(s => s.className && (s.className.includes("text-emerald") || s.className.includes("text-destructive")));
      return resultSpan ? resultSpan.textContent.trim() : "";
    })()`);
    if (testResultMessage) break;
  }
  assert(testResultMessage.length > 0, `Connection test result output displayed: "${testResultMessage}"`);

  ws.close();
  reportResultsAndExit();
}

await main();
