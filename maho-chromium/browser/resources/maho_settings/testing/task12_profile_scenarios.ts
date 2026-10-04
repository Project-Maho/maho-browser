export type ProfileScenarioKind = 'A' | 'B' | 'disposable' | 'lifecycle';

export interface ProfileScenarioProfile {
  readonly id: string;
  readonly name: string;
}

export interface ProfileScenarioContext {
  readonly targetToken: string;
  readonly contextRevision: bigint;
  readonly profileRevision: bigint;
}

export interface ProfileScenarioContextInput {
  readonly targetToken: unknown;
  readonly contextRevision: unknown;
  readonly profileRevision: unknown;
}

export interface SerializedProfileScenarioContext {
  readonly targetToken: string;
  readonly contextRevision: string;
  readonly profileRevision: string;
}

export interface ProfileScenarioSnapshot extends ProfileScenarioContext {
  readonly profiles: ProfileScenarioProfile[];
  readonly selectedProfileId: string | null;
  readonly ready: boolean;
}

export interface ProfileScenarioSeed {
  readonly metadata: {readonly name: string; readonly avatarColor: string};
  readonly homepage: string;
  readonly searchKeyword: string;
  readonly suggestions: boolean;
  readonly download: boolean;
  readonly archive: number;
}

export type ProfileScenarioRequestContext = ProfileScenarioContext;

export interface ProfileScenarioHost {
  snapshot(): Promise<ProfileScenarioSnapshot>;
  listProfileNames(): Promise<readonly string[]>;
  createProfile(name: string, metadata: ProfileScenarioSeed['metadata']): Promise<string>;
  registerCreatedProfile(kind: ProfileScenarioKind, id: string, name: string): void;
  seedProfile(id: string, seed: ProfileScenarioSeed): Promise<void>;
  readProfile(id: string): Promise<unknown>;
  deleteProfile?: (id: string) => Promise<void>;
  restoreSnapshot(snapshot: ProfileScenarioSnapshot): Promise<void>;
  assertRequestCurrent?: (request: ProfileScenarioRequestContext) => Promise<void>;
}

export type ProfileScenarioErrorCode =
  | 'INVALID_RUN_ID'
  | 'PROFILE_NAME_COLLISION'
  | 'STALE_PROFILE_REQUEST'
  | 'MISSING_DELETING_LIFECYCLE_API';

export class ProfileScenarioError extends Error {
  constructor(readonly code: ProfileScenarioErrorCode, message: string) {
    super(message);
    this.name = 'ProfileScenarioError';
  }
}

export interface ProfileScenarioOptions {
  readonly runId: string;
  readonly newerContext?: ProfileScenarioSnapshot;
  readonly deleteLifecycle?: boolean;
  readonly cancel?: boolean;
}

export interface ProfileScenarioResult {
  readonly names: readonly [string, string, string, string];
  readonly ids: Readonly<Record<ProfileScenarioKind, string>>;
  readonly readbacks: {readonly A: ProfileScenarioSeed; readonly B: ProfileScenarioSeed};
}

const RUN_ID_PATTERN = /^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$/;
const CANONICAL_REVISION_PATTERN = /^(?:0|[1-9][0-9]*)$/;

function parseRevision(value: unknown, field: string): bigint {
  if (typeof value !== 'string' || !CANONICAL_REVISION_PATTERN.test(value)) {
    throw new TypeError(`${field} must be a canonical nonnegative decimal integer string`);
  }
  return BigInt(value);
}

export function parseProfileScenarioContext(
  serialized: ProfileScenarioContextInput,
): ProfileScenarioContext {
  if (typeof serialized.targetToken !== 'string') {
    throw new TypeError('targetToken must be a string');
  }
  return {
    targetToken: serialized.targetToken,
    contextRevision: parseRevision(serialized.contextRevision, 'contextRevision'),
    profileRevision: parseRevision(serialized.profileRevision, 'profileRevision'),
  };
}

export function serializeProfileScenarioContext(
  context: ProfileScenarioContext,
): SerializedProfileScenarioContext {
  if (context.contextRevision < 0n || context.profileRevision < 0n) {
    throw new TypeError('Profile scenario revisions must be nonnegative');
  }
  return {
    targetToken: context.targetToken,
    contextRevision: context.contextRevision.toString(),
    profileRevision: context.profileRevision.toString(),
  };
}

const seeds: Readonly<Record<'A' | 'B', ProfileScenarioSeed>> = {
  A: {
    metadata: {name: 'Task 12 Personal', avatarColor: '#2563EB'},
    homepage: 'https://example.com/task12/personal',
    searchKeyword: 'duckduckgo.com',
    suggestions: false,
    download: false,
    archive: 24,
  },
  B: {
    metadata: {name: 'Task 12 Work', avatarColor: '#7C3AED'},
    homepage: 'https://example.com/task12/work',
    searchKeyword: 'google.com',
    suggestions: true,
    download: true,
    archive: 72,
  },
};

function namesFor(runId: string): readonly [string, string, string, string] {
  if (!RUN_ID_PATTERN.test(runId)) {
    throw new ProfileScenarioError('INVALID_RUN_ID', `Invalid Task 12 runId: ${runId}`);
  }
  const prefix = `maho-task12-${runId}`;
  return [`${prefix}-a`, `${prefix}-b`, `${prefix}-disposable`, `${prefix}-lifecycle`];
}

function requestContext(snapshot: ProfileScenarioSnapshot): ProfileScenarioRequestContext {
  return {
    targetToken: snapshot.targetToken,
    contextRevision: snapshot.contextRevision,
    profileRevision: snapshot.profileRevision,
  };
}

function didContextAdvance(
  captured: ProfileScenarioRequestContext,
  newer: ProfileScenarioSnapshot,
): boolean {
  // A token change alone is not progress: both monotonic revisions must move.
  return newer.contextRevision > captured.contextRevision &&
      newer.profileRevision > captured.profileRevision;
}

export async function runProfileScenarios(
  host: ProfileScenarioHost,
  options: ProfileScenarioOptions,
): Promise<ProfileScenarioResult> {
  const before = structuredClone(await host.snapshot());
  const capturedRequest = requestContext(before);
  const names = namesFor(options.runId);
  const existingNames = new Set(await host.listProfileNames());
  const collision = names.find(name => existingNames.has(name));
  if (collision) {
    throw new ProfileScenarioError(
      'PROFILE_NAME_COLLISION',
      `Task 12 profile already exists: ${collision}`,
    );
  }

  const ids = {} as Record<ProfileScenarioKind, string>;
  const kinds: readonly ProfileScenarioKind[] = ['A', 'B', 'disposable', 'lifecycle'];
  for (let index = 0; index < kinds.length; index++) {
    const kind = kinds[index];
    const metadata = kind === 'A' || kind === 'B' ? seeds[kind].metadata : {
      name: names[index],
      avatarColor: kind === 'disposable' ? '#DC2626' : '#059669',
    };
    const id = await host.createProfile(names[index], metadata);
    ids[kind] = id;
    host.registerCreatedProfile(kind, id, names[index]);
  }

  await host.seedProfile(ids.A, seeds.A);
  await host.seedProfile(ids.B, seeds.B);
  const readbacks = {
    A: await host.readProfile(ids.A) as ProfileScenarioSeed,
    B: await host.readProfile(ids.B) as ProfileScenarioSeed,
  };

  await host.assertRequestCurrent?.(capturedRequest);
  if (options.newerContext && !didContextAdvance(capturedRequest, options.newerContext)) {
    throw new ProfileScenarioError(
      'STALE_PROFILE_REQUEST',
      'The replacement profile context did not advance both captured revisions',
    );
  }

  if (options.deleteLifecycle) {
    if (!host.deleteProfile) {
      throw new ProfileScenarioError(
        'MISSING_DELETING_LIFECYCLE_API',
        'The injected host does not expose profile lifecycle deletion',
      );
    }
    await host.deleteProfile(ids.lifecycle);
  }

  if (options.cancel) await host.restoreSnapshot(before);

  return {names, ids, readbacks};
}
