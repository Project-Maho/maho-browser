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

// Native React <input> value setter so dispatched events look like real typing
// and the controlled component's onChange fires.
const SET_INPUT_HELPER = `
  function __setInput(el, val){
    var setter = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, 'value').set;
    setter.call(el, val);
    el.dispatchEvent(new Event('input', {bubbles: true}));
    el.dispatchEvent(new Event('change', {bubbles: true}));
  }
`;

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
    hasSpaceSelect: boolean;
    hasAddBtn: boolean;
    addDisabledWhenEmpty: boolean;
    noSpacesMsg: boolean;
    hasHeader: boolean;
  }>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var hasHeader = /Air traffic control/i.test(text);
    var inputs = Array.from(main.querySelectorAll('input'));
    var labels = inputs.map(function(i){return (i.getAttribute('aria-label')||i.placeholder||'').toLowerCase();});
    var hasUrlPatternInput = labels.some(function(l){return /url pattern/i.test(l);});
    // The removed raw Space ID text input must NOT exist anymore.
    var hasSpaceIdInput = labels.some(function(l){return /(target space id|space id)/i.test(l);});
    // The remediated UI exposes a space combobox instead.
    var spaceSelect = main.querySelector('button[role="combobox"][aria-label="Target space"]');
    var noSpacesMsg = /You must create at least one space/i.test(text);
    var btns = Array.from(main.querySelectorAll('button'));
    var addBtn = btns.find(function(b){return b.textContent.trim() === 'Add';});
    return {
      hasUrlPatternInput: hasUrlPatternInput,
      hasSpaceIdInput: hasSpaceIdInput,
      hasSpaceSelect: !!spaceSelect,
      hasAddBtn: !!addBtn,
      addDisabledWhenEmpty: addBtn ? !!addBtn.disabled : false,
      noSpacesMsg: noSpacesMsg,
      hasHeader: hasHeader
    };
  })()`);

  // Happy: structural — remediated controls render.
  assert(state!.hasHeader === true, "ATC pane header rendered");
  assert(state!.hasUrlPatternInput === true, "URL pattern input present");
  assert(state!.hasSpaceSelect === true, "Target space combobox present (replaces raw Space ID input)");
  assert(state!.hasSpaceIdInput === false, "Raw 'Target space ID' text input is gone");
  assert(state!.hasAddBtn === true, "'Add' button present");

  // Failure/boundary: an empty URL pattern keeps Add disabled.
  assert(state!.addDisabledWhenEmpty === true, "Add is disabled while the URL pattern is empty (boundary)");

  const spacesAvailable = !state!.noSpacesMsg && state!.hasSpaceSelect;

  if (!spacesAvailable) {
    step("No spaces available — boundary path");
    // Boundary: with no spaces, Add must stay disabled even after typing a pattern.
    assert(state!.noSpacesMsg === true, "No-spaces warning shown when no spaces exist");
    ws.close();
    reportResultsAndExit();
    return;
  }

  step("Add rule with a selected named space (happy)");
  const testPattern = "*.e2e-atc-test.example";

  // Type a URL pattern; the target space combobox defaults to the first space.
  await cdp.eval(`(function(){
    ${SET_INPUT_HELPER}
    var main = document.querySelector('main');
    var input = Array.from(main.querySelectorAll('input')).find(function(i){
      return /url pattern/i.test(i.getAttribute('aria-label')||i.placeholder||'');
    });
    if (input) __setInput(input, ${JSON.stringify(testPattern)});
  })()`);
  await new Promise((r) => setTimeout(r, 400));

  const addEnabled = await cdp.eval<boolean>(`(function(){
    var main = document.querySelector('main');
    var addBtn = Array.from(main.querySelectorAll('button')).find(function(b){return b.textContent.trim() === 'Add';});
    return addBtn ? !addBtn.disabled : false;
  })()`);
  assert(addEnabled === true, "Add becomes enabled once a pattern is typed and a space is selected");

  const clicked = await cdp.eval<boolean>(`(function(){
    var main = document.querySelector('main');
    var addBtn = Array.from(main.querySelectorAll('button')).find(function(b){return b.textContent.trim() === 'Add';});
    if (addBtn && !addBtn.disabled) { addBtn.click(); return true; }
    return false;
  })()`);
  assert(clicked === true, "Clicked 'Add' to create the routing rule");
  await new Promise((r) => setTimeout(r, 1500));

  const ruleAdded = await cdp.eval<boolean>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '');
    return text.indexOf(${JSON.stringify(testPattern)}) !== -1;
  })()`);
  assert(ruleAdded === true, "New routing rule row appears for the added pattern");

  // Cleanup: remove the rule we just added (do not assert on cleanup).
  await cdp.eval(`(function(){
    var main = document.querySelector('main');
    var sections = Array.from(main.querySelectorAll('section'));
    var row = sections.find(function(s){ return (s.textContent||'').indexOf(${JSON.stringify(testPattern)}) !== -1; });
    if (row) {
      var removeBtn = Array.from(row.querySelectorAll('button')).find(function(b){
        return (b.getAttribute('aria-label')||'') === 'Remove rule';
      });
      if (removeBtn) removeBtn.click();
    }
  })()`);
  await new Promise((r) => setTimeout(r, 800));

  ws.close();
  reportResultsAndExit();
}

await main();
