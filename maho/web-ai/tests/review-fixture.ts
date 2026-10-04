import { vi } from 'vitest';
import type { ComposerDraft, ComposerDraftScope, MahoBridge } from '../src/bridge/types';
import { scopeKey } from '../src/storage/composer-drafts';

export function deferred<T>() {
  let resolve!: (value: T) => void;
  let reject!: (reason: Error) => void;
  const promise = new Promise<T>((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}

// Subscribe before the action; resolve on the exact DOM state, never a poll delay.
export function domSignal(check: () => boolean): Promise<void> {
  return new Promise((resolve, reject) => {
    const observer = new MutationObserver(inspect);
    const timeout = setTimeout(() => { observer.disconnect(); reject(new Error('DOM signal timed out')); }, 2000);
    function inspect() {
      if (!check()) return;
      clearTimeout(timeout);
      observer.disconnect();
      resolve();
    }
    observer.observe(document.body, { subtree: true, childList: true, attributes: true, characterData: true });
    inspect();
  });
}

export function reviewBridge(overrides: Partial<MahoBridge> = {}) {
  const saved = new Map<string, ComposerDraft>();
  const sent = deferred<void>();
  const freed = deferred<void>();
  const methods = {
    agentCreateSession: vi.fn(async () => 'agent-1'),
    agentSendMessage: vi.fn(async () => { sent.resolve(); return true; }),
    agentSetRuntimeConfig: vi.fn(async () => true),
    agentCancel: vi.fn(async () => true),
    agentFreeSession: vi.fn(async () => { freed.resolve(); }),
    agentPollEvent: vi.fn(async () => null as string | null),
    getAiSettings: vi.fn(async () => ({ provider: 'maho-managed', baseUrl: '', model: '', hasApiKey: false, hasByokOpenai: false, hasByokAnthropic: false })),
    chatSessionStart: vi.fn(async () => 'chat-1'),
    chatSessionResume: vi.fn(async (id: string) => `handle-${id}`),
    chatSessionFree: vi.fn(async () => undefined),
    chatRegisterTool: vi.fn(async () => undefined),
    chatGetHistory: vi.fn(async () => []),
    chatPollEvents: vi.fn(async () => []),
    chatSendMessage: vi.fn(async () => undefined),
    saveConversationMessage: vi.fn(async () => true),
    composerDraftGet: vi.fn(async (scope: ComposerDraftScope) => saved.get(scopeKey(scope)) ?? null),
    composerDraftSet: vi.fn(async (scope: ComposerDraftScope, text: string) => {
      saved.set(scopeKey(scope), { version: 1, text, updatedAt: '2026-09-14T00:00:00Z' });
      return true;
    }),
    composerDraftDelete: vi.fn(async (scope: ComposerDraftScope) => { saved.delete(scopeKey(scope)); return true; }),
    conversationList: vi.fn(async () => [{ id: 'conv-1', title: 'Original', createdAt: '', updatedAt: '' }]),
    conversationProjectList: vi.fn(async () => []),
    conversationRename: vi.fn(async () => true),
    ...overrides,
  };
  return { saved, methods, sent: sent.promise, freed: freed.promise, bridge: methods as unknown as MahoBridge };
}
