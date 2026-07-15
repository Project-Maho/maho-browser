#!/usr/bin/env bun
import {
  CDP,
  assert,
  step,
  getSettingsWs,
  checkRelayOrExit,
  checkBrowserOrExit,
  reportResultsAndExit,
  CDP_URL_ROOT
} from "./cdp_harness";

async function main() {
  await checkRelayOrExit();
  await checkBrowserOrExit();

  const ws = await getSettingsWs();
  const cdp = new CDP(ws);
  await cdp.send("Runtime.enable");
  await cdp.send("Page.enable");
  await new Promise(r => setTimeout(r, 500));

  step("Navigate to Shortcuts Pane");
  await cdp.navigate(`${CDP_URL_ROOT}shortcuts`, 4000);

  // Assert shortcut rows load (at least 30+)
  const rowCount = await cdp.eval<number>(`(function(){
    var sections = document.querySelectorAll('[id^="shortcuts-section-content-"]');
    var total = 0;
    sections.forEach(function(s){ total += s.querySelectorAll('.cursor-pointer').length; });
    return total;
  })()`);
  assert((rowCount ?? 0) >= 30, `Shortcut rows loaded: ${rowCount} (expected >= 30)`);

  step("Select Navigation Category Row");
  // Click first row in the Navigation section
  const selectRow = await cdp.eval<{clicked: boolean; label: string}>(`(function(){
    var navSection = document.getElementById('shortcuts-section-content-navigation');
    if (!navSection) return {clicked: false, label: ''};
    var rows = navSection.querySelectorAll('.cursor-pointer');
    if (rows.length > 0) {
      var label = rows[0].querySelector('.text-sm.font-medium');
      rows[0].click();
      return {clicked: true, label: label ? label.textContent : ''};
    }
    return {clicked: false, label: ''};
  })()`);
  assert(selectRow!.clicked === true, `Clicked first Navigation row: "${selectRow!.label}"`);
  await new Promise(r => setTimeout(r, 500));

  // Assert detail panel updates showing the shortcut's info
  const detailPanel = await cdp.eval<{hasTitle: boolean; hasAction: boolean; title: string}>(`(function(){
    var cards = document.querySelectorAll('.sticky.top-4');
    if (cards.length === 0) return {hasTitle: false, hasAction: false, title: ''};
    var card = cards[0];
    var titleEl = card.querySelector('.text-base');
    var actionEl = card.querySelector('.font-mono.text-xs');
    return {
      hasTitle: !!titleEl && titleEl.textContent.length > 0,
      hasAction: !!actionEl && actionEl.textContent.includes('navigate_'),
      title: titleEl ? titleEl.textContent : ''
    };
  })()`);
  assert(detailPanel!.hasTitle === true, `Detail panel shows title: "${detailPanel!.title}"`);
  assert(detailPanel!.hasAction === true, "Detail panel shows action ID for navigation shortcut");

  step("Open Recording Dialog");
  // Click the Edit button in detail panel
  const clickEdit = await cdp.eval<boolean>(`(function(){
    var card = document.querySelector('.sticky.top-4');
    if (!card) return false;
    var btns = Array.from(card.querySelectorAll('button'));
    var editBtn = btns.find(function(b){ return b.textContent.trim() === 'Edit'; });
    if (editBtn) { editBtn.click(); return true; }
    return false;
  })()`);
  assert(clickEdit === true, "Clicked 'Edit' button in detail panel");
  await new Promise(r => setTimeout(r, 800));

  // Assert recording dialog opens
  const dialogOpen = await cdp.eval<{isOpen: boolean; hasPrompt: boolean}>(`(function(){
    var dialog = document.querySelector('[role="dialog"]');
    if (!dialog) return {isOpen: false, hasPrompt: false};
    var text = dialog.textContent || '';
    return {
      isOpen: true,
      hasPrompt: text.includes('Record Shortcut') || text.includes('Press your key combination')
    };
  })()`);
  assert(dialogOpen!.isOpen === true, "Recording dialog is open");
  assert(dialogOpen!.hasPrompt === true, "Recording dialog shows 'Record Shortcut' prompt");

  step("Record ⌘⇧K through the real interception path");
  // The store exposes no public recording test hook, so drive the real browser
  // interceptor: the open dialog sets recording mode and real CDP key input
  // fires onShortcutRecorded. Do NOT re-add a synthetic .listener_ call.
  await cdp.send("Input.dispatchKeyEvent", {
    type: "rawKeyDown",
    modifiers: 8 + 1, // meta(8) + shift(1) = 9 on macOS
    key: "K",
    code: "KeyK",
    windowsVirtualKeyCode: 75,
    nativeVirtualKeyCode: 75
  });
  await new Promise(r => setTimeout(r, 200));
  await cdp.send("Input.dispatchKeyEvent", {
    type: "keyUp",
    modifiers: 8 + 1,
    key: "K",
    code: "KeyK",
    windowsVirtualKeyCode: 75,
    nativeVirtualKeyCode: 75
  });
  await new Promise(r => setTimeout(r, 900));

  const comboRecorded = await cdp.eval<string>(`(function(){
    var dialog = document.querySelector('[role="dialog"]');
    if (!dialog) return '';
    var comboEl = dialog.querySelector('.font-mono.text-2xl');
    return comboEl ? comboEl.textContent.trim() : '';
  })()`);

  const comboShown = comboRecorded !== '' && comboRecorded !== 'None';
  assert(comboShown === true, `Recording dialog combo display: "${comboRecorded}"`);

  step("Save or Cancel Recording");
  // If a combo was recorded, click Save; otherwise Cancel
  if (comboShown) {
    const clickSave = await cdp.eval<boolean>(`(function(){
      var dialog = document.querySelector('[role="dialog"]');
      if (!dialog) return false;
      var btns = Array.from(dialog.querySelectorAll('button'));
      var saveBtn = btns.find(function(b){ return b.textContent.trim() === 'Save'; });
      if (saveBtn) { saveBtn.click(); return true; }
      return false;
    })()`);
    assert(clickSave === true, "Clicked 'Save' button in recording dialog");
    await new Promise(r => setTimeout(r, 500));

    // Assert dialog closed
    const dialogClosed = await cdp.eval<boolean>(`(function(){
      return !document.querySelector('[role="dialog"]');
    })()`);
    assert(dialogClosed === true, "Recording dialog closed after save");

    step("Persistence Check — Reload and Verify");
    await cdp.send("Page.reload");
    await new Promise(r => setTimeout(r, 4000));

    // Click the same row again to check the saved combo
    await cdp.eval(`(function(){
      var navSection = document.getElementById('shortcuts-section-content-navigation');
      if (navSection) {
        var rows = navSection.querySelectorAll('.cursor-pointer');
        if (rows.length > 0) rows[0].click();
      }
    })()`);
    await new Promise(r => setTimeout(r, 500));

    const persistedCombo = await cdp.eval<string>(`(function(){
      var card = document.querySelector('.sticky.top-4');
      if (!card) return '';
      var comboEl = card.querySelector('.font-mono');
      return comboEl ? comboEl.textContent.trim() : '';
    })()`);
    assert((persistedCombo ?? '').length > 0, `Persisted combo after reload: "${persistedCombo}"`);

    step("Cleanup — Reset Shortcut to Default");
    const resetClicked = await cdp.eval<boolean>(`(function(){
      var card = document.querySelector('.sticky.top-4');
      if (!card) return false;
      var btns = Array.from(card.querySelectorAll('button'));
      var resetBtn = btns.find(function(b){ return b.textContent.trim() === 'Reset'; });
      if (resetBtn && !resetBtn.disabled) { resetBtn.click(); return true; }
      return false;
    })()`);
    assert(resetClicked === true, `Reset button clicked: ${resetClicked}`);
    await new Promise(r => setTimeout(r, 500));
  } else {
    // Cancel the dialog since we couldn't record
    await cdp.eval(`(function(){
      var dialog = document.querySelector('[role="dialog"]');
      if (!dialog) return;
      var btns = Array.from(dialog.querySelectorAll('button'));
      var cancelBtn = btns.find(function(b){ return b.textContent.trim() === 'Cancel'; });
      if (cancelBtn) cancelBtn.click();
    })()`);
    await new Promise(r => setTimeout(r, 500));
  }

  step("Escape Key Closes Dialog Without Change");
  // Open recording dialog again
  const reOpenEdit = await cdp.eval<boolean>(`(function(){
    var card = document.querySelector('.sticky.top-4');
    if (!card) {
      // Re-select a row first
      var navSection = document.getElementById('shortcuts-section-content-navigation');
      if (navSection) {
        var rows = navSection.querySelectorAll('.cursor-pointer');
        if (rows.length > 0) rows[0].click();
      }
      return false;
    }
    var btns = Array.from(card.querySelectorAll('button'));
    var editBtn = btns.find(function(b){ return b.textContent.trim() === 'Edit'; });
    if (editBtn) { editBtn.click(); return true; }
    return false;
  })()`);

  if (!reOpenEdit) {
    // Retry after selecting row
    await new Promise(r => setTimeout(r, 500));
    await cdp.eval(`(function(){
      var card = document.querySelector('.sticky.top-4');
      if (!card) return;
      var btns = Array.from(card.querySelectorAll('button'));
      var editBtn = btns.find(function(b){ return b.textContent.trim() === 'Edit'; });
      if (editBtn) editBtn.click();
    })()`);
  }
  await new Promise(r => setTimeout(r, 800));

  // Verify dialog is open again
  const dialogReOpened = await cdp.eval<boolean>(`(function(){
    return !!document.querySelector('[role="dialog"]');
  })()`);
  assert(dialogReOpened === true, "Recording dialog re-opened for Escape test");

  // Dispatch Escape key
  await cdp.send("Input.dispatchKeyEvent", {
    type: "keyDown",
    key: "Escape",
    code: "Escape",
    windowsVirtualKeyCode: 27,
    nativeVirtualKeyCode: 27
  });
  await new Promise(r => setTimeout(r, 500));

  // Check dialog closed without change — the dialog's own keydown handler catches Escape
  // If CDP dispatch didn't trigger it, use synthetic event
  let dialogAfterEscape = await cdp.eval<boolean>(`(function(){
    return !document.querySelector('[role="dialog"]');
  })()`);

  if (!dialogAfterEscape) {
    // Fallback: dispatch via JS
    await cdp.eval(`(function(){
      var ev = new KeyboardEvent('keydown', {key: 'Escape', code: 'Escape', keyCode: 27, bubbles: true});
      window.dispatchEvent(ev);
    })()`);
    await new Promise(r => setTimeout(r, 500));
    dialogAfterEscape = await cdp.eval<boolean>(`(function(){
      return !document.querySelector('[role="dialog"]');
    })()`) ?? false;
  }
  assert(dialogAfterEscape === true, "Escape key closed recording dialog without saving");

  ws.close();
  reportResultsAndExit();
}

await main();
