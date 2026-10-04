import {describe, expect, test} from 'bun:test';

import {waitForSettingsTarget} from './cdp_harness';

type MessageListener = (event: {data: string}) => void;

class FakeBrowserSocket {
  readonly calls: string[] = [];
  readonly listeners = new Set<MessageListener>();

  addEventListener(type: string, listener: MessageListener): void {
    expect(type).toBe('message');
    this.calls.push('listen');
    this.listeners.add(listener);
  }

  removeEventListener(type: string, listener: MessageListener): void {
    expect(type).toBe('message');
    this.calls.push('unlisten');
    this.listeners.delete(listener);
  }

  send(payload: string): void {
    this.calls.push('discover');
    const command = JSON.parse(payload) as {id: number; method: string};
    expect(command.method).toBe('Target.setDiscoverTargets');
    queueMicrotask(() => this.emit({id: command.id, result: {}}));
  }

  emit(message: unknown): void {
    const event = {data: JSON.stringify(message)};
    for (const listener of [...this.listeners]) listener(event);
  }
}

describe('waitForSettingsTarget', () => {
  test('registers target listeners before discovery and target creation', async () => {
    const socket = new FakeBrowserSocket();
    const wait = waitForSettingsTarget(
      socket as never,
      async () => {
        socket.calls.push('create');
        socket.emit({
          method: 'Target.targetCreated',
          params: {targetInfo: {type: 'page', url: 'chrome://maho-settings/?pane=account'}},
        });
      },
      100,
    );

    await expect(wait).resolves.toMatchObject({url: 'chrome://maho-settings/?pane=account'});
    expect(socket.calls.slice(0, 3)).toEqual(['listen', 'discover', 'create']);
    expect(socket.listeners.size).toBe(0);
  });

  test('accepts targetInfoChanged and removes its listener after timeout', async () => {
    const socket = new FakeBrowserSocket();
    const changed = waitForSettingsTarget(
      socket as never,
      async () => {
        socket.emit({
          method: 'Target.targetInfoChanged',
          params: {targetInfo: {type: 'page', url: 'chrome://maho-settings/?pane=profiles'}},
        });
      },
      100,
    );
    await expect(changed).resolves.toMatchObject({url: 'chrome://maho-settings/?pane=profiles'});
    expect(socket.listeners.size).toBe(0);

    const timedOut = waitForSettingsTarget(socket as never, async () => {}, 10);
    await expect(timedOut).rejects.toThrow('Timed out waiting for maho-settings target');
    expect(socket.listeners.size).toBe(0);
  });
});

test('profiles initial Runtime.evaluate preserves its whitespace regex escape', async () => {
  const profiles = await Bun.file(`${import.meta.dir}/e2e_pane_profiles.ts`).text();
  const initialStart = profiles.indexOf('const initial =');
  const initialEnd = profiles.indexOf("assert(initial.names.length >= 1", initialStart);
  const initialEvalSource = profiles.slice(initialStart, initialEnd);

  expect({
    foundInitialEval: initialStart >= 0 && initialEnd > initialStart,
    hasDoubleEscapedWhitespaceRegex: initialEvalSource.includes('replace(/\\\\s+/g'),
    hasSingleEscapedWhitespaceRegex: initialEvalSource.includes('replace(/\\s+/g'),
  }).toEqual({
    foundInitialEval: true,
    hasDoubleEscapedWhitespaceRegex: true,
    hasSingleEscapedWhitespaceRegex: false,
  });
});

test('default deletion waits for selected-editor convergence before qualified delete', async () => {
  const profiles = await Bun.file(`${import.meta.dir}/e2e_pane_profiles.ts`).text();
  const deletionStart = profiles.indexOf("step('Deletion protection and round trip')");
  const disposableSelectionEnd = profiles.indexOf(
    "assert(true, 'Disposable profile selected for cleanup')",
    deletionStart,
  );
  const deletionSource = profiles.slice(deletionStart, disposableSelectionEnd);
  const defaultSelectionWait = deletionSource.match(
    /await waitForDomMutation\(cdp,\s*`([^`]*)`,\s*\(\) => cdp\.eval(?:<boolean>)?\(`([\s\S]*?)`\)[\s\S]*?\);/,
  );
  const predicateSource = defaultSelectionWait?.[1] ?? '';
  const triggerSource = defaultSelectionWait?.[2] ?? '';
  const convergedSelectionEnd = defaultSelectionWait
    ? (defaultSelectionWait.index ?? -1) + defaultSelectionWait[0].length
    : -1;
  const qualifiedDefaultDeleteIndex = deletionSource.search(
    /button\[aria-label=\$\{js\(`Delete profile \$\{defaultProfileName\}`\)\}\]/,
  );
  const currentRacyOrdering =
    /const defaultDelete = await cdp\.eval[\s\S]*if \(defaultDelete\)\s*\{\s*await cdp\.eval\(`document\.querySelector\('button\[aria-label\^="Delete profile"\]'\)/
      .test(deletionSource);

  expect({
    foundDeletionSlice: deletionStart >= 0 && disposableSelectionEnd > deletionStart,
    defaultSelectionIsWaitTrigger:
      triggerSource.includes('[data-profile-name]') && triggerSource.includes('.click()'),
    predicateWaitsForActualDefaultEditor:
      predicateSource.includes("[data-profile-editor=\"true\"] h3") &&
      /\$\{js\((?:defaultProfileName|'Default')\)\}/.test(predicateSource),
    qualifiedDeleteFollowsConvergence:
      qualifiedDefaultDeleteIndex > convergedSelectionEnd && convergedSelectionEnd >= 0,
    forbidsEvalThenGenericDeleteRace: !currentRacyOrdering,
  }).toEqual({
    foundDeletionSlice: true,
    defaultSelectionIsWaitTrigger: true,
    predicateWaitsForActualDefaultEditor: true,
    qualifiedDeleteFollowsConvergence: true,
    forbidsEvalThenGenericDeleteRace: true,
  });
});

test('required E2Es arm exact waits before their actions', async () => {
  const testingDir = import.meta.dir;
  const [profiles, chromiumSettings, harness] = await Promise.all([
    Bun.file(`${testingDir}/e2e_pane_profiles.ts`).text(),
    Bun.file(`${testingDir}/e2e_pane_chromium_settings.ts`).text(),
    Bun.file(`${testingDir}/cdp_harness.ts`).text(),
  ]);

  expect(chromiumSettings).toMatch(
    /await cdp\.navigate[\s\S]*waitForPageTargetUrl\([\s\S]*chrome:\/\/settings[\s\S]*clickElementByText/,
  );
  expect(chromiumSettings).not.toContain('waitForFrameUrl');
  expect(harness).toContain('export async function waitForPageTargetUrl');
  expect(profiles).toContain('waitForDomMutation');
  expect(profiles).not.toContain('waitForCondition');
  const navigateIndex = profiles.indexOf(
    'await cdp.navigate(`$' + '{CDP_URL_ROOT}profiles`)',
  );
  const storePredicateIndex = profiles.indexOf('!!window.settingsStore', navigateIndex);
  const predicateEndIndex = profiles.indexOf('async () => {},', storePredicateIndex);
  const provisionIndex = profiles.indexOf('const provisionedProfileName');
  const profileUiWaitIndex = profiles.indexOf("step('Profiles catalog and editor')");
  const exceptionRegistrationIndex = profiles.lastIndexOf(
    "cdp.on('Runtime.exceptionThrown'",
    navigateIndex,
  );
  const initialReadiness = profiles.slice(exceptionRegistrationIndex, provisionIndex);
  const storePredicateSource = profiles.slice(storePredicateIndex, predicateEndIndex);

  expect(navigateIndex).toBeGreaterThanOrEqual(0);
  expect(exceptionRegistrationIndex).toBeGreaterThanOrEqual(0);
  expect(exceptionRegistrationIndex).toBeLessThan(navigateIndex);
  expect(navigateIndex).toBeLessThan(storePredicateIndex);
  expect(storePredicateIndex).toBeLessThan(predicateEndIndex);
  expect(storePredicateIndex).toBeLessThan(provisionIndex);
  expect(provisionIndex).toBeLessThan(profileUiWaitIndex);
  expect(initialReadiness).toContain('waitForDomMutation');
  expect(storePredicateSource).toMatch(
    /!!window\.settingsStore[\s\S]*typeof window\.settingsStore\.getHandler === 'function'[\s\S]*typeof window\.settingsStore\.getSnapshot === 'function'[\s\S]*typeof window\.settingsStore\.subscribe === 'function'/,
  );
  expect(storePredicateSource).not.toMatch(/document\.querySelector(?:All)?/);
  expect(initialReadiness).toMatch(
    /try\s*\{[\s\S]*waitForDomMutation[\s\S]*\}\s*catch\s*\(error\)\s*\{[\s\S]*await cdp\.eval[\s\S]*throw new Error/,
  );
  expect(initialReadiness).toContain('Settings store readiness failed:');
  expect(initialReadiness).toContain('Runtime.exceptionThrown');
  expect(initialReadiness).toContain("document.querySelector('maho-settings-app')");
  expect(initialReadiness).toContain('bodyHtmlPrefix');
  expect(initialReadiness).toContain('runtimeExceptions');
  expect(initialReadiness).toContain('cdp.off');
  expect(initialReadiness).not.toContain("document.querySelector('#root')");
  for (const field of [
    'href',
    'readyState',
    'title',
    'rootPresent',
    'rootChildCount',
    'rootTextPrefix',
    'bodyHtmlPrefix',
    'scriptSrcs',
    'storeType',
    'getHandlerType',
    'getSnapshotType',
    'subscribeType',
  ]) {
    expect(initialReadiness).toContain(field);
  }
  expect(profiles.slice(provisionIndex, profileUiWaitIndex)).toContain(
    'handler.createProfile',
  );
  expect(profiles.slice(profileUiWaitIndex)).toMatch(
    /waitForDomMutation\(cdp, `!!document\.querySelector\('\[role="listbox"\]\[aria-label="Profiles"\] \[role="option"\]'\) && !!document\.querySelector\('\[data-profile-editor="true"\]'\)/,
  );
  expect(profiles).not.toMatch(/Bun\.sleep|setTimeout|setInterval|waitForCondition/);
  expect(profiles).toMatch(/handler\.deleteProfile\([\s\S]*reportResultsAndExit/);
  expect(harness).not.toContain('Bun.sleep');
  expect(harness).not.toMatch(/setTimeout\s*\([^,]+,\s*3000\s*\)/);
});
