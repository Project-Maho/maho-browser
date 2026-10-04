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

// Native React <input> value setter so dispatched events fire the controlled
// component's onChange.
const SET_INPUT_HELPER = `
  function __setInput(el, val){
    var setter = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, 'value').set;
    setter.call(el, val);
    el.dispatchEvent(new Event('input', {bubbles: true}));
    el.dispatchEvent(new Event('change', {bubbles: true}));
  }
`;

// A BYOK key value we save then clear. Never printed or read back.
const BYOK_KEY = "sk-e2e-openai-do-not-log";

async function main() {
  await checkRelayOrExit();

  const ws = await getSettingsWs();
  const cdp = new CDP(ws);
  await cdp.send("Runtime.enable");
  await cdp.send("Page.enable");
  await new Promise(r => setTimeout(r, 500));

  step("AI Pane Verification");
  await cdp.navigate(`${CDP_URL_ROOT}maho-ai`, 4000);

  // Count-spy on the handler so we can prove single-commit / invocation without
  // depending on backend side effects. Wraps the real methods and calls through.
  await cdp.eval(`(function(){
    var h = window.settingsStore.getHandler();
    window.__aiSpy = {setAIModel: 0, setBYOK: 0, clearBYOK: 0};
    if (!h.__aiSpyWired) {
      var m = h.setAIModel.bind(h);
      h.setAIModel = function(v){ window.__aiSpy.setAIModel++; return m(v); };
      var s = h.setBYOKKey.bind(h);
      h.setBYOKKey = function(p, k){ window.__aiSpy.setBYOK++; return s(p, k); };
      var c = h.clearBYOKKey.bind(h);
      h.clearBYOKKey = function(p){ window.__aiSpy.clearBYOK++; return c(p); };
      h.__aiSpyWired = true;
    }
    // Auto-accept the Clear confirmation dialog.
    window.confirm = function(){ return true; };
  })()`);

  const initCheck = await cdp.eval<{hasTitle: boolean; hasRadioManaged: boolean; hasRadioByok: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = main ? main.textContent : '';
    var radios = Array.from(document.querySelectorAll('input[type="radio"]'));
    return {
      hasTitle: /Maho AI/i.test(text),
      hasRadioManaged: radios.some(function(r){ return r.nextSibling && r.nextSibling.textContent.includes("Maho Managed"); }),
      hasRadioByok: radios.some(function(r){ return r.nextSibling && r.nextSibling.textContent.includes("Use my own"); })
    };
  })()`);
  assert(initCheck!.hasTitle === true, "AI pane title 'Maho AI' rendered");
  assert(initCheck!.hasRadioManaged === true, "Maho Managed radio option present");
  assert(initCheck!.hasRadioByok === true, "BYOK radio option present");

  step("Switch to BYOK — controls usable without a relay session");
  // The BYOK radio is already checked when provider is any non-managed value
  // (incl. the '(not set)' default), so clicking it fires no onChange. Only
  // click to flip out of managed mode, then choose a concrete provider below.
  await cdp.eval<boolean>(`(function(){
    var radios = Array.from(document.querySelectorAll('input[type="radio"]'));
    var byok = radios.find(function(r){ return r.nextSibling && r.nextSibling.textContent.includes("Use my own"); });
    if (byok && !byok.checked) { byok.click(); return true; }
    return false;
  })()`);
  await new Promise(r => setTimeout(r, 900));

  // The '(not set)' default renders only the provider dropdown; a concrete
  // provider must be selected before the OpenAI BYOK key field exists.
  await cdp.eval(`(function(){
    var trigger = document.querySelector('main button[role="combobox"][aria-label="Provider"]');
    if (trigger) trigger.click();
  })()`);
  await new Promise(r => setTimeout(r, 700));
  await cdp.eval(`(function(){
    var opt = Array.from(document.querySelectorAll('[role="option"]')).find(function(o){ return (o.textContent||'').trim() === 'OpenAI'; });
    if (opt) opt.click();
  })()`);
  await new Promise(r => setTimeout(r, 1400));

  const byokUsable = await cdp.eval<{hasProviderSelect: boolean; providerEnabled: boolean; hasKeyField: boolean; keyEnabled: boolean; hasRelaySession: boolean}>(`(function(){
    var providerSelect = document.querySelector('main button[role="combobox"][aria-label="Provider"]');
    var key = Array.from(document.querySelectorAll('main input')).find(function(i){ return (i.getAttribute('aria-label')||'') === 'OpenAI BYOK Key'; });
    var text = (document.querySelector('main')||{}).textContent || '';
    return {
      hasProviderSelect: !!providerSelect,
      providerEnabled: providerSelect ? !providerSelect.disabled : false,
      hasKeyField: !!key,
      keyEnabled: key ? !key.disabled : false,
      hasRelaySession: /disabled because you are not connected to Sync/i.test(text)
    };
  })()`);
  assert(byokUsable!.hasProviderSelect === true, "BYOK provider selector present");
  assert(byokUsable!.providerEnabled === true, "BYOK provider selector is usable (enabled) with no relay");
  assert(byokUsable!.hasKeyField === true, "OpenAI BYOK key field present in BYOK mode");
  assert(byokUsable!.keyEnabled === true, "OpenAI BYOK key field is usable (enabled) with no relay");

  step("Provider dropdown lists BYOK providers and excludes Google");
  await cdp.eval(`(function(){
    var trigger = document.querySelector('main button[role="combobox"][aria-label="Provider"]');
    if (trigger) trigger.click();
  })()`);
  await new Promise(r => setTimeout(r, 700));
  const providerOptions = await cdp.eval<string[]>(`(function(){
    return Array.from(document.querySelectorAll('[role="option"]')).map(function(o){ return (o.textContent||'').trim(); });
  })()`);
  await cdp.send("Input.dispatchKeyEvent", {type: "keyDown", key: "Escape", code: "Escape", windowsVirtualKeyCode: 27, nativeVirtualKeyCode: 27});
  await cdp.send("Input.dispatchKeyEvent", {type: "keyUp", key: "Escape", code: "Escape", windowsVirtualKeyCode: 27, nativeVirtualKeyCode: 27});
  await new Promise(r => setTimeout(r, 400));

  const optsJoined = (providerOptions ?? []).join(" | ");
  assert(/Anthropic/i.test(optsJoined), "Anthropic option present in provider dropdown");
  assert(/OpenAI/i.test(optsJoined), "OpenAI option present in provider dropdown");
  assert(/OpenAI-compatible/i.test(optsJoined), "OpenAI-compatible option present in provider dropdown");
  assert(/Local Server/i.test(optsJoined), "Local Server option present in provider dropdown");
  assert(!/Google/i.test(optsJoined), "Google provider is excluded from BYOK dropdown");

  step("BYOK key Save then Clear removes the saved-key state");
  await cdp.eval(`(function(){
    ${SET_INPUT_HELPER}
    var key = Array.from(document.querySelectorAll('main input')).find(function(i){ return (i.getAttribute('aria-label')||'') === 'OpenAI BYOK Key'; });
    if (key) __setInput(key, ${JSON.stringify(BYOK_KEY)});
  })()`);
  await new Promise(r => setTimeout(r, 400));
  await cdp.eval(`(function(){
    var key = Array.from(document.querySelectorAll('main input')).find(function(i){ return (i.getAttribute('aria-label')||'') === 'OpenAI BYOK Key'; });
    if (!key) return;
    var row = key.closest('section');
    if (!row) return;
    var save = Array.from(row.querySelectorAll('button')).find(function(b){ return b.textContent.trim() === 'Save'; });
    if (save) save.click();
  })()`);
  await new Promise(r => setTimeout(r, 1800));

  const afterSave = await cdp.eval<{setByokInvoked: boolean; clearVisible: boolean}>(`(function(){
    var key = Array.from(document.querySelectorAll('main input')).find(function(i){ return (i.getAttribute('aria-label')||'') === 'OpenAI BYOK Key'; });
    var clearVisible = false;
    if (key) {
      var row = key.closest('section');
      if (row) clearVisible = Array.from(row.querySelectorAll('button')).some(function(b){ return b.textContent.trim() === 'Clear'; });
    }
    return {setByokInvoked: window.__aiSpy.setBYOK > 0, clearVisible: clearVisible};
  })()`);
  assert(afterSave!.setByokInvoked === true, "setBYOKKey invoked when saving the OpenAI key");
  assert(afterSave!.clearVisible === true, "Clear button appears once a BYOK key is stored");

  await cdp.eval(`(function(){
    var key = Array.from(document.querySelectorAll('main input')).find(function(i){ return (i.getAttribute('aria-label')||'') === 'OpenAI BYOK Key'; });
    if (!key) return;
    var row = key.closest('section');
    if (!row) return;
    var clear = Array.from(row.querySelectorAll('button')).find(function(b){ return b.textContent.trim() === 'Clear'; });
    if (clear) clear.click();
  })()`);
  await new Promise(r => setTimeout(r, 1800));

  const afterClear = await cdp.eval<{clearInvoked: boolean; clearGone: boolean}>(`(function(){
    var key = Array.from(document.querySelectorAll('main input')).find(function(i){ return (i.getAttribute('aria-label')||'') === 'OpenAI BYOK Key'; });
    var clearGone = true;
    if (key) {
      var row = key.closest('section');
      if (row) clearGone = !Array.from(row.querySelectorAll('button')).some(function(b){ return b.textContent.trim() === 'Clear'; });
    }
    return {clearInvoked: window.__aiSpy.clearBYOK === 1, clearGone: clearGone};
  })()`);
  assert(afterClear!.clearInvoked === true, "clearBYOKKey invoked exactly once on Clear");
  assert(afterClear!.clearGone === true, "Clear button removed after clearing the saved-key state");

  step("Explicit model Save commits exactly once");
  // With no stored key, the model list is empty so ModelPicker exposes a custom
  // text input + Save. Reset the spy right before the single Save click.
  await cdp.eval(`(function(){ window.__aiSpy.setAIModel = 0; })()`);
  const hasModelInput = await cdp.eval<boolean>(`(function(){
    ${SET_INPUT_HELPER}
    var el = Array.from(document.querySelectorAll('main input')).find(function(i){ return (i.getAttribute('aria-label')||'') === 'Model Text Input'; });
    if (!el) return false;
    __setInput(el, 'gpt-4o-mini');
    return true;
  })()`);
  assert(hasModelInput === true, "Custom model input available for explicit model entry");
  await new Promise(r => setTimeout(r, 400));
  await cdp.eval(`(function(){
    var el = Array.from(document.querySelectorAll('main input')).find(function(i){ return (i.getAttribute('aria-label')||'') === 'Model Text Input'; });
    if (!el) return;
    var row = el.closest('section') || el.parentElement;
    var save = Array.from((row||document).querySelectorAll('button')).find(function(b){ return b.textContent.trim() === 'Save'; });
    if (save) save.click();
  })()`);
  await new Promise(r => setTimeout(r, 1500));
  const modelCommits = await cdp.eval<number>(`(function(){ return window.__aiSpy.setAIModel; })()`);
  assert(modelCommits === 1, `Explicit model Save commits exactly once (setAIModel calls: ${modelCommits})`);

  step("Approval policy options still hold");
  await cdp.eval(`(function(){
    var trigger = document.querySelector('main button[aria-label="Approval policy"]');
    if (trigger) trigger.click();
  })()`);
  await new Promise(r => setTimeout(r, 700));
  const approvalOptions = await cdp.eval<string[]>(`(function(){
    return Array.from(document.querySelectorAll('[role="option"]')).map(function(o){ return (o.textContent||'').trim(); });
  })()`);
  await cdp.send("Input.dispatchKeyEvent", {type: "keyDown", key: "Escape", code: "Escape", windowsVirtualKeyCode: 27, nativeVirtualKeyCode: 27});
  await cdp.send("Input.dispatchKeyEvent", {type: "keyUp", key: "Escape", code: "Escape", windowsVirtualKeyCode: 27, nativeVirtualKeyCode: 27});
  await new Promise(r => setTimeout(r, 400));
  const approvalJoined = (approvalOptions ?? []).join(" | ");
  assert((approvalOptions ?? []).length === 3, "Approval policy has exactly 3 options");
  assert(/Ask/i.test(approvalJoined), "Ask approval option present");
  assert(/Auto-approve/i.test(approvalJoined), "Auto-approve approval option present");
  assert(/Block/i.test(approvalJoined), "Block approval option present");

  ws.close();
  reportResultsAndExit();
}

await main();
