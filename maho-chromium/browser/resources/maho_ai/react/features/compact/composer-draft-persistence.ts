export const COMPOSER_DRAFT_DEBOUNCE_MS = 300;

export type ComposerDraftScope =
    | {readonly kind: 'new_task'}
    | {readonly kind: 'conversation'; readonly conversationId: string};

export interface ComposerDraftRemote {
  composerDraftGet(scopeJson: string): Promise<{draftJson: string|null}>;
  composerDraftSet(scopeJson: string, text: string): Promise<{ok: boolean}>;
  composerDraftDelete(scopeJson: string): Promise<{ok: boolean}>;
}

export interface ComposerDraftPersistencePort {
  clear(scope: ComposerDraftScope): Promise<void>;
  flush(scope?: ComposerDraftScope): Promise<void>;
  load(scope: ComposerDraftScope): Promise<string|null>;
  schedule(scope: ComposerDraftScope, text: string): void;
}

export function bindComposerDraftLifecycle(
    documentTarget: Document,
    onHidden: () => void,
    onVisible: () => void): () => void {
  const onVisibilityChange = () => {
    if (documentTarget.visibilityState === 'visible') {
      onVisible();
      return;
    }
    onHidden();
  };
  documentTarget.addEventListener('visibilitychange', onVisibilityChange);
  return () => documentTarget.removeEventListener(
      'visibilitychange', onVisibilityChange);
}

type PendingWrite = {
  readonly scope: ComposerDraftScope;
  text: string;
  timer: ReturnType<typeof setTimeout>|null;
};

export function scopeKey(scope: ComposerDraftScope): string {
  switch (scope.kind) {
    case 'new_task':
      return 'new_task';
    case 'conversation':
      return `conversation:${scope.conversationId}`;
  }
}

function readDraftText(value: unknown): string|null {
  if (typeof value !== 'object' || value === null || !('version' in value) ||
      !('text' in value)) {
    return null;
  }
  return value.version === 1 && typeof value.text === 'string' ? value.text : null;
}

export class ComposerDraftPersistence implements ComposerDraftPersistencePort {
  private readonly pendingByScope = new Map<string, PendingWrite>();
  private readonly writeTailByScope = new Map<string, Promise<void>>();

  constructor(private readonly remote: ComposerDraftRemote) {}

  async load(scope: ComposerDraftScope): Promise<string|null> {
    try {
      const {draftJson} = await this.remote.composerDraftGet(JSON.stringify(scope));
      if (!draftJson) {
        return null;
      }
      return readDraftText(JSON.parse(draftJson));
    } catch (error) {
      console.warn('[maho-ai] composer draft load failed', error);
      return null;
    }
  }

  schedule(scope: ComposerDraftScope, text: string): void {
    const key = scopeKey(scope);
    const existing = this.pendingByScope.get(key);
    if (existing?.timer !== null && existing?.timer !== undefined) {
      clearTimeout(existing.timer);
    }

    const pending: PendingWrite = {
      scope,
      text,
      timer: setTimeout(() => {
        pending.timer = null;
        void this.flush(scope);
      }, COMPOSER_DRAFT_DEBOUNCE_MS),
    };
    this.pendingByScope.set(key, pending);
  }

  async flush(scope?: ComposerDraftScope): Promise<void> {
    if (scope) {
      await this.flushKey(scopeKey(scope));
      return;
    }
    await Promise.all([...this.pendingByScope.keys()].map(key => this.flushKey(key)));
  }

  async clear(scope: ComposerDraftScope): Promise<void> {
    const key = scopeKey(scope);
    const pending = this.pendingByScope.get(key);
    if (pending?.timer !== null && pending?.timer !== undefined) {
      clearTimeout(pending.timer);
    }
    this.pendingByScope.delete(key);
    await this.enqueue(key, async () => {
      try {
        const {ok} = await this.remote.composerDraftDelete(JSON.stringify(scope));
        if (!ok) {
          throw new Error('Composer draft delete was not accepted.');
        }
      } catch (error) {
        console.warn('[maho-ai] composer draft delete failed', error);
      }
    });
  }

  private async flushKey(key: string): Promise<void> {
    const pending = this.pendingByScope.get(key);
    if (!pending) {
      return;
    }
    if (pending.timer !== null) {
      clearTimeout(pending.timer);
    }
    this.pendingByScope.delete(key);
    await this.enqueue(key, async () => {
      try {
        if (pending.text === '') {
          const {ok} = await this.remote.composerDraftDelete(
              JSON.stringify(pending.scope));
          if (!ok) {
            throw new Error('Composer draft delete was not accepted.');
          }
          return;
        }
        const {ok} = await this.remote.composerDraftSet(
            JSON.stringify(pending.scope), pending.text);
        if (!ok) {
          throw new Error('Composer draft write was not accepted.');
        }
      } catch (error) {
        console.warn('[maho-ai] composer draft persist failed', error);
      }
    });
  }

  private enqueue(key: string, operation: () => Promise<void>): Promise<void> {
    const previous = this.writeTailByScope.get(key) ?? Promise.resolve();
    const next = previous.then(operation, operation);
    this.writeTailByScope.set(key, next);
    void next.finally(() => {
      if (this.writeTailByScope.get(key) === next) {
        this.writeTailByScope.delete(key);
      }
    });
    return next;
  }
}
