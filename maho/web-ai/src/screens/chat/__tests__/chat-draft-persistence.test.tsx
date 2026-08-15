import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/preact';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { AiSettings, ChatContent, ComposerDraft, ComposerDraftScope } from '../../../bridge/types';
import { COMPOSER_DRAFT_DEBOUNCE_MS } from '../../../storage/composer-drafts';

const NEW_TASK: ComposerDraftScope = { kind: 'new_task' };

const mockBridge = {
  getAiSettings: vi.fn<() => Promise<AiSettings>>(),
  byokGetKey: vi.fn<(provider: string) => Promise<string | null>>(),
  chatSessionStart: vi.fn<() => Promise<string>>(),
  chatSessionResume: vi.fn<() => Promise<string>>(),
  chatSessionFree: vi.fn<(handle: string) => Promise<void>>(),
  chatSendMessage: vi.fn<(handle: string, content: ChatContent) => Promise<void>>(),
  chatCancelTurn: vi.fn<(handle: string) => Promise<void>>(),
  chatPollEvents: vi.fn<(handle: string) => Promise<unknown[]>>(),
  chatRegisterTool: vi.fn<(handle: string) => Promise<void>>(),
  chatSendToolResult: vi.fn<() => Promise<void>>(),
  chatAppendAssistantMessage: vi.fn<() => Promise<void>>(),
  chatGetHistory: vi.fn<(handle: string) => Promise<unknown[]>>(),
  saveConversationMessage: vi.fn<(sessionId: string, role: string, content: string) => Promise<boolean>>(),
  composerDraftGet: vi.fn<(scope: ComposerDraftScope) => Promise<ComposerDraft | null>>(),
  composerDraftSet: vi.fn<(scope: ComposerDraftScope, text: string) => Promise<boolean>>(),
  composerDraftDelete: vi.fn<(scope: ComposerDraftScope) => Promise<boolean>>(),
};

vi.mock('../../../hooks/use-bridge', () => ({
  useBridge: () => mockBridge,
}));

import { ChatScreen } from '../chat-screen';

const OPENAI_SETTINGS: AiSettings = {
  provider: 'openai',
  baseUrl: '',
  model: '',
  hasApiKey: false,
  hasByokOpenai: true,
  hasByokAnthropic: false,
};

function draft(text: string): ComposerDraft {
  return { version: 1, text, updatedAt: '2026-08-11T00:00:00Z' };
}

function input() {
  return screen.getByRole('textbox', { name: 'Message' });
}

async function waitForReadyComposer() {
  await waitFor(() => expect(input()).not.toBeDisabled());
}

beforeEach(() => {
  window.location.hash = '#chat';
  mockBridge.getAiSettings.mockResolvedValue(OPENAI_SETTINGS);
  mockBridge.byokGetKey.mockImplementation(async (provider: string) =>
    provider === 'openai' ? 'sk-openai' : null,
  );
  mockBridge.chatSessionStart.mockResolvedValue('session-new');
  mockBridge.chatSessionResume.mockResolvedValue('native-resumed-handle');
  mockBridge.chatSessionFree.mockResolvedValue(undefined);
  mockBridge.chatSendMessage.mockResolvedValue(undefined);
  mockBridge.chatCancelTurn.mockResolvedValue(undefined);
  mockBridge.chatPollEvents.mockResolvedValue([]);
  mockBridge.chatRegisterTool.mockResolvedValue(undefined);
  mockBridge.chatSendToolResult.mockResolvedValue(undefined);
  mockBridge.chatAppendAssistantMessage.mockResolvedValue(undefined);
  mockBridge.chatGetHistory.mockResolvedValue([]);
  mockBridge.saveConversationMessage.mockResolvedValue(true);
  mockBridge.composerDraftGet.mockResolvedValue(null);
  mockBridge.composerDraftSet.mockResolvedValue(true);
  mockBridge.composerDraftDelete.mockResolvedValue(true);
});

afterEach(() => {
  cleanup();
  vi.clearAllMocks();
  vi.useRealTimers();
});

describe('ChatScreen composer draft persistence', () => {
  it('uses the new_task scope for a brand-new chat even though a session handle exists', async () => {
    render(<ChatScreen />);
    await waitForReadyComposer();

    expect(mockBridge.composerDraftGet).toHaveBeenCalledWith(NEW_TASK);
    expect(mockBridge.composerDraftGet).not.toHaveBeenCalledWith(
      expect.objectContaining({ kind: 'conversation' }),
    );
  });

  it('restores the new_task draft into a brand-new chat composer', async () => {
    mockBridge.composerDraftGet.mockResolvedValue(draft('half-written question'));

    render(<ChatScreen />);

    await waitFor(() => expect(input()).toHaveValue('half-written question'));
  });

  it('restores a conversation-scoped draft when resuming a persisted conversation', async () => {
    mockBridge.composerDraftGet.mockImplementation(async (scope: ComposerDraftScope) =>
      scope.kind === 'conversation' && scope.conversationId === 'conv-42'
        ? draft('resumed draft')
        : null,
    );

    render(<ChatScreen sessionId="conv-42" />);

    await waitFor(() => expect(input()).toHaveValue('resumed draft'));
    expect(mockBridge.composerDraftGet).toHaveBeenCalledWith({
      kind: 'conversation',
      conversationId: 'conv-42',
    });
  });

  it('persists typing under the new_task scope until the row is persisted', async () => {
    vi.useFakeTimers();
    render(<ChatScreen />);
    await vi.waitFor(() => expect(input()).not.toBeDisabled());

    fireEvent.input(input(), { target: { value: 'typed' } });
    await vi.advanceTimersByTimeAsync(COMPOSER_DRAFT_DEBOUNCE_MS);

    expect(mockBridge.composerDraftSet).toHaveBeenCalledWith(NEW_TASK, 'typed');
  });

  it('does not delete the draft when SEND_TURN_START optimistically clears the composer', async () => {
    let resolveSend: (() => void) | null = null;
    mockBridge.chatSendMessage.mockImplementation(
      () => new Promise<void>((resolve) => {
        resolveSend = resolve;
      }),
    );

    render(<ChatScreen />);
    await waitForReadyComposer();

    fireEvent.input(input(), { target: { value: 'in flight' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));

    await waitFor(() => expect(mockBridge.chatSendMessage).toHaveBeenCalled());
    expect(input()).toHaveValue('');
    expect(mockBridge.composerDraftDelete).not.toHaveBeenCalled();

    resolveSend!();
    await waitFor(() => expect(mockBridge.composerDraftDelete).toHaveBeenCalledWith(NEW_TASK));
  });

  it('promotes the draft scope to the conversation after the first successful send', async () => {
    render(<ChatScreen />);
    await waitForReadyComposer();

    fireEvent.input(input(), { target: { value: 'first message' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));

    await waitFor(() => expect(mockBridge.composerDraftDelete).toHaveBeenCalledWith(NEW_TASK));

    fireEvent.input(input(), { target: { value: 'follow-up draft' } });
    await waitFor(() =>
      expect(mockBridge.composerDraftSet).toHaveBeenCalledWith(
        { kind: 'conversation', conversationId: 'session-new' },
        'follow-up draft',
      ),
    );
  });

  it('keeps the draft persisted and restores the text when the send fails', async () => {
    mockBridge.chatSendMessage.mockRejectedValue(new Error('offline'));

    render(<ChatScreen />);
    await waitForReadyComposer();

    fireEvent.input(input(), { target: { value: 'unsent text' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));

    await waitFor(() => expect(input()).toHaveValue('unsent text'));
    expect(mockBridge.composerDraftDelete).not.toHaveBeenCalled();
  });

  it('does not overwrite a replacement typed while a failing send was in flight', async () => {
    let rejectSend: ((error: Error) => void) | null = null;
    mockBridge.chatSendMessage.mockImplementation(
      () => new Promise<void>((_resolve, reject) => {
        rejectSend = reject;
      }),
    );

    render(<ChatScreen />);
    await waitForReadyComposer();

    fireEvent.input(input(), { target: { value: 'original text' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));
    await waitFor(() => expect(mockBridge.chatSendMessage).toHaveBeenCalled());

    fireEvent.input(input(), { target: { value: 'replacement text' } });
    rejectSend!(new Error('offline'));

    await waitFor(() => expect(screen.getByRole('alert')).toBeInTheDocument());
    expect(input()).toHaveValue('replacement text');
  });

  it('flushes the draft when the document becomes hidden', async () => {
    vi.useFakeTimers();
    render(<ChatScreen />);
    await vi.waitFor(() => expect(input()).not.toBeDisabled());

    fireEvent.input(input(), { target: { value: 'panel closing' } });
    const visibility = vi.spyOn(document, 'visibilityState', 'get').mockReturnValue('hidden');
    document.dispatchEvent(new Event('visibilitychange'));
    await vi.advanceTimersByTimeAsync(0);

    expect(mockBridge.composerDraftSet).toHaveBeenCalledWith(NEW_TASK, 'panel closing');
    visibility.mockRestore();
  });

  it('sends normally when the bridge rejects every draft call', async () => {
    mockBridge.composerDraftGet.mockRejectedValue(new Error('no transport'));
    mockBridge.composerDraftSet.mockRejectedValue(new Error('no transport'));
    mockBridge.composerDraftDelete.mockRejectedValue(new Error('no transport'));

    render(<ChatScreen />);
    await waitForReadyComposer();

    fireEvent.input(input(), { target: { value: 'still sends' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));

    await waitFor(() =>
      expect(mockBridge.chatSendMessage).toHaveBeenCalledWith('session-new', {
        kind: 'text',
        text: 'still sends',
      }),
    );
    expect(input()).toHaveValue('');
  });
});
