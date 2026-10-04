import {afterEach, beforeAll, describe, expect, mock, test} from 'bun:test';

type Deferred<T> = {
  promise: Promise<T>;
  resolve(value: T): void;
  reject(error: unknown): void;
};

type ProfileTarget = {profileId: string; targetToken: string};
type SelectedProfileContext = {
  profileId: string;
  targetToken: string;
  contextRevision: bigint;
  profileRevision: bigint;
  lifecycleState: number;
  isHostProfile: boolean;
  isActiveMahoProfile: boolean;
};

type ProfileError = {
  code: number;
  message: string;
  currentContextRevision: bigint;
  currentProfileRevision: bigint | null;
};

const ProfileTargetErrorCode = {
  kInvalidProfileId: 1,
  kProfileDeleting: 3,
  kProfileUnavailable: 4,
  kStaleContext: 5,
  kStaleProfileRevision: 6,
} as const;

function deferred<T>(): Deferred<T> {
  let resolve!: (value: T) => void;
  let reject!: (error: unknown) => void;
  const promise = new Promise<T>((resolvePromise, rejectPromise) => {
    resolve = resolvePromise;
    reject = rejectPromise;
  });
  return {promise, resolve, reject};
}

function target(profileId: string): ProfileTarget {
  return {profileId, targetToken: `token-${profileId}`};
}

function context(profileId: string, revision = 1n): SelectedProfileContext {
  return {
    profileId,
    targetToken: `token-${profileId}`,
    contextRevision: revision,
    profileRevision: revision * 10n,
    lifecycleState: 1,
    isHostProfile: profileId === 'A',
    isActiveMahoProfile: profileId === 'A',
  };
}

function profileError(code: number, message: string): ProfileError {
  return {
    code,
    message,
    currentContextRevision: 0n,
    currentProfileRevision: null,
  };
}

class ListenerEndpoint {
  private nextId = 1;
  addListener(_listener: (...args: any[]) => void): number {
    return this.nextId++;
  }
}

class FakeRouter {
  readonly settingsChanged = new ListenerEndpoint();
  readonly accountStatusChanged = new ListenerEndpoint();
  readonly onBrowserUpdateStateChanged = new ListenerEndpoint();
  removeListener(_id: number): void {}
}

class FakeHandler {
  readonly contextRequests: ProfileTarget[] = [];
  readonly switchProfileCalls: string[] = [];
  readonly contextResults = new Map<string, Array<Promise<any> | any>>();
  readonly metadataResults: Array<Promise<any> | any> = [];
  readonly searchResults: Array<Promise<any> | any> = [];
  readonly downloadResults: Array<Promise<any> | any> = [];
  readonly archiveResults: Array<Promise<any> | any> = [];
  readonly mutationCalls: Array<{name: string; args: any[]}> = [];
  readonly mutationResults = new Map<string, Array<Promise<any> | any>>();
  readonly setSettingCalls: Array<{key: string; value: string}> = [];
  settingsResult = {settings: [{key: 'global.fixture', value: 'stable'}]};

  enqueueContext(profileId: string, result: Promise<any> | any): void {
    const results = this.contextResults.get(profileId) ?? [];
    results.push(result);
    this.contextResults.set(profileId, results);
  }

  async getSelectedProfileContext(requested: ProfileTarget): Promise<any> {
    this.contextRequests.push({...requested});
    const result = this.contextResults.get(requested.profileId)?.shift();
    if (result === undefined) {
      return {result: {context: context(requested.profileId), error: null}};
    }
    return {result: await result};
  }

  async getSelectedProfileMetadata(_target: ProfileTarget): Promise<any> {
    return {result: await this.metadataResults.shift()};
  }

  async getSelectedProfileSearchSettings(_target: ProfileTarget): Promise<any> {
    return {result: await this.searchResults.shift()};
  }

  async getSelectedProfileDownloadSettings(_target: ProfileTarget): Promise<any> {
    return {result: await this.downloadResults.shift()};
  }

  async getSelectedProfileArchiveSettings(_target: ProfileTarget): Promise<any> {
    return {result: await this.archiveResults.shift()};
  }

  enqueueMutation(name: string, result: Promise<any> | any): void {
    const results = this.mutationResults.get(name) ?? [];
    results.push(result);
    this.mutationResults.set(name, results);
  }

  private async mutate(name: string, args: any[]): Promise<any> {
    this.mutationCalls.push({name, args});
    return {result: await this.mutationResults.get(name)?.shift()};
  }

  updateSelectedProfileMetadata(...args: any[]): Promise<any> { return this.mutate('metadata', args); }
  setSelectedProfileDefaultSearchEngine(...args: any[]): Promise<any> { return this.mutate('search-engine', args); }
  setSelectedProfileSearchSuggestionsEnabled(...args: any[]): Promise<any> { return this.mutate('search-suggestions', args); }
  setSelectedProfileDownloadPrompt(...args: any[]): Promise<any> { return this.mutate('download-prompt', args); }
  selectSelectedProfileDownloadDirectory(...args: any[]): Promise<any> { return this.mutate('download-directory', args); }
  setSelectedProfileArchiveTimeout(...args: any[]): Promise<any> { return this.mutate('archive', args); }

  async switchProfile(profileId: string): Promise<void> {
    this.switchProfileCalls.push(profileId);
  }

  async getSettings(): Promise<any> {
    return this.settingsResult;
  }

  async setSetting(key: string, value: string): Promise<any> {
    this.setSettingCalls.push({key, value});
    return {success: true};
  }

  async getBrowserVersionInfo(): Promise<any> {
    return {
      info: {
        version: 'test',
        channel: 'test',
        updateState: 0,
        updateSupported: false,
      },
    };
  }
}

let activeHandler: FakeHandler;
let MahoSettingsStore: typeof import('../react/store.js').MahoSettingsStore;
let stores: Array<InstanceType<typeof MahoSettingsStore>> = [];

beforeAll(async () => {
  const router = new FakeRouter();
  mock.module('../mojo.js', () => ({
    BrowserUpdateState: {kIdle: 0, kChecking: 1},
    PageCallbackRouter: class {},
    PageHandlerFactory: class {},
    PageHandlerRemote: class {},
  }));
  mock.module('../maho_settings.mojom-webui.js', () => ({ProfileTargetErrorCode}));
  mock.module('../../maho_common/react/use_mojo.js', () => ({
    bindMojoPageHandler: () => ({router, handler: activeHandler}),
  }));
  mock.module('sonner', () => ({
    toast: {success: () => {}, error: () => {}},
  }));

  ({MahoSettingsStore} = await import('../react/store.js'));
});

afterEach(() => {
  for (const store of stores) store.dispose();
  stores = [];
});

function createStore(): InstanceType<typeof MahoSettingsStore> {
  activeHandler = new FakeHandler();
  const store = new MahoSettingsStore();
  stores.push(store);
  return store;
}

function installWindowFixture(): void {
  const location = new URL('chrome://maho-settings/?pane=profiles');
  Object.defineProperty(globalThis, 'window', {
    configurable: true,
    value: {
      location,
      history: {
        replaceState: (_state: unknown, _title: string, href: string) => {
          Object.assign(location, new URL(href));
        },
        pushState: (_state: unknown, _title: string, href: string) => {
          Object.assign(location, new URL(href));
        },
      },
      addEventListener: () => {},
      removeEventListener: () => {},
    },
  });
  Object.defineProperty(globalThis, 'requestAnimationFrame', {
    configurable: true,
    value: (_callback: FrameRequestCallback) => 1,
  });
  Object.defineProperty(globalThis, 'cancelAnimationFrame', {
    configurable: true,
    value: (_handle: number) => {},
  });
}

installWindowFixture();

describe('selected-profile store orchestration', () => {
  test('catalog selection accepts a browser-issued target token without switching profiles', async () => {
    const store = createStore();
    activeHandler.enqueueContext('B', {context: context('B'), error: null});
    await store.selectProfileFromCatalog('B');
    expect(activeHandler.contextRequests).toEqual([{profileId: 'B', targetToken: ''}]);
    expect(store.getSnapshot().selectedProfileTarget).toEqual(target('B'));
    expect(store.getSnapshot().selectedProfileContext).toEqual(context('B'));
    expect(activeHandler.switchProfileCalls).toEqual([]);
  });

  test('drops a delayed B context after C is selected and never switches profiles', async () => {
    const store = createStore();
    const delayedB = deferred<any>();
    activeHandler.enqueueContext('B', delayedB.promise);
    activeHandler.enqueueContext('C', {context: context('C'), error: null});

    const selectingB = store.selectProfileTarget(target('B'));
    await store.selectProfileTarget(target('C'));
    delayedB.resolve({context: context('B'), error: null});
    await selectingB;

    expect(store.getSnapshot().selectedProfileTarget).toEqual(target('C'));
    expect(store.getSnapshot().selectedProfileContext).toEqual(context('C'));
    expect(activeHandler.switchProfileCalls).toEqual([]);
  });

  test('clears prior profile snapshots synchronously without changing global settings identity', async () => {
    const store = createStore();
    await store.bootstrap();
    const globalSettings = store.getSnapshot().settings;
    await store.selectProfileTarget(target('B'));

    activeHandler.metadataResults.push({metadata: {name: 'B', avatarColor: '#bbb'}, context: context('B'), error: null});
    activeHandler.searchResults.push({search: {engines: [], suggestionsEnabled: true}, context: context('B'), error: null});
    activeHandler.downloadResults.push({download: {directoryDisplayPath: '/B', promptForDownload: true}, context: context('B'), error: null});
    activeHandler.archiveResults.push({archive: {timeoutHours: 24}, context: context('B'), error: null});
    await Promise.all([
      store.refreshSelectedProfileMetadata(),
      store.refreshSelectedProfileSearch(),
      store.refreshSelectedProfileDownload(),
      store.refreshSelectedProfileArchive(),
    ]);

    const delayedC = deferred<any>();
    activeHandler.enqueueContext('C', delayedC.promise);
    const selectingC = store.selectProfileTarget(target('C'));
    const transitioned = store.getSnapshot();

    expect(transitioned.selectedProfileContext).toBeNull();
    expect(transitioned.selectedProfileMetadata).toBeNull();
    expect(transitioned.selectedProfileSearch).toBeNull();
    expect(transitioned.selectedProfileDownload).toBeNull();
    expect(transitioned.selectedProfileArchive).toBeNull();
    expect(transitioned.settings).toBe(globalSettings);
    expect(transitioned.settings).toEqual([{key: 'global.fixture', value: 'stable'}]);

    delayedC.resolve({context: context('C'), error: null});
    await selectingC;
  });

  test('drops every delayed B domain response after transition to C', async () => {
    const store = createStore();
    await store.selectProfileTarget(target('B'));

    const metadata = deferred<any>();
    const search = deferred<any>();
    const download = deferred<any>();
    const archive = deferred<any>();
    activeHandler.metadataResults.push(metadata.promise);
    activeHandler.searchResults.push(search.promise);
    activeHandler.downloadResults.push(download.promise);
    activeHandler.archiveResults.push(archive.promise);
    const refreshes = [
      store.refreshSelectedProfileMetadata(),
      store.refreshSelectedProfileSearch(),
      store.refreshSelectedProfileDownload(),
      store.refreshSelectedProfileArchive(),
    ];

    await store.selectProfileTarget(target('C'));
    metadata.resolve({metadata: {name: 'B', avatarColor: '#bbb'}, context: context('B'), error: null});
    search.resolve({search: {engines: [], suggestionsEnabled: true}, context: context('B'), error: null});
    download.resolve({download: {directoryDisplayPath: '/B', promptForDownload: true}, context: context('B'), error: null});
    archive.resolve({archive: {timeoutHours: 24}, context: context('B'), error: null});
    await Promise.all(refreshes);

    const state = store.getSnapshot();
    expect(state.selectedProfileTarget).toEqual(target('C'));
    expect(state.selectedProfileContext).toEqual(context('C'));
    expect(state.selectedProfileMetadata).toBeNull();
    expect(state.selectedProfileSearch).toBeNull();
    expect(state.selectedProfileDownload).toBeNull();
    expect(state.selectedProfileArchive).toBeNull();
  });

  test('drops a delayed response when the same target receives a newer context revision', async () => {
    const store = createStore();
    await store.selectProfileTarget(target('B'));

    const metadata = deferred<any>();
    activeHandler.metadataResults.push(metadata.promise);
    const refresh = store.refreshSelectedProfileMetadata();

    activeHandler.enqueueContext('B', {context: context('B', 2n), error: null});
    await store.selectProfileTarget(target('B'));
    metadata.resolve({
      metadata: {name: 'B old revision', avatarColor: '#bbb'},
      context: context('B'),
      error: null,
    });
    await refresh;

    expect(store.getSnapshot().selectedProfileContext).toEqual(context('B', 2n));
    expect(store.getSnapshot().selectedProfileMetadata).toBeNull();
  });

  test('distinguishes unavailable, deleting, and stale errors and refreshes stale context exactly once', async () => {
    const store = createStore();
    activeHandler.enqueueContext('missing', {context: null, error: profileError(ProfileTargetErrorCode.kProfileUnavailable, 'Profile unavailable')});
    await store.selectProfileTarget(target('missing'));
    const unavailable = store.getSnapshot().selectedProfileError;

    activeHandler.enqueueContext('deleting', {context: null, error: profileError(ProfileTargetErrorCode.kProfileDeleting, 'Profile is deleting')});
    await store.selectProfileTarget(target('deleting'));
    const deleting = store.getSnapshot().selectedProfileError;

    expect(unavailable).toBe('Profile unavailable');
    expect(deleting).toBe('Profile is deleting');
    expect(unavailable).not.toBe(deleting);

    for (const code of [ProfileTargetErrorCode.kStaleContext, ProfileTargetErrorCode.kStaleProfileRevision]) {
      await store.selectProfileTarget(target('B'));
      const requestsBefore = activeHandler.contextRequests.length;
      activeHandler.metadataResults.push({
        metadata: null,
        context: null,
        error: profileError(code, code === ProfileTargetErrorCode.kStaleContext ? 'Stale context' : 'Stale profile revision'),
      });
      await store.refreshSelectedProfileMetadata();
      expect(activeHandler.contextRequests.length - requestsBefore).toBe(1);
    }
  });

  test('plumbs current revisions and applies fresh mutation context and snapshots', async () => {
    const store = createStore();
    await store.selectProfileTarget(target('B'));
    const fresh = {...context('B'), contextRevision: 2n, profileRevision: 11n};
    activeHandler.enqueueMutation('metadata', {
      context: fresh,
      metadata: {name: 'Work', avatarColor: '#FF0000'},
      error: null,
    });

    expect(await store.updateSelectedProfileMetadata({name: 'Work', avatarColor: '#FF0000'})).toBe(true);
    expect(activeHandler.mutationCalls[0]).toEqual({
      name: 'metadata',
      args: [target('B'), 1n, {name: 'Work', avatarColor: '#FF0000', expectedProfileRevision: 10n}],
    });
    expect(store.getSnapshot().selectedProfileContext).toEqual(fresh);
    expect(store.getSnapshot().selectedProfileMetadata).toEqual({name: 'Work', avatarColor: '#FF0000'});
    expect(store.getSnapshot().selectedProfileMutationError).toBeNull();
  });

  test('covers every typed mutation domain with current revision plumbing', async () => {
    const cases = [
      ['search-engine', (store: InstanceType<typeof MahoSettingsStore>) => store.setSelectedProfileDefaultSearchEngine('duck'), 'search', {engines: [], suggestionsEnabled: true}],
      ['search-suggestions', (store: InstanceType<typeof MahoSettingsStore>) => store.setSelectedProfileSearchSuggestionsEnabled(false), 'search', {engines: [], suggestionsEnabled: false}],
      ['download-prompt', (store: InstanceType<typeof MahoSettingsStore>) => store.setSelectedProfileDownloadPrompt(true), 'download', {directoryDisplayPath: 'Downloads', promptForDownload: true}],
      ['download-directory', (store: InstanceType<typeof MahoSettingsStore>) => store.selectSelectedProfileDownloadDirectory(), 'download', {directoryDisplayPath: 'Desktop', promptForDownload: true}],
      ['archive', (store: InstanceType<typeof MahoSettingsStore>) => store.setSelectedProfileArchiveTimeout(24), 'archive', {timeoutHours: 24}],
    ] as const;
    for (const [name, invoke, resultKey, snapshot] of cases) {
      const store = createStore();
      await store.selectProfileTarget(target('B'));
      activeHandler.enqueueMutation(name, {context: context('B'), [resultKey]: snapshot, error: null});
      expect(await invoke(store)).toBe(true);
      const call = activeHandler.mutationCalls.at(-1)!;
      expect(call.args[0]).toEqual(target('B'));
      expect(call.args[1]).toBe(1n);
      if (name === 'preferences') expect(call.args[2].expectedProfileRevision).toBe(10n);
      else expect(call.args.at(-1)).toBe(10n);
    }
  });

  test('routes partial-pane allowlisted rows to typed selected-target mutations and preserves no-target legacy writes', async () => {
    const store = createStore();
    await store.selectProfileTarget(target('B'));

    activeHandler.enqueueMutation('archive', {
      context: context('B'),
      archive: {timeoutHours: 24},
      error: null,
    });

    // theme/preload are managed globally now (removed from per-profile routing),
    // so they commit through the standard global setSetting path even with a
    // selected target; only archive remains a routed selected-profile row.
    expect(await store.commitSettingValue('appearance.theme', '2')).toBe(true);
    expect(await store.commitSettingValue('advanced.preload_pages', '3')).toBe(true);
    expect(await store.commitSettingValue('tabs.archive_timeout', '24')).toBe(true);
    expect(await store.commitSettingValue('appearance.sidebar_width', '280')).toBe(false);
    expect(activeHandler.setSettingCalls).toEqual([
      {key: 'appearance.theme', value: '2'},
      {key: 'advanced.preload_pages', value: '3'},
    ]);
    expect(activeHandler.mutationCalls).toEqual([
      {name: 'archive', args: [target('B'), 1n, 24, 10n]},
    ]);

    const hostStore = createStore();
    await hostStore.selectProfileTarget(target('A'));
    expect(await hostStore.commitSettingValue('appearance.sidebar_width', '290')).toBe(true);
    expect(activeHandler.setSettingCalls).toEqual([
      {key: 'appearance.sidebar_width', value: '290'},
    ]);

    const legacyStore = createStore();
    expect(await legacyStore.commitSettingValue('appearance.theme', '1')).toBe(true);
    expect(await legacyStore.commitSettingValue('advanced.preload_pages', '0')).toBe(true);
    expect(await legacyStore.commitSettingValue('tabs.archive_timeout', '168')).toBe(true);
    expect(await legacyStore.commitSettingValue('appearance.sidebar_width', '300')).toBe(true);
    expect(activeHandler.setSettingCalls).toEqual([
      {key: 'appearance.theme', value: '1'},
      {key: 'advanced.preload_pages', value: '0'},
      {key: 'tabs.archive_timeout', value: '168'},
      {key: 'appearance.sidebar_width', value: '300'},
    ]);
  });

  test('keys structured typed-mutation errors to the originating partial-pane row without legacy fallback', async () => {
    const store = createStore();
    await store.selectProfileTarget(target('B'));
    activeHandler.enqueueMutation('archive', {
      context: null,
      archive: null,
      error: profileError(ProfileTargetErrorCode.kStaleProfileRevision, 'Archive settings changed elsewhere'),
    });

    expect(await store.commitSettingValue('tabs.archive_timeout', '24')).toBe(false);
    expect(store.getSnapshot().selectedProfileMutationError?.message).toBe('Archive settings changed elsewhere');
    expect(store.getSnapshot().selectedProfileMutationErrorSettingKey).toBe('tabs.archive_timeout');
    expect(activeHandler.setSettingCalls).toEqual([]);
  });

  test('a stale B mutation cannot clear C same-domain saving state or apply B data', async () => {
    const store = createStore();
    await store.selectProfileTarget(target('B'));
    const delayedB = deferred<any>();
    activeHandler.enqueueMutation('archive', delayedB.promise);
    const savingB = store.commitSettingValue('tabs.archive_timeout', '48');

    await store.selectProfileTarget(target('C'));
    const delayedC = deferred<any>();
    activeHandler.enqueueMutation('archive', delayedC.promise);
    const savingC = store.commitSettingValue('tabs.archive_timeout', '24');
    expect(store.getSnapshot().selectedProfileSavingKeys.has('archive')).toBe(true);

    delayedB.resolve({
      context: context('B'),
      archive: {timeoutHours: 48},
      error: null,
    });
    expect(await savingB).toBe(false);
    expect(store.getSnapshot().selectedProfileSavingKeys.has('archive')).toBe(true);
    expect(store.getSnapshot().selectedProfileArchive).toBeNull();

    delayedC.resolve({
      context: context('C'),
      archive: {timeoutHours: 24},
      error: null,
    });
    expect(await savingC).toBe(true);
    expect(store.getSnapshot().selectedProfileSavingKeys.has('archive')).toBe(false);
    expect(store.getSnapshot().selectedProfileArchive?.timeoutHours).toBe(24);
  });

  test('surfaces structured errors and refreshes stale mutations without applying stale data', async () => {
    const store = createStore();
    await store.selectProfileTarget(target('B'));
    activeHandler.enqueueContext('B', {context: context('B', 2n), error: null});
    activeHandler.enqueueMutation('search-suggestions', {
      context: null,
      search: null,
      error: profileError(ProfileTargetErrorCode.kStaleProfileRevision, 'Profile changed elsewhere'),
    });
    const requestsBefore = activeHandler.contextRequests.length;

    expect(await store.setSelectedProfileSearchSuggestionsEnabled(false)).toBe(false);
    expect(store.getSnapshot().selectedProfileMutationError?.code).toBe(ProfileTargetErrorCode.kStaleProfileRevision);
    expect(store.getSnapshot().selectedProfileMutationError?.message).toBe('Profile changed elsewhere');
    expect(activeHandler.contextRequests.length - requestsBefore).toBe(1);
    expect(store.getSnapshot().selectedProfileSearch).toBeNull();
  });

  test('rejects mutations without a selected target/context', async () => {
    const store = createStore();
    expect(await store.setSelectedProfileArchiveTimeout(24)).toBe(false);
    expect(await store.updateSelectedProfileMetadata({name: 'No target', avatarColor: '#007AFF'})).toBe(false);
    expect(activeHandler.mutationCalls).toEqual([]);
  });

  test('drops delayed mutation success after rapid target switching', async () => {
    const store = createStore();
    await store.selectProfileTarget(target('B'));
    const delayed = deferred<any>();
    activeHandler.enqueueMutation('download-prompt', delayed.promise);
    const saving = store.setSelectedProfileDownloadPrompt(true);
    await store.selectProfileTarget(target('C'));
    delayed.resolve({context: context('B'), download: {directoryDisplayPath: 'B', promptForDownload: true}, error: null});
    expect(await saving).toBe(false);
    expect(store.getSnapshot().selectedProfileTarget).toEqual(target('C'));
    expect(store.getSnapshot().selectedProfileDownload).toBeNull();
  });

  test('invalid targets and mismatched contexts fail closed without restoring prior data', async () => {
    const store = createStore();
    await store.selectProfileTarget(target('B'));
    activeHandler.metadataResults.push({metadata: {name: 'B', avatarColor: '#bbb'}, context: context('B'), error: null});
    await store.refreshSelectedProfileMetadata();
    expect(store.getSnapshot().selectedProfileMetadata?.name).toBe('B');

    activeHandler.enqueueContext('invalid', {
      context: null,
      error: profileError(ProfileTargetErrorCode.kInvalidProfileId, 'Invalid profile target'),
    });
    await store.selectProfileTarget(target('invalid'));
    expect(store.getSnapshot().selectedProfileMetadata).toBeNull();
    expect(store.getSnapshot().selectedProfileError).toBe('Invalid profile target');

    activeHandler.enqueueContext('C', {context: context('other'), error: null});
    await store.selectProfileTarget(target('C'));
    expect(store.getSnapshot().selectedProfileContext).toBeNull();
    expect(store.getSnapshot().selectedProfileMetadata).toBeNull();
    expect(store.getSnapshot().selectedProfileError).toContain('does not match');
  });
});
