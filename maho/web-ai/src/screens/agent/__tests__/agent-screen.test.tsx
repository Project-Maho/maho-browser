import { act, cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { App } from '../../../app';
import type { MahoBridge } from '../../../bridge/types';
import { AgentScreen } from '../agent-screen';

type EventType = 'token' | 'thinking' | 'tool_call' | 'tool_result' | 'artifact_created' | 'complete' | 'error';

function makeMockBridge() {
  let eventQueue: string[] = [];
  const agentCreateSession = vi.fn<MahoBridge['agentCreateSession']>().mockResolvedValue('handle-1');
  const agentSendMessage = vi.fn<MahoBridge['agentSendMessage']>().mockResolvedValue(true);
  const agentCancel = vi.fn<MahoBridge['agentCancel']>().mockResolvedValue(true);
  const agentFreeSession = vi.fn<MahoBridge['agentFreeSession']>().mockResolvedValue(undefined);
  const agentPollEvent = vi.fn<MahoBridge['agentPollEvent']>().mockImplementation(async () => eventQueue.shift() ?? null);
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
    agentSendMessage,
    artifactShare,
    bridge,
    clearQueue: () => {
      eventQueue = [];
    },
    enqueueEvent: (type: EventType, data: any) => {
      eventQueue.push(JSON.stringify({ type, data }));
    },
  };
}

function promptInput() {
  return (
    screen.queryByRole('textbox', { name: 'Task description' }) ??
    screen.getByRole('textbox', { name: 'Message' })
  );
}

async function submitPrompt(prompt: string) {
  fireEvent.input(promptInput(), { target: { value: prompt } });
  fireEvent.click(screen.getByRole('button', { name: 'Send message' }));
  await waitFor(() => expect(mockCtx.agentSendMessage).toHaveBeenCalledWith('handle-1', prompt));
}

let mockCtx = makeMockBridge();

beforeEach(() => {
  window.location.hash = '#agent';
  mockCtx = makeMockBridge();
});

afterEach(() => {
  cleanup();
  vi.clearAllMocks();
  mockCtx.clearQueue();
});

describe('AgentScreen CompactShell contract', () => {
  it('renders the compact structure with one new-session control and no emoji text', () => {
    const { container } = render(<AgentScreen bridge={mockCtx.bridge} />);

    expect(screen.getByTestId('agent-screen')).toBeInTheDocument();
    expect(screen.getByTestId('agent-topbar')).toBeInTheDocument();
    expect(screen.getByTestId('agent-stage')).toHaveTextContent('Ready when you are');
    expect(screen.getByTestId('agent-composer')).toBeInTheDocument();
    expect(screen.getAllByRole('button', { name: 'Start new session' })).toHaveLength(1);
    expect(screen.queryByRole('button', { name: 'New task' })).not.toBeInTheDocument();
    expect(container.textContent ?? '').not.toMatch(/[\u{1F300}-\u{1FAFF}\u{2600}-\u{27BF}]/u);
  });

  it('submits manually, renders the user turn, and clears the persistent composer', async () => {
    render(<AgentScreen bridge={mockCtx.bridge} />);

    await submitPrompt('Summarise this page');

    expect(mockCtx.agentCreateSession).toHaveBeenCalledOnce();
    expect(within(screen.getByTestId('agent-message-user')).getByText('Summarise this page')).toBeInTheDocument();
    expect(promptInput()).toHaveValue('');
    expect(screen.getByRole('button', { name: 'Stop current run' })).toBeInTheDocument();
  });

  it('uses Enter to send, Shift+Enter for newline intent, and ignores IME Enter', async () => {
    render(<AgentScreen bridge={mockCtx.bridge} />);

    fireEvent.input(promptInput(), { target: { value: 'Line one' } });
    fireEvent.keyDown(promptInput(), { key: 'Enter', shiftKey: true });
    expect(mockCtx.agentSendMessage).not.toHaveBeenCalled();

    fireEvent.compositionStart(promptInput());
    fireEvent.keyDown(promptInput(), { key: 'Enter' });
    expect(mockCtx.agentSendMessage).not.toHaveBeenCalled();

    fireEvent.compositionEnd(promptInput());
    fireEvent.keyDown(promptInput(), { key: 'Enter' });
    await waitFor(() => expect(mockCtx.agentSendMessage).toHaveBeenCalledWith('handle-1', 'Line one'));
  });

  it('auto-submits a non-empty goal exactly once per mount', async () => {
    const view = render(<AgentScreen bridge={mockCtx.bridge} goal="Review pinned tabs" />);

    await waitFor(() => expect(mockCtx.agentSendMessage).toHaveBeenCalledWith('handle-1', 'Review pinned tabs'));
    view.rerender(<AgentScreen bridge={mockCtx.bridge} goal="Review pinned tabs" />);

    await waitFor(() => expect(mockCtx.agentSendMessage).toHaveBeenCalledTimes(1));
  });

  it('accumulates tokens into the assistant turn and shows pending state while streaming', async () => {
    render(<AgentScreen bridge={mockCtx.bridge} />);
    await submitPrompt('Go');

    expect(screen.getByTestId('agent-pending-thought')).toHaveTextContent('Thinking');
    act(() => mockCtx.enqueueEvent('token', 'Hello'));
    await waitFor(() => expect(screen.getByTestId('agent-message-assistant')).toHaveTextContent('Hello'));
    act(() => mockCtx.enqueueEvent('token', ' world'));

    await waitFor(() => expect(screen.getByTestId('agent-message-assistant')).toHaveTextContent('Hello world'));
  });

  it('handles complete and error events through stable thread affordances', async () => {
    render(<AgentScreen bridge={mockCtx.bridge} />);
    await submitPrompt('Go');

    act(() => mockCtx.enqueueEvent('complete', { full_text: 'Task done.', tool_calls_json: '[]' }));
    await waitFor(() => expect(screen.getByTestId('agent-message-assistant')).toHaveTextContent('Task done.'));
    expect(screen.getByRole('button', { name: 'Send message' })).toBeInTheDocument();

    fireEvent.input(promptInput(), { target: { value: 'Again' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));
    act(() => mockCtx.enqueueEvent('error', 'Execution error: network timeout'));

    await waitFor(() => expect(screen.getByTestId('agent-error-banner')).toHaveTextContent('Execution error: network timeout'));
  });

  it('renders a typed credential error as an actionable card, not the raw diagnostic', async () => {
    render(<AgentScreen bridge={mockCtx.bridge} />);
    await submitPrompt('Go');

    const envelope = JSON.stringify({
      version: 1,
      kind: 'credential_error',
      code: 'secure_store_unavailable',
    });
    act(() => mockCtx.enqueueEvent('error', envelope));

    await waitFor(() => expect(screen.getByTestId('credential-error-card')).toBeTruthy());
    const card = screen.getByTestId('credential-error-card');
    expect(card).toHaveTextContent('AI credentials are unavailable');
    expect(screen.getByTestId('credential-error-action')).toHaveTextContent('Open AI settings');
    expect(card).not.toHaveTextContent('credential_error');
    expect(card).not.toHaveTextContent('Secure storage callback');
    expect(screen.queryByTestId('agent-error-banner')).toBeNull();
    const userBubble = within(screen.getByTestId('agent-thread')).getByText('Go');
    expect(
      card.compareDocumentPosition(userBubble) & Node.DOCUMENT_POSITION_PRECEDING,
    ).toBeTruthy();
  });

  it('stops the current run and resets through the single topbar new-session action', async () => {
    render(<AgentScreen bridge={mockCtx.bridge} />);
    await submitPrompt('Go');

    fireEvent.click(screen.getByRole('button', { name: 'Stop current run' }));
    await waitFor(() => expect(mockCtx.agentCancel).toHaveBeenCalledWith('handle-1'));

    fireEvent.click(screen.getByRole('button', { name: 'Start new session' }));
    await waitFor(() => {
      expect(mockCtx.agentFreeSession).toHaveBeenCalledWith('handle-1');
      expect(screen.getByTestId('agent-stage')).toBeInTheDocument();
    });
  });

  it('frees the native handle on unmount', async () => {
    const view = render(<AgentScreen bridge={mockCtx.bridge} />);
    await submitPrompt('Go');

    view.unmount();

    await waitFor(() => expect(mockCtx.agentFreeSession).toHaveBeenCalledWith('handle-1'));
  });

  it('renders AgentScreen from the #agent route', async () => {
    window.location.hash = '#agent';

    render(<App />);

    await waitFor(() => expect(screen.getByTestId('agent-screen')).toBeInTheDocument());
  });

  it('shares an artifact with the active opaque handle and shows busy then success state', async () => {
    let resolveShare: ((shared: boolean) => void) | undefined;
    mockCtx.artifactShare.mockImplementation(() => new Promise<boolean>((resolve) => {
      resolveShare = resolve;
    }));
    render(<AgentScreen bridge={mockCtx.bridge} />);
    await submitPrompt('Create a plan');

    act(() => mockCtx.enqueueEvent('artifact_created', {
      artifact_id: 'artifact-42',
      session_id: 'session-private',
      display_name: 'plan.html',
      mime_type: 'text/html',
      size_bytes: 128,
      created_at: 1,
      path: '/private/native/path/plan.html',
    }));

    const shareButton = await screen.findByRole('button', { name: 'Share plan.html' });
    fireEvent.click(shareButton);

    expect(mockCtx.artifactShare).toHaveBeenCalledWith('handle-1', 'artifact-42');
    expect(mockCtx.artifactShare).toHaveBeenCalledTimes(1);
    expect(shareButton).toBeDisabled();
    expect(shareButton).toHaveTextContent('Sharing…');

    act(() => resolveShare?.(true));
    await waitFor(() => expect(shareButton).toHaveTextContent('Shared'));
  });

  it('surfaces native artifact-share rejection and restores the Share action', async () => {
    mockCtx.artifactShare.mockRejectedValue(new Error('Native share sheet unavailable'));
    render(<AgentScreen bridge={mockCtx.bridge} />);
    await submitPrompt('Create a plan');

    act(() => mockCtx.enqueueEvent('artifact_created', {
      artifact_id: 'artifact-42',
      session_id: 'session-private',
      display_name: 'plan.html',
      mime_type: 'text/html',
      size_bytes: 128,
      created_at: 1,
    }));

    const shareButton = await screen.findByRole('button', { name: 'Share plan.html' });
    fireEvent.click(shareButton);

    await waitFor(() => expect(screen.getByRole('alert')).toHaveTextContent('Native share sheet unavailable'));
    expect(shareButton).toBeEnabled();
    expect(shareButton).toHaveTextContent('Share');
    expect(mockCtx.artifactShare).toHaveBeenCalledWith('handle-1', 'artifact-42');
  });

  it('renders thinking, tool calls, tool results, and interleaving in arrival order', async () => {
    render(<AgentScreen bridge={mockCtx.bridge} />);
    await submitPrompt('Go');

    // 1. Send non-empty thinking bubble
    act(() => mockCtx.enqueueEvent('thinking', 'Analyzing requirements'));
    await waitFor(() => expect(screen.getByTestId('agent-thinking-bubble')).toBeInTheDocument());
    fireEvent.click(screen.getByTestId('agent-thinking-bubble').querySelector('button')!);
    await waitFor(() => expect(screen.getByTestId('agent-thinking-bubble')).toHaveTextContent('Analyzing requirements'));

    // 2. Send empty/whitespace thinking (should be suppressed at reducer level)
    act(() => mockCtx.enqueueEvent('thinking', '   '));
    expect(screen.queryAllByTestId('agent-thinking-bubble')).toHaveLength(1);

    // 3. Send text message token
    act(() => mockCtx.enqueueEvent('token', 'Found 1 file.'));
    await waitFor(() => expect(screen.getByTestId('agent-message-assistant')).toHaveTextContent(/Found 1 file\./));

    // 4. Send tool call
    act(() => mockCtx.enqueueEvent('tool_call', { id: 'call-1', name: 'search', args: '{"query": "antigravity"}' }));
    await waitFor(() => expect(screen.getByTestId('agent-tool-call-block')).toBeInTheDocument());
    expect(screen.getByTestId('agent-tool-call-block')).toHaveTextContent('Using search');

    // 5. Send tool result
    act(() => mockCtx.enqueueEvent('tool_result', { id: 'call-1', name: 'search', result: '### Found details\nSuccess!' }));
    await waitFor(() => expect(screen.getByTestId('agent-tool-call-block')).toHaveTextContent('Used search'));

    // Click to expand tool execution details
    fireEvent.click(screen.getByRole('button', { name: 'Expand tool details' }));
    // Verify tool details render result using markdown
    expect(screen.getByTestId('agent-tool-call-block')).toHaveTextContent('Found detailsSuccess!');

    // 6. Send subsequent text token (interleaving)
    act(() => mockCtx.enqueueEvent('token', ' Done summarising.'));
    await waitFor(() => expect(screen.getByTestId('agent-message-assistant')).toHaveTextContent(/Found 1 file\..*Done summarising\./));
  });
});
