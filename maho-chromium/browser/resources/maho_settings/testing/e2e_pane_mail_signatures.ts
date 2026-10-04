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

  step("Mail Signatures Pane Verification");
  await cdp.navigate(`${CDP_URL_ROOT}mail-signatures`, 4000);

  const initial = await cdp.eval<{hasTitle: boolean; settled: boolean; hasContent: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var hasTitle = /Email signatures/i.test(text);
    var settled = !/Loading signatures\\.\\.\\./i.test(text);
    var hasContent = /Email Signatures|No signatures found|Failed to load signatures/i.test(text);
    return {
      hasTitle: hasTitle,
      settled: settled,
      hasContent: hasContent
    };
  })()`);

  assert(initial!.hasTitle === true, "Signatures pane title 'Email signatures' rendered");
  assert(initial!.settled === true, "Signatures pane resolved past the loading state");
  assert(initial!.hasContent === true, "Signatures content, empty-state, or inline error rendered");

  step("Open then Cancel the signature editor");
  const editorOpened = await cdp.eval<boolean>(`(function(){
    var btns = Array.from(document.querySelectorAll('main button'));
    var addBtn = btns.find(b => b.textContent.trim() === 'Add Signature');
    if (!addBtn) return false;
    addBtn.click();
    return true;
  })()`);
  assert(editorOpened === true, "Clicked Add Signature button");
  await new Promise(r => setTimeout(r, 1000));

  const editorVisible = await cdp.eval<{hasEditor: boolean; hasSave: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var buttons = Array.from(main.querySelectorAll('button'));
    var saveBtn = buttons.find(b => b.textContent.trim() === 'Save');
    return {
      hasEditor: /New Signature|Signature Name/i.test(text),
      hasSave: !!saveBtn
    };
  })()`);
  assert(editorVisible!.hasEditor === true, "Signature editor form is visible");
  assert(editorVisible!.hasSave === true, "Save button present in signature editor");

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
    var addBtn = btns.find(b => b.textContent.trim() === 'Add Signature');
    return {
      hasAddButton: !!addBtn,
      editorGone: !/New Signature/i.test(text)
    };
  })()`);
  assert(backToList!.hasAddButton === true, "Returned to signatures list view after cancel");
  assert(backToList!.editorGone === true, "Signature editor closed after cancel");

  ws.close();
  reportResultsAndExit();
}

await main();
