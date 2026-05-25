import React, {useEffect, useMemo, useRef} from 'react';
import {createRoot} from 'react-dom/client';

import type {MahoAiStore} from '../store.js';
import {isSessionReadOnly, RuntimeEventKind, ViewMode} from '../types.js';
import type {AppState, TimelineEntry} from '../types.js';
import {getToolProgressLabel} from '../views/conversation_thread.js';
import {bootstrapStore, createMahoAiStore} from './store.js';
import {CompactShell} from './features/compact/compact-shell.js';
import {
  Composer,
  type ComposerHandlers,
} from './features/compact/composer.js';
import {DeveloperWorkspace} from './features/developer/developer-workspace.js';
import {useAppState} from './hooks/use-app-state.js';
import {cn} from './lib/cn.js';

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

  useEffect(() => {
    if (bootstrappedRef.current) {
      return;
    }

    bootstrappedRef.current = true;
    void bootstrapStore(store).catch((error: unknown) => {
      console.error('[maho-ai] Bootstrap failed:', error);
    });
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
    onToggleBrowserContext: () => store.toggleBrowserContext(),
    onSubmit: () => {
      if (!store.getSnapshot().currentSessionId) {
        void store.startSession();
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
      const snapshot = store.getSnapshot();
      const session = snapshot.sessionsById[sessionId];
      void store.resumeSession(sessionId, session?.isReadOnly ?? false);
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
createRoot(rootElement).render(
    <React.StrictMode>
      <MahoAiApp store={store} />
    </React.StrictMode>);
