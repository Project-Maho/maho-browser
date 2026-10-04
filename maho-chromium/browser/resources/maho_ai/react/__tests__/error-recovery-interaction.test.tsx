import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {
  CredentialErrorCode,
  getCredentialErrorPresentation,
  type CredentialRuntimeEvent,
} from '../../views/credential_error.js';
import {CredentialErrorNotice} from '../components/credential-error-notice.js';
import {RuntimeEventKind} from '../../maho_ai.mojom-webui.js';
import {collectConversationItems} from '../../views/conversation_thread.js';
import {ConversationSystemItem} from '../features/compact/conversation-system-item.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});
function createErrorEvent(
    credentialErrorCode: number | null,
    text = 'Execution failed: server response 500'): CredentialRuntimeEvent {
  return {
    credentialErrorCode,
    kind: RuntimeEventKind.kError,
    sequence: 1,
    sessionId: 'session-1',
    text,
    timestamp: 1,
  };
}

function requireButton(
    container: HTMLElement,
    label: string): HTMLButtonElement {
  const button = Array.from(container.querySelectorAll('button'))
      .find(candidate => candidate.textContent?.trim() === label ||
          candidate.getAttribute('aria-label') === label);
  if (button) {
    return button;
  }
  throw new Error(`Missing button "${label}"`);
}

describe('Error recovery and action interaction in rendered DOM', () => {
  let container: HTMLDivElement;
  let root: Root;

  beforeEach(() => {
    container = document.createElement('div');
    document.body.appendChild(container);
    root = createRoot(container);
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    vi.restoreAllMocks();
  });

  describe('Credential error notice recovery action', () => {
    it('dispatches onOpenSettings with provider settings pane when Retry / Open settings action is clicked', async () => {
      // Given: a typed credential error event (e.g. kProviderNotConfigured)
      const event = createErrorEvent(CredentialErrorCode.kProviderNotConfigured);
      const presentation = getCredentialErrorPresentation(event);
      expect(presentation).not.toBeNull();

      const onOpenSettings = vi.fn<(paneKey: string) => void>();

      // When: rendered into jsdom and action button clicked
      act(() => root.render(
          <CredentialErrorNotice
            onOpenSettings={onOpenSettings}
            presentation={presentation!}
            surface="compact"
          />));

      const actionButton = requireButton(container, presentation!.actionLabel);

      await act(async () => {
        actionButton.dispatchEvent(new MouseEvent('click', {bubbles: true}));
      });

      // Then: onOpenSettings is invoked with expected settings pane
      expect(onOpenSettings).toHaveBeenCalledOnce();
      expect(onOpenSettings).toHaveBeenCalledWith(presentation!.settingsPane);
    });

    it('dispatches onOpenSettings for account-sync pane on managed auth failure', async () => {
      // Given: a managed auth credential failure
      const event = createErrorEvent(CredentialErrorCode.kManagedAuthUnavailable);
      const presentation = getCredentialErrorPresentation(event);
      expect(presentation).not.toBeNull();

      const onOpenSettings = vi.fn<(paneKey: string) => void>();

      // When: action button clicked
      act(() => root.render(
          <CredentialErrorNotice
            onOpenSettings={onOpenSettings}
            presentation={presentation!}
            surface="compact"
          />));

      const actionButton = requireButton(container, presentation!.actionLabel);

      await act(async () => {
        actionButton.dispatchEvent(new MouseEvent('click', {bubbles: true}));
      });

      // Then: target pane key is account-sync
      expect(onOpenSettings).toHaveBeenCalledOnce();
      expect(onOpenSettings).toHaveBeenCalledWith('account-sync');
    });

    it('renders credential error in ConversationSystemItem and dispatches settings action on click', async () => {
      // Given: a credential error item in compact thread
      const event = createErrorEvent(CredentialErrorCode.kCredentialUnusable);
      const items = collectConversationItems([{sessionId: 'session-1', event}]);
      const item = items[0];
      expect(item?.credentialError).toBeDefined();

      const onOpenSettings = vi.fn<(paneKey: string) => void>();

      // When: rendered in ConversationSystemItem and user clicks settings button
      act(() => root.render(
          <ConversationSystemItem
            item={item!}
            onOpenSettings={onOpenSettings}
            onRespondToApproval={vi.fn()}
            readOnly={false}
          />));

      const actionButton = requireButton(container, 'Open AI settings');

      await act(async () => {
        actionButton.dispatchEvent(new MouseEvent('click', {bubbles: true}));
      });

      // Then: onOpenSettings receives maho-ai pane key
      expect(onOpenSettings).toHaveBeenCalledOnce();
      expect(onOpenSettings).toHaveBeenCalledWith('maho-ai');
    });
  });

  describe('Runtime execution error presentation', () => {
    it('renders runtime error notice text cleanly without breaking system item layout', () => {
      // Given: an untyped generic runtime error event
      const event = createErrorEvent(null, 'Execution failed while contacting the runtime.');
      const items = collectConversationItems([{sessionId: 'session-1', event}]);
      const item = items[0];
      expect(item).toBeDefined();

      // When: rendered in ConversationSystemItem
      act(() => root.render(
          <ConversationSystemItem
            item={item!}
            onOpenSettings={vi.fn()}
            onRespondToApproval={vi.fn()}
            readOnly={false}
          />));

      // Then: displays the complete error text and Issue badge
      expect(container.textContent).toContain('Something went wrong');
      expect(container.textContent).toContain('Execution failed while contacting the runtime.');
      expect(container.textContent).toContain('Issue');
    });

    it('renders the whole error payload untruncated and copies it to the clipboard', async () => {
      const longDetail =
          'Execution error: Error from LLM when running completions Permanent error: ' +
          'POST http://127.0.0.1:18801/v1/chat/completions returned 404 Not Found for model ' +
          'claude-opus-4-6-thinking (request id req-abcdef0123456789)';
      const event = createErrorEvent(null, longDetail);
      const items = collectConversationItems([{sessionId: 'session-1', event}]);
      const item = items[0];
      expect(item).toBeDefined();
      expect(item!.errorDetail).toBe(longDetail);

      const writeText = vi.fn<(value: string) => Promise<void>>(() => Promise.resolve());
      Object.defineProperty(navigator, 'clipboard', {
        configurable: true,
        value: {writeText},
      });

      act(() => root.render(
          <ConversationSystemItem
            item={item!}
            onOpenSettings={vi.fn()}
            onRespondToApproval={vi.fn()}
            readOnly={false}
          />));

      const detail = container.querySelector('[data-testid="error-detail"]');
      expect(detail?.textContent).toBe(longDetail);

      const copyButton = container.querySelector<HTMLButtonElement>('[data-action="copy-error"]');
      expect(copyButton).not.toBeNull();

      await act(async () => {
        copyButton!.dispatchEvent(new MouseEvent('click', {bubbles: true}));
      });

      expect(writeText).toHaveBeenCalledWith(longDetail);
    });

    it('renders Retry button and dispatches onRetry callback on click', async () => {
      const event = createErrorEvent(null, 'Connection refused to proxy 127.0.0.1:18801');
      const items = collectConversationItems([{sessionId: 'session-1', event}]);
      const item = items[0];
      expect(item).toBeDefined();

      const onRetry = vi.fn();

      act(() => root.render(
          <ConversationSystemItem
            item={item!}
            onOpenSettings={vi.fn()}
            onRespondToApproval={vi.fn()}
            onRetry={onRetry}
            readOnly={false}
          />));

      const retryButton = requireButton(container, 'Retry');
      await act(async () => {
        retryButton.dispatchEvent(new MouseEvent('click', {bubbles: true}));
      });

      expect(onRetry).toHaveBeenCalledOnce();
      expect(container.textContent).toContain('Connection error — Unable to reach AI proxy or endpoint.');
      expect(container.textContent).toContain('Connection refused to proxy 127.0.0.1:18801');
    });

    it('does not misclassify 401 unauthorized or quota errors as connection unreachable', () => {
      const authEvent = createErrorEvent(null, 'Proxy response 401 Unauthorized: Invalid API key');
      const authItems = collectConversationItems([{sessionId: 'session-1', event: authEvent}]);
      expect(authItems[0]?.text).toBe('Something went wrong');
      expect(authItems[0]?.errorDetail)
          .toBe('Proxy response 401 Unauthorized: Invalid API key');
      expect(authItems[0]?.text).not.toContain('Connection error');

      const quotaEvent = createErrorEvent(null, 'Proxy quota exceeded: 429 Too Many Requests');
      const quotaItems = collectConversationItems([{sessionId: 'session-1', event: quotaEvent}]);
      expect(quotaItems[0]?.text).toBe('Something went wrong');
      expect(quotaItems[0]?.errorDetail)
          .toBe('Proxy quota exceeded: 429 Too Many Requests');
      expect(quotaItems[0]?.text).not.toContain('Connection error');
    });
  });
});
