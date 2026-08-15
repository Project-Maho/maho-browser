import { fireEvent, render, screen } from '@testing-library/preact';
import { describe, expect, it, vi } from 'vitest';
import type { MessageItem } from '../../../store/chatStore';
import { MessageList } from '../MessageList';

describe('MessageList', () => {
  it('renders user and assistant messages with markdown', () => {
    const messages: MessageItem[] = [
      {
        kind: 'chat',
        id: 'u1',
        role: 'user',
        content: { kind: 'text', text: 'Hello AI' },
      },
      {
        kind: 'chat',
        id: 'a1',
        role: 'assistant',
        content: '**Bold answer** with `code`',
        images: [],
        isStreaming: false,
      },
    ];

    const { container } = render(<MessageList messages={messages} />);

    expect(screen.getByText('Hello AI')).toBeTruthy();
    expect(screen.getByText('Bold answer')).toBeTruthy();
    expect(container.querySelector('strong')?.textContent).toBe('Bold answer');
    expect(container.querySelector('code')?.textContent).toBe('code');
  });

  it('renders tool request and tool result cards', () => {
    const onApproveTool = vi.fn();
    const messages: MessageItem[] = [
      {
        kind: 'tool-request',
        id: 'tr1',
        toolCallId: 'call-1',
        toolName: 'open_tab',
        rawArguments: '{"url":"https://example.com"}',
        argsText: '{"url": "https://example.com"}',
        autoRun: false,
        status: 'pending',
      },
      {
        kind: 'tool-result',
        id: 'tres1',
        toolCallId: 'call-0',
        toolName: 'search_history',
        result: { count: 3 },
      },
    ];

    render(
      <MessageList
        messages={messages}
        onApproveTool={onApproveTool}
      />,
    );

    expect(screen.getByText('open_tab')).toBeTruthy();
    expect(screen.getByText('search_history')).toBeTruthy();

    fireEvent.click(screen.getByRole('button', { name: 'Approve open_tab' }));
    expect(onApproveTool).toHaveBeenCalledWith(
      'call-1',
      'open_tab',
      '{"url":"https://example.com"}',
      true,
    );

    fireEvent.click(screen.getByRole('button', { name: 'Deny open_tab' }));
    expect(onApproveTool).toHaveBeenCalledWith(
      'call-1',
      'open_tab',
      '{"url":"https://example.com"}',
      false,
    );
  });

  it('renders thinking bubble and expands on toggle', () => {
    const messages: MessageItem[] = [
      {
        kind: 'thinking',
        id: 'th1',
        thinking: 'Deep internal reasoning',
        isStreaming: false,
      },
    ];

    render(<MessageList messages={messages} />);

    const button = screen.getByRole('button', { name: /Thought/ });
    expect(button).toBeTruthy();
    expect(screen.queryByText('Deep internal reasoning')).toBeNull();

    fireEvent.click(button);
    expect(screen.getByText('Deep internal reasoning')).toBeTruthy();
  });

  it('renders credential error card when error code is provided', () => {
    render(
      <MessageList
        messages={[]}
        credentialErrorCode="secure_store_unavailable"
      />,
    );

    expect(screen.getByTestId('credential-error-card')).toBeTruthy();
    expect(screen.getByText('AI credentials are unavailable')).toBeTruthy();
  });

  it('virtualizes long message histories with top/bottom spacers and windowing', () => {
    // Generate 100 messages to exceed threshold of 30
    const messages: MessageItem[] = Array.from({ length: 100 }, (_, i) => ({
      kind: 'chat',
      id: `msg-${i}`,
      role: i % 2 === 0 ? 'user' : 'assistant',
      content: i % 2 === 0 ? { kind: 'text', text: `Question ${i}` } : `Answer ${i}`,
      images: [],
      isStreaming: false,
    } as MessageItem));

    const { container } = render(
      <MessageList
        messages={messages}
        virtualizeThreshold={30}
        estimatedItemHeight={50}
        overscan={5}
      />,
    );

    // Should only render a subset of DOM nodes plus spacers
    const renderedBubbles = container.querySelectorAll('.chat-row');
    expect(renderedBubbles.length).toBeLessThan(100);
    expect(renderedBubbles.length).toBeGreaterThan(0);

    // Initial scroll position (0) will render start window and have a bottom spacer
    const bottomSpacer = container.querySelector('.chat-virtual-spacer');
    expect(bottomSpacer).not.toBeNull();
  });
});
