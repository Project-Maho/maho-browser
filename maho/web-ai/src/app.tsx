/**
 * App — hash-based screen router.
 *
 * URL format:  #<screen>[?key=value&...]
 *
 * All screens are lazy-loaded to keep per-screen parse time minimal.
 * The BYOKScreen is the Phase A reference; remaining screens are stubs
 * to be filled in during Phases B and C.
 */
import { useHashRoute } from './hooks/use-hash-route';
import { AgentScreen } from './screens/agent/agent-screen';
import { BYOKScreen } from './screens/byok/byok-screen';
import { Icon } from './ui/icon';

declare global {
  interface Window {
    MahoBridgeAndroid?: { onBack?: () => void };
    webkit?: { messageHandlers?: { mahoBridgeNav?: { postMessage: (message: string) => void } } };
  }
}

export function App() {
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

  switch (route.name) {
    case 'byok':
      return <BYOKScreen onBack={handleBack} />;
    case 'agent':
      return <AgentScreen goal={route.params.goal} onBack={handleBack} />;

    // Phase B screens — stubs; native falls back to native screen until implemented.
    case 'chat':
    case 'conversations':
    case 'space-ai-config':
    case 'offline-model':
    case 'privacy':
    case 'visual-search-results':
      return (
        <ComingSoonScreen screenName={route.name} onBack={handleBack} />
      );

    default:
      return <ErrorScreen message={`Unknown screen: "${route.name}"`} onBack={handleBack} />;
  }
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
