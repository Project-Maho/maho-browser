import { render, screen } from '@testing-library/preact';
import { beforeEach, describe, expect, it, vi } from 'vitest';

const mockBridge = {
  byokGetKey: vi.fn().mockResolvedValue(null),
  getAiSettings: vi.fn().mockResolvedValue({
    provider: 'maho-managed',
    model: '',
    baseUrl: '',
    hasApiKey: false,
    hasByokOpenai: false,
    hasByokAnthropic: false,
  }),
  chatSessionStart: vi.fn().mockResolvedValue('session-1'),
  chatSessionResume: vi.fn().mockResolvedValue('resumed-session-1'),
  chatSessionFree: vi.fn().mockResolvedValue(undefined),
  chatPollEvents: vi.fn().mockResolvedValue([]),
  chatGetHistory: vi.fn().mockResolvedValue([]),
  chatRegisterTool: vi.fn().mockResolvedValue(undefined),
  chatSendMessage: vi.fn().mockResolvedValue(undefined),
  saveConversationMessage: vi.fn().mockResolvedValue(true),
  conversationsList: vi.fn().mockResolvedValue([]),
};

vi.mock('../hooks/use-bridge', () => ({
  useBridge: () => mockBridge,
}));

const agentLifecycle = vi.hoisted(() => ({ mounts: 0, unmounts: 0 }));
vi.mock('../screens/agent/agent-screen', async () => {
  const { useEffect } = await import('preact/hooks');
  return {
    AgentScreen: ({ goal }: { goal?: string }) => {
      useEffect(() => {
        agentLifecycle.mounts += 1;
        return () => { agentLifecycle.unmounts += 1; };
      }, []);
      return <div data-testid="agent-screen">{goal}</div>;
    },
  };
});

import { act, fireEvent, waitFor, within } from '@testing-library/preact';

import { App, Profiler } from '../app';

describe('App Router - overlays and Back', () => {
  const navigate = async (hash: string) => {
    await act(async () => {
      window.history.pushState(null, '', hash);
      window.dispatchEvent(new HashChangeEvent('hashchange'));
    });
  };
  const goBack = async () => {
    await act(async () => {
      const popped = new Promise((resolve) => window.addEventListener('popstate', resolve, { once: true }));
      window.history.back();
      await popped;
      window.dispatchEvent(new HashChangeEvent('hashchange'));
    });
  };

  beforeEach(() => {
    agentLifecycle.mounts = 0;
    agentLifecycle.unmounts = 0;
    window.history.replaceState(null, '', '#agent?goal=g1');
  });

  it('keeps the agent session mounted while settings is open from #agent', async () => {
    render(<App />);
    expect(await screen.findByTestId('agent-screen')).toBeVisible();

    await navigate('#byok');
    expect(await screen.findByTestId('aiScreenBYOK')).not.toBeNull();
    expect(screen.getByTestId('agent-screen')).not.toBeVisible();
    expect(agentLifecycle.unmounts).toBe(0);

    await goBack();
    await waitFor(() => expect(screen.getByTestId('agent-screen')).toBeVisible());
    expect(agentLifecycle.mounts).toBe(1);
    expect(agentLifecycle.unmounts).toBe(0);
  });

  it('releases the agent screen when navigating to a non-overlay route', async () => {
    render(<App />);
    await screen.findByTestId('agent-screen');
    await navigate('#privacy');
    expect(screen.queryByTestId('agent-screen')).toBeNull();
    expect(agentLifecycle.unmounts).toBe(1);
  });

  it('resumes a new chat after settings is opened from it and Back is pressed', async () => {
    window.history.replaceState(null, '', '#chat');
    mockBridge.chatGetHistory.mockResolvedValueOnce([
      { id: 'm1', role: 'user', content: 'Remember me', createdAt: '2026-10-24T00:00:00Z' },
    ]);
    render(<App />);
    const input = await screen.findByLabelText('Message');
    await waitFor(() => expect(screen.getByRole('button', { name: 'Send message' })).toBeDisabled());
    fireEvent.input(input, { target: { value: 'Remember me' } });
    await waitFor(() => expect(screen.getByRole('button', { name: 'Send message' })).not.toBeDisabled());
    fireEvent.click(screen.getByRole('button', { name: 'Send message' }));
    await waitFor(() => expect(window.location.hash).toBe('#chat?sessionId=session-1'));

    await navigate('#byok');
    expect(await screen.findByTestId('aiScreenBYOK')).not.toBeNull();

    await goBack();
    await waitFor(() => expect(mockBridge.chatSessionResume).toHaveBeenCalledWith('session-1', expect.anything()));
    expect(await within(await screen.findByRole('log')).findByText('Remember me')).not.toBeNull();
  });

  it('delegates Back to the native shell after a child screen popped history itself', async () => {
    const onBack = vi.fn();
    window.MahoBridgeAndroid = { onBack };
    const back = vi.spyOn(window.history, 'back');
    try {
      window.history.replaceState(null, '', '#privacy');
      render(<App />);
      await navigate('#onboarding?step=auth&mode=reauth');
      // The child screen (not App) goes back, e.g. after re-auth.
      await goBack();
      back.mockClear();

      await waitFor(() => expect(screen.getByText('Coming Soon')).not.toBeNull());
      fireEvent.click(screen.getByRole('button', { name: 'Back' }));
      expect(onBack).toHaveBeenCalledOnce();
      expect(back).not.toHaveBeenCalled();
    } finally {
      back.mockRestore();
      delete window.MahoBridgeAndroid;
    }
  });
});

describe('App Router - Mobile Chat Launch Routing', () => {
  beforeEach(() => {
    mockBridge.byokGetKey.mockResolvedValue(null);
    mockBridge.chatSessionStart.mockResolvedValue('session-1');
  });

  it('routes #chat to ChatScreen (not ComingSoonScreen)', () => {
    window.location.hash = '#chat';
    render(<App />);
    expect(screen.queryByText('Coming Soon')).toBeNull();
    expect(screen.getByTestId('chat-screen')).not.toBeNull();
    expect(screen.getByPlaceholderText('Type a message…')).not.toBeNull();
  });

  it('routes #byok to AiSettingsScreen (not ComingSoonScreen)', async () => {
    window.location.hash = '#byok';
    render(<App />);
    expect(screen.queryByText('Coming Soon')).toBeNull();
    expect(await screen.findByTestId('aiScreenBYOK')).not.toBeNull();
  });

  it('routes #conversations to ConversationListScreen (not ComingSoonScreen)', async () => {
    window.location.hash = '#conversations';
    render(<App />);
    expect(screen.queryByText('Coming Soon')).toBeNull();
    expect(await screen.findByTestId('conversation-list-screen')).not.toBeNull();
    expect(await screen.findByText('Saved Conversations')).not.toBeNull();
  });

  it('keeps out-of-scope mobile routes (#privacy) as ComingSoonScreen', () => {
    window.location.hash = '#privacy';
    render(<App />);
    expect(screen.getByText('Coming Soon')).not.toBeNull();
    expect(screen.getByText('privacy')).not.toBeNull();
  });

  it('routes #pinch-summary to ComingSoonScreen (not the Unknown screen ErrorScreen)', () => {
    window.location.hash = '#pinch-summary';
    render(<App />);
    expect(screen.queryByText(/Unknown screen/)).toBeNull();
    expect(screen.getByText('Coming Soon')).not.toBeNull();
    expect(screen.getByText('pinch-summary')).not.toBeNull();
  });

  it('traces component render durations via Profiler callback when profiling is active', () => {
    const onRender = vi.fn();
    window.location.hash = '#chat';
    render(<App onRender={onRender} />);
    expect(onRender).toHaveBeenCalledWith(
      'ScreenRouter:chat',
      'mount',
      expect.any(Number),
      expect.any(Number),
      expect.any(Number),
      expect.any(Number)
    );
  });

  it('Profiler component invokes onRender callback with mount phase and non-negative duration', () => {
    const onRender = vi.fn();
    render(
      <Profiler id="test-component" onRender={onRender}>
        <div>Hello Profiler</div>
      </Profiler>
    );
    expect(onRender).toHaveBeenCalledTimes(1);
    expect(onRender).toHaveBeenCalledWith(
      'test-component',
      'mount',
      expect.any(Number),
      expect.any(Number),
      expect.any(Number),
      expect.any(Number)
    );
    const [, , actualDuration] = onRender.mock.calls[0];
    expect(actualDuration).toBeGreaterThanOrEqual(0);
  });
});
