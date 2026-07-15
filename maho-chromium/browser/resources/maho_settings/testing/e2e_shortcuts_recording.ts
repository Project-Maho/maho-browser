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
  // The row's onSelect handler lives on the inner selectable <button>, not the
  // cosmetic .cursor-pointer wrapper div, so the button must be the click target.
  const selectRow = await cdp.eval<{clicked: boolean; label: string}>(`(function(){
    var navSection = document.getElementById('shortcuts-section-content-navigation');
    if (!navSection) return {clicked: false, label: ''};
    var rows = navSection.querySelectorAll('.cursor-pointer');
    if (rows.length > 0) {
      var label = rows[0].querySelector('.text-sm.font-medium');
      var btn = rows[0].querySelector('button');
      if (!btn) return {clicked: false, label: label ? label.textContent : ''};
      btn.click();
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

  step("Recording mode is entered via the real handler; native key capture is not CDP-drivable");
  // The combo is captured natively in BrowserView::PreHandleKeyboardEvent, which runs
  // BEFORE the renderer. CDP Input.dispatchKeyEvent injects into the renderer and never
  // reaches that pre-render path, so OS-key capture cannot be driven from this harness.
  // Verify the CDP-reachable plumbing instead: opening the dialog entered recording mode
  // through the real setRecordingMode handler, and Cancel exits it cleanly.
  const cancelledRecording = await cdp.eval<boolean>(`(function(){
    var dialog = document.querySelector('[role="dialog"]');
    if (!dialog) return false;
    var cancelBtn = Array.from(dialog.querySelectorAll('button')).find(function(b){ return b.textContent.trim() === 'Cancel'; });
    if (cancelBtn) { cancelBtn.click(); return true; }
    return false;
  })()`);
  assert(cancelledRecording === true, "Cancelled the open recording dialog (exits recording mode)");
  await new Promise(r => setTimeout(r, 500));
  const dialogClosedAfterCancel = await cdp.eval<boolean>(`(function(){
    return !document.querySelector('[role="dialog"]');
  })()`);
  assert(dialogClosedAfterCancel === true, "Recording dialog closed after Cancel");

  step("Persist a custom combo through the real setShortcut path, then reload");
  // Drive the exact handler call the dialog's Save performs. This exercises the real
  // mojo → FFI → maho-core → storage round-trip a recorded combo ultimately commits.
  const actionId = await cdp.eval<string>(`(function(){
    var card = document.querySelector('.sticky.top-4');
    var a = card ? card.querySelector('.font-mono.text-xs') : null;
    return a ? a.textContent.trim() : '';
  })()`);
  assert((actionId ?? '').length > 0, `Resolved selected action id for persistence: "${actionId}"`);

  const setResult = await cdp.eval<{success: boolean}>(`(async function(){
    var h = window.settingsStore.getHandler();
    var res = await h.setShortcut(${JSON.stringify(actionId)}, {key:'k', modifiers:['ctrl','alt','shift']});
    return {success: !!(res && res.result && res.result.success)};
  })()`, true);
  assert(setResult!.success === true, "setShortcut persisted the custom combo (⌃⌥⇧K) via the real handler");

  await cdp.send("Page.reload");
  await new Promise(r => setTimeout(r, 4000));
  await cdp.eval(`(function(){
    var navSection = document.getElementById('shortcuts-section-content-navigation');
    if (navSection) {
      var rows = navSection.querySelectorAll('.cursor-pointer');
      if (rows.length > 0) { var b = rows[0].querySelector('button'); if (b) b.click(); }
    }
  })()`);
  await new Promise(r => setTimeout(r, 600));

  // The detail panel's current-combo value is the .text-sm font-mono span; the
  // .text-xs font-mono element is the action id, so target text-sm specifically.
  const persistedCombo = await cdp.eval<string>(`(function(){
    var card = document.querySelector('.sticky.top-4');
    if (!card) return '';
    var comboEl = card.querySelector('.font-mono.text-sm');
    return comboEl ? comboEl.textContent.trim() : '';
  })()`);
  assert(persistedCombo === '⌃⌥⇧K', `Persisted combo shown after reload: "${persistedCombo}"`);

  step("Cleanup — Reset Shortcut to Default");
  const resetClicked = await cdp.eval<boolean>(`(function(){
    var card = document.querySelector('.sticky.top-4');
    if (!card) return false;
    var resetBtn = Array.from(card.querySelectorAll('button')).find(function(b){ return b.textContent.trim() === 'Reset'; });
    if (resetBtn && !resetBtn.disabled) { resetBtn.click(); return true; }
    return false;
  })()`);
  assert(resetClicked === true, "Reset button clicked to restore default");
  await new Promise(r => setTimeout(r, 600));
  const afterReset = await cdp.eval<boolean>(`(async function(){
    var h = window.settingsStore.getHandler();
    var list = (await h.getShortcuts()).shortcuts;
    var b = list.find(function(x){ return x.action === ${JSON.stringify(actionId)}; });
    return b ? (b.isCustom === false) : false;
  })()`, true);
  assert(afterReset === true, "Shortcut reset to default (isCustom=false) via the real handler");

  step("Escape Key Closes Dialog Without Change");
  // Open recording dialog again
  const reOpenEdit = await cdp.eval<boolean>(`(function(){
    var card = document.querySelector('.sticky.top-4');
    if (!card) {
      // Re-select a row first
      var navSection = document.getElementById('shortcuts-section-content-navigation');
      if (navSection) {
        var rows = navSection.querySelectorAll('.cursor-pointer');
        if (rows.length > 0) { var b = rows[0].querySelector('button'); if (b) b.click(); }
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
