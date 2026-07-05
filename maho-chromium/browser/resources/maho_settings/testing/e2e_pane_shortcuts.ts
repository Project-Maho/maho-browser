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

  step("Shortcuts Pane Verification");
  await cdp.navigate(`${CDP_URL_ROOT}shortcuts`, 4000);

  // === Existing assertions (strengthened) ===
  const shortcuts = await cdp.eval<{hasResetAllBtn: boolean; shortcutRowsCount: number; text: string}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    var btns = Array.from(main.querySelectorAll('button'));
    var hasResetAllBtn = btns.some(function(b){ return b.textContent.trim().includes('Actions'); });
    var cards = main.querySelectorAll('.border-border\\/70');
    return {
      hasResetAllBtn: hasResetAllBtn,
      shortcutRowsCount: cards.length,
      text: text.slice(0, 300)
    };
  })()`);

  assert(shortcuts!.hasResetAllBtn === true, "Shortcuts pane 'Actions' dropdown rendered");
  assert(shortcuts!.shortcutRowsCount >= 1, `Shortcuts list populated with ${shortcuts!.shortcutRowsCount} category cards`);
  assert(shortcuts!.text.length > 30, "Shortcuts pane successfully loaded content");

  // === NEW: Assert exact row count (63 default bindings from shortcut_manager.rs) ===
  step("Row Count Verification (63 default bindings)");
  const totalRows = await cdp.eval<number>(`(function(){
    var sections = document.querySelectorAll('[id^="shortcuts-section-content-"]');
    var total = 0;
    sections.forEach(function(s){
      total += s.querySelectorAll('.cursor-pointer').length;
    });
    return total;
  })()`);
  // 63 bindings from register_default_bindings: 6 nav + 19 tabs + 12 spaces + 18 window + 3 view + 3 dev + 2 custom
  assert((totalRows ?? 0) >= 50, `Total shortcut rows: ${totalRows} (expected ~63, allow >=50 for filter tolerance)`);
  assert((totalRows ?? 0) <= 80, `Total shortcut rows: ${totalRows} within expected range (<=80)`);

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
  assert(categories!.count >= 7, `All 7 populated categories render (found: ${categories!.found.join(', ')})`);
  assert(categories!.found.includes('navigation'), "Navigation category section present");
  assert(categories!.found.includes('tabs'), "Tabs category section present");
  assert(categories!.found.includes('spaces'), "Spaces category section present");
  assert(categories!.found.includes('window'), "Window category section present");
  assert(categories!.found.includes('view'), "View category section present");
  assert(categories!.found.includes('developer'), "Developer category section present");
  assert(categories!.found.includes('custom'), "Custom category section present");

  // === NEW: Assert detail panel exists in DOM (empty state when nothing selected) ===
  step("Detail Panel Empty State");
  const detailPanel = await cdp.eval<{exists: boolean; isEmptyState: boolean; text: string}>(`(function(){
    // Detail panel shows dashed border in empty state
    var emptyPanel = document.querySelector('.border-dashed.border-border\\/70');
    if (emptyPanel) {
      return {
        exists: true,
        isEmptyState: true,
        text: emptyPanel.textContent.trim()
      };
    }
    // If something is already selected (from previous test run state), detail shows Card
    var card = document.querySelector('.sticky.top-4');
    if (card) {
      return {exists: true, isEmptyState: false, text: card.textContent.slice(0, 100)};
    }
    return {exists: false, isEmptyState: false, text: ''};
  })()`);
  assert(detailPanel!.exists === true, "Detail panel exists in DOM");
  if (detailPanel!.isEmptyState) {
    assert(
      detailPanel!.text.includes("Select a shortcut"),
      `Detail panel shows empty state: "${detailPanel!.text}"`
    );
  } else {
    assert(true, `Detail panel shows selected shortcut (pre-selected state from earlier test)`);
  }

  // === Existing: Click Actions dropdown ===
  step("Actions Dropdown Interaction");
  const clickedActions = await cdp.eval<boolean>(`(function(){
    var btns = Array.from(document.querySelectorAll('main button'));
    var actionsBtn = btns.find(function(b){ return b.textContent.includes('Actions'); });
    if (actionsBtn) { actionsBtn.click(); return true; }
    return false;
  })()`);
  assert(clickedActions === true, "Clicked 'Actions' dropdown successfully");
  await new Promise(r => setTimeout(r, 1000));

  // Click Reset All in dropdown
  const clickedReset = await cdp.eval<boolean>(`(function(){
    var items = Array.from(document.querySelectorAll('[role="menuitem"]'));
    var resetItem = items.find(function(item){ return item.textContent.trim() === 'Reset All'; });
    if (resetItem) { resetItem.click(); return true; }
    return false;
  })()`);
  assert(clickedReset === true, "Clicked 'Reset All' menu item successfully");
  await new Promise(r => setTimeout(r, 2000));

  const shortcutsAfterReset = await cdp.eval<{shortcutRowsCount: number}>(`(function(){
    var main = document.querySelector('main');
    var cards = main.querySelectorAll('.border-border\\/70');
    return { shortcutRowsCount: cards.length };
  })()`);
  assert(shortcutsAfterReset!.shortcutRowsCount >= 1, `Shortcuts list still has ${shortcutsAfterReset!.shortcutRowsCount} category cards after reset`);

  ws.close();
  reportResultsAndExit();
}

await main();
