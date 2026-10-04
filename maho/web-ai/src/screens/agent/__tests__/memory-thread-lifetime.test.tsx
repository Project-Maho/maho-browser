import { act, cleanup, fireEvent, render, screen } from '@testing-library/preact';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { AgentHandle, MahoBridge } from '../../../bridge/types';
import { AgentScreen } from '../agent-screen';

function makeMockBridge(overrides: Partial<MahoBridge> = {}): MahoBridge {
  return {
    byokGetProviders: vi.fn().mockResolvedValue([]),
    byokGetKey: vi.fn().mockResolvedValue(null),
    byokSetKey: vi.fn().mockResolvedValue(true),
    byokDeleteKey: vi.fn().mockResolvedValue(true),
    byokValidateKey: vi.fn().mockResolvedValue(true),
    getAiSettings: vi.fn().mockResolvedValue({
      provider: 'maho-managed',
      baseUrl: '',
      model: '',
      hasApiKey: false,
      hasByokOpenai: false,
      hasByokAnthropic: false,
    }),
    setAiProvider: vi.fn().mockResolvedValue(undefined),
    setAiBaseUrl: vi.fn().mockResolvedValue(undefined),
    setAiApiKey: vi.fn().mockResolvedValue(undefined),
    setAiModel: vi.fn().mockResolvedValue(undefined),
    chatSessionStart: vi.fn().mockResolvedValue('chat-handle'),
    chatSessionResume: vi.fn().mockResolvedValue('chat-resumed-handle'),
    chatSessionFree: vi.fn().mockResolvedValue(undefined),
    chatSendMessage: vi.fn().mockResolvedValue(undefined),
    chatCancelTurn: vi.fn().mockResolvedValue(undefined),
    chatPollEvents: vi.fn().mockResolvedValue([]),
    chatRegisterTool: vi.fn().mockResolvedValue(undefined),
    chatSendToolResult: vi.fn().mockResolvedValue(undefined),
    chatAppendAssistantMessage: vi.fn().mockResolvedValue(undefined),
    chatGetHistory: vi.fn().mockResolvedValue([]),
    conversationCreate: vi.fn().mockResolvedValue('conversation-1'),
    conversationList: vi.fn().mockResolvedValue([]),
    conversationGet: vi.fn().mockResolvedValue(null),
    conversationDelete: vi.fn().mockResolvedValue(true),
    conversationRename: vi.fn().mockResolvedValue(true),
    conversationArchive: vi.fn().mockResolvedValue(true),
    conversationUnarchive: vi.fn().mockResolvedValue(true),
    conversationBulk: vi.fn().mockResolvedValue({ requestedCount: 0, affectedIds: [], unchangedIds: [], missingIds: [] }),
    conversationGetAutoArchivePolicy: vi.fn().mockResolvedValue(null),
    conversationSetAutoArchivePolicy: vi.fn().mockResolvedValue(true),
    conversationGetMessages: vi.fn().mockResolvedValue([]),
    conversationProjectList: vi.fn().mockResolvedValue([]),
    conversationProjectCreate: vi.fn().mockResolvedValue({ id: 'p1', name: 'P', createdAt: '', updatedAt: '' }),
    conversationProjectRename: vi.fn().mockResolvedValue(true),
    conversationProjectDelete: vi.fn().mockResolvedValue(true),
    conversationProjectMove: vi.fn().mockResolvedValue({ requestedCount: 0, affectedIds: [], missingIds: [] }),
    getSpaceAIConfig: vi.fn().mockResolvedValue(null),
    setSpaceAIConfig: vi.fn().mockResolvedValue(undefined),
    pinchEstimateCost: vi.fn().mockResolvedValue({ estimatedCostUsd: 0, inputTokens: 0, model: 'm', outputTokens: 0 }),
    agentCreateSession: vi.fn().mockResolvedValue('handle-default'),
    agentFreeSession: vi.fn().mockResolvedValue(undefined),
    agentSendMessage: vi.fn().mockResolvedValue(true),
    agentCancel: vi.fn().mockResolvedValue(true),
    agentPollEvent: vi.fn().mockResolvedValue(null),
    agentListTools: vi.fn().mockResolvedValue([]),
    agentListArtifacts: vi.fn().mockResolvedValue([]),
    artifactShare: vi.fn().mockResolvedValue(true),
    capturePhoto: vi.fn().mockResolvedValue(null),
    captureScreenshot: vi.fn().mockResolvedValue(null),
    openSettings: vi.fn().mockResolvedValue(undefined),
    hapticFeedback: vi.fn().mockResolvedValue(undefined),
    relaySignIn: vi.fn().mockResolvedValue({ ok: true }),
    relaySignUp: vi.fn().mockResolvedValue({ ok: true }),
    relaySignInWithGoogle: vi.fn().mockResolvedValue({ ok: true }),
    relayAccountStatus: vi.fn().mockResolvedValue({ hasValidSession: true, isReauth: false }),
    openDefaultBrowserSettings: vi.fn().mockResolvedValue(undefined),
    completeOnboarding: vi.fn().mockResolvedValue(undefined),
    ...overrides,
  };
}

describe('AgentScreen lifecycle regressions (U13)', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  afterEach(() => {
    cleanup();
  });

  // DELTA FIX helper (recorded): preact test-utils act schedules its finish()
  // a fixed number of microtask checkpoints after the callback promise, so a
  // promise-resolution cascade (native resolve → finally → submit resume →
  // generation-gated free) needs explicit checkpoints inside the act callback
  // to complete before assertions run. Assertions are unchanged.
  async function flushMicrotasks(): Promise<void> {
    for (let i = 0; i < 6; i++) {
      await Promise.resolve();
    }
  }

  it('memory-thread: unmount before create frees once and never sends', async () => {
    // Given: AgentScreen is rendered, and native agentCreateSession is asynchronous/in-flight
    let resolveCreate!: (handle: AgentHandle) => void;
    const agentCreateSession = vi.fn<MahoBridge['agentCreateSession']>().mockImplementation(
      () => new Promise((resolve) => { resolveCreate = resolve; }),
    );
    const agentFreeSession = vi.fn<MahoBridge['agentFreeSession']>().mockResolvedValue(undefined);
    const agentSendMessage = vi.fn<MahoBridge['agentSendMessage']>().mockResolvedValue(true);

    const bridge = makeMockBridge({ agentCreateSession, agentFreeSession, agentSendMessage });

    const { unmount } = render(<AgentScreen bridge={bridge} />);

    // When: prompt is submitted, initiating session creation
    const input = screen.getByRole('textbox');
    fireEvent.input(input, { target: { value: 'prompt before unmount' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));

    expect(agentCreateSession).toHaveBeenCalledOnce();

    // And When: component unmounts BEFORE agentCreateSession resolves
    unmount();

    // And When: late native session creation completes
    await act(async () => {
      resolveCreate('late-handle-unmount');
      await flushMicrotasks();
    });

    // Then: newly created handle returning to a dead generation must be freed exactly once
    // Production baseline bug: handleRef was null at unmount, cancelledTurnIds was not updated by unmount,
    // so agentFreeSession is never called (0 calls).
    expect(agentFreeSession).toHaveBeenCalledWith('late-handle-unmount');
    expect(agentFreeSession).toHaveBeenCalledTimes(1);

    // And Then: the message must NEVER be sent to the dead generation handle
    // Production baseline bug: agentSendMessage is called with the late handle.
    expect(agentSendMessage).not.toHaveBeenCalled();
  });

  it('memory-thread: unmount after config cannot send', async () => {
    // Given: session creation succeeds, but agentSetRuntimeConfig is in-flight
    let resolveConfig!: (accepted: boolean) => void;
    const agentCreateSession = vi.fn<MahoBridge['agentCreateSession']>().mockResolvedValue('config-handle-1');
    const agentFreeSession = vi.fn<MahoBridge['agentFreeSession']>().mockResolvedValue(undefined);
    const agentSendMessage = vi.fn<MahoBridge['agentSendMessage']>().mockResolvedValue(true);
    const agentSetRuntimeConfig = vi.fn<NonNullable<MahoBridge['agentSetRuntimeConfig']>>().mockImplementation(
      () => new Promise((resolve) => { resolveConfig = resolve; }),
    );

    const bridge = makeMockBridge({
      agentCreateSession,
      agentFreeSession,
      agentSendMessage,
      agentSetRuntimeConfig,
    });

    const { unmount } = render(<AgentScreen bridge={bridge} />);

    // When: suggested task card with runtimeConfig is clicked
    // DELTA FIX (recorded): the idle catalog renders two suggested-task cards
    // by product design (MAX_PASSIVE_SUGGESTIONS = 2); the role locator must
    // select the first card instead of assuming a single match. Assertions
    // below are unchanged.
    const taskCard = screen.getAllByRole('button', { name: /Start suggested task/i })[0]!;
    fireEvent.click(taskCard);

    await act(async () => {
      await Promise.resolve();
      await flushMicrotasks();
    });

    expect(agentCreateSession).toHaveBeenCalledOnce();
    expect(agentSetRuntimeConfig).toHaveBeenCalledOnce();

    // And When: component unmounts while runtimeConfig acceptance is pending
    unmount();

    // And When: config acceptance resolves
    await act(async () => {
      resolveConfig(true);
      await flushMicrotasks();
    });

    // Then: send must NOT proceed because component generation was invalidated on unmount
    // Production baseline bug: no generation check after config await, sends to dead handle.
    expect(agentSendMessage).not.toHaveBeenCalled();
  });

  it('memory-thread: reset cannot accept old generation', async () => {
    // Given: first submit has an in-flight createSession
    let resolveCreate1!: (handle: AgentHandle) => void;
    const agentCreateSession = vi.fn<MahoBridge['agentCreateSession']>().mockImplementationOnce(
      () => new Promise((resolve) => { resolveCreate1 = resolve; }),
    );
    const agentFreeSession = vi.fn<MahoBridge['agentFreeSession']>().mockResolvedValue(undefined);
    const agentSendMessage = vi.fn<MahoBridge['agentSendMessage']>().mockResolvedValue(true);

    const bridge = makeMockBridge({ agentCreateSession, agentFreeSession, agentSendMessage });

    render(<AgentScreen bridge={bridge} />);

    // Submit first prompt
    const input = screen.getByRole('textbox');
    fireEvent.input(input, { target: { value: 'prompt 1' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));

    expect(agentCreateSession).toHaveBeenCalledTimes(1);

    // When: user resets the session via New session topbar button
    const newSessionButton = screen.getByTestId('agent-new-session');
    fireEvent.click(newSessionButton);

    // And When: first session's in-flight createSession resolves
    await act(async () => {
      resolveCreate1('handle-generation-1');
      await flushMicrotasks();
    });

    // Then: old generation handle must be freed exactly once
    // Production baseline bug: handle-generation-1 is accepted as active handle, not freed.
    expect(agentFreeSession).toHaveBeenCalledWith('handle-generation-1');
    expect(agentFreeSession).toHaveBeenCalledTimes(1);

    // And Then: prompt 1 must NOT be sent to the old generation handle
    expect(agentSendMessage).not.toHaveBeenCalledWith('handle-generation-1', expect.anything());
  });
});
