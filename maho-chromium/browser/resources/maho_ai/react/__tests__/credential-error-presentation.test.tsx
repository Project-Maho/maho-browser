import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {RuntimeEventKind} from '../../maho_ai.mojom-webui.js';
import {
  CredentialErrorCode,
  getCredentialErrorPresentation,
  type CredentialRuntimeEvent,
} from '../../views/credential_error.js';
import {collectConversationItems} from '../../views/conversation_thread.js';
import {ConversationSystemItem} from '../features/compact/conversation-system-item.js';
import {EventCard} from '../features/developer/event-card.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

const credentialCases = [
  {
    actionLabel: 'Open AI settings',
    code: CredentialErrorCode.kProviderNotConfigured,
    description: 'Select an AI provider in Settings, then try again.',
    settingsPane: 'maho-ai',
    title: 'Choose an AI provider',
  },
  {
    actionLabel: 'Open AI settings',
    code: CredentialErrorCode.kCredentialUnusable,
    description: 'Enter a valid AI credential in Settings, then try again.',
    settingsPane: 'maho-ai',
    title: 'Update your AI credential',
  },
  {
    actionLabel: 'Open AI settings',
    code: CredentialErrorCode.kSecureStoreUnavailable,
    description: 'AI credentials are temporarily unavailable. Review Settings, then try again.',
    settingsPane: 'maho-ai',
    title: 'AI credentials are unavailable',
  },
  {
    actionLabel: 'Open AI settings',
    code: CredentialErrorCode.kCredentialDecryptFailed,
    description: 'Re-enter your AI credential in Settings, then try again.',
    settingsPane: 'maho-ai',
    title: 'Re-enter your AI credential',
  },
  {
    actionLabel: 'Open account settings',
    code: CredentialErrorCode.kManagedAuthUnavailable,
    description: 'Sign in again in Settings, then try again.',
    settingsPane: 'account-sync',
    title: 'Reconnect your Maho account',
  },
  {
    actionLabel: 'Open AI settings',
    code: CredentialErrorCode.kUnsupportedProvider,
    description: 'Select a supported AI provider in Settings, then try again.',
    settingsPane: 'maho-ai',
    title: 'Choose a supported AI provider',
  },
] as const;

function createErrorEvent(
    credentialErrorCode: number | null,
    text = 'synthetic diagnostic: https://provider.invalid model=secret api_key=hidden decrypt storage'):
    CredentialRuntimeEvent {
  return {
    credentialErrorCode,
    kind: RuntimeEventKind.kError,
    sequence: 1,
    sessionId: 'session-1',
    text,
    timestamp: 1,
  };
}

function requireElement<T extends Element>(element: T | null, description: string): T {
  if (element) {
    return element;
  }
  throw new Error(`Missing ${description}`);
}

describe('typed credential error presentation', () => {
  let container: HTMLDivElement;
  let root: Root;

  beforeEach(() => {
    container = document.createElement('div');
    document.body.append(container);
    root = createRoot(container);
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
  });

  it.each(credentialCases)(
      'maps $code to fixed UI-owned copy and the $settingsPane pane', expected => {
        // Given: a typed credential failure carrying hostile diagnostic text.
        const event = createErrorEvent(expected.code);

        // When: the type/view layer resolves its presentation.
        const presentation = getCredentialErrorPresentation(event);

        // Then: only fixed UI-owned copy and the intended Settings target survive.
        expect(presentation).toEqual({
          actionLabel: expected.actionLabel,
          description: expected.description,
          settingsPane: expected.settingsPane,
          title: expected.title,
        });
        expect(JSON.stringify(presentation)).not.toContain('provider.invalid');
        expect(JSON.stringify(presentation)).not.toContain('api_key');
      });

  it('uses a safe credential fallback for generic and future typed codes', () => {
    // Given: generic and not-yet-known typed credential codes.
    const generic = createErrorEvent(CredentialErrorCode.kGeneric);
    const future = createErrorEvent(999);

    // When: both are resolved by the type/view layer.
    const genericPresentation = getCredentialErrorPresentation(generic);
    const futurePresentation = getCredentialErrorPresentation(future);

    // Then: both use the same safe UI-owned fallback rather than event text.
    expect(genericPresentation).toEqual({
      actionLabel: 'Open AI settings',
      description: 'Maho could not use your AI credential. Review Settings, then try again.',
      settingsPane: 'maho-ai',
      title: 'Review AI settings',
    });
    expect(futurePresentation).toEqual(genericPresentation);
  });

  it('keeps the full raw error text available for an untyped generic runtime error', () => {
    // Given: an ordinary runtime error with no credential category.
    const event = createErrorEvent(null, 'Execution failed while contacting the runtime.');
    const entry = {event, sessionId: event.sessionId};

    // When: compact conversation items are collected.
    const items = collectConversationItems([entry]);

    // Then: the headline stays generic and the untruncated text is carried as detail.
    expect(getCredentialErrorPresentation(event)).toBeNull();
    expect(items).toHaveLength(1);
    expect(items[0]?.text).toBe('Something went wrong');
    expect(items[0]?.errorDetail).toBe('Execution failed while contacting the runtime.');
  });

  it.each(credentialCases)(
      'renders code $code accessibly in compact and developer surfaces with the mapped action', expected => {
        // Given: a typed credential failure and the two product surfaces.
        const event = createErrorEvent(expected.code);
        const item = collectConversationItems([{event, sessionId: event.sessionId}])[0];
        const onOpenSettings = vi.fn<(paneKey: string) => void>();
        if (!item) {
          throw new Error('Expected compact conversation item');
        }

        // When: compact renders the item and its Settings action is activated.
        act(() => root.render(
            <ConversationSystemItem
              item={item}
              onOpenSettings={onOpenSettings}
              onRespondToApproval={vi.fn()}
              readOnly={false}
            />));
        const alert = requireElement(container.querySelector('[role="alert"]'), 'credential alert');
        const button = requireElement(
            container.querySelector(`button[aria-label="${expected.actionLabel}"]`),
            'credential settings action');
        act(() => button.dispatchEvent(new MouseEvent('click', {bubbles: true})));

        // Then: compact uses fixed copy and the mapped Settings pane.
        expect(alert.textContent).toContain(expected.title);
        expect(alert.textContent).toContain(expected.description);
        expect(onOpenSettings).toHaveBeenCalledOnce();
        expect(onOpenSettings).toHaveBeenCalledWith(expected.settingsPane);
        expect(container.textContent).not.toContain('provider.invalid');

        // When: developer renders the same event and activates its action.
        onOpenSettings.mockClear();
        act(() => root.render(
            <EventCard event={event} onOpenSettings={onOpenSettings} />));
        const developerAlert = requireElement(
            container.querySelector('[role="alert"]'), 'developer credential alert');
        const developerButton = requireElement(
            container.querySelector(`button[aria-label="${expected.actionLabel}"]`),
            'developer credential settings action');
        act(() => developerButton.dispatchEvent(new MouseEvent('click', {bubbles: true})));

        // Then: developer uses the same fixed presentation and target.
        expect(developerAlert.textContent).toContain(expected.title);
        expect(developerAlert.textContent).toContain(expected.description);
        expect(onOpenSettings).toHaveBeenCalledOnce();
        expect(onOpenSettings).toHaveBeenCalledWith(expected.settingsPane);
        expect(container.textContent).not.toContain('provider.invalid');
      });

  it.each(credentialCases)(
      'suppresses synthetic raw diagnostics from compact items for code $code', expected => {
        // Given: a typed failure containing prohibited raw diagnostics.
        const event = createErrorEvent(expected.code);

        // When: the compact view model is collected.
        const items = collectConversationItems([{event, sessionId: event.sessionId}]);
        const serialized = JSON.stringify(items);

        // Then: only mapped UI copy remains.
        expect(serialized).toContain(expected.title);
        expect(serialized).toContain(expected.description);
        expect(serialized).not.toContain('provider.invalid');
        expect(serialized).not.toContain('model=secret');
        expect(serialized).not.toContain('api_key');
        expect(serialized).not.toContain('decrypt storage');
      });
});
