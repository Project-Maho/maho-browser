import { act, cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { StreamSubscriber } from '../../../bridge/streaming';
import type { ChatContent } from '../../../bridge/types';

const mockBridge = {
  byokGetKey: vi.fn<(provider: string) => Promise<string | null>>(),
  chatSessionStart: vi.fn<() => Promise<string>>(),
  chatSessionResume: vi.fn<(handle: string) => Promise<string>>(),
  chatSessionFree: vi.fn<(handle: string) => Promise<void>>(),
  chatSendMessage: vi.fn<(handle: string, content: ChatContent) => Promise<void>>(),
  chatCancelTurn: vi.fn<(handle: string) => Promise<void>>(),
  chatRegisterTool: vi.fn<(handle: string) => Promise<void>>(),
  chatSendToolResult: vi.fn<(handle: string, toolCallId: string, result: unknown) => Promise<void>>(),
  chatGetHistory: vi.fn<(handle: string) => Promise<Array<{ id: string; role: 'user' | 'assistant' | 'system' | 'tool'; content: string; createdAt: string; toolCalls?: Array<{ id: string; tool: string; args: string; result?: string }> }>>>(),
  saveConversationMessage: vi.fn<(sessionId: string, role: string, content: string) => Promise<boolean>>(),
};

let latestSubscriber: StreamSubscriber | null = null;
const subscribeStreamMock = vi.fn<(sessionId: string, subscriber: StreamSubscriber) => () => void>();

vi.mock('../../../hooks/use-bridge', () => ({
  useBridge: () => mockBridge,
}));

vi.mock('../../../bridge/streaming', () => ({
  subscribeStream: (sessionId: string, subscriber: StreamSubscriber) => {
    latestSubscriber = subscriber;
    subscribeStreamMock(sessionId, subscriber);
    return () => {
      latestSubscriber = null;
    };
  },
}));

import { ChatScreen } from '../chat-screen';

function requireSubscriber(): Required<StreamSubscriber> {
  if (!latestSubscriber) {
    throw new Error('Expected stream subscriber to be registered.');
  }

  return latestSubscriber as Required<StreamSubscriber>;
}

afterEach(() => {
  cleanup();
  vi.clearAllMocks();
  latestSubscriber = null;
});

beforeEach(() => {
  window.location.hash = '#chat';
  mockBridge.byokGetKey.mockImplementation(async (provider: string) =>
    provider === 'openai' ? 'sk-openai' : null,
  );
  mockBridge.chatSessionStart.mockResolvedValue('session-new');
  mockBridge.chatSessionResume.mockResolvedValue('native-resumed-handle');
  mockBridge.chatSessionFree.mockResolvedValue(undefined);
  mockBridge.chatSendMessage.mockResolvedValue(undefined);
  mockBridge.chatCancelTurn.mockResolvedValue(undefined);
  mockBridge.chatRegisterTool.mockResolvedValue(undefined);
  mockBridge.chatSendToolResult.mockResolvedValue(undefined);
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
      expect(mockBridge.chatSessionResume).toHaveBeenCalledWith('saved-session');
      expect(screen.getByRole('heading', { name: 'Earlier question' })).toBeTruthy();
      expect(screen.getByText('Earlier answer')).toBeTruthy();
    });
  });

  it('sends a resumed conversation through the returned native handle', async () => {
    render(<ChatScreen sessionId="saved-session" />);

    await waitFor(() => {
      expect(mockBridge.chatSessionResume).toHaveBeenCalledWith('saved-session');
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

  it('sending a message calls chatSendMessage and renders the user bubble optimistically', async () => {
    render(<ChatScreen />);

    await waitFor(() => {
      expect(mockBridge.chatSessionStart).toHaveBeenCalled();
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

  it('appends streaming tokens into the assistant bubble', async () => {
    render(<ChatScreen />);

    await waitFor(() => {
      expect(latestSubscriber).not.toBeNull();
    });

    const input = screen.getByLabelText('Message');
    fireEvent.input(input, { target: { value: 'Prompt' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));

    const subscriber = requireSubscriber();
    act(() => {
      subscriber.onToken('Streaming reply');
    });

    expect(screen.getByText('Streaming reply')).toBeTruthy();
  });

  it('replaces the streaming bubble with the final message on completion', async () => {
    render(<ChatScreen />);

    await waitFor(() => {
      expect(latestSubscriber).not.toBeNull();
    });

    const input = screen.getByLabelText('Message');
    fireEvent.input(input, { target: { value: 'Prompt' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));

    const subscriber = requireSubscriber();
    act(() => {
      subscriber.onToken('Partial');
      subscriber.onComplete('Final response');
    });

    await waitFor(() => {
      expect(screen.getByText('Final response')).toBeTruthy();
    });
  });

  it('shows stream errors in the UI', async () => {
    render(<ChatScreen />);

    await waitFor(() => {
      expect(latestSubscriber).not.toBeNull();
    });

    const input = screen.getByLabelText('Message');
    fireEvent.input(input, { target: { value: 'Prompt' } });
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));

    const subscriber = requireSubscriber();
    act(() => {
      subscriber.onError('Streaming failed');
    });

    await waitFor(() => {
      expect(screen.getByRole('alert')).toHaveTextContent('Streaming failed');
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

  it('renders the tool call card and reports unsupported execution when approved', async () => {
    render(<ChatScreen />);

    await waitFor(() => {
      expect(latestSubscriber).not.toBeNull();
    });

    const subscriber = requireSubscriber();
    act(() => {
      subscriber.onToolDelta({
        args: { query: 'maho' },
        id: 'tool-1',
        name: 'search_history',
      });
    });

    expect(screen.getByText('search_history')).toBeTruthy();
    fireEvent.click(screen.getByRole('button', { name: 'Approve search_history' }));

    await waitFor(() => {
      expect(mockBridge.chatSendToolResult).toHaveBeenCalledWith('session-new', 'tool-1', {
        error: 'tool_execution_unsupported',
      });
      expect(screen.getByText('Tool execution unsupported')).toBeTruthy();
      expect(screen.queryByText('Approved')).toBeNull();
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
        expect(mockBridge.chatSessionStart).toHaveBeenCalled();
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

    act(() => {
      latestSubscriber?.onError?.('Stream connection reset by peer');
    });

    await waitFor(() => {
      expect(screen.getByText('Stream connection reset by peer')).toBeTruthy();
    });
    expect(screen.getAllByText('Previous user message').length).toBeGreaterThan(0);
  });
});
