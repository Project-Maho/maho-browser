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
  conversationsList: vi.fn().mockResolvedValue([]),
};

vi.mock('../hooks/use-bridge', () => ({
  useBridge: () => mockBridge,
}));

import { App } from '../app';

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

  it('routes #byok to AiSettingsScreen (not ComingSoonScreen)', () => {
    window.location.hash = '#byok';
    render(<App />);
    expect(screen.queryByText('Coming Soon')).toBeNull();
    expect(screen.getByTestId('aiScreenBYOK')).not.toBeNull();
  });

  it('routes #conversations to ConversationListScreen (not ComingSoonScreen)', () => {
    window.location.hash = '#conversations';
    render(<App />);
    expect(screen.queryByText('Coming Soon')).toBeNull();
    expect(screen.getByTestId('conversation-list-screen')).not.toBeNull();
    expect(screen.getByText('Saved Conversations')).not.toBeNull();
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
});
