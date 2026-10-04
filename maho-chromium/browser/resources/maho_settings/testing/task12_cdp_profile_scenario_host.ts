import type {
  PageHandlerRemote,
  ProfileArchiveSettingsResult,
  ProfileDownloadSettingsResult,
  ProfileInfo,
  ProfileMetadataResult,
  ProfileSearchSettingsResult,
  ProfileTarget,
  ProfileTargetError,
  SelectedProfileContext,
  SelectedProfileContextResult,
} from '../maho_settings.mojom-webui.js';
import type {CdpEventTransport} from './task12_live_lifecycle.js';
import {
  ProfileScenarioError,
  type ProfileScenarioContext,
  type ProfileScenarioHost,
  type ProfileScenarioKind,
  type ProfileScenarioSeed,
  type ProfileScenarioSnapshot,
} from './task12_profile_scenarios.js';

export type ProfileScenarioGeneratedProxy = Pick<PageHandlerRemote,
  'getProfiles' | 'getSelectedProfileContext' | 'createProfile' | 'deleteProfile' |
  'updateSelectedProfileMetadata' |
  'setSelectedProfileDefaultSearchEngine' | 'setSelectedProfileSearchSuggestionsEnabled' |
  'setSelectedProfileDownloadPrompt' | 'setSelectedProfileArchiveTimeout' |
  'getSelectedProfileMetadata' |
  'getSelectedProfileSearchSettings' | 'getSelectedProfileDownloadSettings' |
  'getSelectedProfileArchiveSettings'>;

type ProfileResult =
  | SelectedProfileContextResult
  | ProfileMetadataResult
  | ProfileSearchSettingsResult
  | ProfileDownloadSettingsResult
  | ProfileArchiveSettingsResult;

export class ProfileScenarioProxyError extends Error {
  constructor(
    readonly code: number,
    message: string,
    readonly currentContextRevision: bigint,
    readonly currentProfileRevision: bigint | null,
  ) {
    super(message);
    this.name = 'ProfileScenarioProxyError';
  }
}

function throwResultError(error: ProfileTargetError | null): void {
  if (error) {
    throw new ProfileScenarioProxyError(
      error.code,
      error.message,
      error.currentContextRevision,
      error.currentProfileRevision,
    );
  }
}

function requireContext(result: ProfileResult, operation: string): SelectedProfileContext {
  throwResultError(result.error);
  if (!result.context) throw new Error(`${operation} returned no profile context`);
  return result.context;
}

function target(context: Pick<SelectedProfileContext, 'profileId' | 'targetToken'>): ProfileTarget {
  return {profileId: context.profileId, targetToken: context.targetToken};
}

function snapshotContext(context: SelectedProfileContext): ProfileScenarioContext {
  return {
    targetToken: context.targetToken,
    contextRevision: context.contextRevision,
    profileRevision: context.profileRevision,
  };
}

export function createGeneratedProxyProfileScenarioHost(
  proxy: ProfileScenarioGeneratedProxy,
): ProfileScenarioHost {
  const contexts = new Map<string, SelectedProfileContext>();
  const registered = new Map<ProfileScenarioKind, {id: string; name: string}>();
  const createdIds = new Set<string>();
  let restoredSelectedProfileId: string | null = null;

  const resolveContext = async (profileId: string, targetToken = '') => {
    const response = await proxy.getSelectedProfileContext({profileId, targetToken});
    const context = requireContext(response.result, 'getSelectedProfileContext');
    contexts.set(profileId, context);
    return context;
  };

  const currentContext = async (profileId: string) =>
    contexts.get(profileId) ?? resolveContext(profileId);

  const acceptMutation = (profileId: string, result: ProfileResult, operation: string) => {
    const context = requireContext(result, operation);
    contexts.set(profileId, context);
    return context;
  };

  const updateMetadata = async (
    profileId: string,
    metadata: ProfileScenarioSeed['metadata'],
  ) => {
    const context = await currentContext(profileId);
    const response = await proxy.updateSelectedProfileMetadata(
      target(context),
      context.contextRevision,
      {...metadata, expectedProfileRevision: context.profileRevision},
    );
    acceptMutation(profileId, response.result, 'updateSelectedProfileMetadata');
  };

  return {
    async snapshot() {
      const {profiles} = await proxy.getProfiles();
      const selected = profiles.find(profile => profile.isActive) ?? profiles[0];
      if (!selected) {
        return {
          targetToken: '', contextRevision: 0n, profileRevision: 0n,
          profiles: [], selectedProfileId: null, ready: true,
        };
      }
      const context = await resolveContext(selected.id);
      return {
        ...snapshotContext(context),
        profiles: profiles.map(profile => ({id: profile.id, name: profile.name})),
        selectedProfileId: selected.id,
        ready: true,
      };
    },

    async listProfileNames() {
      return (await proxy.getProfiles()).profiles.map(profile => profile.name);
    },

    async createProfile(name, metadata) {
      const {profile} = await proxy.createProfile(name);
      if (!profile) throw new Error('createProfile returned no profile');
      createdIds.add(profile.id);
      await resolveContext(profile.id);
      await updateMetadata(profile.id, metadata);
      return profile.id;
    },

    registerCreatedProfile(kind, id, name) {
      registered.set(kind, {id, name});
    },

    async seedProfile(profileId, seed) {
      await updateMetadata(profileId, seed.metadata);

      let context = await currentContext(profileId);
      let searchResponse = await proxy.setSelectedProfileDefaultSearchEngine(
        target(context), context.contextRevision, seed.searchKeyword, context.profileRevision);
      acceptMutation(profileId, searchResponse.result, 'setSelectedProfileDefaultSearchEngine');

      context = await currentContext(profileId);
      searchResponse = await proxy.setSelectedProfileSearchSuggestionsEnabled(
        target(context), context.contextRevision, seed.suggestions, context.profileRevision);
      acceptMutation(profileId, searchResponse.result, 'setSelectedProfileSearchSuggestionsEnabled');

      context = await currentContext(profileId);
      const downloadResponse = await proxy.setSelectedProfileDownloadPrompt(
        target(context), context.contextRevision, seed.download, context.profileRevision);
      acceptMutation(profileId, downloadResponse.result, 'setSelectedProfileDownloadPrompt');

      context = await currentContext(profileId);
      const archiveResponse = await proxy.setSelectedProfileArchiveTimeout(
        target(context), context.contextRevision, seed.archive, context.profileRevision);
      acceptMutation(profileId, archiveResponse.result, 'setSelectedProfileArchiveTimeout');
    },

    async readProfile(profileId) {
      const context = await currentContext(profileId);
      const profileTarget = target(context);
      const [metadataResponse, searchResponse, downloadResponse, archiveResponse] =
        await Promise.all([
          proxy.getSelectedProfileMetadata(profileTarget),
          proxy.getSelectedProfileSearchSettings(profileTarget),
          proxy.getSelectedProfileDownloadSettings(profileTarget),
          proxy.getSelectedProfileArchiveSettings(profileTarget),
        ]);
      const metadata = metadataResponse.result;
      const search = searchResponse.result;
      const download = downloadResponse.result;
      const archive = archiveResponse.result;
      requireContext(metadata, 'getSelectedProfileMetadata');
      requireContext(search, 'getSelectedProfileSearchSettings');
      requireContext(download, 'getSelectedProfileDownloadSettings');
      requireContext(archive, 'getSelectedProfileArchiveSettings');
      if (!metadata.metadata || !search.search ||
          !download.download || !archive.archive) {
        throw new Error('Profile readback returned an incomplete result');
      }
      return {
        metadata: metadata.metadata,
        homepage: '',
        searchKeyword: search.search.engines.find(engine => engine.isDefault)?.keyword ?? '',
        suggestions: search.search.suggestionsEnabled,
        download: download.download.promptForDownload,
        archive: archive.archive.timeoutHours,
      } satisfies ProfileScenarioSeed;
    },

    async deleteProfile(profileId) {
      const {success} = await proxy.deleteProfile(profileId);
      if (!success) throw new Error(`deleteProfile failed for ${profileId}`);
      contexts.delete(profileId);
      createdIds.delete(profileId);
    },

    async restoreSnapshot(snapshot) {
      const baselineIds = new Set(snapshot.profiles.map(profile => profile.id));
      for (const profileId of [...createdIds]) {
        if (baselineIds.has(profileId)) continue;
        const {success} = await proxy.deleteProfile(profileId);
        if (!success) throw new Error(`deleteProfile failed while restoring ${profileId}`);
        contexts.delete(profileId);
        createdIds.delete(profileId);
      }
      restoredSelectedProfileId = snapshot.selectedProfileId;
    },

    async assertRequestCurrent(request) {
      let entry = [...contexts.values()].find(context => context.targetToken === request.targetToken);
      if (!entry && restoredSelectedProfileId) {
        const restored = await resolveContext(restoredSelectedProfileId, request.targetToken);
        if (restored.targetToken === request.targetToken) entry = restored;
      }
      if (!entry) {
        throw new ProfileScenarioError('STALE_PROFILE_REQUEST', 'Captured profile target is no longer available');
      }
      const fresh = await resolveContext(entry.profileId, request.targetToken);
      if (fresh.targetToken !== request.targetToken ||
          fresh.contextRevision !== request.contextRevision ||
          fresh.profileRevision !== request.profileRevision) {
        throw new ProfileScenarioError('STALE_PROFILE_REQUEST', 'Captured profile request is stale');
      }
    },
  };
}

interface TaggedBigInt {readonly __mahoTask12BigInt: string}

function decodeBigInts(value: unknown): unknown {
  if (Array.isArray(value)) return value.map(decodeBigInts);
  if (value === null || typeof value !== 'object') return value;
  const record = value as Record<string, unknown>;
  if (Object.keys(record).length === 1 && typeof record.__mahoTask12BigInt === 'string') {
    return BigInt(record.__mahoTask12BigInt);
  }
  return Object.fromEntries(Object.entries(record).map(([key, entry]) => [key, decodeBigInts(entry)]));
}

function encodeBigInts(value: unknown): unknown {
  if (typeof value === 'bigint') return {__mahoTask12BigInt: value.toString()} satisfies TaggedBigInt;
  if (Array.isArray(value)) return value.map(encodeBigInts);
  if (value === null || typeof value !== 'object') return value;
  return Object.fromEntries(Object.entries(value).map(([key, entry]) => [key, encodeBigInts(entry)]));
}

function cdpResultValue(response: unknown): unknown {
  if (response === null || typeof response !== 'object') return undefined;
  const outer = response as Record<string, unknown>;
  const result = outer.result;
  if (result === null || typeof result !== 'object') return undefined;
  const remote = result as Record<string, unknown>;
  if (remote.exceptionDetails) {
    const details = remote.exceptionDetails as Record<string, unknown>;
    throw new Error(typeof details.text === 'string' ? details.text : 'Runtime.evaluate failed');
  }
  const nested = remote.result;
  if (nested !== null && typeof nested === 'object' && 'value' in nested) {
    return (nested as Record<string, unknown>).value;
  }
  return remote.value;
}

function createCdpGeneratedProxy(transport: Pick<CdpEventTransport, 'send'>): ProfileScenarioGeneratedProxy {
  const call = async <T>(method: string, args: readonly unknown[] = []): Promise<T> => {
    const serializedArgs = JSON.stringify(encodeBigInts(args));
    const expression = `(async function(){
      const revive = value => {
        if (Array.isArray(value)) return value.map(revive);
        if (value === null || typeof value !== 'object') return value;
        if (Object.keys(value).length === 1 && typeof value.__mahoTask12BigInt === 'string') {
          return BigInt(value.__mahoTask12BigInt);
        }
        return Object.fromEntries(Object.entries(value).map(([key, entry]) => [key, revive(entry)]));
      };
      const encode = value => {
        if (typeof value === 'bigint') return {__mahoTask12BigInt:value.toString()};
        if (Array.isArray(value)) return value.map(encode);
        if (value === null || typeof value !== 'object') return value;
        return Object.fromEntries(Object.entries(value).map(([key, entry]) => [key, encode(entry)]));
      };
      const handler = window.settingsStore.getHandler();
      if (!handler || typeof handler[${JSON.stringify(method)}] !== 'function') {
        throw new Error(${JSON.stringify(`${method} is unavailable`)});
      }
      return encode(await handler[${JSON.stringify(method)}](...revive(${serializedArgs})));
    })()`;
    const response = await transport.send('Runtime.evaluate', {
      expression,
      awaitPromise: true,
      returnByValue: true,
    });
    const value = cdpResultValue(response);
    if (value === undefined) throw new Error(`${method} returned no by-value result`);
    return decodeBigInts(value) as T;
  };

  return {
    getProfiles: () => call('getProfiles'),
    getSelectedProfileContext: value => call('getSelectedProfileContext', [value]),
    createProfile: name => call('createProfile', [name]),
    deleteProfile: profileId => call('deleteProfile', [profileId]),
    updateSelectedProfileMetadata: (value, revision, update) =>
      call('updateSelectedProfileMetadata', [value, revision, update]),
    setSelectedProfileDefaultSearchEngine: (value, contextRevision, keyword, profileRevision) =>
      call('setSelectedProfileDefaultSearchEngine', [value, contextRevision, keyword, profileRevision]),
    setSelectedProfileSearchSuggestionsEnabled: (value, contextRevision, enabled, profileRevision) =>
      call('setSelectedProfileSearchSuggestionsEnabled', [value, contextRevision, enabled, profileRevision]),
    setSelectedProfileDownloadPrompt: (value, contextRevision, enabled, profileRevision) =>
      call('setSelectedProfileDownloadPrompt', [value, contextRevision, enabled, profileRevision]),
    setSelectedProfileArchiveTimeout: (value, contextRevision, timeoutHours, profileRevision) =>
      call('setSelectedProfileArchiveTimeout', [value, contextRevision, timeoutHours, profileRevision]),
    getSelectedProfileMetadata: value => call('getSelectedProfileMetadata', [value]),
    getSelectedProfileSearchSettings: value => call('getSelectedProfileSearchSettings', [value]),
    getSelectedProfileDownloadSettings: value => call('getSelectedProfileDownloadSettings', [value]),
    getSelectedProfileArchiveSettings: value => call('getSelectedProfileArchiveSettings', [value]),
  };
}

export function createCdpProfileScenarioHost(
  transport: Pick<CdpEventTransport, 'send'>,
): ProfileScenarioHost {
  return createGeneratedProxyProfileScenarioHost(createCdpGeneratedProxy(transport));
}
