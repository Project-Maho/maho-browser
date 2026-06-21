import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/preact';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

interface StreamSubscriber {
  onToken?: (token: string) => void;
  onComplete?: (finalMessage: string) => void;
  onError?: (error: string) => void;
  onToolDelta?: (delta: { id?: string; name?: string; args?: unknown }) => void;
}

interface ToolResult {
  success: boolean;
  output: string;
}

const mockBridge = {
  byokGetKey: vi.fn<(provider: string) => Promise<string | null>>(),
  chatSessionStart: vi.fn<() => Promise<string>>(),
  chatSessionFree: vi.fn<(handle: string) => Promise<void>>(),
  chatSendMessage: vi.fn<() => Promise<void>>(),
  chatCancelTurn: vi.fn<(handle: string) => Promise<void>>(),
  chatRegisterTool: vi.fn<() => Promise<void>>(),
  chatSendToolResult: vi.fn<(handle: string, toolCallId: string, result: ToolResult) => Promise<void>>(),
};

vi.mock('../../../hooks/use-bridge', () => ({
  useBridge: () => mockBridge,
}));

import { AgentScreen } from '../agent-screen';

function getSubscriber(): Required<StreamSubscriber> {
  const globalWindow = window as Window & {
    __mahoAgentStreamSubscribers?: Map<string, StreamSubscriber>;
  };
  const map = globalWindow.__mahoAgentStreamSubscribers;
  if (!map || map.size === 0) throw new Error('Expected stream subscriber');
  const sub = map.values().next().value;
  if (!sub) throw new Error('Expected stream subscriber');
  return sub as Required<StreamSubscriber>;
}

afterEach(() => {
  cleanup();
  vi.clearAllMocks();
  const globalWindow = window as Window & { __mahoAgentStreamSubscribers?: Map<string, StreamSubscriber> };
  globalWindow.__mahoAgentStreamSubscribers?.clear();
});

beforeEach(() => {
  mockBridge.byokGetKey.mockImplementation(async (provider: string) =>
    provider === 'openai' ? 'sk-test' : null,
  );
  mockBridge.chatSessionStart.mockResolvedValue('agent-session-1');
  mockBridge.chatSessionFree.mockResolvedValue(undefined);
  mockBridge.chatSendMessage.mockResolvedValue(undefined);
  mockBridge.chatCancelTurn.mockResolvedValue(undefined);
  mockBridge.chatRegisterTool.mockResolvedValue(undefined);
  mockBridge.chatSendToolResult.mockResolvedValue(undefined);
});

describe('AgentScreen', () => {
  it('shows BYOK CTA and disables Run Agent when API key is missing', async () => {
    mockBridge.byokGetKey.mockResolvedValue(null);

    render(<AgentScreen />);

    await waitFor(() => {
      expect(screen.getByText('API key required')).toBeTruthy();
      expect(screen.getByText('Configure API key')).toBeTruthy();
    });
  });

  it('submits task: calls chatSessionStart + chatRegisterTool×4 + chatSendMessage', async () => {
    render(<AgentScreen />);

    await waitFor(() => {
      expect(screen.getByLabelText('Task description')).toBeTruthy();
    });

    const textarea = screen.getByLabelText('Task description');
    fireEvent.input(textarea, { target: { value: 'Navigate to google.com' } });
    fireEvent.click(screen.getByRole('button', { name: 'Run Agent' }));

    await waitFor(() => {
      expect(mockBridge.chatSessionStart).toHaveBeenCalled();
      expect(mockBridge.chatRegisterTool).toHaveBeenCalledTimes(4);
      expect(mockBridge.chatSendMessage).toHaveBeenCalledWith('agent-session-1', {
        kind: 'text',
        text: 'Navigate to google.com',
      });
    });
  });

  it('appends streaming tokens as thinking entries', async () => {
    render(<AgentScreen />);

    await waitFor(() => expect(screen.getByLabelText('Task description')).toBeTruthy());

    const textarea = screen.getByLabelText('Task description');
    fireEvent.input(textarea, { target: { value: 'test task' } });
    fireEvent.click(screen.getByRole('button', { name: 'Run Agent' }));

    await waitFor(() => {
      const gw = window as Window & { __mahoAgentStreamSubscribers?: Map<string, StreamSubscriber> };
      expect(gw.__mahoAgentStreamSubscribers?.size).toBeGreaterThan(0);
    });

    act(() => {
      getSubscriber().onToken('Navigating to page...');
    });

    await waitFor(() => {
      expect(screen.getByText('Navigating to page...')).toBeTruthy();
    });
  });

  it('renders tool card on tool delta with awaiting_approval stage', async () => {
    render(<AgentScreen />);

    await waitFor(() => expect(screen.getByLabelText('Task description')).toBeTruthy());

    fireEvent.input(screen.getByLabelText('Task description'), { target: { value: 'go' } });
    fireEvent.click(screen.getByRole('button', { name: 'Run Agent' }));

    await waitFor(() => {
      const gw = window as Window & { __mahoAgentStreamSubscribers?: Map<string, StreamSubscriber> };
      expect(gw.__mahoAgentStreamSubscribers?.size).toBeGreaterThan(0);
    });

    act(() => {
      getSubscriber().onToolDelta({
        id: 'tc-1',
        name: 'browser_navigate',
        args: { url: 'https://google.com' },
      });
    });

    await waitFor(() => {
      expect(screen.getByText('browser_navigate')).toBeTruthy();
      expect(screen.getByText('pending')).toBeTruthy();
      expect(screen.getByRole('button', { name: 'Approve browser_navigate' })).toBeTruthy();
      expect(screen.getByRole('button', { name: 'Deny browser_navigate' })).toBeTruthy();
    });
  });

  it('approve calls chatSendToolResult with stub OK result', async () => {
    render(<AgentScreen />);

    await waitFor(() => expect(screen.getByLabelText('Task description')).toBeTruthy());

    fireEvent.input(screen.getByLabelText('Task description'), { target: { value: 'go' } });
    fireEvent.click(screen.getByRole('button', { name: 'Run Agent' }));

    await waitFor(() => {
      const gw = window as Window & { __mahoAgentStreamSubscribers?: Map<string, StreamSubscriber> };
      expect(gw.__mahoAgentStreamSubscribers?.size).toBeGreaterThan(0);
    });

    act(() => {
      getSubscriber().onToolDelta({
        id: 'tc-2',
        name: 'screenshot',
        args: {},
      });
    });

    await waitFor(() => expect(screen.getByRole('button', { name: 'Approve screenshot' })).toBeTruthy());

    fireEvent.click(screen.getByRole('button', { name: 'Approve screenshot' }));

    await waitFor(() => {
      expect(mockBridge.chatSendToolResult).toHaveBeenCalledWith(
        'agent-session-1',
        'tc-2',
        { success: true, output: JSON.stringify({ ok: true, data: '[stub]' }) },
      );
    });
  });

  it('deny calls chatSendToolResult with error', async () => {
    render(<AgentScreen />);

    await waitFor(() => expect(screen.getByLabelText('Task description')).toBeTruthy());

    fireEvent.input(screen.getByLabelText('Task description'), { target: { value: 'go' } });
    fireEvent.click(screen.getByRole('button', { name: 'Run Agent' }));

    await waitFor(() => {
      const gw = window as Window & { __mahoAgentStreamSubscribers?: Map<string, StreamSubscriber> };
      expect(gw.__mahoAgentStreamSubscribers?.size).toBeGreaterThan(0);
    });

    act(() => {
      getSubscriber().onToolDelta({
        id: 'tc-3',
        name: 'dom_query',
        args: { selector: 'h1' },
      });
    });

    await waitFor(() => expect(screen.getByRole('button', { name: 'Deny dom_query' })).toBeTruthy());

    fireEvent.click(screen.getByRole('button', { name: 'Deny dom_query' }));

    await waitFor(() => {
      expect(mockBridge.chatSendToolResult).toHaveBeenCalledWith(
        'agent-session-1',
        'tc-3',
        { success: false, output: JSON.stringify({ error: 'user_denied' }) },
      );
    });
  });

  it('stream complete shows final result and New Task button', async () => {
    render(<AgentScreen />);

    await waitFor(() => expect(screen.getByLabelText('Task description')).toBeTruthy());

    fireEvent.input(screen.getByLabelText('Task description'), { target: { value: 'go' } });
    fireEvent.click(screen.getByRole('button', { name: 'Run Agent' }));

    await waitFor(() => {
      const gw = window as Window & { __mahoAgentStreamSubscribers?: Map<string, StreamSubscriber> };
      expect(gw.__mahoAgentStreamSubscribers?.size).toBeGreaterThan(0);
    });

    act(() => {
      getSubscriber().onComplete('Task completed successfully.');
    });

    await waitFor(() => {
      expect(screen.getByText('Task completed successfully.')).toBeTruthy();
      expect(screen.getByText('New Task')).toBeTruthy();
    });
  });

  it('cancel calls chatCancelTurn', async () => {
    render(<AgentScreen />);

    await waitFor(() => expect(screen.getByLabelText('Task description')).toBeTruthy());

    fireEvent.input(screen.getByLabelText('Task description'), { target: { value: 'go' } });
    fireEvent.click(screen.getByRole('button', { name: 'Run Agent' }));

    await waitFor(() => {
      const gw = window as Window & { __mahoAgentStreamSubscribers?: Map<string, StreamSubscriber> };
      expect(gw.__mahoAgentStreamSubscribers?.size).toBeGreaterThan(0);
    });

    act(() => {
      getSubscriber().onToken('working...');
    });

    await waitFor(() => expect(screen.getByRole('button', { name: 'Cancel' })).toBeTruthy());

    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));

    await waitFor(() => {
      expect(mockBridge.chatCancelTurn).toHaveBeenCalledWith('agent-session-1');
    });
  });

  it('new task frees session and resets state', async () => {
    render(<AgentScreen />);

    await waitFor(() => expect(screen.getByLabelText('Task description')).toBeTruthy());

    fireEvent.input(screen.getByLabelText('Task description'), { target: { value: 'go' } });
    fireEvent.click(screen.getByRole('button', { name: 'Run Agent' }));

    await waitFor(() => {
      const gw = window as Window & { __mahoAgentStreamSubscribers?: Map<string, StreamSubscriber> };
      expect(gw.__mahoAgentStreamSubscribers?.size).toBeGreaterThan(0);
    });

    act(() => {
      getSubscriber().onComplete('Done.');
    });

    await waitFor(() => expect(screen.getByText('New Task')).toBeTruthy());

    fireEvent.click(screen.getByText('New Task'));

    await waitFor(() => {
      expect(mockBridge.chatSessionFree).toHaveBeenCalledWith('agent-session-1');
      expect(screen.getByLabelText('Task description')).toBeTruthy();
    });
  });
});
