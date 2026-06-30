import React, {Component, useEffect, useMemo, useRef, useState} from 'react';
import type {ErrorInfo, ReactNode} from 'react';
import {createRoot} from 'react-dom/client';
import {toast} from 'sonner';

import type {MahoAiStore} from '../store.js';
import {createFileAttachment, isSessionReadOnly, RuntimeEventKind, ViewMode} from '../types.js';
import type {AppState, TimelineEntry} from '../types.js';
import {getToolProgressLabel} from '../views/conversation_thread.js';
import {bootstrapStore, createMahoAiStore} from './store.js';
import {runWithBootGuard} from './boot-guard.js';
import {CompactShell} from './features/compact/compact-shell.js';
import {TabTidyModal} from './features/compact/tab-tidy-modal.js';
import type {TidyFolderSuggestion} from './features/compact/tab-tidy-modal.js';
import {
  Composer,
  type ComposerHandlers,
} from './features/compact/composer.js';
import {DeveloperWorkspace} from './features/developer/developer-workspace.js';
import {useAppState} from './hooks/use-app-state.js';
import {cn} from '@lib/utils';
import {Alert, AlertDescription, AlertTitle} from '@ui/alert';
import {Toaster} from '@ui/sonner';
import {TooltipProvider} from '@ui/tooltip';
import {AlertCircle} from '@icons/lucide';
import {applyTheme, watchAutoTheme} from '@theme/apply_theme';

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
    <Alert className="border-destructive/60 bg-destructive/10 text-destructive shadow-sm" variant="destructive">
      <AlertCircle className="size-4" />
      <AlertTitle>Maho AI failed to start</AlertTitle>
      <AlertDescription>
        <pre className="m-0 whitespace-pre-wrap break-words font-mono text-xs leading-5 text-destructive">{message}</pre>
      </AlertDescription>
    </Alert>
  );
}

function PageHandlerTimeoutCard() {
  return (
    <Alert className="border-destructive/60 bg-destructive/10 text-destructive shadow-sm" variant="destructive">
      <AlertCircle className="size-4" />
      <AlertTitle>Maho AI failed to start</AlertTitle>
      <AlertDescription>
        <pre className="m-0 whitespace-pre-wrap break-words font-mono text-xs leading-5 text-destructive">{'Page handler did not respond within ' +
          String(PAGE_HANDLER_TIMEOUT_MS / 1000) +
          's. Reload the panel or restart the browser.'}</pre>
      </AlertDescription>
    </Alert>
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
  const [tidySuggestions, setTidySuggestions] = useState<TidyFolderSuggestion[] | null>(null);
  const [tidyLoading, setTidyLoading] = useState(false);
  useEffect(() => {
    (window as unknown as Record<string, unknown>)['__mahoAiMounted'] = true;
    applyTheme("auto");
    return watchAutoTheme((resolved) => applyTheme(resolved));
  }, []);

  useEffect(() => {
    void store.refreshAISettings();
    const onVisibility = () => {
      if (document.visibilityState === 'visible') {
        void store.refreshAISettings();
      }
    };
    document.addEventListener('visibilitychange', onVisibility);
    return () => document.removeEventListener('visibilitychange', onVisibility);
  }, [store]);

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
    onToggleBrowserContext: () => store.toggleBrowserContext(),
    onToggleHistoryAttachment: attachment => {
      store.toggleAttachment(attachment);
    },
    onToggleTabAttachment: attachment => {
      store.toggleAttachment(attachment);
    },
    onAttachFiles: async files => {
      const list = Array.from(files);
      for (const file of list) {
        try {
          const attachment = await createFileAttachment(file);
          store.addAttachment(attachment);
        } catch (e: unknown) {
          const errMsg = e instanceof Error ? e.message : String(e);
          console.error('Failed to attach file', file.name, e);
          toast.error(errMsg);
        }
      }
    },
    onRequestFileChooser: () => {
      void store.requestFileChooser();
    },
    onRemoveAttachment: attachment => {
      store.toggleAttachment(attachment);
    },
    onSetActiveAI: (providerId: string, modelId: string) => {
      return store.setActiveAI(providerId, modelId);
    },
    onOpenSettings: () => {
      void store.openSettings();
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
    onSetActiveAI: (providerId: string, modelId: string) => {
      return store.setActiveAI(providerId, modelId);
    },
  }), [store]);

  const handleTidyTabs = () => {
    if (tidyLoading) return;
    setTidyLoading(true);
    const openTabsItems = state.openTabs.items;
    const tabs = openTabsItems.map(t => ({tabId: t.tabId, title: t.title, url: t.url}));
    void store.requestTabTidy(tabs).then(folders => {
      setTidyLoading(false);
      if (folders.length > 0) {
        setTidySuggestions(folders.map(f => ({name: f.name, tabIds: f.tabIds})));
      }
    }).catch(() => {
      setTidyLoading(false);
    });
  };

  const handleTidyApply = (selected: TidyFolderSuggestion[]) => {
    setTidySuggestions(null);
    void store.applyTabTidyFolders(selected.map(f => ({name: f.name, tabIds: f.tabIds})));
  };

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
    <div className="flex h-screen min-h-0 flex-col bg-background text-foreground">
      <section className="grid h-full min-h-0 grid-rows-[minmax(0,1fr)_auto] gap-3 p-3 max-[520px]:gap-2 max-[520px]:p-2">
        {state.bootError ? <BootErrorCard message={state.bootError} /> : (
          <>
            <div className={cn(
                'min-h-0 overflow-hidden',
                state.developerMode ? 'block' : 'grid grid-rows-[auto_minmax(0,1fr)] gap-3')}>
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
                  onTidyTabs={handleTidyTabs}
                  readOnly={readOnly}
                  state={state}
                  thinkingLabel={thinkingLabel}
                />
              )}
            </div>
            <div className="sticky bottom-0 z-10 bg-gradient-to-t from-background via-background/95 to-transparent pt-1">
              <Composer handlers={composerHandlers} state={state} />
            </div>
          </>
        )}
      </section>
      {tidySuggestions && (
        <TabTidyModal
          folders={tidySuggestions}
          onApply={handleTidyApply}
          onDismiss={() => setTidySuggestions(null)}
        />
      )}
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
        <TooltipProvider delayDuration={200}>
          <ErrorBoundary>
            <MahoAiApp store={store} />
          </ErrorBoundary>
          <Toaster position="bottom-right" />
        </TooltipProvider>
      </React.StrictMode>);
});
