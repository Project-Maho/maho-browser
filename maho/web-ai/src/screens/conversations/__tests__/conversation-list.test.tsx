import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/preact';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { ConversationMeta } from '../../../bridge';

const mockBridge = {
  conversationList: vi.fn<() => Promise<ConversationMeta[]>>(),
  conversationCreate: vi.fn<(meta: ConversationMeta) => Promise<string>>(),
  conversationRename: vi.fn<(id: string, title: string) => Promise<void>>(),
  conversationDelete: vi.fn<(id: string) => Promise<void>>(),
};

vi.mock('../../../hooks/use-bridge', () => ({
  useBridge: () => mockBridge,
}));

import { ConversationListScreen } from '../conversation-list-screen';

function makeConversation(overrides: Partial<ConversationMeta> = {}): ConversationMeta {
  return {
    id: overrides.id ?? 'conversation-1',
    title: overrides.title ?? 'Project brief',
    model: overrides.model ?? 'gpt-4.1',
    createdAt: overrides.createdAt ?? '2026-06-18T10:00:00.000Z',
    updatedAt: overrides.updatedAt ?? '2026-06-19T10:30:00.000Z',
    spaceId: overrides.spaceId,
  };
}

describe('ConversationListScreen', () => {
  beforeEach(() => {
    window.location.hash = '#conversations';
    mockBridge.conversationList.mockResolvedValue([]);
    mockBridge.conversationCreate.mockResolvedValue('new-conversation-id');
    mockBridge.conversationRename.mockResolvedValue(undefined);
    mockBridge.conversationDelete.mockResolvedValue(undefined);
    vi.spyOn(window, 'confirm').mockReturnValue(true);
  });

  afterEach(() => {
    cleanup();
    vi.restoreAllMocks();
  });

  it('renders list of conversations', async () => {
    mockBridge.conversationList.mockResolvedValue([
      makeConversation(),
      makeConversation({
        id: 'conversation-2',
        title: 'Design review',
        model: 'claude-sonnet-4',
        updatedAt: '2026-06-20T09:00:00.000Z',
      }),
    ]);

    render(<ConversationListScreen />);

    await waitFor(() => {
      expect(screen.getByText('Project brief')).toBeInTheDocument();
      expect(screen.getByText('Design review')).toBeInTheDocument();
    });

    expect(screen.getByText('claude-sonnet-4')).toBeInTheDocument();
  });

  it('renders empty state when no conversations exist', async () => {
    render(<ConversationListScreen />);

    await waitFor(() => {
      expect(screen.getByText('No saved conversations yet')).toBeInTheDocument();
    });

    expect(screen.getByRole('button', { name: 'Start a new chat' })).toBeInTheDocument();
  });

  it('navigates to chat on item click', async () => {
    mockBridge.conversationList.mockResolvedValue([makeConversation()]);

    render(<ConversationListScreen />);

    const openButton = await screen.findByRole('button', {
      name: 'Open Project brief conversation',
    });
    fireEvent.click(openButton);

    expect(window.location.hash).toBe('#chat?sessionId=conversation-1');
  });

  it('creates a new conversation and navigates', async () => {
    render(<ConversationListScreen />);

    const createButton = await screen.findByRole('button', { name: '+ New' });
    fireEvent.click(createButton);

    await waitFor(() => {
      expect(mockBridge.conversationCreate).toHaveBeenCalledOnce();
      expect(window.location.hash).toBe('#chat?sessionId=new-conversation-id');
    });

    expect(mockBridge.conversationCreate.mock.calls[0]?.[0]).toMatchObject({
      title: 'New chat',
      model: 'default',
    });
  });

  it('renames a conversation inline', async () => {
    mockBridge.conversationList.mockResolvedValue([
      makeConversation({ id: 'conversation-rename', title: 'Daily notes' }),
    ]);

    render(<ConversationListScreen />);

    const actionsButton = await screen.findByRole('button', {
      name: 'Conversation actions for Daily notes',
    });
    fireEvent.click(actionsButton);
    fireEvent.click(screen.getByRole('menuitem', { name: 'Rename' }));

    const input = screen.getByRole('textbox', { name: 'Conversation title' });
    fireEvent.input(input, { target: { value: 'Daily notes — archived' } });
    fireEvent.submit(input.closest('form')!);

    await waitFor(() => {
      expect(mockBridge.conversationRename).toHaveBeenCalledWith(
        'conversation-rename',
        'Daily notes — archived',
      );
      expect(screen.getByText('Daily notes — archived')).toBeInTheDocument();
    });
  });

  it('deletes after confirmation', async () => {
    mockBridge.conversationList.mockResolvedValue([
      makeConversation({ id: 'conversation-delete', title: 'Temporary thread' }),
    ]);

    render(<ConversationListScreen />);

    const actionsButton = await screen.findByRole('button', {
      name: 'Conversation actions for Temporary thread',
    });
    fireEvent.click(actionsButton);
    fireEvent.click(screen.getByRole('menuitem', { name: 'Delete' }));

    await waitFor(() => {
      expect(window.confirm).toHaveBeenCalledWith('Delete this conversation?');
      expect(mockBridge.conversationDelete).toHaveBeenCalledWith('conversation-delete');
      expect(screen.getByText('No saved conversations yet')).toBeInTheDocument();
    });
  });

  it('shows an error on bridge failure', async () => {
    mockBridge.conversationList.mockRejectedValue(new Error('bridge unavailable'));

    render(<ConversationListScreen />);

    await waitFor(() => {
      expect(screen.getByRole('alert')).toHaveTextContent('bridge unavailable');
    });
  });
});
