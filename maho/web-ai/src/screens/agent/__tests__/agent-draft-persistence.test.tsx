import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/preact';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { ComposerDraft, ComposerDraftScope, MahoBridge } from '../../../bridge/types';
import { COMPOSER_DRAFT_DEBOUNCE_MS } from '../../../storage/composer-drafts';
import { AgentScreen } from '../agent-screen';

const NEW_TASK: ComposerDraftScope = { kind: 'new_task' };

function makeBridge(overrides: Partial<MahoBridge> = {}) {
  const composerDraftGet = vi.fn(async (_scope: ComposerDraftScope) => null as ComposerDraft | null);
  const composerDraftSet = vi.fn(async (_scope: ComposerDraftScope, _text: string) => true);
  const composerDraftDelete = vi.fn(async (_scope: ComposerDraftScope) => true);
  const agentCreateSession = vi.fn(async () => 'handle-1');
  const agentSendMessage = vi.fn(async () => true);
  const agentPollEvent = vi.fn(async () => null);
  const agentFreeSession = vi.fn(async () => undefined);
  const agentCancel = vi.fn(async () => true);

  const bridge = {
    agentCreateSession,
    agentSendMessage,
    agentPollEvent,
    agentFreeSession,
    agentCancel,
    agentListTools: vi.fn(async () => []),
    agentListArtifacts: vi.fn(async () => []),
    artifactShare: vi.fn(async () => true),
    composerDraftGet,
    composerDraftSet,
    composerDraftDelete,
    ...overrides,
  } as unknown as MahoBridge;

  return {
    agentCreateSession,
    agentSendMessage,
    bridge,
    composerDraftDelete,
    composerDraftGet,
    composerDraftSet,
  };
}

function promptInput() {
  return (
    screen.queryByRole('textbox', { name: 'Task description' }) ??
    screen.getByRole('textbox', { name: 'Message' })
  );
}

function draft(text: string): ComposerDraft {
  return { version: 1, text, updatedAt: '2026-08-11T00:00:00Z' };
}

beforeEach(() => {
  window.location.hash = '#agent';
});

afterEach(() => {
  cleanup();
  vi.clearAllMocks();
  vi.useRealTimers();
});

describe('AgentScreen composer draft persistence', () => {
  it('restores the persisted new_task draft on mount', async () => {
    const ctx = makeBridge();
    ctx.composerDraftGet.mockResolvedValue(draft('unsent thought'));

    render(<AgentScreen bridge={ctx.bridge} />);

    await waitFor(() => expect(promptInput()).toHaveValue('unsent thought'));
    expect(ctx.composerDraftGet).toHaveBeenCalledWith(NEW_TASK);
  });

  it('persists typing under the new_task scope after the debounce window', async () => {
    vi.useFakeTimers();
    const ctx = makeBridge();
    render(<AgentScreen bridge={ctx.bridge} />);
    await vi.advanceTimersByTimeAsync(0);

    fireEvent.input(promptInput(), { target: { value: 'draft body' } });
    expect(ctx.composerDraftSet).not.toHaveBeenCalled();

    await vi.advanceTimersByTimeAsync(COMPOSER_DRAFT_DEBOUNCE_MS);
    expect(ctx.composerDraftSet).toHaveBeenCalledWith(NEW_TASK, 'draft body');
  });

  it('flushes the pending draft when the composer loses focus', async () => {
    vi.useFakeTimers();
    const ctx = makeBridge();
    render(<AgentScreen bridge={ctx.bridge} />);
    await vi.advanceTimersByTimeAsync(0);

    fireEvent.input(promptInput(), { target: { value: 'blur me' } });
    fireEvent.blur(promptInput());
    await vi.advanceTimersByTimeAsync(0);

    expect(ctx.composerDraftSet).toHaveBeenCalledWith(NEW_TASK, 'blur me');
  });

  it('flushes the pending draft when the document becomes hidden', async () => {
    vi.useFakeTimers();
    const ctx = makeBridge();
    render(<AgentScreen bridge={ctx.bridge} />);
    await vi.advanceTimersByTimeAsync(0);

    fireEvent.input(promptInput(), { target: { value: 'panel closing' } });
    const visibility = vi
      .spyOn(document, 'visibilityState', 'get')
      .mockReturnValue('hidden');
    document.dispatchEvent(new Event('visibilitychange'));
    await vi.advanceTimersByTimeAsync(0);

    expect(ctx.composerDraftSet).toHaveBeenCalledWith(NEW_TASK, 'panel closing');
    visibility.mockRestore();
  });

  it('does not delete the persisted draft before the native send resolves', async () => {
    let resolveSend: ((value: boolean) => void) | null = null;
    const agentSendMessage = vi.fn(
      () => new Promise<boolean>((resolve) => {
        resolveSend = resolve;
      }),
    );
    const ctx = makeBridge({ agentSendMessage } as unknown as Partial<MahoBridge>);
    render(<AgentScreen bridge={ctx.bridge} />);

    fireEvent.input(promptInput(), { target: { value: 'send me' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));

    await waitFor(() => expect(agentSendMessage).toHaveBeenCalled());
    expect(ctx.composerDraftDelete).not.toHaveBeenCalled();

    resolveSend!(true);
    await waitFor(() => expect(ctx.composerDraftDelete).toHaveBeenCalledWith(NEW_TASK));
  });

  it('keeps the persisted draft and restores the text when the send fails', async () => {
    const agentSendMessage = vi.fn(async () => {
      throw new Error('agent unreachable');
    });
    const ctx = makeBridge({ agentSendMessage } as unknown as Partial<MahoBridge>);
    render(<AgentScreen bridge={ctx.bridge} />);

    fireEvent.input(promptInput(), { target: { value: 'will fail' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));

    await waitFor(() => expect(promptInput()).toHaveValue('will fail'));
    expect(ctx.composerDraftDelete).not.toHaveBeenCalled();
  });

  it('does not clobber a replacement typed while the failing send was in flight', async () => {
    let rejectSend: ((error: Error) => void) | null = null;
    const agentSendMessage = vi.fn(
      () => new Promise<boolean>((_resolve, reject) => {
        rejectSend = reject;
      }),
    );
    const ctx = makeBridge({ agentSendMessage } as unknown as Partial<MahoBridge>);
    render(<AgentScreen bridge={ctx.bridge} />);

    fireEvent.input(promptInput(), { target: { value: 'original' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));
    await waitFor(() => expect(agentSendMessage).toHaveBeenCalled());

    fireEvent.input(promptInput(), { target: { value: 'replacement' } });
    rejectSend!(new Error('agent unreachable'));

    await waitFor(() => expect(screen.getByTestId('agent-error-banner')).toBeInTheDocument());
    expect(promptInput()).toHaveValue('replacement');
  });

  it('never overwrites the goal prop with a late restore response', async () => {
    let resolveGet: ((value: ComposerDraft | null) => void) | null = null;
    const composerDraftGet = vi.fn(
      () => new Promise<ComposerDraft | null>((resolve) => {
        resolveGet = resolve;
      }),
    );
    const ctx = makeBridge({ composerDraftGet } as unknown as Partial<MahoBridge>);

    render(<AgentScreen bridge={ctx.bridge} goal="Review pinned tabs" />);
    await waitFor(() => expect(ctx.agentSendMessage).toHaveBeenCalledWith('handle-1', 'Review pinned tabs'));

    resolveGet!(draft('stale draft'));
    await waitFor(() => expect(promptInput()).toHaveValue(''));
  });

  it('never overwrites text typed before the restore response arrives', async () => {
    let resolveGet: ((value: ComposerDraft | null) => void) | null = null;
    const composerDraftGet = vi.fn(
      () => new Promise<ComposerDraft | null>((resolve) => {
        resolveGet = resolve;
      }),
    );
    const ctx = makeBridge({ composerDraftGet } as unknown as Partial<MahoBridge>);

    render(<AgentScreen bridge={ctx.bridge} />);
    await waitFor(() => expect(composerDraftGet).toHaveBeenCalled());
    fireEvent.input(promptInput(), { target: { value: 'typed first' } });

    resolveGet!(draft('stale draft'));
    await waitFor(() => expect(promptInput()).toHaveValue('typed first'));
  });

  it('deletes the draft scope when the user starts a new session', async () => {
    const ctx = makeBridge();
    render(<AgentScreen bridge={ctx.bridge} />);

    fireEvent.input(promptInput(), { target: { value: 'abandoned' } });
    fireEvent.click(screen.getByRole('button', { name: 'Start new session' }));

    await waitFor(() => expect(ctx.composerDraftDelete).toHaveBeenCalledWith(NEW_TASK));
    expect(promptInput()).toHaveValue('');
  });

  it('keeps working when the bridge has no draft support', async () => {
    const ctx = makeBridge({
      composerDraftGet: undefined,
      composerDraftSet: undefined,
      composerDraftDelete: undefined,
    });

    render(<AgentScreen bridge={ctx.bridge} />);
    fireEvent.input(promptInput(), { target: { value: 'no persistence here' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));

    await waitFor(() => expect(ctx.agentSendMessage).toHaveBeenCalledWith('handle-1', 'no persistence here'));
  });
});
