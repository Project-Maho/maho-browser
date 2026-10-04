import {describe, expect, test} from 'bun:test';

import type {
  PageHandlerRemote,
  ProfileTarget,
  SelectedProfileContext,
} from '../maho_settings.mojom-webui.js';
import {
  createCdpProfileScenarioHost,
  createGeneratedProxyProfileScenarioHost,
  type ProfileScenarioGeneratedProxy,
} from './task12_cdp_profile_scenario_host.js';
import type {CdpEventTransport} from './task12_live_lifecycle.js';

const context = (revision: bigint, token = 'token-b'): SelectedProfileContext => ({
  profileId: 'profile-b',
  targetToken: token,
  contextRevision: revision,
  profileRevision: revision + 100n,
  lifecycleState: 1,
  isHostProfile: false,
  isActiveMahoProfile: false,
});

function fakeProxy() {
  const calls: Array<{method: string; args: readonly unknown[]}> = [];
  let nextRevision = 10n;
  const next = () => context(nextRevision++);
  const proxy: ProfileScenarioGeneratedProxy = {
    getProfiles: async () => ({profiles: [
      {id: 'profile-a', name: 'Personal', isDefault: true, isActive: true, spaceIds: []},
      {id: 'profile-b', name: 'Work', isDefault: false, isActive: false, spaceIds: []},
    ]}),
    getSelectedProfileContext: async target => {
      calls.push({method: 'getSelectedProfileContext', args: [target]});
      return {result: {context: {...next(), profileId: target.profileId, targetToken: 'token-b'}, error: null}};
    },
    createProfile: async name => {
      calls.push({method: 'createProfile', args: [name]});
      return {profile: {id: 'profile-b', name, isDefault: false, isActive: false, spaceIds: []}};
    },
    deleteProfile: async id => { calls.push({method: 'deleteProfile', args: [id]}); return {success: true}; },
    updateSelectedProfileMetadata: async (...args) => {
      calls.push({method: 'updateSelectedProfileMetadata', args});
      return {result: {context: next(), metadata: args[2], error: null}};
    },
    setSelectedProfileDefaultSearchEngine: async (...args) => {
      calls.push({method: 'setSelectedProfileDefaultSearchEngine', args});
      return {result: {context: next(), search: null, error: null}};
    },
    setSelectedProfileSearchSuggestionsEnabled: async (...args) => {
      calls.push({method: 'setSelectedProfileSearchSuggestionsEnabled', args});
      return {result: {context: next(), search: null, error: null}};
    },
    setSelectedProfileDownloadPrompt: async (...args) => {
      calls.push({method: 'setSelectedProfileDownloadPrompt', args});
      return {result: {context: next(), download: null, error: null}};
    },
    setSelectedProfileArchiveTimeout: async (...args) => {
      calls.push({method: 'setSelectedProfileArchiveTimeout', args});
      return {result: {context: next(), archive: null, error: null}};
    },
    getSelectedProfileMetadata: async target => ({result: {context: {...next(), profileId: target.profileId}, metadata: {name: 'Work', avatarColor: '#123456'}, error: null}}),
    getSelectedProfileSearchSettings: async target => ({result: {context: {...next(), profileId: target.profileId}, search: {engines: [{keyword: 'search.test', name: 'Search', iconUrl: '', isDefault: true}], suggestionsEnabled: true}, error: null}}),
    getSelectedProfileDownloadSettings: async target => ({result: {context: {...next(), profileId: target.profileId}, download: {directoryDisplayPath: '/Downloads', promptForDownload: true}, error: null}}),
    getSelectedProfileArchiveSettings: async target => ({result: {context: {...next(), profileId: target.profileId}, archive: {timeoutHours: 72}, error: null}}),
  };
  return {calls, proxy};
}

describe('Task 12 generated-proxy ProfileScenarioHost adapter', () => {
  test('covers all eight non-snapshot host methods and exact generated argument conversion', async () => {
    const fake = fakeProxy();
    const host = createGeneratedProxyProfileScenarioHost(fake.proxy);
    expect(await host.listProfileNames()).toEqual(['Personal', 'Work']);
    expect(await host.createProfile('run-work', {name: 'Work QA', avatarColor: '#4F46E5'})).toBe('profile-b');
    host.registerCreatedProfile('B', 'profile-b', 'run-work');
    await host.seedProfile('profile-b', {
      metadata: {name: 'Work QA', avatarColor: '#4F46E5'},
      homepage: 'https://example.com/work', searchKeyword: 'google.com', suggestions: true,
      download: true, archive: 72,
    });
    expect(await host.readProfile('profile-b')).toEqual({
      metadata: {name: 'Work', avatarColor: '#123456'}, homepage: '',
      searchKeyword: 'search.test', suggestions: true, download: true, archive: 72,
    });
    await host.deleteProfile?.('profile-b');
    await host.restoreSnapshot({targetToken: 'token-a', contextRevision: 1n, profileRevision: 2n,
      profiles: [{id: 'profile-a', name: 'Personal'}], selectedProfileId: 'profile-a', ready: true});
    await host.assertRequestCurrent?.({targetToken: 'token-b', contextRevision: 22n, profileRevision: 122n});

    const mutationCalls = fake.calls.filter(call => call.method.startsWith('update') || call.method.startsWith('set'));
    expect(mutationCalls.map(call => call.method)).toEqual([
      'updateSelectedProfileMetadata', 'updateSelectedProfileMetadata',
      'setSelectedProfileDefaultSearchEngine',
      'setSelectedProfileSearchSuggestionsEnabled', 'setSelectedProfileDownloadPrompt',
      'setSelectedProfileArchiveTimeout',
    ]);
    expect(mutationCalls[0]!.args).toEqual([
      {profileId: 'profile-b', targetToken: 'token-b'}, 10n,
      {name: 'Work QA', avatarColor: '#4F46E5', expectedProfileRevision: 110n},
    ]);
    expect(mutationCalls[2]!.args).toEqual([
      {profileId: 'profile-b', targetToken: 'token-b'}, 12n, 'google.com', 112n,
    ]);
    expect(mutationCalls.slice(2).map(call => call.args[1])).toEqual([12n, 13n, 14n, 15n]);
    expect(mutationCalls.slice(3).map(call => call.args.at(-1))).toEqual([113n, 114n, 115n]);
  });

  test('propagates generated proxy rejection and typed Mojo result errors without fallback', async () => {
    const rejection = new Error('remote pipe closed');
    const fake = fakeProxy();
    fake.proxy.createProfile = async () => { throw rejection; };
    await expect(createGeneratedProxyProfileScenarioHost(fake.proxy).createProfile('x', {name: 'x', avatarColor: '#000000'})).rejects.toBe(rejection);

    const resultError = {code: 5, message: 'stale context', currentContextRevision: 99n, currentProfileRevision: 199n};
    const second = fakeProxy();
    second.proxy.getSelectedProfileContext = async () => ({result: {context: null, error: resultError}});
    await expect(createGeneratedProxyProfileScenarioHost(second.proxy).readProfile('profile-b')).rejects.toMatchObject({
      message: 'stale context', code: 5, currentContextRevision: 99n, currentProfileRevision: 199n,
    });
  });

  test('CDP factory creates the generated proxy only after attachment and losslessly converts bigint values', async () => {
    const expressions: string[] = [];
    const transport: CdpEventTransport = {
      on: () => () => undefined,
      async send(method, params) {
        expect(method).toBe('Runtime.evaluate');
        const evaluateParams: {readonly expression?: unknown} = params ?? {};
        const expression = String(evaluateParams.expression);
        expressions.push(expression);
        return {result: {result: {value: {profiles: [{id: 'profile-b', name: 'Work', isDefault: false, isActive: true, spaceIds: []}]}}}};
      },
    };
    const host = createCdpProfileScenarioHost(transport);
    expect(await host.listProfileNames()).toEqual(['Work']);
    expect(expressions).toHaveLength(1);
    expect(expressions[0]).toContain('window.settingsStore.getHandler()');
    expect(expressions[0]).toContain('getProfiles');
    expect(expressions[0]).toContain("typeof value === 'bigint'");
    const exactType: Pick<PageHandlerRemote, 'getProfiles'> | undefined = undefined;
    expect(exactType).toBeUndefined();
  });
});
