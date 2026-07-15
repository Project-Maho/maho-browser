import { act, cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { App } from '../../../app';
import type { MahoBridge } from '../../../bridge/types';
import { AgentScreen } from '../agent-screen';

type CompletePayload = { readonly full_text: string; readonly tool_calls_json: string };
type EventType = 'token' | 'complete' | 'error';

function makeMockBridge() {
  let eventQueue: string[] = [];
  const agentCreateSession = vi.fn<MahoBridge['agentCreateSession']>().mockResolvedValue('handle-1');
  const agentSendMessage = vi.fn<MahoBridge['agentSendMessage']>().mockResolvedValue(true);
  const agentCancel = vi.fn<MahoBridge['agentCancel']>().mockResolvedValue(true);
  const agentFreeSession = vi.fn<MahoBridge['agentFreeSession']>().mockResolvedValue(undefined);
  const agentPollEvent = vi.fn<MahoBridge['agentPollEvent']>().mockImplementation(async () => eventQueue.shift() ?? null);

  const bridge: MahoBridge = {
    byokGetProviders: vi.fn<MahoBridge['byokGetProviders']>().mockResolvedValue([]),
    byokGetKey: vi.fn<MahoBridge['byokGetKey']>().mockResolvedValue(null),
    byokSetKey: vi.fn<MahoBridge['byokSetKey']>().mockResolvedValue(true),
    byokDeleteKey: vi.fn<MahoBridge['byokDeleteKey']>().mockResolvedValue(true),
    byokValidateKey: vi.fn<MahoBridge['byokValidateKey']>().mockResolvedValue(true),
    chatSessionStart: vi.fn<MahoBridge['chatSessionStart']>().mockResolvedValue('chat-handle'),
    chatSessionResume: vi.fn<MahoBridge['chatSessionResume']>().mockResolvedValue(undefined),
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
    conversationGetMessages: vi.fn<MahoBridge['conversationGetMessages']>().mockResolvedValue([]),
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
    capturePhoto: vi.fn<MahoBridge['capturePhoto']>().mockResolvedValue(null),
    captureScreenshot: vi.fn<MahoBridge['captureScreenshot']>().mockResolvedValue(null),
    openSettings: vi.fn<MahoBridge['openSettings']>().mockResolvedValue(undefined),
    hapticFeedback: vi.fn<MahoBridge['hapticFeedback']>().mockResolvedValue(undefined),
  };

  return {
    agentCancel,
    agentCreateSession,
    agentFreeSession,
    agentSendMessage,
    bridge,
    clearQueue: () => {
      eventQueue = [];
    },
    enqueueEvent: (type: EventType, data: string | CompletePayload) => {
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
});
