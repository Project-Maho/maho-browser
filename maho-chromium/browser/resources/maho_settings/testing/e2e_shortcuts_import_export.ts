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
  const alreadyOpen = await cdp.eval<boolean>(`(function(){
    return !!document.querySelector('[role="menu"][data-state="open"]');
  })()`);
  if (alreadyOpen) return true;

  if (!await clickElementByText(cdp, 'button', 'Actions ▾')) return false;
  return waitForCondition(
    cdp,
    `(function(){ return !!document.querySelector('[role="menu"][data-state="open"]'); })()`
  );
}

function isKeyCombo(value: unknown): boolean {
  return value !== null &&
    typeof value === 'object' &&
    !Array.isArray(value) &&
    'key' in value &&
    typeof value.key === 'string' &&
    'modifiers' in value &&
    Array.isArray(value.modifiers) &&
    value.modifiers.every(function(modifier) { return typeof modifier === 'string'; });
}

function isShortcutExportEntry(value: unknown): boolean {
  return value !== null &&
    typeof value === 'object' &&
    !Array.isArray(value) &&
    'action' in value &&
    typeof value.action === 'string' &&
    value.action.length > 0 &&
    'label' in value &&
    typeof value.label === 'string' &&
    'category' in value &&
    typeof value.category === 'string' &&
    value.category.length > 0 &&
    'keyCombo' in value &&
    isKeyCombo(value.keyCombo) &&
    'defaultKeyCombo' in value &&
    isKeyCombo(value.defaultKeyCombo) &&
    'isCustom' in value &&
    typeof value.isCustom === 'boolean' &&
    'enabled' in value &&
    typeof value.enabled === 'boolean' &&
    'updatedAt' in value &&
    typeof value.updatedAt === 'number' &&
    Number.isFinite(value.updatedAt);
}

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
  const shortcutsReady = await waitForCondition(cdp, `(function(){
    return Array.from(document.querySelectorAll('button')).some(function(button){
      return (button.textContent || '').trim() === 'Actions ▾';
    }) && document.querySelectorAll(${JSON.stringify(SHORTCUT_ROW_SELECTOR)}).length === 67;
  })()`);
  assert(shortcutsReady, "Shortcuts pane rendered 67 bindings and the Actions trigger");
  if (!shortcutsReady) {
    ws.close();
    reportResultsAndExit();
    return;
  }

  step("Open Actions Menu and Verify Export/Import");
  const clickedActions = await openActionsMenu(cdp);
  assert(clickedActions === true, "Clicked 'Actions' dropdown");

  // Verify Export JSON and Import JSON items exist
  const menuState = await cdp.eval<{hasExport: boolean; hasImport: boolean; hasResetAll: boolean}>(`(function(){
    var items = Array.from(document.querySelectorAll('[role="menuitem"]'));
    return {
      hasExport: items.some(function(i){ return i.textContent.trim() === 'Export JSON'; }),
      hasImport: items.some(function(i){ return i.textContent.trim() === 'Import JSON'; }),
      hasResetAll: items.some(function(i){ return i.textContent.trim() === 'Reset All'; })
    };
  })()`);
  assert(menuState?.hasExport === true, "Export JSON menu item exists");
  assert(menuState?.hasImport === true, "Import JSON menu item exists");
  assert(menuState?.hasResetAll === true, "Reset All menu item exists");

  step("Export JSON — Validate Schema");
  const exportedJson = await cdp.eval<string>(`(async function(){
    var store = window.settingsStore;
    if (!store || typeof store.getHandler !== 'function') {
      throw new Error('window.settingsStore handler is unavailable');
    }
    var result = await store.getHandler().exportShortcuts();
    return result.jsonData;
  })()`, true);

  assert(
    typeof exportedJson === 'string' && exportedJson.length > 0,
    "Captured non-empty shortcut export JSON from the real Mojo handler"
  );
  if (!exportedJson) {
    ws.close();
    reportResultsAndExit();
    return;
  }

  let parsed: unknown;
  try {
    parsed = JSON.parse(exportedJson);
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    assert(false, `Export JSON parsing failed: ${message}`);
    ws.close();
    reportResultsAndExit();
    return;
  }

  if (parsed === null || typeof parsed !== 'object' || Array.isArray(parsed)) {
    assert(false, "Export JSON must be an object envelope");
    ws.close();
    reportResultsAndExit();
    return;
  }

  assert('version' in parsed && parsed.version === 1, `Export JSON has version field = ${'version' in parsed ? String(parsed.version) : 'missing'}`);
  const platform = 'platform' in parsed && typeof parsed.platform === 'string' ? parsed.platform : '';
  assert(
    ['macos', 'windows', 'linux'].includes(platform),
    `Export JSON platform is valid: "${platform}"`
  );
  const exportedAt = 'exportedAt' in parsed && typeof parsed.exportedAt === 'string' ? parsed.exportedAt : '';
  const isIso = /^\d{4}-\d{2}-\d{2}T/.test(exportedAt) && !Number.isNaN(Date.parse(exportedAt));
  assert(isIso, `Export JSON exportedAt is ISO 8601: "${exportedAt}"`);
  const shortcuts = 'shortcuts' in parsed && Array.isArray(parsed.shortcuts) ? parsed.shortcuts : [];
  assert(
    shortcuts.length > 0,
    `Export JSON shortcuts is non-empty array (${shortcuts.length} items)`
  );
  const invalidShortcutIndex = shortcuts.findIndex(shortcut => !isShortcutExportEntry(shortcut));
  assert(
    invalidShortcutIndex === -1,
    `Every export shortcut has all required fields (first invalid index: ${invalidShortcutIndex})`
  );

  step("Reset All Shortcuts");
  const resetMenuOpen = await openActionsMenu(cdp);
  assert(resetMenuOpen, "Actions menu is open for Reset All");
  const clickedResetAll = resetMenuOpen &&
    await clickElementByText(cdp, '[role="menuitem"]', 'Reset All');
  assert(clickedResetAll === true, "Clicked 'Reset All' in kebab menu");
  const resetCompleted = clickedResetAll && await waitForCondition(
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
  assert(resetCompleted, "Reset All completed with all 67 shortcut bindings rendered");

  // Verify shortcuts are still loaded after reset (not empty)
  const afterReset = await cdp.eval<number>(`(function(){
    return document.querySelectorAll(${JSON.stringify(SHORTCUT_ROW_SELECTOR)}).length;
  })()`);
  assert(afterReset === 67, `Shortcuts still loaded after Reset All: ${afterReset} bindings`);

  step("Import JSON via File Input");
  const importMenuOpen = await openActionsMenu(cdp);
  assert(importMenuOpen, "Actions menu is open for Import JSON");

  const importHookInstalled = await cdp.eval<boolean>(`(function(){
    var origCreate = document.createElement.bind(document);
    window.__mahoOriginalCreateElement = document.createElement;
    window.__mahoImportTriggered = false;
    document.createElement = function(tag) {
      var el = origCreate(tag);
      if (tag === 'input') {
        Object.defineProperty(el, 'click', {value: function(){
          if (el.type === 'file') window.__mahoImportTriggered = true;
        }});
      }
      return el;
    };
    return true;
  })()`);
  assert(importHookInstalled === true, "Installed file input observation hook");
  const clickedImport = importMenuOpen &&
    await clickElementByText(cdp, '[role="menuitem"]', 'Import JSON');
  assert(clickedImport === true, "Clicked 'Import JSON' menu item");
  const importTriggered = await waitForCondition(
    cdp,
    `(function(){ return window.__mahoImportTriggered === true; })()`
  );
  await cdp.eval(`(function(){
    if (window.__mahoOriginalCreateElement) {
      document.createElement = window.__mahoOriginalCreateElement;
    }
    delete window.__mahoOriginalCreateElement;
    delete window.__mahoImportTriggered;
  })()`);
  assert(importTriggered === true, "Import JSON triggers file input dialog");

  ws.close();
  reportResultsAndExit();
}

await main();
