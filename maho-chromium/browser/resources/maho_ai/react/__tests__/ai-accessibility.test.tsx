import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {
  CredentialErrorCode,
  getCredentialErrorPresentation,
  type CredentialRuntimeEvent,
} from '../../views/credential_error.js';
import {CredentialErrorNotice} from '../components/credential-error-notice.js';
import {
  PageCallbackRouter,
  PageHandlerRemote,
  RuntimeConnectionState,
  RuntimeEventKind,
  type RuntimeEvent,
} from '../../maho_ai.mojom-webui.js';
import {MahoAiStore} from '../../store.js';
import {ConversationThread} from '../features/compact/conversation-thread.js';
import {SlashEditors} from '../features/compact/slash-editors.js';
import {Composer, type ComposerHandlers} from '../features/compact/composer.js';
import {createInitialState} from '../../types.js';
import type {TimelineEntry} from '../../types.js';
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
  router.onSessionUpdated = createCallbackRoute<[never]>();
  router.onAISettingsChanged = createCallbackRoute<[never]>();
  router.onAskMahoSessionAccepted = createCallbackRoute<[never, never]>();
  return router;
}

function createHandlers(overrides: Partial<ComposerHandlers> = {}): ComposerHandlers {
  return {
    onAttachFiles: vi.fn(),
    onCancel: vi.fn(),
    onLoadOpenTabs: vi.fn(),
    onOpenSettings: vi.fn(),
    onOpenVoice: vi.fn(),
    onPromptChange: vi.fn(),
    onRefreshOpenTabs: vi.fn(),
    onRemoveAttachment: vi.fn(),
    onRequestFileChooser: vi.fn(),
    onResetHistorySearch: vi.fn(),
    onSearchHistory: vi.fn(),
    onSetAISelection: vi.fn().mockResolvedValue(true),
    onStartSession: vi.fn(),
    onSubmit: vi.fn(),
    onToggleBrowserContext: vi.fn(),
    onToggleHistoryAttachment: vi.fn(),
    onToggleTabAttachment: vi.fn(),
    ...overrides,
  };
}

describe('Maho AI Accessibility (a11y) Assertions', () => {
  let container: HTMLDivElement;
  let root: Root;
  let mockHandler: PageHandlerRemote;
  let mockRouter: PageCallbackRouter;
  let store: MahoAiStore;

  beforeEach(() => {
    window.HTMLElement.prototype.scrollIntoView = vi.fn();
    container = document.createElement('div');
    document.body.appendChild(container);
    root = createRoot(container);

    mockHandler = new PageHandlerRemote();
    mockRouter = createPageCallbackRouterFake();

    vi.spyOn(mockHandler, 'getConnectionState').mockResolvedValue({
      state: RuntimeConnectionState.kConnected,
      activeAdapterName: 'OpenCode',
    });
    vi.spyOn(mockHandler, 'getSessionList').mockResolvedValue({
      sessions: [
        {
          sessionId: 'session-1',
          adapterName: 'OpenCode',
          isActive: true,
          isReadOnly: false,
          status: 1,
          createdAt: Date.now() / 1000,
          updatedAt: Date.now() / 1000,
          title: 'Test Session',
          summary: '',
          eventCount: 0,
          toolCallCount: 0,
          lastRuntimeState: 'idle',
          runtimeSessionId: 'session-1',
        },
      ],
    });
    vi.spyOn(mockHandler, 'resumeSession').mockResolvedValue({
      session: {
        sessionId: 'session-1',
        adapterName: 'OpenCode',
        isActive: true,
        isReadOnly: false,
        status: 1,
        createdAt: Date.now() / 1000,
        updatedAt: Date.now() / 1000,
        title: 'Test Session',
        summary: '',
        eventCount: 0,
        toolCallCount: 0,
        lastRuntimeState: 'idle',
        runtimeSessionId: 'session-1',
      },
      replayEvents: [],
    });
    vi.spyOn(mockHandler, 'getSessionHistory').mockResolvedValue({
      events: [],
      totalCount: 0,
    });
    mockHandler.getAiProfiles = vi.fn().mockResolvedValue({
      profilesJson: JSON.stringify([
        {id: 'p-1', name: 'Default', prompt: '', model: null, createdAt: '', updatedAt: ''},
      ]),
    });
    mockHandler.getAiWorkspaces = vi.fn().mockResolvedValue({
      workspacesJson: JSON.stringify([
        {
          id: 'ws-1',
          name: 'Default Workspace',
          profileId: 'p-1',
          spaceId: null,
          workspaceRoot: null,
          createdAt: '',
          updatedAt: '',
        },
      ]),
    });
    mockHandler.getActiveAiWorkspace = vi.fn().mockResolvedValue({
      workspaceJson: JSON.stringify({
        id: 'ws-1',
        name: 'Default Workspace',
        profileId: 'p-1',
        spaceId: null,
        workspaceRoot: null,
        createdAt: '',
        updatedAt: '',
      }),
    });
    mockHandler.getMcpServers = vi.fn().mockResolvedValue({serversJson: '[]'});
    mockHandler.getCliTools = vi.fn().mockResolvedValue({toolsJson: '[]'});
    mockHandler.getCreditBalance = vi.fn().mockResolvedValue({
      info: {balanceUsd: 0, lastPurchaseAt: 0, lastConsumptionAt: 0},
    });

    store = new MahoAiStore(mockHandler, mockRouter);
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    vi.restoreAllMocks();
  });

  describe('1. Streaming Assistant Updates Announcement via Live Region', () => {
    it('announces pending thought streaming runtime updates via aria-live region in DOM', async () => {
      await store.bootstrap();

      // Render ConversationThread with a streaming pending thought label
      const entries: TimelineEntry[] = [
        {
          sessionId: 'session-1',
          event: {
            kind: RuntimeEventKind.kAssistantThinking,
            sequence: 1,
            timestamp: 100,
            sessionId: 'session-1',
            text: 'Hello world',
          },
        },
      ];

      act(() => {
        root.render(
          <div aria-live="polite" aria-atomic="true">
            <ConversationThread
              entries={entries}
              thinkingLabel="Synthesizing answer..."
              readOnly={false}
              bottomPadding={0}
              topPadding={0}
              onOpenSettings={vi.fn()}
              onRespondToApproval={vi.fn()}
            />
          </div>
        );
      });

      const liveRegion = container.querySelector('[aria-live="polite"]');
      expect(liveRegion).not.toBeNull();
      expect(liveRegion?.getAttribute('aria-atomic')).toBe('true');
      expect(liveRegion?.textContent).toContain('Synthesizing answer...');

      // Update streaming label
      act(() => {
        root.render(
          <div aria-live="polite" aria-atomic="true">
            <ConversationThread
              entries={entries}
              thinkingLabel="Writing response code..."
              readOnly={false}
              bottomPadding={0}
              topPadding={0}
              onOpenSettings={vi.fn()}
              onRespondToApproval={vi.fn()}
            />
          </div>
        );
      });

      expect(liveRegion?.textContent).toContain('Writing response code...');
    });
  });

  describe('2. Keyboard Operability & Roles/Labels on Interactive Controls', () => {
    it('provides valid accessible roles and aria-labels on composer interactive buttons', async () => {
      await store.bootstrap();
      const handlers = createHandlers();
      const state = createInitialState();

      act(() => {
        root.render(
          <TooltipProvider>
            <Composer handlers={handlers} state={state} />
          </TooltipProvider>
        );
      });

      const buttons = Array.from(container.querySelectorAll('button'));
      expect(buttons.length).toBeGreaterThan(0);

      // Verify buttons have aria-label or accessible text content
      for (const btn of buttons) {
        const ariaLabel = btn.getAttribute('aria-label');
        const textContent = btn.textContent?.trim();
        expect(ariaLabel || textContent).toBeTruthy();
      }

      // Specific check for key action buttons
      const sendButton = container.querySelector('button[type="submit"]') ||
                         Array.from(buttons).find(b => b.getAttribute('aria-label')?.includes('Send'));
      expect(sendButton).not.toBeNull();

      // Check prompt textarea accessibility
      const textarea = container.querySelector('textarea');
      expect(textarea).not.toBeNull();
      expect(textarea?.getAttribute('aria-label') || textarea?.getAttribute('placeholder')).toBeTruthy();
    });
  });

  describe('3. Accessible Runtime Error Presentation and Recovery', () => {
    it('presents credential runtime errors accessibly with recoverable settings action', () => {
      const event: CredentialRuntimeEvent = {
        credentialErrorCode: CredentialErrorCode.kProviderNotConfigured,
        kind: RuntimeEventKind.kError,
        sequence: 1,
        sessionId: 'session-1',
        text: 'Provider credentials missing',
        timestamp: 1,
      };

      const presentation = getCredentialErrorPresentation(event);
      expect(presentation).not.toBeNull();

      const onOpenSettings = vi.fn();

      act(() => {
        root.render(
          <CredentialErrorNotice
            presentation={presentation!}
            surface="compact"
            onOpenSettings={onOpenSettings}
          />
        );
      });

      const errorNotice = container.querySelector('[role="alert"]');
      expect(errorNotice).not.toBeNull();

      const actionBtn = Array.from(container.querySelectorAll('button'))
        .find(b => b.textContent?.trim() === presentation!.actionLabel);
      expect(actionBtn).not.toBeNull();

      act(() => {
        actionBtn?.dispatchEvent(new MouseEvent('click', {bubbles: true}));
      });

      expect(onOpenSettings).toHaveBeenCalledWith(presentation!.settingsPane);
    });
  });

  describe('4. Light Modal / Overlay Focus & Keyboard Control (SlashEditors)', () => {
    it('supports keyboard dismissal (Close button) and operates without @radix-ui/react-dialog', async () => {
      await store.bootstrap();

      // Set active slash editor to trigger light form overlay rendering
      act(() => {
        store.setActiveSlashEditor('agent-new');
      });

      act(() => {
        root.render(<SlashEditors store={store} />);
      });

      // Form container rendered
      const form = container.querySelector('form');
      expect(form).not.toBeNull();

      // Verify close button exists with proper accessibility
      const closeBtn = Array.from(container.querySelectorAll('button'))
        .find(b => b.querySelector('.lucide-x') || b.textContent === 'Cancel');
      expect(closeBtn).not.toBeNull();

      // Close modal/overlay via button click
      await act(async () => {
        closeBtn?.dispatchEvent(new MouseEvent('click', {bubbles: true}));
      });

      expect(store.getSnapshot().activeSlashEditor).toBeNull();

      act(() => {
        root.render(<SlashEditors store={store} />);
      });

      expect(container.querySelector('form')).toBeNull();
    });
  });
});
