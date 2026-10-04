// Standalone visual-preview harness for the agent side panel.
//
// Renders the real CompactShell / Composer components against an in-memory
// mock page handler so the panel can be reviewed at its production widths
// without a Chromium build. Dev-only: never referenced by BUILD.gn.
import {useEffect, useState} from 'react';
import {createRoot} from 'react-dom/client';

import {
  PageCallbackRouter,
  PageHandlerRemote,
  RuntimeConnectionState,
  SessionStatus,
} from './maho_ai.mojom-webui.js';
import {MahoAiStore} from '../store.js';
import {CompactShell} from '../react/features/compact/compact-shell.js';
import {ConversationMessage} from '../react/features/compact/conversation-message.js';
import {PendingThought} from '../react/features/compact/pending-thought.js';
import {Composer} from '../react/features/compact/composer.js';
import {CommandHints} from '../react/features/compact/command-hints.js';
import {RuntimeConfigCard} from '../react/features/compact/runtime-config-card.js';
import {useAppState} from '../react/hooks/use-app-state.js';
import {TooltipProvider} from '@ui/tooltip';
import {applyTheme} from '@theme/apply_theme';

const WIDTHS = [380, 420, 500] as const;

function noop(): void {}

function createRoute() {
  return {addListener: () => 1, removeListener: () => true};
}

// A route that actually delivers, so the preview can exercise the real
// observer path in MahoAiStore instead of a no-op stub.
function createEmittingRoute<Args extends unknown[]>() {
  const listeners = new Map<number, (...args: Args) => void>();
  let nextId = 1;
  return {
    addListener(listener: (...args: Args) => void): number {
      const id = nextId++;
      listeners.set(id, listener);
      return id;
    },
    removeListener(id: number): boolean {
      return listeners.delete(id);
    },
    emit(...args: Args): void {
      for (const listener of [...listeners.values()]) {
        listener(...args);
      }
    },
  };
}

const runtimeConfigRoute = createEmittingRoute<[{
  permissionTier: string;
  finalConfirm: boolean;
  proactiveMode: boolean;
  mailReadAllowed: boolean;
}]>();

function createSession(sessionId: string) {
  return {
    sessionId,
    title: 'Cancel subscription and request refunds',
    summary: '',
    createdAt: 1,
    updatedAt: 1,
    adapterName: 'OpenCode',
    isActive: false,
    isReadOnly: false,
    status: SessionStatus.kIdle,
    eventCount: 0,
    toolCallCount: 0,
    runtimeSessionId: sessionId,
    lastRuntimeState: 'idle',
  };
}

function createMockStore(): MahoAiStore {
  const handler = new PageHandlerRemote() as PageHandlerRemote &
      Record<string, unknown>;
  const router = new PageCallbackRouter() as PageCallbackRouter &
      Record<string, unknown>;
  router.onRuntimeEvent = createRoute();
  router.onConnectionStateChanged = createRoute();
  router.onSessionUpdated = createRoute();
  router.onAISettingsChanged = createRoute();
  router.onAskMahoSessionAccepted = createRoute();
  router.onRoutineRunStatusChanged = createRoute();
  router.onRuntimeConfigChanged = runtimeConfigRoute;

  handler['getConnectionState'] = async () => ({
    state: RuntimeConnectionState.kConnected,
    activeAdapterName: 'OpenCode',
  });
  handler['getSessionList'] = async () => ({sessions: [createSession('s1')]});
  handler['getSessionHistory'] = async () => ({events: [], totalCount: 0});
  handler['resumeSession'] = async () => ({
    session: createSession('s1'),
    replayEvents: [],
  });
  handler['getRuntimeConfig'] = async () => ({
    config: {permissionTier: 'guard', finalConfirm: true, proactiveMode: false},
  });
  handler['setRuntimeConfig'] = async () => ({accepted: true});
  handler['getAiProfiles'] = async () => ({profilesJson: JSON.stringify([
    {id: 'default', name: 'Work', isActive: true},
  ])});
  handler['getAiWorkspaces'] = async () => ({workspacesJson: '[]'});
  handler['getCreditBalance'] = async () => ({
    info: {balanceUsd: 12.5, lastPurchaseAt: 0, lastConsumptionAt: 0},
  });
  handler['getAISettings'] = async () => ({
    info: {
      activeProviderId: 'anthropic',
      activeModelId: 'claude-sonnet-4.5',
      activeReasoningEffort: 2,
      providerOptions: [
        {
          id: 'anthropic',
          label: 'Anthropic',
          modelOptions: [
            {id: 'claude-sonnet-4.5', label: 'Sonnet 4.5'},
            {id: 'claude-opus-4.1', label: 'Opus 4.1'},
          ],
        },
      ],
      reasoningOptions: [
        {effort: 1, label: 'Low'},
        {effort: 2, label: 'Medium'},
        {effort: 3, label: 'High'},
      ],
    },
  });

  return new MahoAiStore(handler, router);
}

function PanelPreview({store, width}: {store: MahoAiStore; width: number}) {
  const state = useAppState(store);
  const handlers = {
    onCancel: noop,
    onComposerBlur: noop,
    onPromptChange: (value: string) => store.setComposerPrompt(value),
    onLoadOpenTabs: noop,
    onResetHistorySearch: noop,
    onSearchHistory: noop,
    onToggleBrowserContext: noop,
    onToggleHistoryAttachment: noop,
    onToggleTabAttachment: noop,
    onAttachFiles: noop,
    onRequestFileChooser: noop,
    onRemoveAttachment: noop,
    onSetAISelection: async () => true,
    onOpenSettings: noop,
    onOpenVoice: noop,
    onSubmit: noop,
  } as unknown as Parameters<typeof Composer>[0]['handlers'];

  return (
    <figure className="m-0 flex flex-col items-center gap-2">
      <figcaption className="font-sans text-[11px] font-medium tracking-wide text-neutral-500">
        {width}px
      </figcaption>
      <div
        className="maho-ai-page-surface flex h-[760px] flex-col overflow-hidden rounded-xl border border-border bg-background/90 text-foreground shadow-[var(--shadow-overlay)] backdrop-blur-2xl"
        style={{width: `${width}px`}}>
        <section className="grid h-full min-h-0 min-w-0 grid-rows-[minmax(0,1fr)_auto] gap-2 p-2.5">
          <div className="grid min-h-0 min-w-0 grid-cols-[minmax(0,1fr)] grid-rows-[auto_auto_minmax(0,1fr)] gap-2 overflow-hidden">
            <div />
            <div />
            <CompactShell
              store={store}
              entries={[]}
              hasMessages={false}
              onClosePanel={noop}
              onGetViewMode={() => Promise.resolve(0 as never)}
              onOpenSettings={noop}
              onRespondToApproval={noop}
              onSetViewMode={noop}
              onStartSession={noop}
              readOnly={false}
              thinkingLabel={null}
            />
          </div>
          <div className="z-10 min-w-0 shrink-0">
            <Composer
              commandDisclosure={<CommandHints disabled={false} onPick={noop} />}
              handlers={handlers}
              permissionControl={<RuntimeConfigCard store={store} />}
              state={state}
            />
          </div>
        </section>
      </div>
    </figure>
  );
}

// Static thread mock: exercises the running-task visuals (message bubbles,
// elapsed timer) without driving a live runtime.
function ThreadPreview({store, width}: {store: MahoAiStore; width: number}) {
  const state = useAppState(store);
  const handlers = {
    onCancel: noop,
    onComposerBlur: noop,
    onPromptChange: (value: string) => store.setComposerPrompt(value),
    onLoadOpenTabs: noop,
    onResetHistorySearch: noop,
    onSearchHistory: noop,
    onToggleBrowserContext: noop,
    onToggleHistoryAttachment: noop,
    onToggleTabAttachment: noop,
    onAttachFiles: noop,
    onRequestFileChooser: noop,
    onRemoveAttachment: noop,
    onSetAISelection: async () => true,
    onOpenSettings: noop,
    onOpenVoice: noop,
    onSubmit: noop,
  } as unknown as Parameters<typeof Composer>[0]['handlers'];

  return (
    <figure className="m-0 flex flex-col items-center gap-2">
      <figcaption className="font-sans text-[11px] font-medium tracking-wide text-neutral-500">
        {width}px · running
      </figcaption>
      <div
        className="maho-ai-page-surface flex h-[760px] flex-col overflow-hidden rounded-xl border border-border bg-background/90 text-foreground shadow-[var(--shadow-overlay)] backdrop-blur-2xl"
        style={{width: `${width}px`}}>
        <section className="grid h-full min-h-0 min-w-0 grid-rows-[minmax(0,1fr)_auto] gap-2 p-2.5">
          <div className="grid min-h-0 min-w-0 grid-rows-[minmax(0,1fr)] overflow-y-auto">
            <div className="grid content-start gap-3">
              <ConversationMessage
                item={{
                  id: 'm1',
                  role: 'user',
                  text: 'Cancel my subscription and request a refund',
                  markdown: false,
                } as never} />
              <ConversationMessage
                item={{
                  id: 'm2',
                  role: 'assistant',
                  markdown: true,
                  text: 'I found the billing page and located your active plan.\n\n- Opened **Account → Billing**\n- Located the *Pro* subscription\n- Preparing the cancellation request\n\nI\'ll ask before submitting anything irreversible.',
                } as never} />
              <PendingThought label="Submitting the refund request" />
            </div>
          </div>
          <div className="z-10 min-w-0 shrink-0">
            <Composer
              handlers={handlers}
              permissionControl={<RuntimeConfigCard store={store} />}
              state={state}
            />
          </div>
        </section>
      </div>
    </figure>
  );
}

// Measures real horizontal overflow for each rendered panel and prints the
// result into the DOM, so it can be read back through the Maho CLI (which has
// no layout-metrics capability and no JS eval by design).
function OverflowProbe() {
  const [report, setReport] = useState<string>('measuring');
  useEffect(() => {
    const measure = () => {
      const rows: string[] = [];
      for (const figure of document.querySelectorAll('figure')) {
        const caption = figure.querySelector('figcaption')?.textContent?.trim();
        const surface = figure.querySelector('.maho-ai-page-surface');
        if (!caption || !(surface instanceof HTMLElement)) {
          continue;
        }
        // Any descendant wider than its own client box means a clipped or
        // scrolling row, which is the defect this criterion forbids.
        let worst = 0;
        let worstTag = '-';
        let worstDetail = '';
        for (const node of surface.querySelectorAll('*')) {
          if (!(node instanceof HTMLElement)) {
            continue;
          }
          const delta = node.scrollWidth - node.clientWidth;
          if (delta > worst) {
            worst = delta;
            worstTag = node.dataset.composer !== undefined
                ? '[data-composer]'
                : node.tagName.toLowerCase();
            // Distinguish intentional ellipsis truncation from real clipping.
            const style = getComputedStyle(node);
            const truncated = style.textOverflow === 'ellipsis' &&
                style.overflow !== 'visible';
            worstDetail = `${truncated ? 'ELLIPSIS' : 'CLIPPED'}:"${
                (node.textContent || '').trim().slice(0, 24)}"`;
          }
        }
        const surfaceDelta = surface.scrollWidth - surface.clientWidth;
        rows.push(`${caption} surface=${surfaceDelta}px worst=${worst}px(${
            worstTag} ${worstDetail})`);
      }
      setReport(rows.join(' | ') || 'no panels found');
    };
    const raf = requestAnimationFrame(() => setTimeout(measure, 300));
    return () => cancelAnimationFrame(raf);
  }, []);
  return (
    <p
      data-overflow-report
      className="w-full font-mono text-[12px] text-neutral-400">
      OVERFLOW {report}
    </p>
  );
}

function App({store}: {store: MahoAiStore}) {
  return (
    <TooltipProvider delayDuration={200}>
      <div className="flex flex-wrap items-start justify-center gap-6 p-6">
        {WIDTHS.map(width => (
          <PanelPreview key={width} store={store} width={width} />
        ))}
        <ThreadPreview store={store} width={420} />
        <OverflowProbe />
      </div>
    </TooltipProvider>
  );
}

const theme =
    new URLSearchParams(location.search).get('theme') === 'light' ? 'light' : 'dark';
applyTheme(theme);
document.body.style.background = theme === 'light' ? '#e9e9ec' : '#131315';

const rootElement = document.getElementById('app');
if (!rootElement) {
  throw new Error('Missing #app root for the panel preview.');
}
const store = createMockStore();
void store.bootstrap()
    .then(() => store.refreshAISettings())
    .finally(() => {
      createRoot(rootElement).render(<App store={store} />);

      // `?extTier=<tier>` simulates chrome://maho-settings writing the shared
      // pref: the browser pushes OnRuntimeConfigChanged and the panel must
      // re-render from the observer alone, with no user interaction here.
      const externalTier =
          new URLSearchParams(location.search).get('extTier');
      if (externalTier) {
        setTimeout(() => {
          runtimeConfigRoute.emit({
            permissionTier: externalTier,
            finalConfirm: false,
            proactiveMode: true,
            mailReadAllowed: true,
          });
        }, 500);
      }
    });
