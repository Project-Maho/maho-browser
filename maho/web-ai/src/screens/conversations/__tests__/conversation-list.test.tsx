import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/preact';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type {
  ComposerDraftScope,
  ConversationBulkResponse,
  ConversationListOptions,
  ConversationMeta,
  ConversationProject,
  ConversationProjectMoveResult,
} from '../../../bridge';

const mockBridge = {
  conversationList: vi.fn<(options?: ConversationListOptions) => Promise<ConversationMeta[]>>(),
  conversationCreate: vi.fn<(meta: ConversationMeta) => Promise<string>>(),
  conversationRename: vi.fn<(id: string, title: string) => Promise<boolean>>(),
  conversationDelete: vi.fn<(id: string) => Promise<boolean>>(),
  conversationArchive: vi.fn<(id: string) => Promise<boolean>>(),
  conversationUnarchive: vi.fn<(id: string) => Promise<boolean>>(),
  conversationBulk: vi.fn<
    (op: 'archive' | 'unarchive' | 'delete', ids: string[]) => Promise<ConversationBulkResponse>
  >(),
  composerDraftDelete: vi.fn<(scope: ComposerDraftScope) => Promise<boolean>>(),
  conversationProjectList: vi.fn<() => Promise<ConversationProject[]>>(),
  conversationProjectCreate: vi.fn<(name: string) => Promise<ConversationProject>>(),
  conversationProjectRename: vi.fn<(id: string, name: string) => Promise<boolean>>(),
  conversationProjectDelete: vi.fn<(id: string) => Promise<boolean>>(),
  conversationProjectMove: vi.fn<(ids: string[], projectId: string | null) => Promise<ConversationProjectMoveResult>>(),
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
    archivedAt: overrides.archivedAt,
    spaceId: overrides.spaceId,
    projectId: overrides.projectId,
  };
}

const bulkResult = (overrides: Partial<Exclude<ConversationBulkResponse, { error: string }>> = {}) => ({
  requestedCount: overrides.requestedCount ?? 1,
  affectedIds: overrides.affectedIds ?? ['conversation-1'],
  unchangedIds: overrides.unchangedIds ?? [],
  missingIds: overrides.missingIds ?? [],
});

describe('ConversationListScreen archive management', () => {
  beforeEach(() => {
    window.location.hash = '#conversations';
    mockBridge.conversationList.mockResolvedValue([]);
    mockBridge.conversationCreate.mockResolvedValue('new-conversation-id');
    mockBridge.conversationRename.mockResolvedValue(true);
    mockBridge.conversationDelete.mockResolvedValue(true);
    mockBridge.conversationArchive.mockResolvedValue(true);
    mockBridge.conversationUnarchive.mockResolvedValue(true);
    mockBridge.conversationBulk.mockResolvedValue(bulkResult());
    mockBridge.composerDraftDelete.mockResolvedValue(true);
    mockBridge.conversationProjectList.mockResolvedValue([]);
    mockBridge.conversationProjectCreate.mockResolvedValue({
      id: 'project-new',
      name: 'New project',
      createdAt: '2026-06-20T09:00:00Z',
      updatedAt: '2026-06-20T09:00:00Z',
    });
    mockBridge.conversationProjectRename.mockResolvedValue(true);
    mockBridge.conversationProjectDelete.mockResolvedValue(true);
    mockBridge.conversationProjectMove.mockResolvedValue({
      requestedCount: 1,
      affectedIds: ['conversation-1'],
      missingIds: [],
    });
    vi.spyOn(window, 'confirm').mockReturnValue(true);
    vi.spyOn(window, 'prompt');
  });

  afterEach(() => {
    cleanup();
    vi.restoreAllMocks();
  });

  it('loads Active by default and switches to Archived with loading feedback', async () => {
    let resolveArchived: ((rows: ConversationMeta[]) => void) | undefined;
    mockBridge.conversationList
      .mockResolvedValueOnce([makeConversation({ title: 'Active chat' })])
      .mockImplementationOnce(() => new Promise((resolve) => { resolveArchived = resolve; }));

    render(<ConversationListScreen />);

    expect(await screen.findByText('Active chat')).toBeInTheDocument();
    expect(mockBridge.conversationList).toHaveBeenNthCalledWith(1, { state: 'active', limit: 100 });

    fireEvent.click(screen.getByRole('tab', { name: 'Archived' }));
    expect(screen.getByText('Loading archived conversations…')).toBeInTheDocument();

    resolveArchived?.([
      makeConversation({ title: 'Archived chat', archivedAt: '2026-06-20T09:00:00.000Z' }),
    ]);
    expect(await screen.findByText('Archived chat')).toBeInTheDocument();
    expect(mockBridge.conversationList).toHaveBeenNthCalledWith(2, { state: 'archived', limit: 100 });
    expect(screen.getByText(/Archived Jun/)).toBeInTheDocument();
  });

  it('ignores an older filter response that settles after the selected filter', async () => {
    let resolveActive: ((rows: ConversationMeta[]) => void) | undefined;
    mockBridge.conversationList
      .mockImplementationOnce(() => new Promise((resolve) => { resolveActive = resolve; }))
      .mockResolvedValueOnce([
        makeConversation({ title: 'Archived chat', archivedAt: '2026-06-20T09:00:00.000Z' }),
      ]);

    render(<ConversationListScreen />);
    fireEvent.click(screen.getByRole('tab', { name: 'Archived' }));

    expect(await screen.findByText('Archived chat')).toBeInTheDocument();
    resolveActive?.([makeConversation({ title: 'Stale active chat' })]);

    await waitFor(() => {
      expect(screen.queryByText('Stale active chat')).not.toBeInTheDocument();
    });
    expect(screen.getByText('Archived chat')).toBeInTheDocument();
  });

  it('shows a filter-specific load error and retries the current filter', async () => {
    mockBridge.conversationList
      .mockResolvedValueOnce([])
      .mockRejectedValueOnce(new Error('archive store unavailable'))
      .mockResolvedValueOnce([makeConversation({ title: 'Recovered', archivedAt: '2026-06-20T09:00:00Z' })]);

    render(<ConversationListScreen />);
    await screen.findByText('No active conversations');
    fireEvent.click(screen.getByRole('tab', { name: 'Archived' }));

    expect(await screen.findByRole('alert')).toHaveTextContent('archive store unavailable');
    fireEvent.click(screen.getByRole('button', { name: 'Retry loading archived conversations' }));
    expect(await screen.findByText('Recovered')).toBeInTheDocument();
  });

  it('offers Archive but not Delete for an active row, then moves it out of view', async () => {
    mockBridge.conversationList.mockResolvedValue([
      makeConversation({ id: 'active-1', title: 'Active notes' }),
    ]);

    render(<ConversationListScreen />);
    fireEvent.click(await screen.findByRole('button', { name: 'Conversation actions for Active notes' }));

    expect(screen.getByRole('menuitem', { name: 'Archive' })).toBeInTheDocument();
    expect(screen.queryByRole('menuitem', { name: 'Delete' })).not.toBeInTheDocument();
    fireEvent.click(screen.getByRole('menuitem', { name: 'Archive' }));

    await waitFor(() => {
      expect(mockBridge.conversationArchive).toHaveBeenCalledWith('active-1');
      expect(screen.queryByText('Active notes')).not.toBeInTheDocument();
    });
  });

  it('offers Unarchive and Delete only after switching to Archived', async () => {
    mockBridge.conversationList
      .mockResolvedValueOnce([])
      .mockResolvedValueOnce([
        makeConversation({ id: 'archived-1', title: 'Old notes', archivedAt: '2026-06-20T09:00:00Z' }),
      ]);

    render(<ConversationListScreen />);
    await screen.findByText('No active conversations');
    expect(screen.queryByText('Delete')).not.toBeInTheDocument();

    fireEvent.click(screen.getByRole('tab', { name: 'Archived' }));
    fireEvent.click(await screen.findByRole('button', { name: 'Conversation actions for Old notes' }));
    expect(screen.getByRole('menuitem', { name: 'Unarchive' })).toBeInTheDocument();
    expect(screen.getByRole('menuitem', { name: 'Delete' })).toBeInTheDocument();
  });

  it('enters multi-select, selects all in view, and archives with one bulk RPC', async () => {
    mockBridge.conversationList.mockResolvedValue([
      makeConversation({ id: 'a', title: 'Alpha' }),
      makeConversation({ id: 'b', title: 'Beta' }),
      makeConversation({ id: 'c', title: 'Gamma' }),
    ]);
    mockBridge.conversationBulk.mockResolvedValue(bulkResult({
      requestedCount: 3,
      affectedIds: ['a', 'b', 'c'],
    }));

    render(<ConversationListScreen />);
    await screen.findByText('Alpha');
    fireEvent.click(screen.getByRole('button', { name: 'Select conversations' }));
    fireEvent.click(screen.getByRole('checkbox', { name: 'Select all active conversations' }));
    fireEvent.click(screen.getByRole('button', { name: 'Archive 3 conversations' }));

    await waitFor(() => {
      expect(mockBridge.conversationBulk).toHaveBeenCalledTimes(1);
      expect(mockBridge.conversationBulk).toHaveBeenCalledWith('archive', ['a', 'b', 'c']);
      expect(screen.getByText('No active conversations')).toBeInTheDocument();
    });
  });

  it('renders affected, unchanged, and missing bulk outcomes', async () => {
    mockBridge.conversationList.mockResolvedValue([
      makeConversation({ id: 'a', title: 'Alpha' }),
      makeConversation({ id: 'b', title: 'Beta' }),
      makeConversation({ id: 'c', title: 'Gamma' }),
    ]);
    mockBridge.conversationBulk.mockResolvedValue({
      requestedCount: 3,
      affectedIds: ['a'],
      unchangedIds: ['b'],
      missingIds: ['c'],
    });

    render(<ConversationListScreen />);
    await screen.findByText('Alpha');
    fireEvent.click(screen.getByRole('button', { name: 'Select conversations' }));
    for (const title of ['Alpha', 'Beta', 'Gamma']) {
      fireEvent.click(screen.getByRole('checkbox', { name: `Select ${title}` }));
    }
    fireEvent.click(screen.getByRole('button', { name: 'Archive 3 conversations' }));

    expect(await screen.findByRole('status')).toHaveTextContent(
      'Archived 1 conversation. 1 was already in that state. 1 could not be found.',
    );
    expect(screen.queryByText('Alpha')).not.toBeInTheDocument();
    expect(screen.getByText('Beta')).toBeInTheDocument();
  });

  it('shows active-session blocked results without applying a partial delete', async () => {
    mockBridge.conversationList
      .mockResolvedValueOnce([])
      .mockResolvedValueOnce([
        makeConversation({ id: 'streaming', title: 'Live run', archivedAt: '2026-06-20T09:00:00Z' }),
        makeConversation({ id: 'idle', title: 'Idle run', archivedAt: '2026-06-19T09:00:00Z' }),
      ]);
    mockBridge.conversationBulk.mockResolvedValue({ error: 'active_sessions', ids: ['streaming'] });

    render(<ConversationListScreen />);
    await screen.findByText('No active conversations');
    fireEvent.click(screen.getByRole('tab', { name: 'Archived' }));
    await screen.findByText('Live run');
    fireEvent.click(screen.getByRole('button', { name: 'Select conversations' }));
    fireEvent.click(screen.getByRole('checkbox', { name: 'Select all archived conversations' }));
    fireEvent.click(screen.getByRole('button', { name: 'Delete 2 conversations' }));

    expect(window.confirm).toHaveBeenCalledWith('Delete 2 archived conversations?');
    expect(await screen.findByRole('alert')).toHaveTextContent(
      'Cannot delete while an active session is using: Live run.',
    );
    expect(screen.getByText('Live run')).toBeInTheDocument();
    expect(screen.getByText('Idle run')).toBeInTheDocument();
    expect(mockBridge.composerDraftDelete).not.toHaveBeenCalled();
  });

  it('purges composer drafts only for successfully bulk-deleted archived conversations', async () => {
    mockBridge.conversationList
      .mockResolvedValueOnce([])
      .mockResolvedValueOnce([
        makeConversation({ id: 'deleted', title: 'Delete me', archivedAt: '2026-06-20T09:00:00Z' }),
        makeConversation({ id: 'missing', title: 'Gone elsewhere', archivedAt: '2026-06-19T09:00:00Z' }),
      ]);
    mockBridge.conversationBulk.mockResolvedValue({
      requestedCount: 2,
      affectedIds: ['deleted'],
      unchangedIds: [],
      missingIds: ['missing'],
    });

    render(<ConversationListScreen />);
    await screen.findByText('No active conversations');
    fireEvent.click(screen.getByRole('tab', { name: 'Archived' }));
    await screen.findByText('Delete me');
    fireEvent.click(screen.getByRole('button', { name: 'Select conversations' }));
    fireEvent.click(screen.getByRole('checkbox', { name: 'Select all archived conversations' }));
    fireEvent.click(screen.getByRole('button', { name: 'Delete 2 conversations' }));

    await waitFor(() => {
      expect(mockBridge.conversationBulk).toHaveBeenCalledWith('delete', ['deleted', 'missing']);
      expect(mockBridge.composerDraftDelete).toHaveBeenCalledTimes(1);
      expect(mockBridge.composerDraftDelete).toHaveBeenCalledWith({
        kind: 'conversation',
        conversationId: 'deleted',
      });
    });
  });

  it('creates, renames, and deletes projects while reloading grouped conversations', async () => {
    mockBridge.conversationProjectList
      .mockResolvedValueOnce([])
      .mockResolvedValueOnce([{ id: 'project-new', name: 'Research', createdAt: '2026-06-20T09:00:00Z', updatedAt: '2026-06-20T09:00:00Z' }])
      .mockResolvedValueOnce([{ id: 'project-new', name: 'Renamed research', createdAt: '2026-06-20T09:00:00Z', updatedAt: '2026-06-20T10:00:00Z' }])
      .mockResolvedValueOnce([]);
    mockBridge.conversationList
      .mockResolvedValueOnce([])
      .mockResolvedValueOnce([makeConversation({ id: 'grouped', title: 'Grouped chat', projectId: 'project-new' })])
      .mockResolvedValueOnce([makeConversation({ id: 'grouped', title: 'Grouped chat', projectId: 'project-new' })])
      .mockResolvedValueOnce([makeConversation({ id: 'grouped', title: 'Grouped chat', projectId: null })]);
    vi.mocked(window.prompt)
      .mockReturnValueOnce('Research')
      .mockReturnValueOnce('Renamed research');

    render(<ConversationListScreen />);
    await screen.findByText('No active conversations');
    fireEvent.click(screen.getByRole('button', { name: 'Create project' }));

    expect(await screen.findByRole('heading', { name: 'Research' })).toBeInTheDocument();
    expect(mockBridge.conversationProjectCreate).toHaveBeenCalledWith('Research');
    expect(screen.getByText('Grouped chat')).toBeInTheDocument();

    fireEvent.click(screen.getByRole('button', { name: 'Project actions for Research' }));
    fireEvent.click(screen.getByRole('menuitem', { name: 'Rename project' }));
    expect(await screen.findByRole('heading', { name: 'Renamed research' })).toBeInTheDocument();
    expect(mockBridge.conversationProjectRename).toHaveBeenCalledWith('project-new', 'Renamed research');

    fireEvent.click(screen.getByRole('button', { name: 'Project actions for Renamed research' }));
    fireEvent.click(screen.getByRole('menuitem', { name: 'Delete project' }));
    await waitFor(() => expect(mockBridge.conversationProjectDelete).toHaveBeenCalledWith('project-new'));
    expect(await screen.findByRole('heading', { name: 'No project' })).toBeInTheDocument();
  });

  it('moves one conversation and bulk selection into projects', async () => {
    mockBridge.conversationProjectList.mockResolvedValue([
      { id: 'project-a', name: 'Alpha project', createdAt: '2026-06-20T09:00:00Z', updatedAt: '2026-06-20T09:00:00Z' },
    ]);
    mockBridge.conversationList.mockResolvedValue([
      makeConversation({ id: 'a', title: 'Alpha', projectId: null }),
      makeConversation({ id: 'b', title: 'Beta', projectId: null }),
    ]);
    mockBridge.conversationProjectMove
      .mockResolvedValueOnce({ requestedCount: 1, affectedIds: ['a'], missingIds: [] })
      .mockResolvedValueOnce({ requestedCount: 1, affectedIds: ['b'], missingIds: [] });

    render(<ConversationListScreen />);
    await screen.findByText('Alpha');
    fireEvent.click(screen.getByRole('button', { name: 'Conversation actions for Alpha' }));
    fireEvent.click(screen.getByRole('menuitem', { name: 'Move to Alpha project' }));
    await waitFor(() => expect(mockBridge.conversationProjectMove).toHaveBeenCalledWith(['a'], 'project-a'));

    fireEvent.click(screen.getByRole('button', { name: 'Select conversations' }));
    fireEvent.click(screen.getByRole('checkbox', { name: 'Select Beta' }));
    fireEvent.click(screen.getByRole('button', { name: 'Move 1 conversation' }));
    fireEvent.click(screen.getByRole('menuitem', { name: 'Move to Alpha project' }));
    await waitFor(() => expect(mockBridge.conversationProjectMove).toHaveBeenCalledWith(['b'], 'project-a'));
  });

  it('keeps conversations visible and reports failure when their project was deleted elsewhere', async () => {
    mockBridge.conversationProjectList.mockResolvedValue([
      { id: 'deleted-project', name: 'Deleted elsewhere', createdAt: '2026-06-20T09:00:00Z', updatedAt: '2026-06-20T09:00:00Z' },
    ]);
    mockBridge.conversationList.mockResolvedValue([
      makeConversation({ id: 'orphan', title: 'Keep me', projectId: 'deleted-project' }),
    ]);
    mockBridge.conversationProjectMove.mockRejectedValue(new Error('Project no longer exists. Reloaded projects.'));

    render(<ConversationListScreen />);
    await screen.findByText('Keep me');
    fireEvent.click(screen.getByRole('button', { name: 'Conversation actions for Keep me' }));
    fireEvent.click(screen.getByRole('menuitem', { name: 'Remove from project' }));

    expect(await screen.findByRole('alert')).toHaveTextContent('Project no longer exists. Reloaded projects.');
    expect(screen.getByText('Keep me')).toBeInTheDocument();
  });

  it('keeps rename and navigation behavior for active conversations', async () => {
    mockBridge.conversationList.mockResolvedValue([
      makeConversation({ id: 'conversation-rename', title: 'Daily notes' }),
    ]);

    render(<ConversationListScreen />);
    const row = await screen.findByRole('listitem');
    fireEvent.click(within(row).getByRole('button', { name: 'Conversation actions for Daily notes' }));
    fireEvent.click(screen.getByRole('menuitem', { name: 'Rename' }));
    const input = screen.getByRole('textbox', { name: 'Conversation title' });
    fireEvent.input(input, { target: { value: 'Renamed notes' } });
    fireEvent.submit(input.closest('form')!);

    expect(await screen.findByText('Renamed notes')).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: 'Open Renamed notes conversation' }));
    expect(window.location.hash).toBe('#chat?sessionId=conversation-rename');
  });
});
