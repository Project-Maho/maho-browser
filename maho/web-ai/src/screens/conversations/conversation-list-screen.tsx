import { useCallback, useEffect, useReducer, useRef } from 'preact/hooks';
import { type ConversationMeta, type MahoBridge } from '../../bridge';
import { useBridge } from '../../hooks/use-bridge';
import { Icon } from '../../ui/icon';

const LONG_PRESS_MS = 450;
const UPDATED_AT_FORMATTER = new Intl.DateTimeFormat(undefined, {
  month: 'short',
  day: 'numeric',
});

interface ConversationListScreenProps {
  onBack?: () => void;
  bridge?: MahoBridge;
  onNavigateToChat?: (sessionId: string) => void;
}

interface State {
  loading: boolean;
  creating: boolean;
  renaming: boolean;
  deletingId: string | null;
  conversations: ConversationMeta[];
  errorMessage: string | null;
  menuId: string | null;
  renamingId: string | null;
  renameDraft: string;
  suppressNavigateId: string | null;
}

type Action =
  | { type: 'LOAD_START' }
  | { type: 'LOAD_DONE'; conversations: ConversationMeta[] }
  | { type: 'LOAD_ERROR'; message: string }
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
  | { type: 'DELETE_START'; id: string }
  | { type: 'DELETE_DONE'; id: string }
  | { type: 'DELETE_ERROR'; message: string }
  | { type: 'CLEAR_SUPPRESSED_NAVIGATION'; id: string };

function initialState(): State {
  return {
    loading: true,
    creating: false,
    renaming: false,
    deletingId: null,
    conversations: [],
    errorMessage: null,
    menuId: null,
    renamingId: null,
    renameDraft: '',
    suppressNavigateId: null,
  };
}

function reducer(state: State, action: Action): State {
  switch (action.type) {
    case 'LOAD_START':
      return { ...state, loading: true, errorMessage: null };
    case 'LOAD_DONE':
      return {
        ...state,
        loading: false,
        errorMessage: null,
        conversations: sortConversations(action.conversations),
      };
    case 'LOAD_ERROR':
      return { ...state, loading: false, errorMessage: action.message };

    case 'CREATE_START':
      return { ...state, creating: true, errorMessage: null };
    case 'CREATE_DONE':
      return { ...state, creating: false };
    case 'CREATE_ERROR':
      return { ...state, creating: false, errorMessage: action.message };

    case 'OPEN_MENU':
      return {
        ...state,
        menuId: action.id,
        errorMessage: null,
        suppressNavigateId: action.suppressNavigate ? action.id : state.suppressNavigateId,
      };
    case 'CLOSE_MENU':
      return { ...state, menuId: null };

    case 'START_RENAME':
      return {
        ...state,
        menuId: null,
        renamingId: action.id,
        renameDraft: action.title,
        renaming: false,
        errorMessage: null,
      };
    case 'UPDATE_RENAME_DRAFT':
      return { ...state, renameDraft: action.title, errorMessage: null };
    case 'CANCEL_RENAME':
      return { ...state, renamingId: null, renameDraft: '', renaming: false };
    case 'RENAME_START':
      return { ...state, renaming: true, errorMessage: null };
    case 'RENAME_DONE':
      return {
        ...state,
        renaming: false,
        renamingId: null,
        renameDraft: '',
        conversations: sortConversations(
          state.conversations.map((conversation) =>
            conversation.id === action.id
              ? {
                  ...conversation,
                  title: action.title,
                  updatedAt: new Date().toISOString(),
                }
              : conversation,
          ),
        ),
      };
    case 'RENAME_ERROR':
      return { ...state, renaming: false, errorMessage: action.message };

    case 'DELETE_START':
      return { ...state, deletingId: action.id, menuId: null, errorMessage: null };
    case 'DELETE_DONE':
      return {
        ...state,
        deletingId: null,
        conversations: state.conversations.filter((conversation) => conversation.id !== action.id),
      };
    case 'DELETE_ERROR':
      return { ...state, deletingId: null, errorMessage: action.message };

    case 'CLEAR_SUPPRESSED_NAVIGATION':
      return state.suppressNavigateId === action.id
        ? { ...state, suppressNavigateId: null }
        : state;

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

  const clearLongPress = useCallback(() => {
    if (longPressTimerRef.current !== null) {
      window.clearTimeout(longPressTimerRef.current);
      longPressTimerRef.current = null;
    }
  }, []);

  const loadConversations = useCallback(async () => {
    dispatch({ type: 'LOAD_START' });
    try {
      const conversations = await bridge.conversationList();
      dispatch({ type: 'LOAD_DONE', conversations });
    } catch (error) {
      dispatch({
        type: 'LOAD_ERROR',
        message: toErrorMessage(error, 'Unable to load saved conversations.'),
      });
    }
  }, [bridge]);

  useEffect(() => {
    void loadConversations();
  }, [loadConversations]);

  useEffect(() => clearLongPress, [clearLongPress]);

  useEffect(() => {
    if (!state.renamingId) {
      return;
    }
    renameInputRef.current?.focus();
    renameInputRef.current?.select();
  }, [state.renamingId]);

  useEffect(() => {
    if (!state.menuId) {
      return undefined;
    }

    const handlePointerDown = (event: globalThis.MouseEvent) => {
      const target = event.target;
      if (target instanceof HTMLElement && target.closest('[data-conversation-menu-root="true"]')) {
        return;
      }
      dispatch({ type: 'CLOSE_MENU' });
    };

    document.addEventListener('mousedown', handlePointerDown);
    return () => {
      document.removeEventListener('mousedown', handlePointerDown);
    };
  }, [state.menuId]);

  const createConversation = useCallback(async () => {
    dispatch({ type: 'CREATE_START' });
    const meta = buildConversationMeta();

    try {
      const sessionId = await bridge.conversationCreate(meta);
      dispatch({ type: 'CREATE_DONE' });
      onNavigateToChat(sessionId);
    } catch (error) {
      dispatch({
        type: 'CREATE_ERROR',
        message: toErrorMessage(error, 'Unable to start a new conversation right now.'),
      });
    }
  }, [bridge, onNavigateToChat]);

  const beginRename = useCallback((conversation: ConversationMeta) => {
    dispatch({ type: 'START_RENAME', id: conversation.id, title: displayTitle(conversation) });
  }, []);

  const submitRename = useCallback(
    async (conversationId: string, currentTitle: string) => {
      const nextTitle = state.renameDraft.trim();
      if (!nextTitle) {
        dispatch({ type: 'RENAME_ERROR', message: 'Conversation title cannot be empty.' });
        return;
      }

      if (nextTitle === currentTitle.trim()) {
        dispatch({ type: 'CANCEL_RENAME' });
        return;
      }

      dispatch({ type: 'RENAME_START' });
      try {
        await bridge.conversationRename(conversationId, nextTitle);
        dispatch({ type: 'RENAME_DONE', id: conversationId, title: nextTitle });
      } catch (error) {
        dispatch({
          type: 'RENAME_ERROR',
          message: toErrorMessage(error, 'Unable to rename this conversation.'),
        });
      }
    },
    [bridge, state.renameDraft],
  );

  const deleteConversation = useCallback(
    async (conversationId: string) => {
      if (!window.confirm('Delete this conversation?')) {
        dispatch({ type: 'CLOSE_MENU' });
        return;
      }

      dispatch({ type: 'DELETE_START', id: conversationId });
      try {
        await bridge.conversationDelete(conversationId);
        dispatch({ type: 'DELETE_DONE', id: conversationId });
      } catch (error) {
        dispatch({
          type: 'DELETE_ERROR',
          message: toErrorMessage(error, 'Unable to delete this conversation.'),
        });
      }
    },
    [bridge],
  );

  const handleNavigate = useCallback(
    (conversationId: string) => {
      if (state.suppressNavigateId === conversationId) {
        dispatch({ type: 'CLEAR_SUPPRESSED_NAVIGATION', id: conversationId });
        return;
      }
      onNavigateToChat(conversationId);
    },
    [onNavigateToChat, state.suppressNavigateId],
  );

  const scheduleLongPressMenu = useCallback(
    (conversationId: string) => {
      clearLongPress();
      longPressTimerRef.current = window.setTimeout(() => {
        dispatch({ type: 'OPEN_MENU', id: conversationId, suppressNavigate: true });
        longPressTimerRef.current = null;
      }, LONG_PRESS_MS);
    },
    [clearLongPress],
  );

  return (
    <div class="conversation-list-screen" data-testid="conversation-list-screen">
      <header class="cl-header">
        <div class="cl-header-main">
          {onBack && (
            <button
              type="button"
              class="cl-back-button"
              onClick={onBack}
              aria-label="Back"
            >
              <Icon name="chevron-left" size={18} aria-hidden />
            </button>
          )}
          <div>
            <p class="cl-eyebrow">History</p>
            <h1 class="cl-title-heading">Saved Conversations</h1>
            <p class="cl-subtitle">Jump back into recent chats or spin up a fresh one.</p>
          </div>
        </div>
        <button
          type="button"
          class="cl-primary-button"
          onClick={() => void createConversation()}
          disabled={state.creating}
        >
          {state.creating ? 'Creating…' : '+ New'}
        </button>
      </header>

      <main class="cl-content">
        {state.errorMessage && (
          <div class="cl-error-banner" role="alert">
            {state.errorMessage}
          </div>
        )}

        {state.loading ? (
          <div class="cl-panel cl-loading-panel" aria-live="polite">
            <span class="cl-spinner" aria-hidden="true" />
            <span>Loading saved conversations…</span>
          </div>
        ) : state.conversations.length === 0 ? (
          <section class="cl-empty-state">
            <div class="cl-empty-icon" aria-hidden="true">
              <Icon name="bot" size={28} aria-hidden />
            </div>
            <h2 class="cl-empty-heading">No saved conversations yet</h2>
            <p class="cl-empty-copy">
              Start a chat once, and it will show up here for quick resume across devices.
            </p>
            <button
              type="button"
              class="cl-cta-button"
              onClick={() => void createConversation()}
              disabled={state.creating}
            >
              {state.creating ? 'Creating…' : 'Start a new chat'}
            </button>
          </section>
        ) : (
          <ul class="cl-list" aria-label="Saved conversations">
            {state.conversations.map((conversation) => {
              const title = displayTitle(conversation);
              const isDeleting = state.deletingId === conversation.id;
              const isRenaming = state.renamingId === conversation.id;
              const isMenuOpen = state.menuId === conversation.id;

              return (
                <li
                  key={conversation.id}
                  class="cl-item"
                  data-conversation-menu-root="true"
                >
                  {isRenaming ? (
                    <form
                      class="cl-rename-form"
                      onSubmit={(event) => {
                        event.preventDefault();
                        void submitRename(conversation.id, title);
                      }}
                    >
                      <input
                        ref={renameInputRef}
                        type="text"
                        value={state.renameDraft}
                        class="cl-rename-input"
                        aria-label="Conversation title"
                        onInput={(event) =>
                          dispatch({
                            type: 'UPDATE_RENAME_DRAFT',
                            title: (event.target as HTMLInputElement).value,
                          })
                        }
                        onKeyDown={(event) => {
                          if (event.key === 'Escape') {
                            dispatch({ type: 'CANCEL_RENAME' });
                          }
                        }}
                      />
                      <button
                        type="submit"
                        class="cl-inline-button cl-inline-button--primary"
                        disabled={state.renaming}
                        aria-label="Save title"
                      >
                        {state.renaming ? 'Saving…' : 'Save'}
                      </button>
                      <button
                        type="button"
                        class="cl-inline-button"
                        onClick={() => dispatch({ type: 'CANCEL_RENAME' })}
                        disabled={state.renaming}
                        aria-label="Cancel rename"
                      >
                        Cancel
                      </button>
                    </form>
                  ) : (
                    <>
                      <button
                        type="button"
                        class="cl-item-main"
                        onClick={() => handleNavigate(conversation.id)}
                        onContextMenu={(event) => {
                          event.preventDefault();
                          clearLongPress();
                          dispatch({
                            type: 'OPEN_MENU',
                            id: conversation.id,
                            suppressNavigate: true,
                          });
                        }}
                        onPointerDown={() => scheduleLongPressMenu(conversation.id)}
                        onPointerUp={clearLongPress}
                        onPointerLeave={clearLongPress}
                        onPointerCancel={clearLongPress}
                        aria-label={`Open ${title} conversation`}
                      >
                        <span class="cl-item-title">{title}</span>
                        <span class="cl-item-meta">
                          <span class="cl-item-model">{displayModel(conversation)}</span>
                          <span class="cl-item-separator" aria-hidden="true">
                            ·
                          </span>
                          <span>Updated {formatUpdatedAt(conversation.updatedAt)}</span>
                        </span>
                      </button>

                      <div class="cl-item-actions">
                        <button
                          type="button"
                          class="cl-menu-trigger"
                          onClick={() =>
                            dispatch({
                              type: isMenuOpen ? 'CLOSE_MENU' : 'OPEN_MENU',
                              id: conversation.id,
                            })
                          }
                          aria-haspopup="menu"
                          aria-expanded={isMenuOpen}
                          aria-label={`Conversation actions for ${title}`}
                          disabled={isDeleting}
                        >
                          ⋯
                        </button>

                        {isMenuOpen && (
                          <div class="cl-menu" role="menu" aria-label={`${title} actions`}>
                            <button
                              type="button"
                              class="cl-menu-button"
                              role="menuitem"
                              onClick={() => beginRename(conversation)}
                            >
                              Rename
                            </button>
                            <button
                              type="button"
                              class="cl-menu-button cl-menu-button--destructive"
                              role="menuitem"
                              onClick={() => void deleteConversation(conversation.id)}
                            >
                              Delete
                            </button>
                          </div>
                        )}
                      </div>
                    </>
                  )}
                </li>
              );
            })}
          </ul>
        )}
      </main>
    </div>
  );
}

function displayTitle(conversation: ConversationMeta): string {
  const title = conversation.title?.trim() ?? '';
  return title.length > 0 ? title : 'Untitled chat';
}

function displayModel(conversation: ConversationMeta): string {
  const model = conversation.model?.trim();
  return model && model.length > 0 ? model : 'default';
}

function formatUpdatedAt(timestamp: string): string {
  const date = new Date(timestamp);
  if (Number.isNaN(date.getTime())) {
    return 'recently';
  }
  return UPDATED_AT_FORMATTER.format(date);
}

function sortConversations(conversations: ConversationMeta[]): ConversationMeta[] {
  return [...conversations].sort(
    (left, right) => Date.parse(right.updatedAt) - Date.parse(left.updatedAt),
  );
}

function buildConversationMeta(): ConversationMeta {
  const now = new Date().toISOString();
  return {
    id: generateConversationId(),
    title: 'New chat',
    model: 'default',
    createdAt: now,
    updatedAt: now,
  };
}

function generateConversationId(): string {
  if (typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function') {
    return crypto.randomUUID();
  }

  return `conversation-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 10)}`;
}

function toErrorMessage(error: unknown, fallback: string): string {
  if (error instanceof Error) {
    return error.message;
  }
  if (typeof error === 'string' && error.trim().length > 0) {
    return error;
  }
  return fallback;
}

function navigateToChatHash(sessionId: string): void {
  window.location.hash = `#chat?sessionId=${encodeURIComponent(sessionId)}`;
}
