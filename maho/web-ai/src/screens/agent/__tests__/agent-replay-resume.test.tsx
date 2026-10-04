/**
 * Plan row 14 — web-ai SPA replay-resume UX.
 *
 * The SPA's only session identity is the live native handle: after a turn
 * finishes the screen stays attached to the same session. On resume, the
 * kernel/replay buffer re-delivers historical interaction_request events
 * (FFI kind 8) through agentPollEvent, and approval cards must
 * re-materialize with their kernel lifecycle state:
 *   - state "pending"            → active, answerable card
 *   - terminal state ("expired") → disabled card showing the final state
 *
 * Also pins replay fidelity in the reducer: a replayed redelivery must never
 * resurrect a card the user already answered, and an unsupported replayed
 * payload must fall back to the plain thread (no card, no crash).
 */
import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/preact';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { MahoBridge } from '../../../bridge/types';
import { agentReducer, initialAgentState } from '../agent-reducer';
import { AgentScreen } from '../agent-screen';

type EventType =
  | 'token'
  | 'thinking'
  | 'tool_call'
  | 'tool_result'
  | 'artifact_created'
  | 'interaction_request'
  | 'complete'
  | 'error';

function makeMockBridge() {
  let eventQueue: string[] = [];
  const agentCreateSession = vi.fn<MahoBridge['agentCreateSession']>().mockResolvedValue('handle-1');
  const agentSendMessage = vi.fn<MahoBridge['agentSendMessage']>().mockResolvedValue(true);
  const agentCancel = vi.fn<MahoBridge['agentCancel']>().mockResolvedValue(true);
  const agentFreeSession = vi.fn<MahoBridge['agentFreeSession']>().mockResolvedValue(undefined);
  const agentPollEvent = vi.fn<MahoBridge['agentPollEvent']>().mockImplementation(async () => eventQueue.shift() ?? null);
  const agentResolveInteraction = vi.fn<NonNullable<MahoBridge['agentResolveInteraction']>>().mockResolvedValue(true);
  const artifactShare = vi.fn<MahoBridge['artifactShare']>().mockResolvedValue(true);

  const bridge: MahoBridge = {
    byokGetProviders: vi.fn<MahoBridge['byokGetProviders']>().mockResolvedValue([]),
    byokGetKey: vi.fn<MahoBridge['byokGetKey']>().mockResolvedValue(null),
    byokSetKey: vi.fn<MahoBridge['byokSetKey']>().mockResolvedValue(true),
    byokDeleteKey: vi.fn<MahoBridge['byokDeleteKey']>().mockResolvedValue(true),
    byokValidateKey: vi.fn<MahoBridge['byokValidateKey']>().mockResolvedValue(true),
    getAiSettings: vi.fn<MahoBridge['getAiSettings']>().mockResolvedValue({
      provider: 'maho-managed',
      baseUrl: '',
      model: '',
      hasApiKey: false,
      hasByokOpenai: false,
      hasByokAnthropic: false,
    }),
    setAiProvider: vi.fn<MahoBridge['setAiProvider']>().mockResolvedValue(undefined),
    setAiBaseUrl: vi.fn<MahoBridge['setAiBaseUrl']>().mockResolvedValue(undefined),
    setAiApiKey: vi.fn<MahoBridge['setAiApiKey']>().mockResolvedValue(undefined),
    setAiModel: vi.fn<MahoBridge['setAiModel']>().mockResolvedValue(undefined),
    chatSessionStart: vi.fn<MahoBridge['chatSessionStart']>().mockResolvedValue('chat-handle'),
    chatSessionResume: vi.fn<MahoBridge['chatSessionResume']>().mockResolvedValue('chat-resumed-handle'),
    chatSessionFree: vi.fn<MahoBridge['chatSessionFree']>().mockResolvedValue(undefined),
    chatSendMessage: vi.fn<MahoBridge['chatSendMessage']>().mockResolvedValue(undefined),
    chatCancelTurn: vi.fn<MahoBridge['chatCancelTurn']>().mockResolvedValue(undefined),
    chatPollEvents: vi.fn<MahoBridge['chatPollEvents']>().mockResolvedValue([]),
    chatRegisterTool: vi.fn<MahoBridge['chatRegisterTool']>().mockResolvedValue(undefined),
    chatSendToolResult: vi.fn<MahoBridge['chatSendToolResult']>().mockResolvedValue(undefined),
    chatAppendAssistantMessage: vi.fn<MahoBridge['chatAppendAssistantMessage']>().mockResolvedValue(undefined),
    chatGetHistory: vi.fn<MahoBridge['chatGetHistory']>().mockResolvedValue([]),
    conversationCreate: vi.fn<MahoBridge['conversationCreate']>().mockResolvedValue('conversation-1'),
    conversationList: vi.fn<MahoBridge['conversationList']>().mockResolvedValue([]),
    conversationGet: vi.fn<MahoBridge['conversationGet']>().mockResolvedValue(null),
    conversationDelete: vi.fn<MahoBridge['conversationDelete']>().mockResolvedValue(true),
    conversationRename: vi.fn<MahoBridge['conversationRename']>().mockResolvedValue(true),
    conversationArchive: vi.fn<MahoBridge['conversationArchive']>().mockResolvedValue(true),
    conversationUnarchive: vi.fn<MahoBridge['conversationUnarchive']>().mockResolvedValue(true),
    conversationBulk: vi.fn<MahoBridge['conversationBulk']>().mockResolvedValue({
      requestedCount: 0,
      affectedIds: [],
      unchangedIds: [],
      missingIds: [],
    }),
    conversationGetAutoArchivePolicy: vi.fn<MahoBridge['conversationGetAutoArchivePolicy']>().mockResolvedValue(null),
    conversationSetAutoArchivePolicy: vi.fn<MahoBridge['conversationSetAutoArchivePolicy']>().mockResolvedValue(true),
    conversationGetMessages: vi.fn<MahoBridge['conversationGetMessages']>().mockResolvedValue([]),
    conversationProjectList: vi.fn<MahoBridge['conversationProjectList']>().mockResolvedValue([]),
    conversationProjectCreate: vi.fn<MahoBridge['conversationProjectCreate']>().mockResolvedValue({ id: 'project-1', name: 'Project', createdAt: '', updatedAt: '' }),
    conversationProjectRename: vi.fn<MahoBridge['conversationProjectRename']>().mockResolvedValue(true),
    conversationProjectDelete: vi.fn<MahoBridge['conversationProjectDelete']>().mockResolvedValue(true),
    conversationProjectMove: vi.fn<MahoBridge['conversationProjectMove']>().mockResolvedValue({ requestedCount: 0, affectedIds: [], missingIds: [] }),
    getSpaceAIConfig: vi.fn<MahoBridge['getSpaceAIConfig']>().mockResolvedValue(null),
    setSpaceAIConfig: vi.fn<MahoBridge['setSpaceAIConfig']>().mockResolvedValue(undefined),
    pinchEstimateCost: vi.fn<MahoBridge['pinchEstimateCost']>().mockResolvedValue({
      estimatedCostUsd: 0,
      inputTokens: 0,
      model: 'test',
      outputTokens: 0,
    }),
    agentCreateSession,
    agentFreeSession,
    agentSendMessage,
    agentCancel,
    agentPollEvent,
    agentResolveInteraction,
    agentListTools: vi.fn<MahoBridge['agentListTools']>().mockResolvedValue([]),
    agentListArtifacts: vi.fn<MahoBridge['agentListArtifacts']>().mockResolvedValue([]),
    artifactShare,
    capturePhoto: vi.fn<MahoBridge['capturePhoto']>().mockResolvedValue(null),
    captureScreenshot: vi.fn<MahoBridge['captureScreenshot']>().mockResolvedValue(null),
    openSettings: vi.fn<MahoBridge['openSettings']>().mockResolvedValue(undefined),
    hapticFeedback: vi.fn<MahoBridge['hapticFeedback']>().mockResolvedValue(undefined),
    relaySignIn: vi.fn<MahoBridge['relaySignIn']>().mockResolvedValue({ ok: true }),
    relaySignUp: vi.fn<MahoBridge['relaySignUp']>().mockResolvedValue({ ok: true }),
    relaySignInWithGoogle: vi.fn<MahoBridge['relaySignInWithGoogle']>().mockResolvedValue({ ok: true }),
    relayAccountStatus: vi.fn<MahoBridge['relayAccountStatus']>().mockResolvedValue({ hasValidSession: true, isReauth: false }),
    openDefaultBrowserSettings: vi.fn<MahoBridge['openDefaultBrowserSettings']>().mockResolvedValue(undefined),
    completeOnboarding: vi.fn<MahoBridge['completeOnboarding']>().mockResolvedValue(undefined),
  };

  return {
    agentCancel,
    agentCreateSession,
    agentFreeSession,
    agentPollEvent,
    agentResolveInteraction,
    agentSendMessage,
    bridge,
    clearQueue: () => {
      eventQueue = [];
    },
    enqueueEvent: (type: EventType, data: unknown) => {
      eventQueue.push(JSON.stringify({ type, data }));
    },
  };
}

let mockCtx = makeMockBridge();

beforeEach(() => {
  mockCtx = makeMockBridge();
});

afterEach(() => {
  cleanup();
  vi.clearAllMocks();
  mockCtx.clearQueue();
});

function promptInput() {
  return screen.getByRole('textbox', { name: 'Task description' });
}

async function runFirstTurnToCompletion() {
  render(<AgentScreen bridge={mockCtx.bridge} />);
  fireEvent.input(promptInput(), { target: { value: 'Go' } });
  fireEvent.click(screen.getByRole('button', { name: 'Send message' }));
  await waitFor(() => expect(mockCtx.agentSendMessage).toHaveBeenCalledWith('handle-1', 'Go'));
  act(() => mockCtx.enqueueEvent('complete', { full_text: 'Done.' }));
  await waitFor(() => expect(screen.getByTestId('agent-message-assistant')).toHaveTextContent('Done.'));
}

describe('AgentScreen replay-resume approval cards', () => {
  it('re-materializes a pending interaction request as an active card after the turn completed', async () => {
    await runFirstTurnToCompletion();

    // Resume bootstrap: the kernel replays a still-pending approval request
    // over the attached session.
    act(() => mockCtx.enqueueEvent('interaction_request', {
      request_id: 'req-resume-1',
      kind: 'confirmation',
      args: JSON.stringify({ effect_description: 'Submit the order form' }),
      state: 'pending',
    }));

    await waitFor(() => expect(screen.getByTestId('agent-approval-card')).toBeInTheDocument());
    expect(screen.getByTestId('agent-approval-card')).toHaveTextContent('Submit the order form');
    expect(screen.getByTestId('agent-approval-confirm')).toBeEnabled();
    expect(screen.getByTestId('agent-approval-deny')).toBeEnabled();
  });

  it('re-materializes an expired interaction request as a disabled card showing the final state', async () => {
    await runFirstTurnToCompletion();

    act(() => mockCtx.enqueueEvent('interaction_request', {
      request_id: 'req-resume-2',
      kind: 'confirmation',
      args: JSON.stringify({ effect_description: 'Submit the order form' }),
      state: 'expired',
    }));

    await waitFor(() => expect(screen.getByTestId('agent-approval-card')).toBeInTheDocument());
    expect(screen.getByTestId('agent-approval-confirm')).toBeDisabled();
    expect(screen.getByTestId('agent-approval-deny')).toBeDisabled();
    expect(screen.getByTestId('agent-approval-status')).toHaveTextContent('Expired');
  });

  it('falls back to the plain thread when a replayed payload is unsupported', async () => {
    await runFirstTurnToCompletion();

    // No request id — the normalization must discard it safely.
    act(() => mockCtx.enqueueEvent('interaction_request', { kind: 'question' }));
    // A following valid event proves the queue advanced past the bad payload.
    act(() => mockCtx.enqueueEvent('token', ' still here'));

    await waitFor(() => expect(screen.getByTestId('agent-thread')).toHaveTextContent('still here'));
    expect(screen.queryByTestId('agent-approval-card')).toBeNull();
  });
});

describe('agentReducer replay fidelity', () => {
  it('a replayed redelivery never resurrects a card the user already answered', () => {
    let state = initialAgentState();
    const base = {
      interactionKind: 'confirmation' as const,
      question: 'Drop database production_db_v2?',
      options: [],
      artifactRef: null,
    };

    state = agentReducer(state, {
      type: 'INTERACTION_REQUEST',
      requestId: 'req-1',
      ...base,
      initialState: 'pending',
      messageId: 'm1',
      createdAt: new Date(),
    });
    state = agentReducer(state, { type: 'INTERACTION_RESOLVE', requestId: 'req-1', answerLabel: 'Confirmed' });
    // Stale replay redelivery of the same request id arrives after the answer.
    state = agentReducer(state, {
      type: 'INTERACTION_REQUEST',
      requestId: 'req-1',
      ...base,
      initialState: 'pending',
      messageId: 'm2',
      createdAt: new Date(),
    });

    const block = state.messages
      .flatMap((message) => (message.role === 'assistant' ? [...message.blocks] : []))
      .find((entry) => entry.kind === 'interaction_request' && entry.id === 'req-1');
    expect(block).toBeDefined();
    if (block?.kind !== 'interaction_request') throw new Error('Expected interaction_request block');
    expect(block.status).toBe('resolved');
    expect(block.answerLabel).toBe('Confirmed');
  });
});
