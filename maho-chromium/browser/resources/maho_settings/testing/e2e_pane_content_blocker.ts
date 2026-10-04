#!/usr/bin/env bun
import {
  CDP,
  CDP_URL_ROOT,
  assert,
  checkBrowserOrExit,
  getSettingsWs,
  reportResultsAndExit,
  step,
  waitForCondition,
} from "./cdp_harness";

type PaneSnapshot = {
  readonly text: string;
  readonly checkedMode: string;
  readonly updateDisabled: boolean;
  readonly rebuildDisabled: boolean;
  readonly toggleDisabled: boolean;
  readonly removeDisabled: boolean;
};

async function remountPane(cdp: CDP): Promise<void> {
  await cdp.eval(`window.settingsStore.selectPane('profiles')`);
  await Bun.sleep(100);
  await cdp.eval(`window.settingsStore.selectPane('content-blocker')`);
  await waitForCondition(cdp, `document.querySelector('main')?.textContent.includes('E2E List') === true`);
}

async function waitForText(cdp: CDP, text: string): Promise<void> {
  await waitForCondition(
      cdp,
      `document.querySelector('main')?.textContent.includes(${JSON.stringify(text)}) === true`);
}

async function snapshot(cdp: CDP): Promise<PaneSnapshot | undefined> {
  return cdp.eval<PaneSnapshot>(`(function(){
    var main = document.querySelector('main');
    var checked = main.querySelector('input[name="contentBlockingMode"]:checked');
    var update = Array.from(main.querySelectorAll('button')).find(function(button){
      return (button.textContent || '').includes('Check for Updates');
    });
    var rebuild = Array.from(main.querySelectorAll('button')).find(function(button){
      return (button.textContent || '').includes('Rebuild Engine');
    });
    var toggle = main.querySelector('button[role="switch"][aria-label="Enable E2E List"]');
    var remove = Array.from(main.querySelectorAll('button')).find(function(button){
      return (button.textContent || '').trim() === 'Remove';
    });
    return {
      text: (main.textContent || '').replace(/\\s+/g, ' '),
      checkedMode: checked ? checked.value : '',
      updateDisabled: update ? update.disabled : true,
      rebuildDisabled: rebuild ? rebuild.disabled : true,
      toggleDisabled: toggle ? toggle.disabled : true,
      removeDisabled: remove ? remove.disabled : true
    };
  })()`);
}

async function clickButton(cdp: CDP, label: string): Promise<void> {
  await cdp.eval(`(function(){
    var button = Array.from(document.querySelectorAll('main button')).find(function(candidate){
      return (candidate.textContent || '').trim() === ${JSON.stringify(label)};
    });
    if (button) button.click();
  })()`);
}

async function main(): Promise<void> {
  await checkBrowserOrExit();
  const ws = await getSettingsWs();
  const cdp = new CDP(ws);
  await cdp.send("Runtime.enable");
  await cdp.send("Page.enable");

  step("Content blocker pane fixtures");
  await cdp.navigate(`${CDP_URL_ROOT}content-blocker`, 1500);
  const fixtureInstalled = await cdp.eval<boolean>(`(function(){
    var handler = window.settingsStore.getHandler();
    window.__contentBlockerE2E = {
      failure: '',
      lists: [{
        id: 'e2e-list', name: 'E2E List', url: 'https://example.test/list.txt',
        enabled: true, ruleCount: 12, etag: null, lastModified: null,
        sha256: null, lastAttemptTimestamp: 0n, lastSuccessTimestamp: 0n,
        failureCount: 0, lastStatus: 200, lastError: null
      }],
      stats: {
        mode: 0, totalRuleCount: 12, filterListCount: 1,
        engineGeneration: 1n, healthStatus: 'healthy', overallError: null,
        lastUpdateTimestamp: 0n
      },
      deferredLists: [], deferredStats: [], deferLoads: false, updateCalls: 0
    };
    var fixture = window.__contentBlockerE2E;
    function mutationResponse(success) {
      return {
        success: success,
        mutation: {
          success: success,
          errorCode: success ? null : 'e2e-failure',
          errorMessage: success ? null : 'E2E requested failure',
          compileRequired: false,
          compileScheduled: false,
          stats: Object.assign({}, fixture.stats)
        }
      };
    }
    handler.getFilterLists = async function(){
      if (fixture.deferLoads) {
        return new Promise(function(resolve){ fixture.deferredLists.push(resolve); });
      }
      return {lists: fixture.lists.map(function(list){ return Object.assign({}, list); })};
    };
    handler.getContentBlockerStats = async function(){
      if (fixture.deferLoads) {
        return new Promise(function(resolve){ fixture.deferredStats.push(resolve); });
      }
      return {stats: Object.assign({}, fixture.stats)};
    };
    handler.setContentBlockingMode = async function(mode){
      var success = fixture.failure !== 'mode';
      if (success) fixture.stats.mode = mode;
      return mutationResponse(success);
    };
    handler.triggerFilterUpdate = async function(){
      fixture.updateCalls++;
      if (fixture.failure === 'update-false') return mutationResponse(false);
      if (fixture.failure === 'update-reject') throw new Error('update rejected');
      return mutationResponse(true);
    };
    handler.rebuildContentRules = async function(){
      var success = fixture.failure !== 'rebuild';
      if (success) fixture.stats.engineGeneration += 1n;
      var response = mutationResponse(success);
      return {
        success: response.success,
        stats: Object.assign({}, fixture.stats),
        mutation: response.mutation
      };
    };
    handler.addFilterList = async function(){
      return mutationResponse(fixture.failure !== 'add');
    };
    handler.toggleFilterList = async function(id, enabled){
      var success = fixture.failure !== 'toggle';
      if (success) {
        fixture.lists = fixture.lists.map(function(list){
          return list.id === id ? Object.assign({}, list, {enabled: enabled}) : list;
        });
      }
      return mutationResponse(success);
    };
    handler.removeFilterList = async function(id){
      var success = fixture.failure !== 'remove';
      if (success) {
        fixture.lists = fixture.lists.filter(function(list){ return list.id !== id; });
      }
      return mutationResponse(success);
    };
    return true;
  })()`);
  assert(fixtureInstalled === true, "Content blocker handler fixtures installed");
  await remountPane(cdp);

  const initial = await snapshot(cdp);
  assert(initial?.text.includes("Engine status") === true, "Engine status is rendered");
  assert(initial?.text.includes("E2E List") === true, "Filter list fixture is rendered");

  step("Rejected mode and updater results");
  await cdp.eval(`window.__contentBlockerE2E.failure = 'mode'`);
  await cdp.eval(`document.querySelector('input[value="extension"]').click()`);
  await waitForText(cdp, "Failed to change content blocking mode.");
  const modeFailure = await snapshot(cdp);
  assert(modeFailure?.text.includes("Failed to change content blocking mode.") === true,
      "Rejected mode result surfaces an error");
  assert(modeFailure?.checkedMode === "native", "Rejected mode leaves Native Mode selected");

  await cdp.eval(`window.__contentBlockerE2E.failure = 'update-false'`);
  await clickButton(cdp, "Check for Updates");
  await waitForText(cdp, "Failed to check for filter updates.");
  const updateFailure = await snapshot(cdp);
  assert(updateFailure?.text.includes("Failed to check for filter updates.") === true,
      "Rejected updater enqueue surfaces an error");

  await cdp.eval(`window.__contentBlockerE2E.failure = 'update-reject'`);
  await clickButton(cdp, "Check for Updates");
  await waitForCondition(cdp, `(function(){
    var main = document.querySelector('main');
    var button = Array.from(main.querySelectorAll('button')).find(function(candidate){
      return (candidate.textContent || '').includes('Check for Updates');
    });
    return window.__contentBlockerE2E.updateCalls === 2 &&
        main.textContent.includes('Failed to check for filter updates.') && button && !button.disabled;
  })()`);
  const updateRejection = await snapshot(cdp);
  assert(updateRejection?.text.includes("Failed to check for filter updates.") === true,
      "Thrown updater error surfaces an error");
  assert(updateRejection?.updateDisabled === false, "Updater rejection resets busy state");

  step("Rejected rebuild and list mutations");
  await cdp.eval(`window.__contentBlockerE2E.failure = 'rebuild'`);
  await clickButton(cdp, "Rebuild Engine");
  await waitForCondition(cdp, `(function(){
    var main = document.querySelector('main');
    var button = Array.from(main.querySelectorAll('button')).find(function(candidate){
      return (candidate.textContent || '').includes('Rebuild Engine');
    });
    return main.textContent.includes('Failed to rebuild the content blocker engine.') &&
        main.textContent.includes('Gen #1') && button && !button.disabled;
  })()`);
  const rebuildFailure = await snapshot(cdp);
  assert(rebuildFailure?.text.includes("Failed to rebuild the content blocker engine.") === true,
      "Rebuild rejection surfaces an error");
  assert(rebuildFailure?.text.includes("Gen #1") === true,
      "Rejected rebuild keeps the previous engine generation");
  assert(rebuildFailure?.rebuildDisabled === false, "Rejected rebuild resets busy state");

  await cdp.eval(`window.__contentBlockerE2E.failure = 'toggle'`);
  await cdp.eval(`document.querySelector('button[role="switch"][aria-label="Enable E2E List"]').click()`);
  await waitForCondition(cdp, `(function(){
    var main = document.querySelector('main');
    var toggle = main.querySelector('button[role="switch"][aria-label="Enable E2E List"]');
    return main.textContent.includes('Failed to update E2E List.') && toggle &&
        toggle.getAttribute('aria-checked') === 'true' && !toggle.disabled;
  })()`);
  const toggleFailure = await snapshot(cdp);
  assert(toggleFailure?.text.includes("Failed to update E2E List.") === true,
      "Toggle rejection surfaces a list error");
  const toggleStillEnabled = await cdp.eval<boolean>(
       `document.querySelector('button[role="switch"][aria-label="Enable E2E List"]')?.getAttribute('aria-checked') === 'true'`);
  assert(toggleStillEnabled === true, "Rejected toggle keeps the previous enabled state");
  assert(toggleFailure?.toggleDisabled === false, "Rejected toggle resets busy state");

  await cdp.eval(`window.__contentBlockerE2E.failure = 'remove'`);
  await clickButton(cdp, "Remove");
  await waitForCondition(cdp, `(function(){
    var main = document.querySelector('main');
    var button = Array.from(main.querySelectorAll('button')).find(function(candidate){
      return (candidate.textContent || '').trim() === 'Remove';
    });
    return main.textContent.includes('Failed to remove E2E List.') &&
        main.textContent.includes('E2E List') && button && !button.disabled;
  })()`);
  const removeFailure = await snapshot(cdp);
  assert(removeFailure?.text.includes("Failed to remove E2E List.") === true,
      "Remove rejection surfaces a list error");
  assert(removeFailure?.text.includes("E2E List") === true, "Rejected remove keeps the list row");
  assert(removeFailure?.removeDisabled === false, "Rejected remove resets busy state");

  await cdp.eval(`(function(){
    window.__contentBlockerE2E.failure = 'add';
    function setInput(label, value){
      var input = document.querySelector('input[aria-label="' + label + '"]');
      var setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set;
      setter.call(input, value);
      input.dispatchEvent(new Event('input', {bubbles: true}));
    }
    setInput('Filter list name', 'Rejected List');
    setInput('Filter list URL', 'https://example.test/rejected.txt');
  })()`);
  await Bun.sleep(50);
  await clickButton(cdp, "Add filter list");
  await waitForText(cdp, "Failed to add filter list.");
  assert((await snapshot(cdp))?.text.includes("Failed to add filter list.") === true,
      "Rejected add result surfaces a list error");

  step("Overlapping loads keep the newest result");
  await cdp.eval(`(function(){
    var fixture = window.__contentBlockerE2E;
    fixture.failure = '';
    fixture.deferLoads = true;
    document.querySelector('input[value="extension"]').click();
    Array.from(document.querySelectorAll('main button')).find(function(button){
      return (button.textContent || '').trim() === 'Check for Updates';
    }).click();
  })()`);
  await waitForCondition(cdp, `window.__contentBlockerE2E.deferredStats.length === 2`);
  const pendingLoads = await cdp.eval<number>(`window.__contentBlockerE2E.deferredStats.length`);
  assert(pendingLoads === 2, "Two overlapping refreshes were started");
  await cdp.eval(`(function(){
    var fixture = window.__contentBlockerE2E;
    fixture.deferredLists[1]({lists: [Object.assign({}, fixture.lists[0], {name: 'Newest List'})]});
    fixture.deferredStats[1]({stats: Object.assign({}, fixture.stats, {engineGeneration: 22n})});
  })()`);
  await waitForText(cdp, "Newest List");
  await cdp.eval(`(function(){
    var fixture = window.__contentBlockerE2E;
    fixture.deferredLists[0]({lists: [Object.assign({}, fixture.lists[0], {name: 'Stale List'})]});
    fixture.deferredStats[0]({stats: Object.assign({}, fixture.stats, {engineGeneration: 11n})});
    fixture.deferLoads = false;
  })()`);
  await Bun.sleep(50);
  const overlap = await snapshot(cdp);
  assert(overlap?.text.includes("Newest List") === true, "Newest overlapping load remains rendered");
  assert(overlap?.text.includes("Stale List") === false, "Stale overlapping load cannot overwrite state");
  assert(/update (complete|completed|successful)/i.test(overlap?.text || "") === false,
      "Accepted updater enqueue is not presented as a completed update");

  step("Unknown mode is native-off");
  await cdp.eval(`window.__contentBlockerE2E.stats.mode = 99`);
  await remountPane(cdp);
  const unknownMode = await cdp.eval<{readonly checkedCount: number; readonly nativeControlsDisabled: boolean}>(`(function(){
    var main = document.querySelector('main');
    var nativeControls = [
      Array.from(main.querySelectorAll('button')).find(function(button){ return (button.textContent || '').includes('Check for Updates'); }),
      Array.from(main.querySelectorAll('button')).find(function(button){ return (button.textContent || '').includes('Rebuild Engine'); }),
      main.querySelector('button[role="switch"]'),
      Array.from(main.querySelectorAll('button')).find(function(button){ return (button.textContent || '').trim() === 'Remove'; }),
      main.querySelector('input[aria-label="Filter list name"]'),
      main.querySelector('input[aria-label="Filter list URL"]'),
      Array.from(main.querySelectorAll('button')).find(function(button){ return (button.textContent || '').includes('Add filter list'); })
    ];
    return {
      checkedCount: main.querySelectorAll('input[name="contentBlockingMode"]:checked').length,
      nativeControlsDisabled: nativeControls.every(function(control){ return control && control.disabled; })
    };
  })()`);
  assert(unknownMode?.checkedCount === 0, "Unknown mode selects no known radio option");
  assert(unknownMode?.nativeControlsDisabled === true, "Unknown mode disables all native-only controls");

  ws.close();
  reportResultsAndExit();
}

await main();
