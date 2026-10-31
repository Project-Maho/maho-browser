import React, {Component, useCallback, useEffect, useMemo, useRef, useState} from 'react';
import type {ErrorInfo, ReactNode} from 'react';
import {createRoot} from 'react-dom/client';
import {toast} from 'sonner';

import type {MahoAiStore} from '../store.js';
import {bindComposerDraftLifecycle} from './features/compact/composer-draft-persistence.js';
import {createFileAttachment, isSessionReadOnly, RuntimeEventKind, ViewMode} from '../types.js';
import type {AISelectionRequest, AppState, QuickAction, TimelineEntry} from '../types.js';
import {collectConversationItems, getPendingThreadLabel} from '../views/conversation_thread.js';
import {bootstrapStore, createMahoAiStore} from './store.js';
import {runWithBootGuard} from './boot-guard.js';
import {CompactShell} from './features/compact/compact-shell.js';
import {CompactTopbar} from './features/compact/compact-topbar.js';
import {RoutineWorkspace} from './features/compact/routine-workspace.js';
import {CommandHints} from './features/compact/command-hints.js';
import {RuntimeConfigCard} from './features/compact/runtime-config-card.js';
import {VoiceInputModal} from './features/compact/voice-input-modal.js';
import {SlashEditors} from './features/compact/slash-editors.js';
import {
  Composer,
  type ComposerHandlers,
} from './features/compact/composer.js';
import {DeveloperWorkspace} from './features/developer/developer-workspace.js';
import {useAppState} from './hooks/use-app-state.js';
import {useVoiceInput, type VoiceFinalControls} from './hooks/use-voice-input.js';
import {ControlActivityTimeline} from './components/control-activity-timeline.js';
import {cn} from '@lib/utils';
import {Alert, AlertDescription, AlertTitle} from '@ui/alert';
import {Toaster} from '@ui/sonner';
import {TooltipProvider} from '@ui/tooltip';
import {AlertCircle} from '@icons/lucide';
import {applyTheme, watchAutoTheme} from '@theme/apply_theme';

const PAGE_HANDLER_TIMEOUT_MS = 3000;

function BootErrorCard(
    {message, onRetry}: {message: string; onRetry?: () => void}) {
  return (
    <Alert className="border-destructive/60 bg-destructive/10 text-destructive shadow-sm" variant="destructive">
      <AlertCircle className="size-4" />
      <AlertTitle>Maho AI failed to start</AlertTitle>
      <AlertDescription>
        <div className="flex flex-col gap-2">
          <p>Maho AI could not reach its runtime. Retry, or open settings to check the connection.</p>
          {onRetry ? (
            <button
              className="self-start rounded-lg border border-destructive/50 px-3 py-1.5 text-xs font-medium transition-colors hover:bg-destructive/15"
              data-boot-retry
              onClick={onRetry}
              type="button">
              Retry
            </button>
          ) : null}
          <details data-boot-details>
            <summary className="cursor-pointer select-none text-xs font-medium">
              Details
            </summary>
            <pre className="m-0 mt-1 whitespace-pre-wrap break-words font-mono text-xs leading-5 text-destructive">{message}</pre>
          </details>
        </div>
      </AlertDescription>
    </Alert>
  );
}

function PageHandlerTimeoutCard({onRetry}: {onRetry?: () => void}) {
  return (
    <Alert className="border-destructive/60 bg-destructive/10 text-destructive shadow-sm" variant="destructive">
      <AlertCircle className="size-4" />
      <AlertTitle>Maho AI failed to start</AlertTitle>
      <AlertDescription>
        <div className="flex flex-col gap-2">
          <p>The page handler did not respond within {String(PAGE_HANDLER_TIMEOUT_MS / 1000)}s. Retry, or restart the browser.</p>
          {onRetry ? (
            <button
              className="self-start rounded-lg border border-destructive/50 px-3 py-1.5 text-xs font-medium transition-colors hover:bg-destructive/15"
              data-boot-retry
              onClick={onRetry}
              type="button">
              Retry
            </button>
          ) : null}
          <details data-boot-details>
            <summary className="cursor-pointer select-none text-xs font-medium">
              Details
            </summary>
            <pre className="m-0 mt-1 whitespace-pre-wrap break-words font-mono text-xs leading-5 text-destructive">{'Page handler did not respond within ' +
              String(PAGE_HANDLER_TIMEOUT_MS / 1000) +
              's. Reload the panel or restart the browser.'}</pre>
          </details>
        </div>
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

export function MahoAiApp({store}: {store: MahoAiStore}) {
  useEffect(() => {
    return () => {
      store.dispose();
    };
  }, [store]);

  const [compactSurface, setCompactSurface] =
      useState<'chat'|'routines'>('chat');
  const routineSurfaceClient = useMemo(
      () => store.getRoutineSurfaceClient(
          surface => setCompactSurface(surface.surface)),
      [store]);
  const routineClient =
      useMemo(() => store.getRoutineOperationsClient(), [store]);
  const state = useAppState(store);
  const bootstrappedRef = useRef(false);
  const [handlerTimedOut, setHandlerTimedOut] = React.useState(false);

  useEffect(() => {
    void routineSurfaceClient.initialize().catch((error: unknown) => {
      console.error('[maho-ai] Routine surface handoff failed:', error);
    });
    return () => routineSurfaceClient.dispose();
  }, [routineSurfaceClient]);
  useEffect(() => {
    (window as unknown as Record<string, unknown>)['__mahoAiMounted'] = true;
    applyTheme("auto");
    return watchAutoTheme((resolved) => applyTheme(resolved));
  }, []);

  useEffect(() => {
    void store.refreshAISettings();
    return bindComposerDraftLifecycle(
        document,
        () => void store.flushComposerDraft(),
        () => void store.refreshAISettings());
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

  const runSubmission = useCallback((submission: Promise<void>) => {
    void submission.catch((error: unknown) => {
      toast.error(error instanceof Error ? error.message : String(error));
    });
  }, []);

  const retryBootstrap = useCallback(() => {
    void bootstrapStore(store)
      .then(() => setHandlerTimedOut(false))
      .catch((error: unknown) => {
        console.error('[maho-ai] Bootstrap retry failed:', error);
      });
  }, [store]);

  const handleComposerSubmit = useCallback(() => {
    const stateSnapshot = store.getSnapshot();
    const prompt = stateSnapshot.composer.prompt.trim();

    if (prompt.startsWith('/')) {
      const parts = prompt.split(/\s+/);
      const cmd = parts[0].toLowerCase();

      if (cmd === '/agent') {
        const sub = parts[1]?.toLowerCase();
        if (sub === 'new') {
          store.setActiveSlashEditor('agent-new');
          store.setComposerPrompt('');
          return;
        } else if (sub === 'switch') {
          const name = parts.slice(2).join(' ').trim();
          if (name) {
            const targetProfile = stateSnapshot.profiles.find(p => p.name.toLowerCase() === name.toLowerCase());
            if (targetProfile && stateSnapshot.activeWorkspace) {
              void store.switchWorkspaceProfile(stateSnapshot.activeWorkspace.id, targetProfile.id);
            } else {
              toast.error(`Agent profile "${name}" not found`);
            }
          } else {
            toast.error('Usage: /agent switch <name>');
          }
          store.setComposerPrompt('');
          return;
        } else {
          toast.error('Usage: /agent new | /agent switch <name>');
          store.setComposerPrompt('');
          return;
        }
      } else if (cmd === '/tools') {
        const sub = parts[1]?.toLowerCase();
        const target = parts[2]?.toLowerCase();
        if (sub === 'add') {
          if (target === 'mcp') {
            store.setActiveSlashEditor('mcp-add');
            store.setComposerPrompt('');
            return;
          } else if (target === 'cli') {
            store.setActiveSlashEditor('cli-add');
            store.setComposerPrompt('');
            return;
          } else {
            toast.error('Usage: /tools add mcp | /tools add cli');
            store.setComposerPrompt('');
            return;
          }
        } else {
          toast.error('Usage: /tools add mcp | /tools add cli');
          store.setComposerPrompt('');
          return;
        }
      } else if (cmd === '/workspace') {
        const sub = parts[1]?.toLowerCase();
        if (sub === 'import') {
          const path = parts.slice(2).join(' ').trim();
          if (path && stateSnapshot.activeWorkspace) {
            void store.importProfileFromToml(stateSnapshot.activeWorkspace.id, path);
          } else {
            toast.error('Usage: /workspace import <path>');
          }
          store.setComposerPrompt('');
          return;
        } else {
          toast.error('Usage: /workspace import <path>');
          store.setComposerPrompt('');
          return;
        }
      } else {
        toast.error(`Unknown command "${cmd}". Available: /agent, /tools, /workspace`);
        store.setComposerPrompt('');
        return;
      }
    }

    if (!store.getSnapshot().currentSessionId) {
      runSubmission(store.startSessionThenSubmit());
      return;
    }

    runSubmission(store.submitPrompt());
  }, [runSubmission, store]);

  const handleRegenerate = useCallback(() => {
    runSubmission(store.regenerateLastResponse());
  }, [runSubmission, store]);

  const handleVoiceFinal = useCallback((transcript: string, controls: VoiceFinalControls) => {
    const text = transcript.trim();
    if (!text) {
      store.setVoiceError("Didn't catch that");
      return;
    }

    store.setComposerPrompt(text);
    handleComposerSubmit();
    void controls.stop().then(() => {
      store.closeVoice();
    });
  }, [handleComposerSubmit, store]);

  const {start: startVoiceInput, stop: stopVoiceInput} = useVoiceInput({
    store,
    onFinalTranscript: handleVoiceFinal,
  });

  const handleOpenVoice = useCallback(() => {
    store.openVoice();
    void startVoiceInput();
  }, [startVoiceInput, store]);

  const handleCloseVoice = useCallback(() => {
    void stopVoiceInput().then(() => {
      store.closeVoice();
    });
  }, [stopVoiceInput, store]);

  const composerHandlers = useMemo<ComposerHandlers>(() => ({
    onPromptChange: (value: string) => store.setComposerPrompt(value),
    onCancel: () => {
      void store.cancelTurn();
    },
    onComposerBlur: () => {
      void store.flushComposerDraft();
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
    onSetAISelection: (selection: AISelectionRequest) => {
      return store.setDefaultAISelection(selection);
    },
    onOpenSettings: () => {
      void store.openSettings();
    },
    onOpenVoice: handleOpenVoice,
    onSubmit: handleComposerSubmit,
  }), [handleComposerSubmit, handleOpenVoice, store]);

  const modeHandlers = useMemo(() => ({
    onNewSession: () => {
      void store.startSession();
    },
    onClosePanel: () => {
      void store.closePanel();
    },
    onOpenSettings: (paneKey?: string) => {
      void store.openSettings(paneKey);
    },
    onConnectMcp: () => {
      void store.openSettings('maho-ai-developers');
    },
    onGetViewMode: () => store.getViewMode(),
    onResumeSession: (sessionId: string) => {
      void store.resumeSession(sessionId);
    },
    onRespondToApproval: (approvalId: string, approved: boolean) => {
      void store.respondToApproval(approvalId, approved).then(dispatched => {
        if (!dispatched) {
          toast.error(
              approved ? "Couldn't send your approval — the action was not allowed."
                       : "Couldn't send your denial — the action was not stopped.");
        }
      });
    },
    onSearch: (query: string) => store.setSessionSearchQuery(query),
    onSelectInspector: (kind: AppState['selectedInspector']['kind'], id?: string) =>
      store.setSelectedInspector(kind, id),
    onSetDeveloperMode: (enabled: boolean) => store.setDeveloperMode(enabled),
    onSetViewMode: (mode: ViewMode) => {
      void store.setViewMode(mode);
    },
    onSetAISelection: (selection: AISelectionRequest) => {
      return store.setDefaultAISelection(selection);
    },
  }), [store]);

  const sessionEvents = state.currentSessionId ?
      (state.eventsBySessionId[state.currentSessionId] || []) :
      [];
  const conversationItems = useMemo(
      () => collectConversationItems(sessionEvents),
      [sessionEvents]);
  const hasMessages = conversationItems.length > 0;
  const currentSession = state.currentSessionId ? state.sessionsById[state.currentSessionId] : null;
  const readOnly = isSessionReadOnly(currentSession);
  const pendingTurn = !!(state.currentSessionId && state.turnPendingBySessionId[state.currentSessionId]);
  const thinkingLabel = getPendingThreadLabel(sessionEvents, pendingTurn);

  if (handlerTimedOut && !state.bootError) {
    return <PageHandlerTimeoutCard onRetry={retryBootstrap} />;
  }

  return (
    <div className="maho-ai-page-surface flex h-screen min-h-0 flex-col overflow-hidden rounded-l-xl border border-border bg-background/90 text-foreground shadow-[var(--shadow-overlay)] backdrop-blur-2xl">
      <section className="grid h-full min-h-0 min-w-0 grid-rows-[minmax(0,1fr)_auto] gap-2 p-2.5 max-[400px]:p-2">
        <div hidden={compactSurface !== 'routines'}>
          <CompactTopbar
            store={store}
            onClosePanel={modeHandlers.onClosePanel}
            onGetViewMode={modeHandlers.onGetViewMode}
            onOpenRoutines={() => setCompactSurface('routines')}
            onOpenSettings={modeHandlers.onOpenSettings}
            onResumeSession={modeHandlers.onResumeSession}
            onSetViewMode={modeHandlers.onSetViewMode}
            onStartSession={() => {
              setCompactSurface('chat');
              modeHandlers.onNewSession();
            }}
            routinesActive
          />
          <RoutineWorkspace
            client={routineClient}
            onBackToChat={() => setCompactSurface('chat')}
          />
        </div>
        {compactSurface === 'chat' ? (state.bootError ? <BootErrorCard message={state.bootError} onRetry={retryBootstrap} /> : (
          <>
            <div className={cn(
                'min-h-0 min-w-0 overflow-hidden',
                state.developerMode
                    ? 'block'
                    : 'grid grid-cols-[minmax(0,1fr)] grid-rows-[auto_auto_minmax(0,1fr)] gap-2')}>
              <SlashEditors store={store} />
              <ControlActivityTimeline />
              {state.developerMode ? (
                <DeveloperWorkspace
                  onConnect={modeHandlers.onConnectMcp}
                  onNewSession={modeHandlers.onNewSession}
                  onOpenSettings={modeHandlers.onOpenSettings}
                  onRespondToApproval={modeHandlers.onRespondToApproval}
                  onResumeSession={modeHandlers.onResumeSession}
                  onSearch={modeHandlers.onSearch}
                  onSelectInspector={modeHandlers.onSelectInspector}
                  onSetDeveloperMode={modeHandlers.onSetDeveloperMode}
                  state={state}
                />
              ) : (
                <CompactShell
                  store={store}
                  entries={sessionEvents}
                  hasMessages={hasMessages}
                  onClosePanel={modeHandlers.onClosePanel}
                  onConnect={modeHandlers.onConnectMcp}
                  onGetViewMode={modeHandlers.onGetViewMode}
                  onOpenRoutines={() => setCompactSurface('routines')}
                  onOpenSettings={modeHandlers.onOpenSettings}
                  onRegenerate={handleRegenerate}
                  onRespondToApproval={modeHandlers.onRespondToApproval}
                  onResumeSession={modeHandlers.onResumeSession}
                  onSetViewMode={modeHandlers.onSetViewMode}
                  onStartSession={() => {
                    setCompactSurface('chat');
                    modeHandlers.onNewSession();
                  }}
                  readOnly={readOnly}
                  thinkingLabel={thinkingLabel}
                />
              )}
            </div>
            <div className="z-10 min-w-0 shrink-0" data-composer-dock>
              <Composer
                commandDisclosure={
                  <CommandHints
                    disabled={readOnly || pendingTurn}
                    onPick={command => store.setComposerPrompt(command)}
                  />
                }
                handlers={composerHandlers}
                permissionControl={<RuntimeConfigCard store={store} />}
                state={state}
              />
            </div>
          </>
        )) : null}
      </section>
      {state.voice.active ? (
        <VoiceInputModal
          state={state}
          onClose={handleCloseVoice}
          onRetry={handleOpenVoice}
        />
      ) : null}
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
