import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';
import {toast} from 'sonner';

import {ErrorDetail} from '../components/error-detail.js';
import {ConversationSystemItem} from '../features/compact/conversation-system-item.js';
import {ConversationThread} from '../features/compact/conversation-thread.js';
import {CompactShell} from '../features/compact/compact-shell.js';
import {EventCard} from '../features/developer/event-card.js';
import {collectConversationItems, type ConversationItem} from '../../views/conversation_thread.js';
import {
  PageCallbackRouter,
  PageHandlerRemote,
  RuntimeConnectionState,
  RuntimeEventKind,
  type AISettingsInfo,
  type RuntimeEvent,
  type SessionInfo,
} from '../../maho_ai.mojom-webui.js';
import {MahoAiStore} from '../../store.js';
import {ViewMode, type RuntimeEventRecord, type TimelineEntry} from '../../types.js';
import {TooltipProvider} from '@ui/tooltip';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

type Callback<TArgs extends readonly unknown[]> = (...args: TArgs) => void;

function createCallbackRoute<TArgs extends readonly unknown[]>(): {
  addListener(listener: Callback<TArgs>): void;
} {
  return {addListener: vi.fn()};
}

function createPageCallbackRouterFake(): PageCallbackRouter {
  const router = new PageCallbackRouter();
  router.onRuntimeEvent = createCallbackRoute<[RuntimeEvent]>();
  router.onConnectionStateChanged = createCallbackRoute<[RuntimeConnectionState]>();
  router.onSessionUpdated = createCallbackRoute<[SessionInfo]>();
  router.onAISettingsChanged = createCallbackRoute<[AISettingsInfo]>();
  router.onAskMahoSessionAccepted = createCallbackRoute<[string, SessionInfo]>();
  return router;
}

function createMockStore(): MahoAiStore {
  const handler = new PageHandlerRemote();
  const router = createPageCallbackRouterFake();
  return new MahoAiStore(handler, router);
}

describe('ErrorDetail component and AI agent panel outputs absence', () => {
  let container: HTMLDivElement;
  let root: Root;

  beforeEach(() => {
    vi.useFakeTimers();
    container = document.createElement('div');
    document.body.appendChild(container);
    root = createRoot(container);
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    vi.useRealTimers();
    vi.restoreAllMocks();
  });

  describe('ErrorDetail component', () => {
    it('renders the default label and full untruncated error text', () => {
      const fullError = 'Detailed stack trace:\n  at Object.execute (agent.ts:42:15)\n  at processTicksAndRejections (node:internal/process/task_queues:95:5)\nError: Service unavailable with payload {"status": 503, "retryAfter": 30}';
      act(() => {
        root.render(<ErrorDetail text={fullError} />);
      });

      expect(container.textContent).toContain('Error details');
      const pre = container.querySelector('[data-testid="error-detail"]');
      expect(pre).not.toBeNull();
      expect(pre?.textContent).toBe(fullError);
    });

    it('renders a custom label when provided', () => {
      const errorText = 'Connection failed: ECONNREFUSED 127.0.0.1:8080';
      act(() => {
        root.render(<ErrorDetail label="Diagnostic trace" text={errorText} />);
      });

      expect(container.textContent).toContain('Diagnostic trace');
      const pre = container.querySelector('[data-testid="error-detail"]');
      expect(pre?.textContent).toBe(errorText);
    });

    it('copies the exact error text to clipboard when copy button is clicked', async () => {
      const fullError = 'CRITICAL: Failed to load model weights from https://api.provider.com/v1\nStatus: 502 Bad Gateway';
      const writeText = vi.fn<(text: string) => Promise<void>>().mockResolvedValue(undefined);
      Object.defineProperty(navigator, 'clipboard', {
        configurable: true,
        value: {writeText},
      });

      act(() => {
        root.render(<ErrorDetail text={fullError} />);
      });

      const copyBtn = container.querySelector<HTMLButtonElement>('[data-action="copy-error"]');
      expect(copyBtn).not.toBeNull();
      expect(copyBtn?.getAttribute('aria-label')).toBe('Copy error details');

      await act(async () => {
        copyBtn!.dispatchEvent(new MouseEvent('click', {bubbles: true}));
      });

      expect(writeText).toHaveBeenCalledOnce();
      expect(writeText).toHaveBeenCalledWith(fullError);
    });

    it('transitions to copied state and reverts after timeout', async () => {
      const fullError = 'TypeError: Cannot read properties of undefined';
      const writeText = vi.fn<(text: string) => Promise<void>>().mockResolvedValue(undefined);
      Object.defineProperty(navigator, 'clipboard', {
        configurable: true,
        value: {writeText},
      });

      act(() => {
        root.render(<ErrorDetail text={fullError} />);
      });

      const copyBtn = container.querySelector<HTMLButtonElement>('[data-action="copy-error"]')!;
      const liveStatus = container.querySelector('[role="status"]')!;

      expect(copyBtn.getAttribute('aria-label')).toBe('Copy error details');
      expect(liveStatus.textContent).toBe('');

      await act(async () => {
        copyBtn.dispatchEvent(new MouseEvent('click', {bubbles: true}));
      });

      expect(copyBtn.getAttribute('aria-label')).toBe('Copied');
      expect(liveStatus.textContent).toBe('Copied');

      await act(async () => {
        await vi.advanceTimersByTimeAsync(1800);
      });

      expect(copyBtn.getAttribute('aria-label')).toBe('Copy error details');
      expect(liveStatus.textContent).toBe('');
    });

    it('handles clipboard failure gracefully by notifying user and updating status', async () => {
      const fullError = 'SecurityError: Clipboard access denied';
      const writeText = vi.fn<(text: string) => Promise<void>>().mockRejectedValue(new Error('Permission denied'));
      Object.defineProperty(navigator, 'clipboard', {
        configurable: true,
        value: {writeText},
      });
      const toastError = vi.spyOn(toast, 'error').mockImplementation(() => '');
      const consoleError = vi.spyOn(console, 'error').mockImplementation(() => {});

      act(() => {
        root.render(<ErrorDetail text={fullError} />);
      });

      const copyBtn = container.querySelector<HTMLButtonElement>('[data-action="copy-error"]')!;
      const liveStatus = container.querySelector('[role="status"]')!;

      await act(async () => {
        copyBtn.dispatchEvent(new MouseEvent('click', {bubbles: true}));
      });

      expect(toastError).toHaveBeenCalledWith('Could not copy the error details to the clipboard.');
      expect(copyBtn.getAttribute('aria-label')).toBe('Copy failed');
      expect(liveStatus.textContent).toBe('Copy failed');
      expect(consoleError).toHaveBeenCalled();
    });

    it('preserves formatting with whitespace-pre-wrap and overflow-wrap:anywhere', () => {
      act(() => {
        root.render(<ErrorDetail text="line 1\n  indented line 2\n    indented line 3" />);
      });

      const pre = container.querySelector<HTMLPreElement>('[data-testid="error-detail"]')!;
      expect(pre.className).toContain('whitespace-pre-wrap');
      expect(pre.className).toContain('[overflow-wrap:anywhere]');
      expect(pre.className).toContain('font-mono');
      expect(pre.className).toContain('select-text');
    });
  });

  describe('Integration in conversation system item and developer workspace', () => {
    it('renders ErrorDetail inside ConversationSystemItem when errorDetail is present', async () => {
      const longError = 'HTTP 500 Internal Server Error: Database deadlock occurred';
      const item: ConversationItem = {
        errorDetail: longError,
        key: 'error-item-1',
        kind: 'activity',
        markdown: false,
        role: 'system',
        text: 'Something went wrong',
        timestamp: 1,
        tone: 'danger',
      };

      const writeText = vi.fn<(text: string) => Promise<void>>().mockResolvedValue(undefined);
      Object.defineProperty(navigator, 'clipboard', {
        configurable: true,
        value: {writeText},
      });

      act(() => {
        root.render(
          <ConversationSystemItem
            item={item}
            onOpenSettings={vi.fn()}
            onRespondToApproval={vi.fn()}
            readOnly={false}
          />
        );
      });

      const pre = container.querySelector('[data-testid="error-detail"]');
      expect(pre).not.toBeNull();
      expect(pre?.textContent).toBe(longError);

      const copyBtn = container.querySelector<HTMLButtonElement>('[data-action="copy-error"]');
      expect(copyBtn).not.toBeNull();
      await act(async () => {
        copyBtn!.dispatchEvent(new MouseEvent('click', {bubbles: true}));
      });
      expect(writeText).toHaveBeenCalledWith(longError);
    });

    it('renders ErrorDetail inside developer EventCard when event has runtime error', async () => {
      const devError = 'Runtime error: failed to initialize sandbox environment';
      const event: RuntimeEventRecord = {
        kind: RuntimeEventKind.kError,
        sequence: 1,
        sessionId: 'session-1',
        text: devError,
        timestamp: 1,
      };

      act(() => {
        root.render(
          <EventCard
            event={event}
            onOpenSettings={vi.fn()}
          />
        );
      });

      expect(container.textContent).toContain('Runtime error');
      const pre = container.querySelector('[data-testid="error-detail"]');
      expect(pre).not.toBeNull();
      expect(pre?.textContent).toBe(devError);

      const copyBtn = container.querySelector<HTMLButtonElement>('[data-action="copy-error"]');
      expect(copyBtn).not.toBeNull();
    });

    it('renders runtime errors via ConversationThread as ConversationSystemItem with ErrorDetail, NOT as ToolExecutionGroup', () => {
      const runtimeErrorText = 'no available accounts for this model';
      const event: RuntimeEventRecord = {
        kind: RuntimeEventKind.kError,
        sequence: 1,
        sessionId: 'session-1',
        text: runtimeErrorText,
        timestamp: 1000,
      };

      const entries: TimelineEntry[] = [{sessionId: 'session-1', event}];
      const items = collectConversationItems(entries);

      // Verify collectConversationItems does NOT set kind: 'activity' on generic kError
      expect(items[0]?.kind).toBeUndefined();
      expect(items[0]?.text).toBe('Something went wrong');
      expect(items[0]?.errorDetail).toBe(runtimeErrorText);

      act(() => {
        root.render(
          <ConversationThread
            bottomPadding={0}
            entries={entries}
            onOpenSettings={vi.fn()}
            onRegenerate={vi.fn()}
            onRespondToApproval={vi.fn()}
            readOnly={false}
            thinkingLabel={null}
            topPadding={0}
          />
        );
      });

      // Must NOT be grouped into ToolExecutionGroup / "Ran commands"
      expect(container.textContent).not.toContain('Ran commands');
      expect(container.querySelector('[data-testid="tool-execution-group"]')).toBeNull();

      // Must render ErrorDetail with the verbatim error message and copy button
      const pre = container.querySelector('[data-testid="error-detail"]');
      expect(pre).not.toBeNull();
      expect(pre?.textContent).toBe(runtimeErrorText);
      expect(container.querySelector('[data-action="copy-error"]')).not.toBeNull();
      expect(container.textContent).toContain('Something went wrong');
    });
  });

  describe('Verification that AI agent panel has NO "OUTPUTS" section', () => {
    it('verifies that CompactShell does not contain an Outputs section or header', () => {
      const store = createMockStore();
      const entries: TimelineEntry[] = [
        {
          sessionId: 'session-1',
          event: {
            kind: RuntimeEventKind.kUserPrompt,
            sequence: 1,
            sessionId: 'session-1',
            text: 'Hello Maho',
            timestamp: 1,
          },
        },
      ];

      act(() => {
        root.render(
          <TooltipProvider delayDuration={200}>
            <CompactShell
              store={store}
              entries={entries}
              hasMessages={true}
              onClosePanel={vi.fn()}
              onGetViewMode={vi.fn().mockResolvedValue(ViewMode.kSidebar)}
              onOpenSettings={vi.fn()}
              onRespondToApproval={vi.fn()}
              onSetViewMode={vi.fn()}
              onStartSession={vi.fn()}
              readOnly={false}
              thinkingLabel={null}
            />
          </TooltipProvider>
        );
      });

      // Assert that there is NO section or header for "Outputs"
      const allText = container.textContent || '';
      expect(allText).not.toMatch(/outputs\s*(\(\d+\))?/i);
      expect(allText).not.toContain('No outputs yet');

      // Verify no outputs-section elements exist
      expect(container.querySelector('[data-testid="outputs-section"]')).toBeNull();
      expect(container.querySelector('section[aria-label*="Output"]')).toBeNull();
    });

    it('verifies that empty stage does not contain an Outputs section', () => {
      const store = createMockStore();

      act(() => {
        root.render(
          <TooltipProvider delayDuration={200}>
            <CompactShell
              store={store}
              entries={[]}
              hasMessages={false}
              onClosePanel={vi.fn()}
              onGetViewMode={vi.fn().mockResolvedValue(ViewMode.kSidebar)}
              onOpenSettings={vi.fn()}
              onRespondToApproval={vi.fn()}
              onSetViewMode={vi.fn()}
              onStartSession={vi.fn()}
              readOnly={false}
              thinkingLabel={null}
            />
          </TooltipProvider>
        );
      });

      const allText = container.textContent || '';
      expect(allText).not.toMatch(/outputs/i);
      expect(allText).not.toContain('No outputs yet');
    });
  });
});
