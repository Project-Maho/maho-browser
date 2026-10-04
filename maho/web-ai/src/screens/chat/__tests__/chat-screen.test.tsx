import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { AiSettings, ChatContent, ChatSessionOpts } from '../../../bridge/types';

const mockBridge = {
  getAiSettings: vi.fn<() => Promise<AiSettings>>(),
  byokGetKey: vi.fn<(provider: string) => Promise<string | null>>(),
  chatSessionStart: vi.fn<(opts: ChatSessionOpts) => Promise<string>>(),
  chatSessionResume: vi.fn<(handle: string, opts: ChatSessionOpts) => Promise<string>>(),
  chatSessionFree: vi.fn<(handle: string) => Promise<void>>(),
  chatSendMessage: vi.fn<(handle: string, content: ChatContent) => Promise<void>>(),
  chatCancelTurn: vi.fn<(handle: string) => Promise<void>>(),
  chatPollEvents: vi.fn<(handle: string) => Promise<unknown[]>>(),
  chatRegisterTool: vi.fn<(handle: string) => Promise<void>>(),
  chatSendToolResult: vi.fn<(handle: string, toolCallId: string, result: unknown, options: { toolName: string; trigger: boolean }) => Promise<void>>(),
  chatAppendAssistantMessage: vi.fn<(handle: string, content: string, toolCallsJson?: string) => Promise<void>>(),
  browserToolInvoke: vi.fn<(name: string, args: Record<string, unknown>) => Promise<unknown>>(),
  chatGetHistory: vi.fn<(handle: string) => Promise<Array<{ id: string; role: 'user' | 'assistant' | 'system' | 'tool'; content: string; createdAt: string; toolCalls?: Array<{ id: string; tool: string; args: string; result?: string }> }>>>(),
  saveConversationMessage: vi.fn<(sessionId: string, role: string, content: string) => Promise<boolean>>(),
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

const pollQueue: unknown[][] = [];

function enqueuePoll(...batches: unknown[][]): void {
  pollQueue.push(...batches);
}

afterEach(() => {
  cleanup();
  vi.clearAllMocks();
  pollQueue.length = 0;
});

beforeEach(() => {
  window.location.hash = '#chat';
  mockBridge.browserToolInvoke = vi.fn<(name: string, args: Record<string, unknown>) => Promise<unknown>>();
  mockBridge.getAiSettings.mockResolvedValue(OPENAI_SETTINGS);
  mockBridge.byokGetKey.mockImplementation(async (provider: string) =>
    provider === 'openai' ? 'sk-openai' : null,
  );
  mockBridge.chatSessionStart.mockResolvedValue('session-new');
  mockBridge.chatSessionResume.mockResolvedValue('native-resumed-handle');
  mockBridge.chatSessionFree.mockResolvedValue(undefined);
  mockBridge.chatSendMessage.mockResolvedValue(undefined);
  mockBridge.chatCancelTurn.mockResolvedValue(undefined);
  mockBridge.chatPollEvents.mockImplementation(async () => pollQueue.shift() ?? []);
  mockBridge.chatRegisterTool.mockResolvedValue(undefined);
  mockBridge.chatSendToolResult.mockResolvedValue(undefined);
  mockBridge.chatAppendAssistantMessage.mockResolvedValue(undefined);
  mockBridge.browserToolInvoke.mockResolvedValue({ ok: true, result: [] });
  mockBridge.chatGetHistory.mockResolvedValue([]);
  mockBridge.saveConversationMessage.mockResolvedValue(true);
});

describe('ChatScreen', () => {
  it('renders the empty state when no messages and no sessionId', async () => {
    render(<ChatScreen />);

    await waitFor(() => {
      expect(screen.getByText('Start a conversation')).toBeTruthy();
    });
  });

  it('uses the selected compatible config, normalizes its endpoint, and does not expose its key', async () => {
    mockBridge.getAiSettings.mockResolvedValue({
      provider: 'openai-compatible',
      baseUrl: 'http://127.0.0.1:18080/v1/chat/completions///',
      model: 'f3-fixture-model',
      hasApiKey: false,
      hasByokOpenai: true,
      hasByokAnthropic: true,
    });

    render(<ChatScreen />);

    await waitFor(() => {
      expect(mockBridge.chatSessionStart).toHaveBeenCalledWith(expect.objectContaining({
        credentialProvider: 'openai-compatible',
        endpoint: 'http://127.0.0.1:18080/v1/chat/completions',
        model: 'f3-fixture-model',
      }));
    });

    const options = mockBridge.chatSessionStart.mock.calls[0]?.[0];
    expect(options).not.toHaveProperty('apiKey');
    expect(mockBridge.byokGetKey).not.toHaveBeenCalled();
  });

  it('starts Maho-managed chat with only the native credential marker and system instruction', async () => {
    mockBridge.getAiSettings.mockResolvedValue({
      provider: 'maho-managed',
      baseUrl: '',
      model: '',
      hasApiKey: true,
      hasByokOpenai: true,
      hasByokAnthropic: true,
    });

    render(<ChatScreen model="js-controlled-model" />);

    await waitFor(() => {
      expect(mockBridge.chatSessionStart).toHaveBeenCalledWith({
        credentialProvider: 'maho-managed',
        systemInstruction: 'You are the in-browser AI assistant for Maho Browser. Keep answers practical, concise, and grounded in the user’s question.',
      });
    });
    expect(mockBridge.byokGetKey).not.toHaveBeenCalled();
    expect(screen.queryByRole('button', { name: 'Configure API key' })).toBeNull();
  });

  it('does not fall back when the selected provider is missing its credential', async () => {
    mockBridge.getAiSettings.mockResolvedValue({
      ...OPENAI_SETTINGS,
      hasByokOpenai: false,
      hasByokAnthropic: true,
    });
    mockBridge.byokGetKey.mockImplementation(async (provider) =>
      provider === 'anthropic' ? 'sk-anthropic' : null,
    );

    render(<ChatScreen />);

    await waitFor(() => {
      expect(screen.getByRole('button', { name: 'Configure API key' })).toBeTruthy();
    });
    expect(mockBridge.byokGetKey).toHaveBeenCalledTimes(1);
    expect(mockBridge.byokGetKey).toHaveBeenCalledWith('openai');
    expect(mockBridge.chatSessionStart).not.toHaveBeenCalled();
  });

  it('renders the typed managed-auth card when native chat bootstrap rejects it', async () => {
    mockBridge.getAiSettings.mockResolvedValue({
      provider: 'maho-managed',
      baseUrl: '',
      model: '',
      hasApiKey: false,
      hasByokOpenai: false,
      hasByokAnthropic: false,
    });
    const envelope = JSON.stringify({
      version: 1,
      kind: 'credential_error',
      code: 'managed_auth_unavailable',
    });
    mockBridge.chatSessionStart.mockRejectedValue(new Error(envelope));

    render(<ChatScreen />);

    const card = await screen.findByTestId('credential-error-card');
    expect(card).toHaveTextContent('Reconnect your Maho account');
    expect(card).not.toHaveTextContent('credential_error');
    expect(card).not.toHaveTextContent('Managed chat is unavailable');
  });

  it('loads history when a sessionId is provided', async () => {
    mockBridge.chatGetHistory.mockResolvedValue([
      {
        content: 'Earlier question',
        createdAt: new Date().toISOString(),
        id: 'm1',
        role: 'user',
      },
      {
        content: 'Earlier answer',
        createdAt: new Date().toISOString(),
        id: 'm2',
        role: 'assistant',
      },
    ]);

    render(<ChatScreen sessionId="saved-session" />);

    await waitFor(() => {
      expect(mockBridge.chatSessionResume).toHaveBeenCalledWith(
        'saved-session',
        expect.objectContaining({
          apiKey: 'sk-openai',
          endpoint: 'https://api.openai.com/v1/chat/completions',
          model: 'gpt-4o',
        }),
      );
      expect(screen.getByRole('heading', { name: 'Earlier question' })).toBeTruthy();
      expect(screen.getByText('Earlier answer')).toBeTruthy();
    });
  });

  it('sends a resumed conversation through the returned native handle', async () => {
    render(<ChatScreen sessionId="saved-session" />);

    await waitFor(() => {
      expect(mockBridge.chatSessionResume).toHaveBeenCalledWith(
        'saved-session',
        expect.objectContaining({
          apiKey: 'sk-openai',
          endpoint: 'https://api.openai.com/v1/chat/completions',
          model: 'gpt-4o',
        }),
      );
    });

    const input = screen.getByLabelText('Message');
    fireEvent.input(input, { target: { value: 'Continue this conversation' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));

    await waitFor(() => {
      expect(mockBridge.chatSendMessage).toHaveBeenCalledWith('native-resumed-handle', {
        kind: 'text',
        text: 'Continue this conversation',
      });
      expect(mockBridge.saveConversationMessage).toHaveBeenCalledWith(
        'saved-session',
        'user',
        'Continue this conversation',
      );
    });
  });

  it('starts a new session with the selected provider options before sending', async () => {
    render(<ChatScreen />);

    await waitFor(() => {
      expect(mockBridge.chatSessionStart).toHaveBeenCalledWith(expect.objectContaining({
        apiKey: 'sk-openai',
        endpoint: 'https://api.openai.com/v1/chat/completions',
        model: 'gpt-4o',
      }));
    });

    const input = screen.getByLabelText('Message');
    fireEvent.input(input, { target: { value: 'Hello from test' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));

    await waitFor(() => {
      expect(mockBridge.chatSendMessage).toHaveBeenCalledWith('session-new', {
        kind: 'text',
        text: 'Hello from test',
      });
      expect(within(screen.getByRole('log')).getByText('Hello from test')).toBeTruthy();
    });
  });

  it('normalizes native wire tokens and complete payloads', async () => {
    enqueuePoll(
      [{ type: 'token', data: 'Partial' }],
      [{ type: 'complete', data: { full_text: 'Final response', tool_calls_json: '[]' } }],
    );
    render(<ChatScreen />);

    await waitFor(() => {
      expect(mockBridge.chatPollEvents).toHaveBeenCalledWith('session-new');
    });

    const input = screen.getByLabelText('Message');
    fireEvent.input(input, { target: { value: 'Prompt' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));

    await waitFor(() => {
      expect(screen.getByText('Final response')).toBeTruthy();
    });
  });

  it('normalizes native wire errors into the UI', async () => {
    enqueuePoll([], [{ type: 'error', data: 'Streaming failed' }]);
    render(<ChatScreen />);

    await waitFor(() => {
      expect(mockBridge.chatPollEvents).toHaveBeenCalledWith('session-new');
    });

    const input = screen.getByLabelText('Message');
    fireEvent.input(input, { target: { value: 'Prompt' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));

    await waitFor(() => {
      expect(screen.getByRole('alert')).toHaveTextContent('Streaming failed');
    });
  });

  it('renders a typed credential error as an actionable card, not the raw diagnostic', async () => {
    const envelope = JSON.stringify({
      version: 1,
      kind: 'credential_error',
      code: 'secure_store_unavailable',
    });
    enqueuePoll([], [{ type: 'error', data: envelope }]);
    render(<ChatScreen />);

    await waitFor(() => {
      expect(mockBridge.chatPollEvents).toHaveBeenCalledWith('session-new');
    });

    const input = screen.getByLabelText('Message');
    fireEvent.input(input, { target: { value: 'Prompt' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));

    await waitFor(() => {
      expect(screen.getByTestId('credential-error-card')).toBeTruthy();
    });
    const card = screen.getByTestId('credential-error-card');
    expect(card).toHaveTextContent('AI credentials are unavailable');
    expect(screen.getByTestId('credential-error-action')).toHaveTextContent('Open AI settings');
    // The raw envelope / diagnostic is never surfaced verbatim.
    expect(card).not.toHaveTextContent('credential_error');
    expect(card).not.toHaveTextContent('Secure storage callback');
    // The credential card must follow the message that triggered it, not float
    // above the conversation like the legacy top banner.
    const userBubble = within(screen.getByRole('log')).getByText('Prompt');
    expect(
      card.compareDocumentPosition(userBubble) & Node.DOCUMENT_POSITION_PRECEDING,
    ).toBeTruthy();
  });

  it('keeps polling for two sequential completed turns without overlapping requests', async () => {
    const pollResolvers: Array<(events: unknown[]) => void> = [];
    let inFlight = 0;
    let maxInFlight = 0;
    mockBridge.chatPollEvents.mockImplementation(async () => {
      inFlight += 1;
      maxInFlight = Math.max(maxInFlight, inFlight);
      const events = await new Promise<unknown[]>((resolve) => {
        pollResolvers.push(resolve);
      });
      inFlight -= 1;
      return events;
    });

    render(<ChatScreen />);
    await waitFor(() => expect(pollResolvers).toHaveLength(1));

    const input = screen.getByLabelText('Message');
    fireEvent.input(input, { target: { value: 'First' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));
    pollResolvers.shift()?.([{ type: 'complete', data: { full_text: 'First reply' } }]);
    await screen.findByText('First reply');

    await waitFor(() => expect(pollResolvers).toHaveLength(1));
    fireEvent.input(input, { target: { value: 'Second' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));
    pollResolvers.shift()?.([{ type: 'complete', data: { full_text: 'Second reply' } }]);

    await waitFor(() => {
      expect(screen.getByText('Second reply')).toBeTruthy();
      expect(mockBridge.chatSendMessage).toHaveBeenCalledTimes(2);
      expect(maxInFlight).toBe(1);
    });
  });

  it('cancel button calls chatCancelTurn', async () => {
    render(<ChatScreen />);

    await waitFor(() => {
      expect(mockBridge.chatSessionStart).toHaveBeenCalled();
    });

    const input = screen.getByLabelText('Message');
    fireEvent.input(input, { target: { value: 'Cancel me' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));

    fireEvent.click(await screen.findByRole('button', { name: 'Cancel response' }));

    await waitFor(() => {
      expect(mockBridge.chatCancelTurn).toHaveBeenCalledWith('session-new');
    });
  });

  it('renders a thinking bubble from native thinking wire events', async () => {
    enqueuePoll(
      [{ type: 'thinking', data: 'Let me reason' }],
      [{ type: 'thinking', data: ' step by step.' }],
      [{ type: 'token', data: 'Final answer' }],
      [{ type: 'complete', data: { full_text: 'Final answer', tool_calls_json: '[]' } }],
    );
    render(<ChatScreen />);

    const input = screen.getByLabelText('Message');
    fireEvent.input(input, { target: { value: 'Think' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));

    await waitFor(() => {
      expect(screen.getByTestId('chat-thinking-bubble')).toBeTruthy();
    });

    // Header shows streaming state then settles to 'Thought'.
    await waitFor(() => {
      expect(screen.getByText('Thought')).toBeTruthy();
    });
    expect(screen.getByText('Final answer')).toBeTruthy();

    // Thinking content is collapsed by default; expand to reveal it.
    fireEvent.click(screen.getByRole('button', { name: /Thought/ }));
    await waitFor(() => {
      expect(screen.getByText('Let me reason step by step.')).toBeTruthy();
    });
  });

  it('renders the tool call card and reports unsupported execution when approved', async () => {
    mockBridge.browserToolInvoke = undefined as never;
    enqueuePoll([{
      kind: 'tool_call',
      toolCallId: 'tool-1',
      name: 'search_history',
      args: { query: 'maho' },
    }]);
    render(<ChatScreen />);

    await waitFor(() => {
      expect(screen.getByText('search_history')).toBeTruthy();
    });

    expect(screen.getByText('search_history')).toBeTruthy();
    fireEvent.click(screen.getByRole('button', { name: 'Approve search_history' }));

    await waitFor(() => {
      expect(mockBridge.chatSendToolResult).toHaveBeenCalledWith('session-new', 'tool-1', {
        error: 'tool_execution_unsupported',
      }, { toolName: 'search_history', trigger: true });
      expect(screen.getByText('Tool execution unsupported')).toBeTruthy();
      expect(screen.queryByText('Approved')).toBeNull();
    });
  });

  it('auto-runs completed read tools and renders their exact native result', async () => {
    const nativeResult = {
      ok: true,
      result: [{ title: 'Example Domain', url: 'https://example.com' }],
    };
    mockBridge.browserToolInvoke.mockResolvedValue(nativeResult);
    enqueuePoll([{
      type: 'complete',
      data: {
        full_text: 'I found one tab.',
        tool_calls_json: JSON.stringify([{
          arguments_json: '{}',
          id: 'read-tabs',
          name: 'list_tabs',
        }]),
      },
    }]);

    render(<ChatScreen />);

    await waitFor(() => {
      expect(mockBridge.chatAppendAssistantMessage).toHaveBeenCalledWith(
        'session-new',
        'I found one tab.',
        JSON.stringify([{
          arguments_json: '{}',
          id: 'read-tabs',
          name: 'list_tabs',
        }]),
      );
      expect(mockBridge.browserToolInvoke).toHaveBeenCalledWith('list_tabs', {});
      expect(mockBridge.chatSendToolResult).toHaveBeenCalledWith('session-new', 'read-tabs', {
        output: JSON.stringify(nativeResult),
      }, { toolName: 'list_tabs', trigger: true });
    });

    expect(screen.getAllByText('list_tabs')).toHaveLength(2);
    expect(screen.getByText('Ran automatically')).toBeTruthy();
    expect(screen.queryByRole('button', { name: 'Approve list_tabs' })).toBeNull();
    expect(within(screen.getByRole('log')).getByText((_, node) =>
      node?.tagName === 'PRE' && node.textContent?.includes('Example Domain') === true,
    )).toBeTruthy();
  });

  it('sends multi-tool results in order and triggers only after the final result', async () => {
    const results = new Map([
      ['list_tabs', { ok: true, result: [{ url: 'https://example.com' }] }],
      ['get_page_info', { ok: true, result: { title: 'Example Domain', url: 'https://example.com' } }],
    ]);
    mockBridge.browserToolInvoke.mockImplementation(async (name) => results.get(name));
    enqueuePoll([{
      type: 'complete',
      data: {
        full_text: 'I checked the browser state.',
        tool_calls_json: JSON.stringify([
          { arguments_json: '{}', id: 'tabs', name: 'list_tabs' },
          { arguments_json: '{}', id: 'page', name: 'get_page_info' },
        ]),
      },
    }]);

    render(<ChatScreen />);

    await waitFor(() => {
      expect(mockBridge.chatSendToolResult).toHaveBeenCalledTimes(2);
    });

    expect(mockBridge.chatSendToolResult.mock.calls).toEqual([
      ['session-new', 'tabs', { output: JSON.stringify(results.get('list_tabs')) }, { toolName: 'list_tabs', trigger: false }],
      ['session-new', 'page', { output: JSON.stringify(results.get('get_page_info')) }, { toolName: 'get_page_info', trigger: true }],
    ]);
  });

  it('waits for action approval before continuing its completed tool batch', async () => {
    const openedUrl = 'https://example.com/approval-then-read';
    const nativeResults = new Map([
      ['open_tab', { ok: true, result: { url: openedUrl } }],
      ['list_tabs', { ok: true, result: [{ url: openedUrl }] }],
    ]);
    mockBridge.browserToolInvoke.mockImplementation(async (name) => nativeResults.get(name));
    enqueuePoll([{
      type: 'complete',
      data: {
        full_text: 'I can open the page and verify it.',
        tool_calls_json: JSON.stringify([
          { arguments_json: JSON.stringify({ url: openedUrl }), id: 'open', name: 'open_tab' },
          { arguments_json: '{}', id: 'tabs-after-open', name: 'list_tabs' },
        ]),
      },
    }]);

    render(<ChatScreen />);

    const approve = await screen.findByRole('button', { name: 'Approve open_tab' });
    expect(mockBridge.browserToolInvoke).not.toHaveBeenCalled();
    fireEvent.click(approve);

    await waitFor(() => {
      expect(mockBridge.browserToolInvoke.mock.calls).toEqual([
        ['open_tab', { url: openedUrl }],
        ['list_tabs', {}],
      ]);
      expect(mockBridge.chatSendToolResult.mock.calls).toEqual([
        ['session-new', 'open', { output: JSON.stringify(nativeResults.get('open_tab')) }, { toolName: 'open_tab', trigger: false }],
        ['session-new', 'tabs-after-open', { output: JSON.stringify(nativeResults.get('list_tabs')) }, { toolName: 'list_tabs', trigger: true }],
      ]);
    });
  });

  it('blocks a new turn while a completed action tool awaits approval', async () => {
    enqueuePoll([{
      type: 'complete',
      data: {
        full_text: 'I need approval before opening that page.',
        tool_calls_json: JSON.stringify([{
          arguments_json: '{"url":"https://example.com"}',
          id: 'pending-open',
          name: 'open_tab',
        }]),
      },
    }]);

    render(<ChatScreen />);

    await screen.findByRole('button', { name: 'Approve open_tab' });
    const input = screen.getByLabelText('Message');
    expect(input).toBeDisabled();
    expect(screen.getByRole('button', { name: 'Send message' })).toBeDisabled();

    fireEvent.input(input, { target: { value: 'Do something else instead' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));

    expect(mockBridge.chatSendMessage).not.toHaveBeenCalled();
  });

  it('approval-gates completed action tools and forwards the exact native result', async () => {
    const nativeResult = { ok: true, result: { url: 'https://example.com' } };
    mockBridge.browserToolInvoke.mockResolvedValue(nativeResult);
    enqueuePoll([{
      type: 'complete',
      data: {
        full_text: 'I can open that page.',
        tool_calls_json: JSON.stringify([{
          arguments_json: '{"url":"https://example.com"}',
          id: 'open-example',
          name: 'open_tab',
        }]),
      },
    }]);

    render(<ChatScreen />);

    const approve = await screen.findByRole('button', { name: 'Approve open_tab' });
    expect(mockBridge.browserToolInvoke).not.toHaveBeenCalled();
    fireEvent.click(approve);

    await waitFor(() => {
      expect(mockBridge.browserToolInvoke).toHaveBeenCalledWith('open_tab', {
        url: 'https://example.com',
      });
      expect(mockBridge.chatSendToolResult).toHaveBeenCalledWith('session-new', 'open-example', {
        output: JSON.stringify(nativeResult),
      }, { toolName: 'open_tab', trigger: true });
    });
    expect(screen.getByText('Approved')).toBeTruthy();
  });

  it('keeps completed action tools unsupported without the native RPC', async () => {
    mockBridge.browserToolInvoke = undefined as never;
    enqueuePoll([{
      type: 'complete',
      data: {
        full_text: 'I can open that page.',
        tool_calls_json: JSON.stringify([{
          arguments_json: '{"url":"https://example.com"}',
          id: 'unsupported-open',
          name: 'open_tab',
        }]),
      },
    }]);

    render(<ChatScreen />);

    fireEvent.click(await screen.findByRole('button', { name: 'Approve open_tab' }));

    await waitFor(() => {
      expect(mockBridge.chatSendToolResult).toHaveBeenCalledWith('session-new', 'unsupported-open', {
        error: 'tool_execution_unsupported',
      }, { toolName: 'open_tab', trigger: true });
      expect(screen.getByText('Tool execution unsupported')).toBeTruthy();
    });
  });

  it('shows the BYOK fallback CTA and disables send when no key is configured', async () => {
    mockBridge.byokGetKey.mockResolvedValue(null);

    render(<ChatScreen />);

    await waitFor(() => {
      expect(screen.getByRole('button', { name: 'Configure API key' })).toBeTruthy();
    });

    expect(screen.getByRole('button', { name: 'Send message' })).toBeDisabled();
  });

  it('uploads an image and sends the image chat content', async () => {
    const originalFileReader = globalThis.FileReader;

    class MockFileReader {
      public error: DOMException | null = null;
      public onerror: ((this: FileReader, ev: ProgressEvent<FileReader>) => unknown) | null = null;
      public onload: ((this: FileReader, ev: ProgressEvent<FileReader>) => unknown) | null = null;
      public result: string | ArrayBuffer | null = null;

      readAsDataURL(): void {
        this.result = 'data:image/png;base64,aGVsbG8=';
        this.onload?.call(
          this as unknown as FileReader,
          new ProgressEvent('load') as ProgressEvent<FileReader>,
        );
      }
    }

    globalThis.FileReader = MockFileReader as unknown as typeof FileReader;

    try {
      const { container } = render(<ChatScreen />);

      await waitFor(() => {
        expect(mockBridge.chatPollEvents).toHaveBeenCalledWith('session-new');
      });

      const fileInput = container.querySelector('input[type="file"]');
      expect(fileInput).not.toBeNull();

      const file = new File(['hello'], 'hello.png', { type: 'image/png' });
      fireEvent.change(fileInput as HTMLInputElement, { target: { files: [file] } });

      await waitFor(() => {
        expect(mockBridge.chatSendMessage).toHaveBeenCalledWith('session-new', {
          base64: 'aGVsbG8=',
          kind: 'image',
          mime: 'image/png',
        });
      });
    } finally {
      globalThis.FileReader = originalFileReader;
    }
  });

  it('renders markdown formatting for assistant messages', async () => {
    mockBridge.chatGetHistory.mockResolvedValue([
      {
        content: '**bold** *italic* `code`',
        createdAt: new Date().toISOString(),
        id: 'assistant-markdown',
        role: 'assistant',
      },
    ]);

    const { container } = render(<ChatScreen sessionId="markdown-session" />);

    await waitFor(() => {
      expect(screen.getByText('bold')).toBeTruthy();
    });

    expect(container.querySelector('strong')?.textContent).toBe('bold');
    expect(container.querySelector('em')?.textContent).toBe('italic');
    expect(container.querySelector('code')?.textContent).toBe('code');
  });

  it('handles stream errors gracefully without corrupting previous messages', async () => {
    enqueuePoll([], [{ type: 'error', data: 'Stream connection reset by peer' }]);
    mockBridge.chatGetHistory.mockResolvedValue([
      {
        content: 'Previous user message',
        createdAt: new Date().toISOString(),
        id: 'msg-1',
        role: 'user',
      },
    ]);

    const { container } = render(<ChatScreen sessionId="stream-err-session" />);

    await waitFor(() => {
      expect(screen.getAllByText('Previous user message').length).toBeGreaterThan(0);
    });

    const textarea = screen.getByPlaceholderText('Type a message…');
    fireEvent.input(textarea, { target: { value: 'New prompt' } });
    fireEvent.submit(container.querySelector('form')!);

    await waitFor(() => {
      expect(mockBridge.chatSendMessage).toHaveBeenCalled();
    });

    await waitFor(() => {
      expect(screen.getByText('Stream connection reset by peer')).toBeTruthy();
    });
    expect(screen.getAllByText('Previous user message').length).toBeGreaterThan(0);
  });
});
