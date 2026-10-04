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

// Remount SyncPane without a document reload so handler fixtures survive:
// selectPane away then back re-runs loadStatus() against the stubbed handler.
async function remountSyncPane(cdp: CDP): Promise<void> {
  await cdp.eval(`(function(){ window.settingsStore.selectPane('profiles'); })()`);
  await new Promise((r) => setTimeout(r, 500));
  await cdp.eval(`(function(){ window.settingsStore.selectPane('account-sync'); })()`);
  await new Promise((r) => setTimeout(r, 900));
}

async function main() {
  await checkRelayOrExit();

  const ws = await getSettingsWs();
  const cdp = new CDP(ws);
  await cdp.send("Runtime.enable");
  await cdp.send("Page.enable");
  await new Promise((r) => setTimeout(r, 500));

  step("Account & Sync Pane Verification");
  await cdp.navigate(`${CDP_URL_ROOT}account-sync`, 4000);

  const base = await cdp.eval<{hasHeader: boolean; hasStatusText: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '').replace(/\\s+/g, ' ');
    return {
      hasHeader: /Account/i.test(text) && /Sync/i.test(text),
      hasStatusText: /(Not connected|Connected|offline|online|Status|Syncing)/i.test(text)
    };
  })()`);
  assert(base!.hasHeader === true, "Account pane with merged sync section rendered");
  assert(base!.hasStatusText === true, "Sync status text visible");

  // A live multi-device relay session with remote peers cannot be provisioned
  // from CDP, so we install handler fixtures on the real store handler and drive
  // the REAL SyncPane React flow (RemoteDeviceRow → rename/disconnect handlers →
  // loadStatus refresh). Only backend responses are simulated; the frontend
  // wiring under test is exercised end to end. Fixtures live for this document
  // only — the next test's navigate() reloads the page and restores the real
  // handler. We never assert relay URL or room id.
  step("Install remote-device fixtures on the live handler");
  const stubbed = await cdp.eval<boolean>(`(function(){
    var h = window.settingsStore.getHandler();
    window.__e2eSync = {
      devices: [{id: 'e2e-remote-1', name: 'E2E Remote', deviceType: 'Desktop', isOnline: true}],
      renameCalled: false,
      disconnectCalled: false,
      disconnectSucceeds: true
    };
    h.getSyncStatus = async function(){ return {status: {isSyncing: true, statusLabel: 'Connected', errorMessage: ''}}; };
    h.getSyncDevices = async function(){ return {devices: window.__e2eSync.devices.slice()}; };
    h.renameSyncDevice = async function(id, name){
      window.__e2eSync.renameCalled = true;
      var d = window.__e2eSync.devices.find(function(x){ return x.id === id; });
      if (d) d.name = name;
      return {success: true};
    };
    h.disconnectSyncDevice = async function(id){
      window.__e2eSync.disconnectCalled = true;
      if (window.__e2eSync.disconnectSucceeds) {
        window.__e2eSync.devices = window.__e2eSync.devices.filter(function(x){ return x.id !== id; });
        return {success: true};
      }
      return {success: false};
    };
    return true;
  })()`);
  assert(stubbed === true, "Handler fixtures installed on the live store handler");

  await remountSyncPane(cdp);

  const rowPresent = await cdp.eval<boolean>(`(function(){
    var main = document.querySelector('main');
    return (main ? main.textContent : '').indexOf('E2E Remote') !== -1;
  })()`);
  assert(rowPresent === true, "Remote device row rendered from fixtures");

  step("Rename remote device invokes and refreshes (happy)");
  await cdp.eval(`(function(){
    var main = document.querySelector('main');
    var sections = Array.from(main.querySelectorAll('section'));
    var row = sections.find(function(s){ return (s.textContent||'').indexOf('E2E Remote') !== -1; });
    if (row) {
      var btn = Array.from(row.querySelectorAll('button')).find(function(b){ return b.textContent.trim() === 'Rename'; });
      if (btn) btn.click();
    }
  })()`);
  await new Promise((r) => setTimeout(r, 600));
  await cdp.eval(`(function(){
    ${SET_INPUT_HELPER}
    var el = document.querySelector('main input[aria-label="New device name"]');
    if (el) __setInput(el, 'E2E Renamed');
  })()`);
  await new Promise((r) => setTimeout(r, 300));
  await cdp.eval(`(function(){
    var main = document.querySelector('main');
    var sections = Array.from(main.querySelectorAll('section'));
    var row = sections.find(function(s){ return !!s.querySelector('input[aria-label="New device name"]'); });
    if (row) {
      var btn = Array.from(row.querySelectorAll('button')).find(function(b){ return b.textContent.trim() === 'Save'; });
      if (btn) btn.click();
    }
  })()`);
  await new Promise((r) => setTimeout(r, 1200));

  const renameResult = await cdp.eval<{invoked: boolean; refreshedName: boolean; notEditing: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '');
    return {
      invoked: window.__e2eSync.renameCalled === true,
      refreshedName: text.indexOf('E2E Renamed') !== -1,
      notEditing: !main.querySelector('input[aria-label="New device name"]')
    };
  })()`);
  assert(renameResult!.invoked === true, "renameSyncDevice was invoked");
  assert(renameResult!.refreshedName === true, "Row refreshed to the renamed device name after save");
  assert(renameResult!.notEditing === true, "Rename edit mode closed after successful save");

  step("Confirmed disconnect invokes and refreshes (happy)");
  await cdp.eval(`(function(){
    var main = document.querySelector('main');
    var sections = Array.from(main.querySelectorAll('section'));
    var row = sections.find(function(s){ return (s.textContent||'').indexOf('E2E Renamed') !== -1; });
    if (row) {
      var btn = Array.from(row.querySelectorAll('button')).find(function(b){ return b.textContent.trim() === 'Disconnect'; });
      if (btn) btn.click();
    }
  })()`);
  await new Promise((r) => setTimeout(r, 600));
  await cdp.eval(`(function(){
    var main = document.querySelector('main');
    var sections = Array.from(main.querySelectorAll('section'));
    var row = sections.find(function(s){ return /Disconnect .*\\?/.test(s.textContent||''); });
    if (row) {
      var btn = Array.from(row.querySelectorAll('button')).find(function(b){ return b.textContent.trim() === 'Confirm'; });
      if (btn) btn.click();
    }
  })()`);
  await new Promise((r) => setTimeout(r, 1200));

  const disconnectResult = await cdp.eval<{invoked: boolean; rowGone: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '');
    return {
      invoked: window.__e2eSync.disconnectCalled === true,
      rowGone: text.indexOf('E2E Renamed') === -1
    };
  })()`);
  assert(disconnectResult!.invoked === true, "disconnectSyncDevice was invoked");
  assert(disconnectResult!.rowGone === true, "Device row removed after confirmed disconnect refresh");

  step("Rejected disconnect keeps the row and shows an error (failure)");
  await cdp.eval(`(function(){
    window.__e2eSync.devices = [{id: 'e2e-remote-2', name: 'E2E Reject', deviceType: 'Desktop', isOnline: true}];
    window.__e2eSync.disconnectSucceeds = false;
    window.__e2eSync.disconnectCalled = false;
  })()`);
  await remountSyncPane(cdp);

  await cdp.eval(`(function(){
    var main = document.querySelector('main');
    var sections = Array.from(main.querySelectorAll('section'));
    var row = sections.find(function(s){ return (s.textContent||'').indexOf('E2E Reject') !== -1; });
    if (row) {
      var btn = Array.from(row.querySelectorAll('button')).find(function(b){ return b.textContent.trim() === 'Disconnect'; });
      if (btn) btn.click();
    }
  })()`);
  await new Promise((r) => setTimeout(r, 600));
  await cdp.eval(`(function(){
    var main = document.querySelector('main');
    var sections = Array.from(main.querySelectorAll('section'));
    var row = sections.find(function(s){ return /Disconnect .*\\?/.test(s.textContent||''); });
    if (row) {
      var btn = Array.from(row.querySelectorAll('button')).find(function(b){ return b.textContent.trim() === 'Confirm'; });
      if (btn) btn.click();
    }
  })()`);
  await new Promise((r) => setTimeout(r, 1200));

  const rejectResult = await cdp.eval<{invoked: boolean; rowIntact: boolean; errorShown: boolean}>(`(function(){
    var main = document.querySelector('main');
    var text = (main ? main.textContent : '');
    return {
      invoked: window.__e2eSync.disconnectCalled === true,
      rowIntact: text.indexOf('E2E Reject') !== -1,
      errorShown: /Failed to disconnect device/i.test(text)
    };
  })()`);
  assert(rejectResult!.invoked === true, "Rejected disconnect still invoked the handler");
  assert(rejectResult!.rowIntact === true, "Device row stays intact when the disconnect is rejected");
  assert(rejectResult!.errorShown === true, "An error message is shown when the disconnect is rejected");

  ws.close();
  reportResultsAndExit();
}

await main();
