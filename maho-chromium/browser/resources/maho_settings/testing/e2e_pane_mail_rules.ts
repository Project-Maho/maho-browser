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

  step("Mail Rules Pane Verification");
  await cdp.navigate(`${CDP_URL_ROOT}mail-rules`, 4000);

  const initial = await cdp.eval<{hasTitle: boolean; settled: boolean; hasContent: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var hasTitle = /Mail rules & filters/i.test(text);
    var settled = !/Loading rules\\.\\.\\./i.test(text);
    var hasContent = /Mail Filters & Rules|No rules found|Failed to load rules/i.test(text);
    return {
      hasTitle: hasTitle,
      settled: settled,
      hasContent: hasContent
    };
  })()`);

  assert(initial!.hasTitle === true, "Rules pane title 'Mail rules & filters' rendered");
  assert(initial!.settled === true, "Rules pane resolved past the loading state");
  assert(initial!.hasContent === true, "Rules content, empty-state, or inline error rendered");

  step("Open then Cancel the rule editor");
  const editorOpened = await cdp.eval<boolean>(`(function(){
    var btns = Array.from(document.querySelectorAll('main button'));
    var addBtn = btns.find(b => b.textContent.trim() === 'Add Rule');
    if (!addBtn) return false;
    addBtn.click();
    return true;
  })()`);
  assert(editorOpened === true, "Clicked Add Rule button");
  await new Promise(r => setTimeout(r, 1000));

  const editorVisible = await cdp.eval<{hasEditor: boolean; hasSave: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var buttons = Array.from(main.querySelectorAll('button'));
    var saveBtn = buttons.find(b => b.textContent.trim() === 'Save');
    return {
      hasEditor: /New Rule|Rule Name/i.test(text),
      hasSave: !!saveBtn
    };
  })()`);
  assert(editorVisible!.hasEditor === true, "Rule editor form is visible");
  assert(editorVisible!.hasSave === true, "Save button present in rule editor");

  const canceled = await cdp.eval<boolean>(`(function(){
    var buttons = Array.from(document.querySelectorAll('main button'));
    var cancelBtn = buttons.find(b => b.textContent.trim() === 'Cancel');
    if (!cancelBtn) return false;
    cancelBtn.click();
    return true;
  })()`);
  assert(canceled === true, "Clicked Cancel button to close the editor");
  await new Promise(r => setTimeout(r, 1000));

  const backToList = await cdp.eval<{hasAddButton: boolean; editorGone: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var btns = Array.from(main.querySelectorAll('button'));
    var addBtn = btns.find(b => b.textContent.trim() === 'Add Rule');
    return {
      hasAddButton: !!addBtn,
      editorGone: !/New Rule/i.test(text)
    };
  })()`);
  assert(backToList!.hasAddButton === true, "Returned to rules list view after cancel");
  assert(backToList!.editorGone === true, "Rule editor closed after cancel");

  ws.close();
  reportResultsAndExit();
}

await main();
