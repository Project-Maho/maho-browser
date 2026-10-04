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

  step("Mail Accounts Pane Verification");
  await cdp.navigate(`${CDP_URL_ROOT}mail`, 4000);

  const initial = await cdp.eval<{hasTitle: boolean; hasAddAccountButton: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var hasTitle = /Connected accounts/i.test(text);
    var btns = Array.from(main.querySelectorAll('button'));
    var addBtn = btns.find(b => b.textContent.trim() === 'Add account');
    return {
      hasTitle: hasTitle,
      hasAddAccountButton: !!addBtn
    };
  })()`);

  assert(initial!.hasTitle === true, "Connected accounts section title rendered");
  assert(initial!.hasAddAccountButton === true, "Add account button present");

  // Click Add account button
  const pickerOpened = await cdp.eval<boolean>(`(function(){
    var btns = Array.from(document.querySelectorAll('main button'));
    var addBtn = btns.find(b => b.textContent.trim() === 'Add account');
    if (!addBtn) return false;
    addBtn.click();
    return true;
  })()`);

  assert(pickerOpened === true, "Clicked Add account button");
  await new Promise(r => setTimeout(r, 1000));

  // Verify picker choices are visible
  const pickerVisible = await cdp.eval<{hasGmail: boolean; hasOutlook: boolean; hasImap: boolean; hasCancel: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var buttons = Array.from(main.querySelectorAll('button'));
    var cancelBtn = buttons.find(b => b.textContent.trim() === 'Cancel');
    return {
      hasGmail: /Gmail/i.test(text),
      hasOutlook: /Outlook/i.test(text),
      hasImap: /IMAP/i.test(text),
      hasCancel: !!cancelBtn
    };
  })()`);

  assert(pickerVisible!.hasGmail === true, "Gmail provider option visible");
  assert(pickerVisible!.hasOutlook === true, "Outlook provider option visible");
  assert(pickerVisible!.hasImap === true, "IMAP provider option visible");
  assert(pickerVisible!.hasCancel === true, "Cancel button visible in picker");

  // Click Cancel to return to list
  const canceled = await cdp.eval<boolean>(`(function(){
    var buttons = Array.from(document.querySelectorAll('main button'));
    var cancelBtn = buttons.find(b => b.textContent.trim() === 'Cancel');
    if (!cancelBtn) return false;
    cancelBtn.click();
    return true;
  })()`);

  assert(canceled === true, "Clicked Cancel button to return to list");
  await new Promise(r => setTimeout(r, 1000));

  const backToList = await cdp.eval<{hasAddAccountButton: boolean}>(`(function(){
    var main = document.querySelector('main');
    var btns = Array.from(main.querySelectorAll('button'));
    var addBtn = btns.find(b => b.textContent.trim() === 'Add account');
    return {
      hasAddAccountButton: !!addBtn
    };
  })()`);

  assert(backToList!.hasAddAccountButton === true, "Successfully returned to mail accounts list view");

  step("OAuth Cancel/Cleanup Verification");
  // Click Add account again to enter picker
  await cdp.eval(`(function(){
    var btns = Array.from(document.querySelectorAll('main button'));
    var addBtn = btns.find(b => b.textContent.trim() === 'Add account');
    if (addBtn) addBtn.click();
  })()`);
  await new Promise(r => setTimeout(r, 1000));

  // Click Gmail to trigger OAuth start
  const clickedGmail = await cdp.eval<boolean>(`(function(){
    var buttons = Array.from(document.querySelectorAll('main button'));
    var gmailBtn = buttons.find(b => /Gmail/i.test(b.textContent));
    if (!gmailBtn) return false;
    gmailBtn.click();
    return true;
  })()`);
  assert(clickedGmail === true, "Triggered OAuth flow for Gmail");
  await new Promise(r => setTimeout(r, 2000));

  // Verify we are waiting for authorization
  const waitingText = await cdp.eval<boolean>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\s+/g, ' ');
    return /Waiting for authorization/i.test(text);
  })()`);
  assert(waitingText === true, "Waiting for authorization state displayed");

  // Click Cancel to cancel active OAuth
  const clickedCancel = await cdp.eval<boolean>(`(function(){
    var buttons = Array.from(document.querySelectorAll('main button'));
    var cancelBtn = buttons.find(b => b.textContent.trim() === 'Cancel');
    if (!cancelBtn) return false;
    cancelBtn.click();
    return true;
  })()`);
  assert(clickedCancel === true, "Clicked Cancel button to abort OAuth");
  await new Promise(r => setTimeout(r, 2000));

  // Verify we are back on the list screen and no error is shown
  const backToListNoFeedback = await cdp.eval<{hasAddAccountButton: boolean; hasError: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\s+/g, ' ');
    var buttons = Array.from(main.querySelectorAll('button'));
    var addBtn = buttons.find(b => b.textContent.trim() === 'Add account');
    var hasError = /timed out|error/i.test(text);
    return {
      hasAddAccountButton: !!addBtn,
      hasError: hasError
    };
  })()`);
  assert(backToListNoFeedback!.hasAddAccountButton === true, "Returned to accounts list view after cancel");
  assert(backToListNoFeedback!.hasError === false, "No error feedback displayed after cancellation");

  ws.close();
  reportResultsAndExit();
}

await main();
