import { useCallback, useEffect, useReducer, useRef } from 'preact/hooks';
import type {
  ConversationBulkResult,
  ConversationListState,
  ConversationMeta,
  ConversationProject,
  MahoBridge,
} from '../../bridge';
import { useBridge } from '../../hooks/use-bridge';
import { Icon } from '../../ui/icon';
import './conversations.css';

const LONG_PRESS_MS = 450;
const DATE_FORMATTER = new Intl.DateTimeFormat(undefined, { month: 'short', day: 'numeric' });

type VisibleState = Exclude<ConversationListState, 'all'>;
type BulkOperation = 'archive' | 'unarchive' | 'delete';

interface ConversationListScreenProps {
  onBack?: () => void;
  bridge?: MahoBridge;
  onNavigateToChat?: (sessionId: string) => void;
}

interface State {
  filter: VisibleState;
  loading: boolean;
  creating: boolean;
  renaming: boolean;
  operating: boolean;
  conversations: ConversationMeta[];
  projects: ConversationProject[];
  errorMessage: string | null;
  noticeMessage: string | null;
  menuId: string | null;
  projectMenuId: string | null;
  moveMenuIds: string[] | null;
  renamingId: string | null;
  renameDraft: string;
  selectionMode: boolean;
  selectedIds: string[];
  suppressNavigateId: string | null;
}

type Action =
  | { type: 'FILTER_CHANGE'; filter: VisibleState }
  | { type: 'LOAD_START' }
  | { type: 'LOAD_DONE'; conversations: ConversationMeta[]; projects: ConversationProject[] }
  | { type: 'LOAD_ERROR'; message: string }
  | { type: 'PROJECT_MENU'; id: string | null }
  | { type: 'MOVE_MENU'; ids: string[] | null }
  | { type: 'PROJECT_MOVE_DONE'; ids: string[]; projectId: string | null }
  | { type: 'PROJECTS_REPLACED'; projects: ConversationProject[]; conversations?: ConversationMeta[] }
  | { type: 'CREATE_START' }
  | { type: 'CREATE_DONE' }
  | { type: 'CREATE_ERROR'; message: string }
  | { type: 'OPEN_MENU'; id: string; suppressNavigate?: boolean }
  | { type: 'CLOSE_MENU' }
  | { type: 'START_RENAME'; id: string; title: string }
  | { type: 'UPDATE_RENAME_DRAFT'; title: string }
  | { type: 'CANCEL_RENAME' }
  | { type: 'RENAME_START' }
  | { type: 'RENAME_DONE'; id: string; title: string }
  | { type: 'RENAME_ERROR'; message: string }
  | { type: 'OPERATION_START' }
  | { type: 'OPERATION_DONE'; result: ConversationBulkResult; operation: BulkOperation }
  | { type: 'OPERATION_ERROR'; message: string }
  | { type: 'TOGGLE_SELECTION_MODE' }
  | { type: 'TOGGLE_SELECTED'; id: string }
  | { type: 'SELECT_ALL' }
  | { type: 'CLEAR_SUPPRESSED_NAVIGATION'; id: string };

function initialState(): State {
  return {
    filter: 'active',
    loading: true,
    creating: false,
    renaming: false,
    operating: false,
    conversations: [],
    projects: [],
    errorMessage: null,
    noticeMessage: null,
    menuId: null,
    projectMenuId: null,
    moveMenuIds: null,
    renamingId: null,
    renameDraft: '',
    selectionMode: false,
    selectedIds: [],
    suppressNavigateId: null,
  };
}

function reducer(state: State, action: Action): State {
  switch (action.type) {
    case 'FILTER_CHANGE':
      return { ...state, filter: action.filter, loading: true, conversations: [], selectedIds: [], selectionMode: false, menuId: null, projectMenuId: null, moveMenuIds: null, errorMessage: null, noticeMessage: null };
    case 'LOAD_START':
      return { ...state, loading: true, errorMessage: null };
    case 'LOAD_DONE':
      return { ...state, loading: false, conversations: sortConversations(action.conversations), projects: action.projects, errorMessage: null };
    case 'PROJECT_MENU':
      return { ...state, projectMenuId: action.id, menuId: null, moveMenuIds: null };
    case 'MOVE_MENU':
      return { ...state, moveMenuIds: action.ids, menuId: null, projectMenuId: null };
    case 'PROJECT_MOVE_DONE': {
      const moved = new Set(action.ids);
      return {
        ...state,
        conversations: state.conversations.map((conversation) => moved.has(conversation.id) ? { ...conversation, projectId: action.projectId } : conversation),
        selectedIds: [],
        selectionMode: false,
        moveMenuIds: null,
        noticeMessage: `Moved ${action.ids.length} conversation${action.ids.length === 1 ? '' : 's'}.`,
      };
    }
    case 'PROJECTS_REPLACED':
      return { ...state, projects: action.projects, conversations: action.conversations ?? state.conversations, projectMenuId: null, moveMenuIds: null };
    case 'LOAD_ERROR':
      return { ...state, loading: false, conversations: [], errorMessage: action.message };
    case 'CREATE_START':
      return { ...state, creating: true, errorMessage: null };
    case 'CREATE_DONE':
      return { ...state, creating: false };
    case 'CREATE_ERROR':
      return { ...state, creating: false, errorMessage: action.message };
    case 'OPEN_MENU':
      return { ...state, menuId: action.id, errorMessage: null, suppressNavigateId: action.suppressNavigate ? action.id : state.suppressNavigateId };
    case 'CLOSE_MENU':
      return { ...state, menuId: null };
    case 'START_RENAME':
      return { ...state, menuId: null, renamingId: action.id, renameDraft: action.title, renaming: false, errorMessage: null };
    case 'UPDATE_RENAME_DRAFT':
      return { ...state, renameDraft: action.title, errorMessage: null };
    case 'CANCEL_RENAME':
      return { ...state, renamingId: null, renameDraft: '', renaming: false };
    case 'RENAME_START':
      return { ...state, renaming: true, errorMessage: null };
    case 'RENAME_ERROR':
      return { ...state, renaming: false, errorMessage: action.message };
    case 'RENAME_DONE':
      return {
        ...state,
        renaming: false,
        renamingId: null,
        renameDraft: '',
        conversations: sortConversations(state.conversations.map((conversation) =>
          conversation.id === action.id
            ? { ...conversation, title: action.title, updatedAt: new Date().toISOString() }
            : conversation)),
      };
    case 'OPERATION_START':
      return { ...state, operating: true, menuId: null, errorMessage: null, noticeMessage: null };
    case 'OPERATION_DONE': {
      const removed = new Set(action.result.affectedIds);
      return {
        ...state,
        operating: false,
        conversations: state.conversations.filter((conversation) => !removed.has(conversation.id)),
        selectedIds: state.selectedIds.filter((id) => !removed.has(id)),
        selectionMode: action.result.unchangedIds.length + action.result.missingIds.length > 0,
        noticeMessage: bulkNotice(action.operation, action.result),
      };
    }
    case 'OPERATION_ERROR':
      return { ...state, operating: false, errorMessage: action.message };
    case 'TOGGLE_SELECTION_MODE':
      return { ...state, selectionMode: !state.selectionMode, selectedIds: [], menuId: null, noticeMessage: null };
    case 'TOGGLE_SELECTED':
      return {
        ...state,
        selectedIds: state.selectedIds.includes(action.id)
          ? state.selectedIds.filter((id) => id !== action.id)
          : [...state.selectedIds, action.id],
      };
    case 'SELECT_ALL':
      return {
        ...state,
        selectedIds: state.selectedIds.length === state.conversations.length
          ? []
          : state.conversations.map((conversation) => conversation.id),
      };
    case 'CLEAR_SUPPRESSED_NAVIGATION':
      return state.suppressNavigateId === action.id ? { ...state, suppressNavigateId: null } : state;
    default:
      return state;
  }
}

export function ConversationListScreen({
  onBack,
  bridge: bridgeProp,
  onNavigateToChat = navigateToChatHash,
}: ConversationListScreenProps) {
  const bridgeFromHook = useBridge();
  const bridge = bridgeProp ?? bridgeFromHook;
  const [state, dispatch] = useReducer(reducer, undefined, initialState);
  const longPressTimerRef = useRef<number | null>(null);
  const renameInputRef = useRef<HTMLInputElement | null>(null);
  const loadGenerationRef = useRef(0);

  const clearLongPress = useCallback(() => {
    if (longPressTimerRef.current !== null) {
      window.clearTimeout(longPressTimerRef.current);
      longPressTimerRef.current = null;
    }
  }, []);

  const loadConversations = useCallback(async () => {
    const generation = ++loadGenerationRef.current;
    dispatch({ type: 'LOAD_START' });
    try {
      const [conversations, projects] = await Promise.all([
        bridge.conversationList({ state: state.filter, limit: 100 }),
        bridge.conversationProjectList(),
      ]);
      if (generation !== loadGenerationRef.current) return;
      dispatch({ type: 'LOAD_DONE', conversations, projects });
    } catch (error) {
      if (generation !== loadGenerationRef.current) return;
      dispatch({ type: 'LOAD_ERROR', message: toErrorMessage(error, `Unable to load ${state.filter} conversations.`) });
    }
  }, [bridge, state.filter]);

  useEffect(() => { void loadConversations(); }, [loadConversations]);
  useEffect(() => clearLongPress, [clearLongPress]);
  useEffect(() => {
    if (state.renamingId) {
      renameInputRef.current?.focus();
      renameInputRef.current?.select();
    }
  }, [state.renamingId]);

  useEffect(() => {
    if (!state.menuId) return undefined;
    const handlePointerDown = (event: globalThis.MouseEvent) => {
      const target = event.target;
      if (target instanceof HTMLElement && target.closest('[data-conversation-menu-root="true"]')) return;
      dispatch({ type: 'CLOSE_MENU' });
    };
    document.addEventListener('mousedown', handlePointerDown);
    return () => document.removeEventListener('mousedown', handlePointerDown);
  }, [state.menuId]);

  const createConversation = useCallback(async () => {
    dispatch({ type: 'CREATE_START' });
    const meta = buildConversationMeta();
    try {
      const sessionId = await bridge.conversationCreate(meta);
      dispatch({ type: 'CREATE_DONE' });
      onNavigateToChat(sessionId);
    } catch (error) {
      dispatch({ type: 'CREATE_ERROR', message: toErrorMessage(error, 'Unable to start a new conversation right now.') });
    }
  }, [bridge, onNavigateToChat]);

  const reloadProjectData = useCallback(async () => {
    const [conversations, projects] = await Promise.all([
      bridge.conversationList({ state: state.filter, limit: 100 }),
      bridge.conversationProjectList(),
    ]);
    dispatch({ type: 'PROJECTS_REPLACED', projects, conversations: sortConversations(conversations) });
  }, [bridge, state.filter]);

  const createProject = useCallback(async () => {
    const name = window.prompt('Project name')?.trim();
    if (!name) return;
    try {
      await bridge.conversationProjectCreate(name);
      await reloadProjectData();
    } catch (error) {
      dispatch({ type: 'OPERATION_ERROR', message: toErrorMessage(error, 'Unable to create this project.') });
    }
  }, [bridge, reloadProjectData]);

  const renameProject = useCallback(async (project: ConversationProject) => {
    const name = window.prompt('Project name', project.name)?.trim();
    if (!name || name === project.name) return;
    try {
      const ok = await bridge.conversationProjectRename(project.id, name);
      if (!ok) throw new Error('Unable to rename this project.');
      await reloadProjectData();
    } catch (error) {
      dispatch({ type: 'OPERATION_ERROR', message: toErrorMessage(error, 'Unable to rename this project.') });
    }
  }, [bridge, reloadProjectData]);

  const deleteProject = useCallback(async (project: ConversationProject) => {
    if (!window.confirm(`Delete project “${project.name}”? Conversations will remain in No project.`)) return;
    try {
      const ok = await bridge.conversationProjectDelete(project.id);
      if (!ok) throw new Error('Unable to delete this project.');
      await reloadProjectData();
    } catch (error) {
      dispatch({ type: 'OPERATION_ERROR', message: toErrorMessage(error, 'Unable to delete this project.') });
    }
  }, [bridge, reloadProjectData]);

  const moveConversations = useCallback(async (ids: string[], projectId: string | null) => {
    try {
      const result = await bridge.conversationProjectMove(ids, projectId);
      if (result.missingIds.length > 0) {
        throw new Error(`${result.missingIds.length} conversation${result.missingIds.length === 1 ? '' : 's'} could not be found.`);
      }
      dispatch({ type: 'PROJECT_MOVE_DONE', ids: result.affectedIds, projectId });
    } catch (error) {
      try {
        const projects = await bridge.conversationProjectList();
        dispatch({ type: 'PROJECTS_REPLACED', projects });
      } catch { /* Preserve the original mutation error. */ }
      dispatch({ type: 'OPERATION_ERROR', message: toErrorMessage(error, 'Unable to move the selected conversations.') });
    }
  }, [bridge]);

  const submitRename = useCallback(async (conversationId: string, currentTitle: string) => {
    const nextTitle = state.renameDraft.trim();
    if (!nextTitle) {
      dispatch({ type: 'OPERATION_ERROR', message: 'Conversation title cannot be empty.' });
      return;
    }
    if (nextTitle === currentTitle.trim()) {
      dispatch({ type: 'CANCEL_RENAME' });
      return;
    }
    dispatch({ type: 'RENAME_START' });
    try {
      const ok = await bridge.conversationRename(conversationId, nextTitle);
      if (!ok) throw new Error('Unable to rename this conversation.');
      dispatch({ type: 'RENAME_DONE', id: conversationId, title: nextTitle });
    } catch (error) {
      dispatch({ type: 'RENAME_ERROR', message: toErrorMessage(error, 'Unable to rename this conversation.') });
    }
  }, [bridge, state.renameDraft]);

  const runOperation = useCallback(async (operation: BulkOperation, ids: string[]) => {
    if (ids.length === 0) return;
    if (operation === 'delete' && !window.confirm(`Delete ${ids.length} archived conversation${ids.length === 1 ? '' : 's'}?`)) {
      dispatch({ type: 'CLOSE_MENU' });
      return;
    }
    dispatch({ type: 'OPERATION_START' });
    try {
      const response = ids.length === 1 && operation !== 'delete'
        ? await runSingleStateOperation(bridge, operation, ids[0]!)
        : await bridge.conversationBulk(operation, ids);
      if ('error' in response) {
        const names = response.ids.map((id) => displayTitle(state.conversations.find((item) => item.id === id) ?? { id, title: id, createdAt: '', updatedAt: '' }));
        dispatch({ type: 'OPERATION_ERROR', message: `Cannot delete while an active session is using: ${names.join(', ')}.` });
        return;
      }
      if (operation === 'delete' && bridge.composerDraftDelete) {
        await Promise.allSettled(response.affectedIds.map((conversationId) =>
          bridge.composerDraftDelete?.({ kind: 'conversation', conversationId })));
      }
      dispatch({ type: 'OPERATION_DONE', result: response, operation });
    } catch (error) {
      dispatch({ type: 'OPERATION_ERROR', message: toErrorMessage(error, `Unable to ${operation} the selected conversations.`) });
    }
  }, [bridge, state.conversations]);

  const handleNavigate = useCallback((conversationId: string) => {
    if (state.selectionMode) {
      dispatch({ type: 'TOGGLE_SELECTED', id: conversationId });
      return;
    }
    if (state.suppressNavigateId === conversationId) {
      dispatch({ type: 'CLEAR_SUPPRESSED_NAVIGATION', id: conversationId });
      return;
    }
    onNavigateToChat(conversationId);
  }, [onNavigateToChat, state.selectionMode, state.suppressNavigateId]);

  const scheduleLongPressMenu = useCallback((conversationId: string) => {
    if (state.selectionMode) return;
    clearLongPress();
    longPressTimerRef.current = window.setTimeout(() => {
      dispatch({ type: 'OPEN_MENU', id: conversationId, suppressNavigate: true });
      longPressTimerRef.current = null;
    }, LONG_PRESS_MS);
  }, [clearLongPress, state.selectionMode]);

  const selectedCount = state.selectedIds.length;
  const allSelected = state.conversations.length > 0 && selectedCount === state.conversations.length;
  const bulkOperation: BulkOperation = state.filter === 'active' ? 'archive' : 'unarchive';

  return (
    <div class="conversation-list-screen" data-testid="conversation-list-screen">
      <header class="cl-header">
        <div class="cl-header-main">
          {onBack && <button type="button" class="cl-back-button" onClick={onBack} aria-label="Back"><Icon name="chevron-left" size={18} aria-hidden /></button>}
          <div><p class="cl-eyebrow">History</p><h1 class="cl-title-heading">Saved Conversations</h1><p class="cl-subtitle">Keep active work close and archive finished chats.</p></div>
        </div>
        <div class="cl-header-actions">
          <button type="button" class="cl-secondary-button" onClick={() => void createProject()} aria-label="Create project">Project +</button>
          <button type="button" class="cl-secondary-button" onClick={() => dispatch({ type: 'TOGGLE_SELECTION_MODE' })} disabled={state.loading || state.conversations.length === 0} aria-label={state.selectionMode ? 'Done selecting conversations' : 'Select conversations'}>{state.selectionMode ? 'Done' : 'Select'}</button>
          <button type="button" class="cl-primary-button" onClick={() => void createConversation()} disabled={state.creating}>{state.creating ? 'Creating…' : '+ New'}</button>
        </div>
      </header>

      <main class="cl-content">
        <div class="cl-filter-row">
          <div class="cl-tabs" role="tablist" aria-label="Conversation state">
            {(['active', 'archived'] as const).map((filter) => (
              <button key={filter} type="button" role="tab" aria-selected={state.filter === filter} class="cl-tab" onClick={() => state.filter !== filter && dispatch({ type: 'FILTER_CHANGE', filter })}>{filter === 'active' ? 'Active' : 'Archived'}</button>
            ))}
          </div>
        </div>

        {state.selectionMode && state.conversations.length > 0 && (
          <div class="cl-selection-toolbar">
            <label class="cl-select-all"><input type="checkbox" checked={allSelected} onChange={() => dispatch({ type: 'SELECT_ALL' })} aria-label={`Select all ${state.filter} conversations`} /><span>{selectedCount} selected</span></label>
            <div class="cl-bulk-actions">
              <div class="cl-item-actions">
                <button type="button" disabled={selectedCount === 0 || state.operating} onClick={() => dispatch({ type: 'MOVE_MENU', ids: state.selectedIds })}>Move {selectedCount} conversation{selectedCount === 1 ? '' : 's'}</button>
                {state.moveMenuIds && <ProjectMoveMenu projects={state.projects} includeUnassigned onMove={(projectId) => void moveConversations(state.moveMenuIds!, projectId)} />}
              </div>
              <button type="button" disabled={selectedCount === 0 || state.operating} onClick={() => void runOperation(bulkOperation, state.selectedIds)}>{capitalize(bulkOperation)} {selectedCount} conversation{selectedCount === 1 ? '' : 's'}</button>
              {state.filter === 'archived' && <button type="button" class="cl-destructive-button" disabled={selectedCount === 0 || state.operating} onClick={() => void runOperation('delete', state.selectedIds)}>Delete {selectedCount} conversation{selectedCount === 1 ? '' : 's'}</button>}
            </div>
          </div>
        )}

        {state.errorMessage && <div class="cl-error-banner" role="alert">{state.errorMessage}{!state.loading && state.conversations.length === 0 && <button type="button" class="cl-retry-button" onClick={() => void loadConversations()} aria-label={`Retry loading ${state.filter} conversations`}>Retry</button>}</div>}
        {state.noticeMessage && <div class="cl-notice-banner" role="status">{state.noticeMessage}</div>}

        {state.loading ? (
          <div class="cl-panel cl-loading-panel" aria-live="polite"><span class="cl-spinner" aria-hidden="true" /><span>Loading {state.filter} conversations…</span></div>
        ) : state.errorMessage && state.conversations.length === 0 ? null : state.conversations.length === 0 ? (
          <section class="cl-empty-state"><div class="cl-empty-icon" aria-hidden="true"><Icon name="bot" size={28} aria-hidden /></div><h2 class="cl-empty-heading">No {state.filter} conversations</h2><p class="cl-empty-copy">{state.filter === 'active' ? 'New and unarchived chats appear here.' : 'Archive a conversation when you are finished with it.'}</p>{state.filter === 'active' && <button type="button" class="cl-cta-button" onClick={() => void createConversation()} disabled={state.creating}>{state.creating ? 'Creating…' : 'Start a new chat'}</button>}</section>
        ) : (
          <div class="cl-project-groups">
            {conversationGroups(state.conversations, state.projects).map((group) => (
              <section key={group.id ?? 'unassigned'} class="cl-project-group">
                <header class="cl-project-header">
                  <h2 class="cl-project-title">{group.name}</h2>
                  {group.project && <div class="cl-item-actions"><button type="button" class="cl-menu-trigger" onClick={() => dispatch({ type: 'PROJECT_MENU', id: state.projectMenuId === group.project!.id ? null : group.project!.id })} aria-label={`Project actions for ${group.name}`}>⋯</button>{state.projectMenuId === group.project.id && <div class="cl-menu" role="menu" aria-label={`${group.name} project actions`}><button type="button" class="cl-menu-button" role="menuitem" onClick={() => void renameProject(group.project!)}>Rename project</button><button type="button" class="cl-menu-button cl-menu-button--destructive" role="menuitem" onClick={() => void deleteProject(group.project!)}>Delete project</button></div>}</div>}
                </header>
                <ul class="cl-list" aria-label={`${group.name} conversations`}>
            {group.conversations.map((conversation) => {
              const title = displayTitle(conversation);
              const isRenaming = state.renamingId === conversation.id;
              const isMenuOpen = state.menuId === conversation.id;
              const selected = state.selectedIds.includes(conversation.id);
              return (
                <li key={conversation.id} class={`cl-item${selected ? ' cl-item--selected' : ''}`} data-conversation-menu-root="true">
                  {state.selectionMode && <label class="cl-row-checkbox"><input type="checkbox" checked={selected} onChange={() => dispatch({ type: 'TOGGLE_SELECTED', id: conversation.id })} aria-label={`Select ${title}`} /></label>}
                  {isRenaming ? (
                    <form class="cl-rename-form" onSubmit={(event) => { event.preventDefault(); void submitRename(conversation.id, title); }}>
                      <input ref={renameInputRef} type="text" value={state.renameDraft} class="cl-rename-input" aria-label="Conversation title" onInput={(event) => dispatch({ type: 'UPDATE_RENAME_DRAFT', title: (event.target as HTMLInputElement).value })} onKeyDown={(event) => event.key === 'Escape' && dispatch({ type: 'CANCEL_RENAME' })} />
                      <button type="submit" class="cl-inline-button cl-inline-button--primary" disabled={state.renaming} aria-label="Save title">{state.renaming ? 'Saving…' : 'Save'}</button>
                      <button type="button" class="cl-inline-button" onClick={() => dispatch({ type: 'CANCEL_RENAME' })} disabled={state.renaming} aria-label="Cancel rename">Cancel</button>
                    </form>
                  ) : <>
                    <button type="button" class="cl-item-main" onClick={() => handleNavigate(conversation.id)} onContextMenu={(event) => { event.preventDefault(); clearLongPress(); dispatch({ type: 'OPEN_MENU', id: conversation.id, suppressNavigate: true }); }} onPointerDown={() => scheduleLongPressMenu(conversation.id)} onPointerUp={clearLongPress} onPointerLeave={clearLongPress} onPointerCancel={clearLongPress} aria-label={state.selectionMode ? `Toggle ${title} selection` : `Open ${title} conversation`}>
                      <span class="cl-item-title">{title}</span>
                      <span class="cl-item-meta"><span class="cl-item-model">{displayModel(conversation)}</span><span class="cl-item-separator" aria-hidden="true">·</span><span>{state.filter === 'archived' ? `Archived ${formatDate(conversation.archivedAt)}` : `Updated ${formatDate(conversation.updatedAt)}`}</span></span>
                    </button>
                    {!state.selectionMode && <div class="cl-item-actions"><button type="button" class="cl-menu-trigger" onClick={() => dispatch({ type: isMenuOpen ? 'CLOSE_MENU' : 'OPEN_MENU', id: conversation.id })} aria-haspopup="menu" aria-expanded={isMenuOpen} aria-label={`Conversation actions for ${title}`} disabled={state.operating}>⋯</button>{isMenuOpen && <div class="cl-menu" role="menu" aria-label={`${title} actions`}><button type="button" class="cl-menu-button" role="menuitem" onClick={() => dispatch({ type: 'START_RENAME', id: conversation.id, title })}>Rename</button><ProjectMoveMenu projects={state.projects} includeUnassigned={conversation.projectId != null} onMove={(projectId) => void moveConversations([conversation.id], projectId)} />{state.filter === 'active' ? <button type="button" class="cl-menu-button" role="menuitem" onClick={() => void runOperation('archive', [conversation.id])}>Archive</button> : <><button type="button" class="cl-menu-button" role="menuitem" onClick={() => void runOperation('unarchive', [conversation.id])}>Unarchive</button><button type="button" class="cl-menu-button cl-menu-button--destructive" role="menuitem" onClick={() => void runOperation('delete', [conversation.id])}>Delete</button></>}</div>}</div>}
                  </>}
                </li>
              );
            })}
                </ul>
              </section>
            ))}
          </div>
        )}
      </main>
    </div>
  );
}

function ProjectMoveMenu({ projects, includeUnassigned, onMove }: { projects: ConversationProject[]; includeUnassigned: boolean; onMove: (projectId: string | null) => void }) {
  return <>
    {includeUnassigned && <button type="button" class="cl-menu-button" role="menuitem" onClick={() => onMove(null)}>Remove from project</button>}
    {projects.map((project) => <button key={project.id} type="button" class="cl-menu-button" role="menuitem" onClick={() => onMove(project.id)}>Move to {project.name}</button>)}
  </>;
}

function conversationGroups(conversations: ConversationMeta[], projects: ConversationProject[]) {
  const knownProjects = new Map(projects.map((project) => [project.id, project]));
  const grouped: Array<{ id: string | null; name: string; project?: ConversationProject; conversations: ConversationMeta[] }> = projects.map((project) => ({ id: project.id, name: project.name, project, conversations: conversations.filter((conversation) => conversation.projectId === project.id) }));
  const unassigned = conversations.filter((conversation) => !conversation.projectId || !knownProjects.has(conversation.projectId));
  if (unassigned.length > 0) grouped.push({ id: null, name: 'No project', project: undefined, conversations: unassigned });
  return grouped.filter((group) => group.conversations.length > 0);
}

async function runSingleStateOperation(bridge: MahoBridge, operation: 'archive' | 'unarchive', id: string): Promise<ConversationBulkResult> {
  const ok = operation === 'archive' ? await bridge.conversationArchive(id) : await bridge.conversationUnarchive(id);
  if (!ok) throw new Error(`Unable to ${operation} this conversation.`);
  return { requestedCount: 1, affectedIds: [id], unchangedIds: [], missingIds: [] };
}

function bulkNotice(operation: BulkOperation, result: ConversationBulkResult): string {
  const verb = operation === 'archive' ? 'Archived' : operation === 'unarchive' ? 'Unarchived' : 'Deleted';
  const parts = [`${verb} ${result.affectedIds.length} conversation${result.affectedIds.length === 1 ? '' : 's'}.`];
  if (result.unchangedIds.length > 0) parts.push(`${result.unchangedIds.length} was already in that state.`);
  if (result.missingIds.length > 0) parts.push(`${result.missingIds.length} could not be found.`);
  return parts.join(' ');
}

function displayTitle(conversation: ConversationMeta): string { return conversation.title?.trim() || 'Untitled chat'; }
function displayModel(conversation: ConversationMeta): string { return conversation.model?.trim() || 'default'; }
function formatDate(timestamp?: string | null): string { const date = new Date(timestamp ?? ''); return Number.isNaN(date.getTime()) ? 'recently' : DATE_FORMATTER.format(date); }
function sortConversations(conversations: ConversationMeta[]): ConversationMeta[] { return [...conversations].sort((left, right) => Date.parse(right.archivedAt ?? right.updatedAt) - Date.parse(left.archivedAt ?? left.updatedAt)); }
function capitalize(value: string): string { return value.charAt(0).toUpperCase() + value.slice(1); }
function buildConversationMeta(): ConversationMeta { const now = new Date().toISOString(); return { id: generateConversationId(), title: 'New chat', model: 'default', createdAt: now, updatedAt: now }; }
function generateConversationId(): string { return typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function' ? crypto.randomUUID() : `conversation-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 10)}`; }
function toErrorMessage(error: unknown, fallback: string): string { return error instanceof Error ? error.message : typeof error === 'string' && error.trim() ? error : fallback; }
function navigateToChatHash(sessionId: string): void { window.location.hash = `#chat?sessionId=${encodeURIComponent(sessionId)}`; }
