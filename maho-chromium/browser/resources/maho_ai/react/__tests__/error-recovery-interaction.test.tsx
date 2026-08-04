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

      // Then: displays error text and Issue badge
      expect(container.textContent).toContain('Something went wrong — Execution failed while contacting the runtime.');
      expect(container.textContent).toContain('Issue');
    });
  });
});
