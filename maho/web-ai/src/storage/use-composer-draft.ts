/**
 * useComposerDraft — screen-facing glue for the composer draft store.
 *
 * The hook owns the restore race and the flush triggers so screens only have to
 * report draft edits and send outcomes. Reducers stay pure: every persistence
 * call happens here, never inside a reducer.
 *
 * Restore rule: a late `load` response is applied ONLY if the scope is unchanged
 * and nothing has been typed / auto-submitted since the request began. That keeps
 * an auto-submitted goal or fresh typing from being clobbered by stale storage.
 */
import { useCallback, useEffect, useMemo, useRef } from 'preact/hooks';
import type { ComposerDraftScope, MahoBridge } from '../bridge/types';
import { createComposerDraftStore, scopeKey } from './composer-drafts';

export interface UseComposerDraftOptions {
  bridge: MahoBridge;
  /** Current draft scope, or null while the scope is not yet decided. */
  scope: ComposerDraftScope | null;
  /** Applies restored text once when entering a scope. */
  onRestore: (text: string) => void;
  /** Set when the composer must not be hydrated (e.g. an auto-submitted goal). */
  suppressRestore?: boolean;
}

export interface ComposerDraftController {
  /** Report a composer edit (debounced persist). */
  handleDraftChange: (text: string) => void;
  /** Persist the current text immediately (blur / visibility change). */
  flush: () => void;
  /** Capture the submitted revision; acknowledge without deleting later edits. */
  captureSend: () => (destination?: ComposerDraftScope) => Promise<void>;
  /** Delete the persisted draft for the current scope. Await before reset. */
  clear: () => Promise<void>;
  /** Delete the persisted draft for an explicit scope (e.g. the pre-promotion one). */
  clearScope: (scope: ComposerDraftScope) => Promise<void>;
}

export function useComposerDraft({
  bridge,
  scope,
  onRestore,
  suppressRestore = false,
}: UseComposerDraftOptions): ComposerDraftController {
  const store = useMemo(() => createComposerDraftStore(bridge), [bridge]);
  const onRestoreRef = useRef(onRestore);
  const suppressRestoreRef = useRef(suppressRestore);
  const dirtyRef = useRef(false);
  const editRef = useRef({ revision: 0, text: '' });
  const scopeRef = useRef<ComposerDraftScope | null>(scope);

  onRestoreRef.current = onRestore;
  suppressRestoreRef.current = suppressRestore;
  scopeRef.current = scope;

  const currentKey = scope === null ? null : scopeKey(scope);

  useEffect(() => {
    if (scope === null || currentKey === null) return;

    // Any keystroke or goal auto-submit between request and response wins.
    dirtyRef.current = false;
    let cancelled = false;

    void store.load(scope).then((text) => {
      if (cancelled || text === null || text === '') return;
      if (dirtyRef.current || suppressRestoreRef.current) return;
      const activeScope = scopeRef.current;
      if (activeScope === null || scopeKey(activeScope) !== currentKey) return;
      onRestoreRef.current(text);
    });

    return () => {
      cancelled = true;
      void store.flush(scope);
    };
  }, [currentKey, scope, store]);

  const flush = useCallback(() => {
    const activeScope = scopeRef.current;
    if (activeScope === null) return;
    void store.flush(activeScope);
  }, [store]);

  useEffect(() => {
    const onVisibilityChange = () => {
      if (document.visibilityState === 'hidden') {
        flush();
      }
    };
    document.addEventListener('visibilitychange', onVisibilityChange);
    return () => {
      document.removeEventListener('visibilitychange', onVisibilityChange);
    };
  }, [flush]);

  useEffect(() => {
    return () => {
      // flush synchronously queues every pending write before dispose drops timers.
      void store.flush();
      store.dispose();
    };
  }, [store]);

  const handleDraftChange = useCallback(
    (text: string) => {
      dirtyRef.current = true;
      editRef.current = { revision: editRef.current.revision + 1, text };
      const activeScope = scopeRef.current;
      if (activeScope === null) return;
      store.save(activeScope, text);
    },
    [store],
  );

  const clearScope = useCallback(
    async (target: ComposerDraftScope) => {
      await store.clear(target);
    },
    [store],
  );

  const clear = useCallback(async () => {
    const activeScope = scopeRef.current;
    if (activeScope === null) return;
    await store.clear(activeScope);
  }, [store]);

  const captureSend = useCallback(() => {
    const submittedScope = scopeRef.current;
    const revision = editRef.current.revision;
    return async (destination?: ComposerDraftScope) => {
      if (submittedScope === null) return;
      if (editRef.current.revision === revision) {
        await store.clear(submittedScope);
      } else if (destination && scopeKey(destination) !== scopeKey(submittedScope)) {
        store.save(destination, editRef.current.text);
        await store.flush(destination);
        await store.clear(submittedScope);
      } else {
        await store.flush(submittedScope);
      }
    };
  }, [store]);

  return { captureSend, clear, clearScope, flush, handleDraftChange };
}
