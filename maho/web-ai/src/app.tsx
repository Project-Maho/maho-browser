/**
 * App — hash-based screen router.
 *
 * URL format:  #<screen>[?key=value&...]
 *
 * Non-initial screens are lazy-loaded to keep initial shell parse and load time minimal.
 * The initial shell loads only the minimal Chat UI synchronously.
 * The AI settings screen keeps the Phase A #byok route for native navigation.
 * to be filled in during Phases B and C.
 */
import { Fragment, type ComponentChildren } from 'preact';
import { Suspense, lazy } from 'preact/compat';
import { useLayoutEffect, useRef } from 'preact/hooks';
import { useHashRoute } from './hooks/use-hash-route';
import { ChatScreen } from './screens/chat/chat-screen';
import { Icon } from './ui/icon';

/**
 * Callback signature for render profiling measurements.
 * Follows the standard React Profiler `onRender` callback interface.
 *
 * @param id The string identifier of the component tree being measured.
 * @param phase Identifies whether the component tree just mounted ('mount') or re-rendered ('update').
 * @param actualDuration Time in milliseconds spent rendering the committed update.
 * @param baseDuration Estimated time in milliseconds to render the entire subtree without memoization.
 * @param startTime Timestamp when Preact began rendering this update.
 * @param commitTime Timestamp when Preact committed this update.
 */
export type ProfilerOnRenderCallback = (
  id: string,
  phase: 'mount' | 'update',
  actualDuration: number,
  baseDuration: number,
  startTime: number,
  commitTime: number
) => void;

/**
 * Props for the execution/mount performance tracer component.
 */
export interface ProfilerProps {
  /** Identifier for the component tree being measured. */
  id: string;
  /** Callback fired whenever the wrapped component tree mounts or updates. */
  onRender: ProfilerOnRenderCallback;
  /** Children elements to measure. */
  children: ComponentChildren;
}

/**
 * Execution/mount performance tracer component.
 *
 * Preact core and `preact/compat` do not export a built-in `Profiler` component.
 * This component acts as Maho's lightweight execution and mount performance tracer,
 * capturing sub-millisecond component render timings via `useLayoutEffect` and
 * `performance.now()` with zero production overhead when profiling is inactive.
 */
export function Profiler({ id, onRender, children }: ProfilerProps) {
  const startTimeRef = useRef<number>(0);
  startTimeRef.current = typeof performance !== 'undefined' ? performance.now() : Date.now();
  const isMountRef = useRef<boolean>(true);

  useLayoutEffect(() => {
    const commitTime = typeof performance !== 'undefined' ? performance.now() : Date.now();
    const actualDuration = commitTime - startTimeRef.current;
    const phase = isMountRef.current ? 'mount' : 'update';
    isMountRef.current = false;
    onRender(id, phase, actualDuration, actualDuration, startTimeRef.current, commitTime);
  });

  return <Fragment>{children}</Fragment>;
}

export const logRenderProfile: ProfilerOnRenderCallback = (
  id,
  phase,
  actualDuration,
  baseDuration,
  startTime,
  commitTime
) => {
  if (typeof window !== 'undefined' && Array.isArray(window.__MAHO_PROFILES__)) {
    window.__MAHO_PROFILES__.push({
      id,
      phase,
      actualDuration,
      baseDuration,
      startTime,
      commitTime,
    });
  }
  if (typeof console !== 'undefined' && console.debug) {
    console.debug(`[Profiler] <${id}> [${phase}] actual=${actualDuration.toFixed(2)}ms base=${baseDuration.toFixed(2)}ms`);
  }
};

export function isProfilingEnabled(): boolean {
  try {
    if (typeof import.meta !== 'undefined' && (import.meta.env?.DEV || import.meta.env?.VITE_ENABLE_PROFILING === 'true')) {
      return true;
    }
  } catch {}
  try {
    if (typeof process !== 'undefined' && (process.env?.NODE_ENV === 'development' || process.env?.ENABLE_PROFILING === 'true')) {
      return true;
    }
  } catch {}
  if (typeof window !== 'undefined') {
    if (window.__MAHO_PROFILING__ || window.__ENABLE_PROFILING__) {
      return true;
    }
    try {
      if (window.localStorage?.getItem('maho_profiling') === 'true') {
        return true;
      }
    } catch {}
  }
  return false;
}

const AgentScreen = lazy(() =>
  import('./screens/agent/agent-screen').then((m) => ({ default: m.AgentScreen }))
);
const AiSettingsScreen = lazy(() =>
  import('./screens/byok/byok-screen').then((m) => ({ default: m.AiSettingsScreen }))
);
const ConversationListScreen = lazy(() =>
  import('./screens/conversations/conversation-list-screen').then((m) => ({ default: m.ConversationListScreen }))
);
const OnboardingScreen = lazy(() =>
  import('./screens/onboarding/onboarding-screen').then((m) => ({ default: m.OnboardingScreen }))
);

declare global {
  interface Window {
    MahoBridgeAndroid?: { onBack?: () => void };
    webkit?: { messageHandlers?: { mahoBridgeNav?: { postMessage: (message: string) => void } } };
    __MAHO_PROFILING__?: boolean;
    __ENABLE_PROFILING__?: boolean;
    __MAHO_PROFILES__?: Array<{
      id: string;
      phase: 'mount' | 'update';
      actualDuration: number;
      baseDuration: number;
      startTime: number;
      commitTime: number;
    }>;
  }
}

export function App({ onRender = logRenderProfile }: { onRender?: ProfilerOnRenderCallback } = {}) {
  const route = useHashRoute();

  const handleBack = () => {
    if (window.MahoBridgeAndroid?.onBack) {
      window.MahoBridgeAndroid.onBack();
    } else if (window.webkit?.messageHandlers?.mahoBridgeNav) {
      window.webkit.messageHandlers.mahoBridgeNav.postMessage('back');
    } else {
      window.history.back();
    }
  };

  const renderScreen = () => {
    switch (route.name) {
      case 'onboarding':
        return (
          <Suspense fallback={<ScreenFallback />}>
            <OnboardingScreen />
          </Suspense>
        );
      case 'byok':
        return (
          <Suspense fallback={<ScreenFallback onBack={handleBack} />}>
            <AiSettingsScreen onBack={handleBack} />
          </Suspense>
        );
      case 'agent':
        return (
          <Suspense fallback={<ScreenFallback onBack={handleBack} />}>
            <AgentScreen goal={route.params.goal} onBack={handleBack} />
          </Suspense>
        );
      case 'chat':
        return <ChatScreen onBack={handleBack} sessionId={route.params.sessionId} />;
      case 'conversations':
        return (
          <Suspense fallback={<ScreenFallback onBack={handleBack} />}>
            <ConversationListScreen
              onBack={handleBack}
              onNavigateToChat={(id) => {
                window.location.hash = `#chat?sessionId=${id}`;
              }}
            />
          </Suspense>
        );

      // Out-of-scope mobile screens remain ComingSoon
      case 'space-ai-config':
      case 'offline-model':
      case 'privacy':
      case 'pinch-summary':
      case 'visual-search-results':
        return (
          <ComingSoonScreen screenName={route.name} onBack={handleBack} />
        );

      default:
        return <ErrorScreen message={`Unknown screen: "${route.name}"`} onBack={handleBack} />;
    }
  };

  const screen = renderScreen();

  if (isProfilingEnabled()) {
    return (
      <Profiler id={`ScreenRouter:${route.name}`} onRender={onRender}>
        {screen}
      </Profiler>
    );
  }

  return screen;
}

// ─── Fallback screen ─────────────────────────────────────────────────────────

function ScreenFallback({ onBack }: { onBack?: () => void }) {
  return (
    <div
      class="flex flex-col items-center justify-center min-h-screen gap-4 p-8 text-center bg-[var(--color-surface)]"
      role="status"
      aria-label="Loading screen"
    >
      {onBack && (
        <button
          type="button"
          class="absolute top-4 left-4 p-2 rounded-xl hover:bg-[var(--color-surface-hover)] transition-colors"
          onClick={onBack}
          aria-label="Back"
        >
          <Icon name="chevron-left" size={20} aria-hidden />
        </button>
      )}
      <Icon name="loader" size={28} class="animate-spin text-[var(--color-on-surface-secondary)]" aria-hidden />
      <span class="sr-only">Loading…</span>
    </div>
  );
}

// ─── Placeholder screens ─────────────────────────────────────────────────────

function ComingSoonScreen({ screenName, onBack }: { screenName: string; onBack: () => void }) {
  return (
    <div class="flex flex-col items-center justify-center min-h-screen gap-4 p-8 text-center bg-[var(--color-surface)]">
      <button
        type="button"
        class="absolute top-4 left-4 p-2 rounded-xl hover:bg-[var(--color-surface-hover)] transition-colors"
        onClick={onBack}
        aria-label="Back"
      >
        <Icon name="chevron-left" size={20} aria-hidden />
      </button>
      <Icon name="bot" size={40} class="text-[var(--color-on-surface-secondary)]" aria-hidden />
      <h1 class="text-lg font-semibold text-[var(--color-on-surface)]">Coming Soon</h1>
      <p class="text-sm text-[var(--color-on-surface-secondary)]">
        <code>{screenName}</code> is being migrated. The native screen is still active.
      </p>
    </div>
  );
}

function ErrorScreen({ message, onBack }: { message: string; onBack: () => void }) {
  return (
    <div class="flex flex-col items-center justify-center min-h-screen gap-4 p-8 text-center bg-[var(--color-surface)]">
      <button
        type="button"
        class="absolute top-4 left-4 p-2 rounded-xl hover:bg-[var(--color-surface-hover)] transition-colors"
        onClick={onBack}
        aria-label="Back"
      >
        <Icon name="chevron-left" size={20} aria-hidden />
      </button>
      <Icon name="circle-alert" size={40} class="text-[var(--color-error)]" aria-hidden />
      <h1 class="text-lg font-semibold text-[var(--color-error)]">Navigation Error</h1>
      <p class="text-sm text-[var(--color-on-surface-secondary)]">{message}</p>
    </div>
  );
}
