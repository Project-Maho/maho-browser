/**
 * Composer draft persistence adapter.
 *
 * Drafts live in maho-core's settings table (via the native bridge), never in
 * localStorage/sessionStorage: web storage is wiped by iOS website-data clearing
 * and diverges per platform.
 *
 * Rules enforced here (screens must not re-implement them):
 *  - Writes are debounced with a 300 ms trailing timer per scope.
 *  - Writes for one scope are serialized on a promise chain, so a stale write can
 *    never land after a newer one.
 *  - `flush` (textarea blur, `visibilitychange` → hidden) bypasses the debounce.
 *  - An empty string deletes the row (native contract), so `save(scope, '')` is a
 *    delete rather than an empty-payload write.
 *  - Persistence failures are swallowed: the in-memory draft is authoritative for
 *    the UI, and a bridge without draft support (or a rejecting one) must never
 *    break typing or sending.
 */
import type { ComposerDraft, ComposerDraftScope, MahoBridge } from '../bridge/types';

export const COMPOSER_DRAFT_DEBOUNCE_MS = 300;

export interface ComposerDraftStore {
  /** Reads the persisted draft text, or null when absent/unsupported/failed. */
  load(scope: ComposerDraftScope): Promise<string | null>;
  /** Queues a debounced write for `scope`. Empty text queues a delete. */
  save(scope: ComposerDraftScope, text: string): void;
  /** Writes any pending value for `scope` immediately (all scopes when omitted). */
  flush(scope?: ComposerDraftScope): Promise<void>;
  /** Deletes the persisted draft for `scope` and drops any pending write. */
  clear(scope: ComposerDraftScope): Promise<void>;
  /** Cancels pending timers. Does not flush. */
  dispose(): void;
}

interface ScopeState {
  chain: Promise<void>;
  pendingText: string | null;
  timer: ReturnType<typeof setTimeout> | null;
}

export function scopeKey(scope: ComposerDraftScope): string {
  return scope.kind === 'new_task' ? 'new_task' : `conversation:${scope.conversationId}`;
}

export function createComposerDraftStore(bridge: MahoBridge): ComposerDraftStore {
  const scopes = new Map<string, ScopeState>();

  function stateFor(key: string): ScopeState {
    const existing = scopes.get(key);
    if (existing) return existing;
    const created: ScopeState = { chain: Promise.resolve(), pendingText: null, timer: null };
    scopes.set(key, created);
    return created;
  }

  function cancelTimer(state: ScopeState): void {
    if (state.timer !== null) {
      clearTimeout(state.timer);
      state.timer = null;
    }
  }

  function enqueue(state: ScopeState, operation: () => Promise<unknown>): Promise<void> {
    const next = state.chain.then(
      () => operation().then(
        () => undefined,
        () => undefined,
      ),
      () => undefined,
    );
    state.chain = next;
    return next;
  }

  function writeNow(scope: ComposerDraftScope, key: string, text: string): Promise<void> {
    const state = stateFor(key);
    return enqueue(state, async () => {
      if (text === '') {
        await bridge.composerDraftDelete?.(scope);
        return;
      }
      await bridge.composerDraftSet?.(scope, text);
    });
  }

  return {
    async load(scope) {
      const key = scopeKey(scope);
      const state = stateFor(key);
      let draft: ComposerDraft | null = null;
      await enqueue(state, async () => {
        draft = (await bridge.composerDraftGet?.(scope)) ?? null;
      });
      const resolved = draft as ComposerDraft | null;
      return resolved === null ? null : resolved.text;
    },

    save(scope, text) {
      const key = scopeKey(scope);
      const state = stateFor(key);
      state.pendingText = text;
      cancelTimer(state);
      state.timer = setTimeout(() => {
        state.timer = null;
        const pending = state.pendingText;
        state.pendingText = null;
        if (pending === null) return;
        void writeNow(scope, key, pending);
      }, COMPOSER_DRAFT_DEBOUNCE_MS);
    },

    async flush(scope) {
      if (scope === undefined) {
        await Promise.all(
          [...scopes.keys()].map((key) => {
            const state = scopes.get(key);
            if (!state) return Promise.resolve();
            return flushState(key, state);
          }),
        );
        return;
      }
      const key = scopeKey(scope);
      const state = scopes.get(key);
      if (!state) return;
      await flushState(key, state, scope);
    },

    async clear(scope) {
      const key = scopeKey(scope);
      const state = stateFor(key);
      cancelTimer(state);
      state.pendingText = null;
      await enqueue(state, async () => {
        await bridge.composerDraftDelete?.(scope);
      });
    },

    dispose() {
      for (const state of scopes.values()) {
        cancelTimer(state);
        state.pendingText = null;
      }
    },
  };

  function flushState(
    key: string,
    state: ScopeState,
    knownScope?: ComposerDraftScope,
  ): Promise<void> {
    cancelTimer(state);
    const pending = state.pendingText;
    state.pendingText = null;
    if (pending === null) {
      return state.chain;
    }
    return writeNow(knownScope ?? scopeFromKey(key), key, pending);
  }
}

function scopeFromKey(key: string): ComposerDraftScope {
  if (key === 'new_task') {
    return { kind: 'new_task' };
  }
  return { kind: 'conversation', conversationId: key.slice('conversation:'.length) };
}
