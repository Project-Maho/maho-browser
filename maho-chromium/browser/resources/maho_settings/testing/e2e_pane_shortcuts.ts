#!/usr/bin/env bun
import {
  CDP,
  assert,
  step,
  getSettingsWs,
  checkRelayOrExit,
  checkBrowserOrExit,
  reportResultsAndExit,
  CDP_URL_ROOT,
  clickElementByText,
  waitForCondition
} from "./cdp_harness";

const SHORTCUT_ROW_SELECTOR = '[id^="shortcuts-section-content-"] > div > [data-state]';

async function openActionsMenu(cdp: CDP): Promise<boolean> {
  if (!await clickElementByText(cdp, 'button', 'Actions ▾')) return false;
  return waitForCondition(
    cdp,
    `(function(){ return !!document.querySelector('[role="menu"][data-state="open"]'); })()`
  );
}

async function main() {
  await checkRelayOrExit();
  await checkBrowserOrExit();

  const ws = await getSettingsWs();
  const cdp = new CDP(ws);
  await cdp.send("Runtime.enable");
  await cdp.send("Page.enable");
  await new Promise(r => setTimeout(r, 500));

  step("Shortcuts Pane Verification");
  await cdp.navigate(`${CDP_URL_ROOT}shortcuts`, 4000);
  const shortcutsReady = await waitForCondition(cdp, `(function(){
    return !!document.querySelector('main') &&
      Array.from(document.querySelectorAll('button')).some(function(button){
        return (button.textContent || '').trim() === 'Actions ▾';
      }) &&
      document.querySelectorAll(${JSON.stringify(SHORTCUT_ROW_SELECTOR)}).length === 67 &&
      !!Array.from(document.querySelectorAll('.border-dashed')).find(function(panel){
        return (panel.textContent || '').includes('Select a shortcut to view details or modify.');
      });
  })()`);
  assert(shortcutsReady, "Shortcuts pane reached its fully rendered empty-detail state");
  if (!shortcutsReady) {
    ws.close();
    reportResultsAndExit();
    return;
  }

  // === Existing assertions (strengthened) ===
  const shortcuts = await cdp.eval<{hasResetAllBtn: boolean; shortcutRowsCount: number; text: string}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var btns = Array.from(document.querySelectorAll('button'));
    var hasResetAllBtn = btns.some(function(b){ return b.textContent.trim() === 'Actions ▾'; });
    var cards = document.querySelectorAll('[id^="shortcuts-section-content-"]');
    return {
      hasResetAllBtn: hasResetAllBtn,
      shortcutRowsCount: cards.length,
      text: text.slice(0, 300)
    };
  })()`);

  assert(shortcuts?.hasResetAllBtn === true, "Shortcuts pane 'Actions' dropdown rendered");
  assert((shortcuts?.shortcutRowsCount ?? 0) >= 1, `Shortcuts list populated with ${shortcuts?.shortcutRowsCount ?? 0} category cards`);
  assert((shortcuts?.text.length ?? 0) > 30, "Shortcuts pane successfully loaded content");

  step("Row Count Verification (67 live bindings)");
  const totalRows = await cdp.eval<number>(`(function(){
    return document.querySelectorAll(${JSON.stringify(SHORTCUT_ROW_SELECTOR)}).length;
  })()`);
  assert(totalRows === 67, `Total rendered shortcut bindings: ${totalRows} (expected 67)`);

  // === NEW: Assert categories render (7 with data: Navigation, Tabs, Spaces, Window, View, Developer, Custom) ===
  step("Category Sections Verification");
  const categories = await cdp.eval<{found: string[]; count: number}>(`(function(){
    var expected = ['navigation', 'tabs', 'spaces', 'window', 'view', 'developer', 'custom'];
    var found = [];
    expected.forEach(function(key){
      if (document.getElementById('shortcuts-section-content-' + key)) {
        found.push(key);
      }
    });
    return {found: found, count: found.length};
  })()`);
  // Edit category has 0 shortcuts and is filtered out by the UI
  const foundCategories = categories?.found ?? [];
  assert((categories?.count ?? 0) >= 7, `All 7 populated categories render (found: ${foundCategories.join(', ')})`);
  assert(foundCategories.includes('navigation'), "Navigation category section present");
  assert(foundCategories.includes('tabs'), "Tabs category section present");
  assert(foundCategories.includes('spaces'), "Spaces category section present");
  assert(foundCategories.includes('window'), "Window category section present");
  assert(foundCategories.includes('view'), "View category section present");
  assert(foundCategories.includes('developer'), "Developer category section present");
  assert(foundCategories.includes('custom'), "Custom category section present");

  // === NEW: Assert detail panel exists in DOM (empty state when nothing selected) ===
  step("Detail Panel Empty State");
  const detailPanel = await cdp.eval<{exists: boolean; isEmptyState: boolean; text: string; selectedActionId: string}>(`(function(){
    var emptyPanel = Array.from(document.querySelectorAll('.border-dashed')).find(function(panel){
      return (panel.textContent || '').includes('Select a shortcut to view details or modify.');
    });
    if (emptyPanel) {
      return {
        exists: true,
        isEmptyState: true,
        text: emptyPanel.textContent.trim(),
        selectedActionId: ''
      };
    }
    // If something is already selected (from previous test run state), detail shows Card
    var card = document.querySelector('.sticky.top-4');
    if (card) {
      var actionId = card.querySelector('.font-mono.text-xs');
      return {
        exists: true,
        isEmptyState: false,
        text: card.textContent.slice(0, 300),
        selectedActionId: actionId ? actionId.textContent.trim() : ''
      };
    }
    return {exists: false, isEmptyState: false, text: '', selectedActionId: ''};
  })()`);
  assert(detailPanel?.exists === true, "Detail panel exists in DOM");
  if (!detailPanel) {
    ws.close();
    reportResultsAndExit();
    return;
  }
  if (detailPanel.isEmptyState) {
    assert(
      detailPanel.text.includes("Select a shortcut"),
      `Detail panel shows empty state: "${detailPanel.text}"`
    );
  } else {
    assert(
      detailPanel.selectedActionId.length > 0 &&
        detailPanel.text.includes('Current Combo') &&
        detailPanel.text.includes('Status') &&
        detailPanel.text.includes('Enabled'),
      `Detail panel shows selected shortcut controls for action "${detailPanel.selectedActionId}"`
    );
  }

  // === Existing: Click Actions dropdown ===
  step("Actions Dropdown Interaction");
  const clickedActions = await openActionsMenu(cdp);
  assert(clickedActions === true, "Clicked 'Actions' dropdown successfully");

  // Click Reset All in dropdown
  const clickedReset = clickedActions &&
    await clickElementByText(cdp, '[role="menuitem"]', 'Reset All');
  assert(clickedReset === true, "Clicked 'Reset All' menu item successfully");
  const resetCompleted = clickedReset && await waitForCondition(
    cdp,
    `(function(){
      var actionsButton = Array.from(document.querySelectorAll('button')).find(function(button){
        return (button.textContent || '').trim() === 'Actions ▾';
      });
      return document.querySelectorAll(${JSON.stringify(SHORTCUT_ROW_SELECTOR)}).length === 67 &&
        !!actionsButton && actionsButton.getAttribute('aria-expanded') === 'false' &&
        !document.querySelector('[role="menu"]') &&
        !document.querySelector('[data-radix-popper-content-wrapper]') &&
        getComputedStyle(document.body).pointerEvents !== 'none';
    })()`
  );
  assert(resetCompleted, "Reset All completed with all 67 bindings rendered");

  const shortcutsAfterReset = await cdp.eval<{shortcutRowsCount: number}>(`(function(){
    return { shortcutRowsCount: document.querySelectorAll(${JSON.stringify(SHORTCUT_ROW_SELECTOR)}).length };
  })()`);
  assert(
    shortcutsAfterReset?.shortcutRowsCount === 67,
    `Shortcuts list still has ${shortcutsAfterReset?.shortcutRowsCount ?? 0} bindings after reset`
  );

  ws.close();
  reportResultsAndExit();
}

await main();
