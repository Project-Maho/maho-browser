import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { ComposerDraft, ComposerDraftScope, MahoBridge } from '../../bridge/types';
import { COMPOSER_DRAFT_DEBOUNCE_MS, createComposerDraftStore, scopeKey } from '../composer-drafts';

const NEW_TASK: ComposerDraftScope = { kind: 'new_task' };
const CONVERSATION: ComposerDraftScope = { kind: 'conversation', conversationId: 'conv-1' };

function draft(text: string): ComposerDraft {
  return { version: 1, text, updatedAt: '2026-08-11T00:00:00Z' };
}

function makeBridge(overrides: Partial<MahoBridge> = {}) {
  const composerDraftGet = vi.fn(async (_scope: ComposerDraftScope) => null as ComposerDraft | null);
  const composerDraftSet = vi.fn(async (_scope: ComposerDraftScope, _text: string) => true);
  const composerDraftDelete = vi.fn(async (_scope: ComposerDraftScope) => true);
  const bridge = {
    composerDraftGet,
    composerDraftSet,
    composerDraftDelete,
    ...overrides,
  } as unknown as MahoBridge;
  return { bridge, composerDraftGet, composerDraftSet, composerDraftDelete };
}

beforeEach(() => {
  vi.useFakeTimers();
});

afterEach(() => {
  vi.useRealTimers();
  vi.clearAllMocks();
});

describe('scopeKey', () => {
  it('maps scopes to the core key suffixes', () => {
    expect(scopeKey(NEW_TASK)).toBe('new_task');
    expect(scopeKey(CONVERSATION)).toBe('conversation:conv-1');
  });
});

describe('createComposerDraftStore', () => {
  it('coalesces rapid keystrokes into a single trailing write', async () => {
    const { bridge, composerDraftSet } = makeBridge();
    const store = createComposerDraftStore(bridge);

    store.save(NEW_TASK, 'h');
    store.save(NEW_TASK, 'he');
    store.save(NEW_TASK, 'hel');
    expect(composerDraftSet).not.toHaveBeenCalled();

    await vi.advanceTimersByTimeAsync(COMPOSER_DRAFT_DEBOUNCE_MS);

    expect(composerDraftSet).toHaveBeenCalledTimes(1);
    expect(composerDraftSet).toHaveBeenCalledWith(NEW_TASK, 'hel');
  });

  it('debounces each scope independently', async () => {
    const { bridge, composerDraftSet } = makeBridge();
    const store = createComposerDraftStore(bridge);

    store.save(NEW_TASK, 'task');
    store.save(CONVERSATION, 'chat');
    await vi.advanceTimersByTimeAsync(COMPOSER_DRAFT_DEBOUNCE_MS);

    expect(composerDraftSet.mock.calls).toEqual([
      [NEW_TASK, 'task'],
      [CONVERSATION, 'chat'],
    ]);
  });

  it('serializes writes per scope so a slow older write cannot land last', async () => {
    const completions: Array<() => void> = [];
    const order: string[] = [];
    const composerDraftSet = vi.fn((_scope: ComposerDraftScope, text: string) =>
      new Promise<boolean>((resolve) => {
        completions.push(() => {
          order.push(text);
          resolve(true);
        });
      }),
    );
    const { bridge } = makeBridge({ composerDraftSet } as unknown as Partial<MahoBridge>);
    const store = createComposerDraftStore(bridge);

    store.save(NEW_TASK, 'first');
    await vi.advanceTimersByTimeAsync(COMPOSER_DRAFT_DEBOUNCE_MS);
    store.save(NEW_TASK, 'second');
    await vi.advanceTimersByTimeAsync(COMPOSER_DRAFT_DEBOUNCE_MS);

    // Only the first write has been issued: the second waits on the chain.
    expect(composerDraftSet).toHaveBeenCalledTimes(1);

    completions[0]();
    await vi.advanceTimersByTimeAsync(0);
    expect(composerDraftSet).toHaveBeenCalledTimes(2);

    completions[1]();
    await vi.advanceTimersByTimeAsync(0);
    expect(order).toEqual(['first', 'second']);
  });

  it('flush writes the pending value immediately without waiting for the timer', async () => {
    const { bridge, composerDraftSet } = makeBridge();
    const store = createComposerDraftStore(bridge);

    store.save(NEW_TASK, 'blurred');
    await store.flush(NEW_TASK);

    expect(composerDraftSet).toHaveBeenCalledWith(NEW_TASK, 'blurred');

    // The cancelled timer must not produce a second write.
    await vi.advanceTimersByTimeAsync(COMPOSER_DRAFT_DEBOUNCE_MS * 2);
    expect(composerDraftSet).toHaveBeenCalledTimes(1);
  });

  it('flush without a scope drains every pending scope', async () => {
    const { bridge, composerDraftSet } = makeBridge();
    const store = createComposerDraftStore(bridge);

    store.save(NEW_TASK, 'a');
    store.save(CONVERSATION, 'b');
    await store.flush();

    expect(composerDraftSet).toHaveBeenCalledTimes(2);
    expect(composerDraftSet).toHaveBeenCalledWith(NEW_TASK, 'a');
    expect(composerDraftSet).toHaveBeenCalledWith(CONVERSATION, 'b');
  });

  it('treats an empty draft as a delete rather than an empty write', async () => {
    const { bridge, composerDraftSet, composerDraftDelete } = makeBridge();
    const store = createComposerDraftStore(bridge);

    store.save(CONVERSATION, '');
    await vi.advanceTimersByTimeAsync(COMPOSER_DRAFT_DEBOUNCE_MS);

    expect(composerDraftSet).not.toHaveBeenCalled();
    expect(composerDraftDelete).toHaveBeenCalledWith(CONVERSATION);
  });

  it('clear cancels a pending write and deletes the row', async () => {
    const { bridge, composerDraftSet, composerDraftDelete } = makeBridge();
    const store = createComposerDraftStore(bridge);

    store.save(NEW_TASK, 'about to be sent');
    await store.clear(NEW_TASK);

    expect(composerDraftDelete).toHaveBeenCalledWith(NEW_TASK);
    await vi.advanceTimersByTimeAsync(COMPOSER_DRAFT_DEBOUNCE_MS * 2);
    expect(composerDraftSet).not.toHaveBeenCalled();
  });

  it('load returns the persisted text and null when absent', async () => {
    const { bridge, composerDraftGet } = makeBridge();
    composerDraftGet.mockResolvedValueOnce(draft('restored'));

    const store = createComposerDraftStore(bridge);
    await expect(store.load(NEW_TASK)).resolves.toBe('restored');
    await expect(store.load(NEW_TASK)).resolves.toBeNull();
  });

  it('load resolves null when the bridge rejects', async () => {
    const composerDraftGet = vi.fn(async () => {
      throw new Error('bridge exploded');
    });
    const { bridge } = makeBridge({ composerDraftGet } as unknown as Partial<MahoBridge>);
    const store = createComposerDraftStore(bridge);

    await expect(store.load(NEW_TASK)).resolves.toBeNull();
  });

  it('swallows write failures so typing keeps working', async () => {
    const composerDraftSet = vi.fn(async () => {
      throw new Error('no native transport available');
    });
    const { bridge } = makeBridge({ composerDraftSet } as unknown as Partial<MahoBridge>);
    const store = createComposerDraftStore(bridge);

    store.save(NEW_TASK, 'still typed');
    await vi.advanceTimersByTimeAsync(COMPOSER_DRAFT_DEBOUNCE_MS);

    store.save(NEW_TASK, 'and more');
    await vi.advanceTimersByTimeAsync(COMPOSER_DRAFT_DEBOUNCE_MS);
    expect(composerDraftSet).toHaveBeenCalledTimes(2);
  });

  it('is a no-op against a host bridge without draft support', async () => {
    const bridge = {} as MahoBridge;
    const store = createComposerDraftStore(bridge);

    await expect(store.load(NEW_TASK)).resolves.toBeNull();
    store.save(NEW_TASK, 'text');
    await vi.advanceTimersByTimeAsync(COMPOSER_DRAFT_DEBOUNCE_MS);
    await expect(store.clear(NEW_TASK)).resolves.toBeUndefined();
  });

  it('dispose drops pending timers without writing', async () => {
    const { bridge, composerDraftSet } = makeBridge();
    const store = createComposerDraftStore(bridge);

    store.save(NEW_TASK, 'dropped');
    store.dispose();
    await vi.advanceTimersByTimeAsync(COMPOSER_DRAFT_DEBOUNCE_MS * 2);

    expect(composerDraftSet).not.toHaveBeenCalled();
  });
});
