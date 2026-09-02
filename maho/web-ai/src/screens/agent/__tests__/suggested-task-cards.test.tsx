/**
 * Plan row 14 — mobile SPA parity of the desktop row-12 idle suggested-task
 * cards. Clicking a card takes the SPA's existing session-create path
 * (agentCreateSession), applies the guard / final-confirm / proactive flag
 * set through agentSetRuntimeConfig (FFI maho_agent_set_runtime_config
 * parity, rows 1/12), then starts the suggested prompt. A broker rejection
 * means the task is not started (fail-closed), and suggestions disabled
 * means no cards render at all.
 */
import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { AgentRuntimeConfig, MahoBridge } from '../../../bridge/types';
import { AgentScreen } from '../agent-screen';
import {
  SUGGESTED_TASKS,
  SUGGESTED_TASK_RUNTIME_CONFIG,
  getSuggestedTaskCards,
} from '../suggested-tasks';

function makeMockBridge() {
  let eventQueue: string[] = [];
  const agentCreateSession = vi.fn<MahoBridge['agentCreateSession']>().mockResolvedValue('handle-1');
  const agentSendMessage = vi.fn<MahoBridge['agentSendMessage']>().mockResolvedValue(true);
  const agentCancel = vi.fn<MahoBridge['agentCancel']>().mockResolvedValue(true);
  const agentFreeSession = vi.fn<MahoBridge['agentFreeSession']>().mockResolvedValue(undefined);
  const agentPollEvent = vi.fn<MahoBridge['agentPollEvent']>().mockImplementation(async () => eventQueue.shift() ?? null);
  const agentSetRuntimeConfig = vi.fn<NonNullable<MahoBridge['agentSetRuntimeConfig']>>().mockResolvedValue(true);
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
    agentSetRuntimeConfig,
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
    agentCreateSession,
    agentPollEvent,
    agentSendMessage,
    agentSetRuntimeConfig,
    bridge,
    clearQueue: () => {
      eventQueue = [];
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

describe('suggested task catalog (backend suggestions.rs parity)', () => {
  it('mirrors the backend catalog with non-blank title + prompt pairs', () => {
    expect(SUGGESTED_TASKS.length).toBeGreaterThan(0);
    expect(SUGGESTED_TASKS.length).toBeLessThanOrEqual(2); // MAX_PASSIVE_SUGGESTIONS
    for (const task of SUGGESTED_TASKS) {
      expect(task.title.trim().length).toBeGreaterThan(0);
      expect(task.prompt.trim().length).toBeGreaterThan(0);
    }
  });

  it('returns the full catalog when suggestion settings are enabled', () => {
    expect(getSuggestedTaskCards({ enabled: true })).toEqual(SUGGESTED_TASKS);
  });

  it('returns zero cards when suggestions are disabled (SuggestionSettings::disabled parity)', () => {
    expect(getSuggestedTaskCards({ enabled: false })).toEqual([]);
  });

  it('carries the guard/final-confirm/proactive flag set for new sessions', () => {
    const config: AgentRuntimeConfig = SUGGESTED_TASK_RUNTIME_CONFIG;
    expect(config).toEqual({
      permissionTier: 'guard',
      finalConfirm: true,
      proactiveMode: true,
    });
  });
});

describe('AgentScreen idle suggested-task cards', () => {
  it('renders one card per catalog entry with title and prompt', () => {
    render(<AgentScreen bridge={mockCtx.bridge} />);

    expect(screen.getByTestId('agent-suggested-tasks')).toBeInTheDocument();
    const cards = screen.getAllByTestId('agent-suggested-task');
    expect(cards).toHaveLength(SUGGESTED_TASKS.length);
    cards.forEach((card, index) => {
      expect(card.textContent).toContain(SUGGESTED_TASKS[index]!.title);
      expect(card.textContent).toContain(SUGGESTED_TASKS[index]!.prompt);
    });
  });

  it('creates a session with the 3-flag runtime config and starts the prompt on click', async () => {
    render(<AgentScreen bridge={mockCtx.bridge} />);

    fireEvent.click(screen.getAllByTestId('agent-suggested-task')[0]!);

    // Card click reuses the SPA session-create path...
    await waitFor(() => expect(mockCtx.agentSetRuntimeConfig).toHaveBeenCalledOnce());
    expect(mockCtx.agentCreateSession).toHaveBeenCalledOnce();
    // ...applies the 3-flag payload to the created session (create → flags → send)...
    expect(mockCtx.agentSetRuntimeConfig).toHaveBeenCalledWith('handle-1', SUGGESTED_TASK_RUNTIME_CONFIG);
    expect(mockCtx.agentSetRuntimeConfig.mock.invocationCallOrder[0]!)
      .toBeGreaterThan(mockCtx.agentCreateSession.mock.invocationCallOrder[0]!);
    // ...and starts the suggested task with the card's prompt.
    await waitFor(() => expect(mockCtx.agentSendMessage).toHaveBeenCalledWith('handle-1', SUGGESTED_TASKS[0]!.prompt));
    expect(mockCtx.agentSendMessage).toHaveBeenCalledOnce();
    expect(within(screen.getByTestId('agent-message-user')).getByText(SUGGESTED_TASKS[0]!.prompt)).toBeInTheDocument();
    // The thread took over — idle cards are gone.
    await waitFor(() => expect(screen.queryByTestId('agent-suggested-tasks')).toBeNull());
  });

  it('renders zero cards when suggestions are disabled', () => {
    render(<AgentScreen bridge={mockCtx.bridge} suggestionsEnabled={false} />);

    expect(screen.queryByTestId('agent-suggested-tasks')).toBeNull();
    expect(screen.queryAllByTestId('agent-suggested-task')).toHaveLength(0);
  });

  it('does not start the task when the broker rejects the flag set', async () => {
    mockCtx.agentSetRuntimeConfig.mockResolvedValue(false);
    render(<AgentScreen bridge={mockCtx.bridge} />);

    fireEvent.click(screen.getAllByTestId('agent-suggested-task')[0]!);

    await waitFor(() => expect(mockCtx.agentSetRuntimeConfig).toHaveBeenCalledOnce());
    expect(mockCtx.agentSendMessage).not.toHaveBeenCalled();
    expect(screen.getByTestId('agent-error-banner')).toBeInTheDocument();
  });
});
