import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {
  COMPOSER_DRAFT_DEBOUNCE_MS,
  ComposerDraftPersistence,
  type ComposerDraftRemote,
  type ComposerDraftScope,
} from '../features/compact/composer-draft-persistence.js';

const NEW_TASK = {kind: 'new_task'} as const satisfies ComposerDraftScope;
const CONVERSATION = {
  kind: 'conversation',
  conversationId: 'session-1',
} as const satisfies ComposerDraftScope;

function createDeferred<T>(): {
  readonly promise: Promise<T>;
  readonly resolve: (value: T) => void;
} {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>(complete => {
    resolve = complete;
  });
  return {promise, resolve};
}

function createRemote(): ComposerDraftRemote & {
  composerDraftDelete: ReturnType<typeof vi.fn>;
  composerDraftGet: ReturnType<typeof vi.fn>;
  composerDraftSet: ReturnType<typeof vi.fn>;
} {
  return {
    composerDraftDelete: vi.fn(async () => ({ok: true})),
    composerDraftGet: vi.fn(async () => ({draftJson: null})),
    composerDraftSet: vi.fn(async () => ({ok: true})),
  };
}

describe('ComposerDraftPersistence', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it('loads the stored text for the requested scope', async () => {
    // Given
    const remote = createRemote();
    remote.composerDraftGet.mockResolvedValue({
      draftJson: JSON.stringify({
        version: 1,
        text: 'restored desktop draft',
        updatedAt: '2026-08-11T00:00:00Z',
      }),
    });
    const persistence = new ComposerDraftPersistence(remote);

    // When
    const text = await persistence.load(CONVERSATION);

    // Then
    expect(text).toBe('restored desktop draft');
    expect(remote.composerDraftGet).toHaveBeenCalledWith(
        JSON.stringify(CONVERSATION));
  });

  it('persists only the latest text after the trailing debounce', async () => {
    // Given
    const remote = createRemote();
    const persistence = new ComposerDraftPersistence(remote);

    // When
    persistence.schedule(NEW_TASK, 'first');
    persistence.schedule(NEW_TASK, 'final draft');
    await vi.advanceTimersByTimeAsync(COMPOSER_DRAFT_DEBOUNCE_MS - 1);

    // Then
    expect(remote.composerDraftSet).not.toHaveBeenCalled();

    // When
    await vi.advanceTimersByTimeAsync(1);

    // Then
    expect(remote.composerDraftSet).toHaveBeenCalledOnce();
    expect(remote.composerDraftSet).toHaveBeenCalledWith(
        JSON.stringify(NEW_TASK), 'final draft');
  });

  it('flushes a pending write immediately for blur or hidden-page handling', async () => {
    // Given
    const remote = createRemote();
    const persistence = new ComposerDraftPersistence(remote);
    persistence.schedule(CONVERSATION, 'flush now');

    // When
    await persistence.flush(CONVERSATION);

    // Then
    expect(remote.composerDraftSet).toHaveBeenCalledOnce();
    expect(remote.composerDraftSet).toHaveBeenCalledWith(
        JSON.stringify(CONVERSATION), 'flush now');
  });

  it('serializes newer writes behind an in-flight write for the same scope', async () => {
    // Given
    const remote = createRemote();
    const firstWrite = createDeferred<{ok: boolean}>();
    remote.composerDraftSet
        .mockReturnValueOnce(firstWrite.promise)
        .mockResolvedValueOnce({ok: true});
    const persistence = new ComposerDraftPersistence(remote);
    persistence.schedule(NEW_TASK, 'older');
    const olderFlush = persistence.flush(NEW_TASK);
    await Promise.resolve();
    persistence.schedule(NEW_TASK, 'newer');
    const newerFlush = persistence.flush(NEW_TASK);

    // Then
    expect(remote.composerDraftSet).toHaveBeenCalledOnce();

    // When
    firstWrite.resolve({ok: true});
    await Promise.all([olderFlush, newerFlush]);

    // Then
    expect(remote.composerDraftSet.mock.calls).toEqual([
      [JSON.stringify(NEW_TASK), 'older'],
      [JSON.stringify(NEW_TASK), 'newer'],
    ]);
  });

  it('keeps writes for separate scopes independent', async () => {
    // Given
    const remote = createRemote();
    const persistence = new ComposerDraftPersistence(remote);

    // When
    persistence.schedule(NEW_TASK, 'new task text');
    persistence.schedule(CONVERSATION, 'conversation text');
    await persistence.flush();

    // Then
    expect(remote.composerDraftSet.mock.calls).toEqual([
      [JSON.stringify(NEW_TASK), 'new task text'],
      [JSON.stringify(CONVERSATION), 'conversation text'],
    ]);
  });

  it('treats explicit false mutation responses as non-blocking failures', async () => {
    // Given
    const remote = createRemote();
    remote.composerDraftSet.mockResolvedValue({ok: false});
    remote.composerDraftDelete.mockResolvedValue({ok: false});
    const warn = vi.spyOn(console, 'warn').mockImplementation(() => undefined);
    const persistence = new ComposerDraftPersistence(remote);

    // When / Then
    persistence.schedule(NEW_TASK, 'not accepted');
    await expect(persistence.flush(NEW_TASK)).resolves.toBeUndefined();
    await expect(persistence.clear(NEW_TASK)).resolves.toBeUndefined();
    expect(warn).toHaveBeenCalledTimes(2);
    warn.mockRestore();
  });

  it('deletes the requested scope without using browser storage', async () => {
    // Given
    const remote = createRemote();
    const persistence = new ComposerDraftPersistence(remote);

    // When
    await persistence.clear(NEW_TASK);

    // Then
    expect(remote.composerDraftDelete).toHaveBeenCalledWith(
        JSON.stringify(NEW_TASK));
  });
});
