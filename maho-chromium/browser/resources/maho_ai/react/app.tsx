import React, {Component, useEffect, useMemo, useRef} from 'react';
import type {ErrorInfo, ReactNode} from 'react';
import {createRoot} from 'react-dom/client';

import type {MahoAiStore} from '../store.js';
import {isSessionReadOnly, RuntimeEventKind, ViewMode} from '../types.js';
import type {AppState, TimelineEntry} from '../types.js';
import {getToolProgressLabel} from '../views/conversation_thread.js';
import {bootstrapStore, createMahoAiStore} from './store.js';
import {runWithBootGuard} from './boot-guard.js';
import {CompactShell} from './features/compact/compact-shell.js';
import {
  Composer,
  type ComposerHandlers,
} from './features/compact/composer.js';
import {DeveloperWorkspace} from './features/developer/developer-workspace.js';
import {useAppState} from './hooks/use-app-state.js';
import {cn} from './lib/cn.js';

const PAGE_HANDLER_TIMEOUT_MS = 3000;

function getPendingThreadLabel(entries: TimelineEntry[], pending: boolean): string | null {
  if (!pending) {
    return null;
  }

  let latestToolName: string | undefined = undefined;
  for (let index = entries.length - 1; index >= 0; index--) {
    const event = entries[index]?.event;
    if (!event) {
      continue;
    }

    if (event.kind === RuntimeEventKind.kAssistantToken ||
        event.kind === RuntimeEventKind.kAssistantThinking ||
        event.kind === RuntimeEventKind.kTurnComplete) {
      return null;
    }

    if (!latestToolName && event.kind === RuntimeEventKind.kToolRequest) {
      latestToolName = event.toolCall?.toolName || undefined;
    }

    if (event.kind === RuntimeEventKind.kUserPrompt) {
      break;
    }
  }

  return getToolProgressLabel(latestToolName);
}

function BootErrorCard({message}: {message: string}) {
  return (
    <section className="boot-error">
      <h1>Maho AI failed to start</h1>
      <pre>{message}</pre>
    </section>
  );
}

function PageHandlerTimeoutCard() {
  return (
    <section className="boot-error">
      <h1>Maho AI failed to start</h1>
      <pre>{'Page handler did not respond within ' +
        String(PAGE_HANDLER_TIMEOUT_MS / 1000) +
        's. Reload the panel or restart the browser.'}</pre>
    </section>
  );
}

interface ErrorBoundaryState {
  caughtError: Error | null;
}

class ErrorBoundary extends Component<{children: ReactNode}, ErrorBoundaryState> {
  constructor(props: {children: ReactNode}) {
    super(props);
    this.state = {caughtError: null};
  }

  static getDerivedStateFromError(error: unknown): ErrorBoundaryState {
    return {caughtError: error instanceof Error ? error : new Error(String(error))};
  }

  componentDidCatch(error: Error, info: ErrorInfo): void {
    console.error('[maho-ai] Uncaught render error:', error, info.componentStack);
  }

  render(): ReactNode {
    if (this.state.caughtError) {
      return <BootErrorCard message={this.state.caughtError.message} />;
    }
    return this.props.children;
  }
}

function isEditableTarget(target: EventTarget | null): boolean {
  if (!(target instanceof HTMLElement)) {
    return false;
  }

  const tagName = target.tagName.toLowerCase();
  return target.isContentEditable || tagName === 'input' || tagName === 'textarea' ||
      tagName === 'select';
}

function MahoAiApp({store}: {store: MahoAiStore}) {
  const state = useAppState(store);
  const bootstrappedRef = useRef(false);
  const [handlerTimedOut, setHandlerTimedOut] = React.useState(false);

  useEffect(() => {
    (window as unknown as Record<string, unknown>)['__mahoAiMounted'] = true;
  }, []);

  useEffect(() => {
    const timeoutId = setTimeout(() => {
      setHandlerTimedOut(true);
    }, PAGE_HANDLER_TIMEOUT_MS);

    if (bootstrappedRef.current) {
      clearTimeout(timeoutId);
      return;
    }

    bootstrappedRef.current = true;
    void bootstrapStore(store)
      .then(() => {
        clearTimeout(timeoutId);
        setHandlerTimedOut(false);
      })
      .catch((error: unknown) => {
        clearTimeout(timeoutId);
        console.error('[maho-ai] Bootstrap failed:', error);
      });

    return () => clearTimeout(timeoutId);
  }, [store]);

  useEffect(() => {
    const handleKeyDown = (event: KeyboardEvent) => {
      if (isEditableTarget(event.target)) {
        return;
      }

      if (!event.shiftKey || !(event.metaKey || event.ctrlKey) ||
          event.key.toLowerCase() !== 'd') {
        return;
      }

      event.preventDefault();
      const snapshot = store.getSnapshot();
      store.setDeveloperMode(!snapshot.developerMode);
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [store]);

  const composerHandlers = useMemo<ComposerHandlers>(() => ({
    onPromptChange: (value: string) => store.setComposerPrompt(value),
    onCancel: () => {
      void store.cancelTurn();
    },
    onStartSession: () => {
      void store.startSession();
    },
    onLoadOpenTabs: () => {
      void store.loadOpenTabs();
    },
    onRefreshOpenTabs: () => {
      void store.loadOpenTabs(true);
    },
    onResetHistorySearch: () => {
      store.resetHistorySearch();
    },
    onSearchHistory: (query: string) => {
      void store.searchHistory(query);
    },
    onResetBookmarksSearch: () => {
      store.resetBookmarksSearch();
    },
    onSearchBookmarks: (query: string) => {
      void store.searchBookmarks(query);
    },
    onToggleBrowserContext: () => store.toggleBrowserContext(),
    onToggleHistoryAttachment: attachment => {
      store.toggleAttachment(attachment);
    },
    onToggleTabAttachment: attachment => {
      store.toggleAttachment(attachment);
    },
    onToggleBookmarkAttachment: attachment => {
      store.toggleAttachment(attachment);
    },
    onSubmit: () => {
      if (!store.getSnapshot().currentSessionId) {
        void store.startSessionThenSubmit();
        return;
      }

      void store.submitPrompt();
    },
  }), [store]);

  const modeHandlers = useMemo(() => ({
    onNewSession: () => {
      void store.startSession();
    },
    onClosePanel: () => {
      void store.closePanel();
    },
    onOpenSettings: () => {
      void store.openSettings();
    },
    onGetViewMode: () => store.getViewMode(),
    onResumeSession: (sessionId: string) => {
      void store.resumeSession(sessionId);
    },
    onRespondToApproval: (approvalId: string, approved: boolean) => {
      void store.respondToApproval(approvalId, approved);
    },
    onSearch: (query: string) => store.setSessionSearchQuery(query),
    onSelectInspector: (kind: AppState['selectedInspector']['kind'], id?: string) =>
      store.setSelectedInspector(kind, id),
    onSetDeveloperMode: (enabled: boolean) => store.setDeveloperMode(enabled),
    onSetViewMode: (mode: ViewMode) => {
      void store.setViewMode(mode);
    },
  }), [store]);

  const sessionEvents = state.currentSessionId ?
      (state.eventsBySessionId[state.currentSessionId] || []) :
      [];
  const hasMessages = sessionEvents.length > 0;
  const currentSession = state.currentSessionId ? state.sessionsById[state.currentSessionId] : null;
  const readOnly = isSessionReadOnly(currentSession);
  const pendingTurn = !!(state.currentSessionId && state.turnPendingBySessionId[state.currentSessionId]);
  const thinkingLabel = getPendingThreadLabel(sessionEvents, pendingTurn);

  if (handlerTimedOut && !state.bootError) {
    return <PageHandlerTimeoutCard />;
  }

  return (
    <div className={cn('console-shell', state.developerMode && 'console-shell--developer')}>
      <section className={cn('compact-panel', state.developerMode && 'compact-panel--developer')}>
        {state.bootError ? <BootErrorCard message={state.bootError} /> : (
          <>
            <div className="compact-panel__stage-host">
              {state.developerMode ? (
                <DeveloperWorkspace
                  onNewSession={modeHandlers.onNewSession}
                  onRespondToApproval={modeHandlers.onRespondToApproval}
                  onResumeSession={modeHandlers.onResumeSession}
                  onSearch={modeHandlers.onSearch}
                  onSelectInspector={modeHandlers.onSelectInspector}
                  onSetDeveloperMode={modeHandlers.onSetDeveloperMode}
                  state={state}
                />
              ) : (
                <CompactShell
                  entries={sessionEvents}
                  hasMessages={hasMessages}
                  onClosePanel={modeHandlers.onClosePanel}
                  onGetViewMode={modeHandlers.onGetViewMode}
                  onOpenSettings={modeHandlers.onOpenSettings}
                  onRespondToApproval={modeHandlers.onRespondToApproval}
                  onSetViewMode={modeHandlers.onSetViewMode}
                  onStartSession={modeHandlers.onNewSession}
                  readOnly={readOnly}
                  state={state}
                  thinkingLabel={thinkingLabel}
                />
              )}
            </div>
            <div className={cn(
                'compact-panel__composer-host',
                state.developerMode && 'console-composer-dock')}>
              <Composer handlers={composerHandlers} state={state} />
            </div>
          </>
        )}
      </section>
    </div>
  );
}

const rootElement = document.getElementById('app');

if (!rootElement) {
  throw new Error('Missing #app root for maho_ai.');
}

const store = createMahoAiStore();
runWithBootGuard(rootElement, () => {
  createRoot(rootElement).render(
      <React.StrictMode>
        <ErrorBoundary>
          <MahoAiApp store={store} />
        </ErrorBoundary>
      </React.StrictMode>);
});
