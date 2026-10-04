import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {TooltipProvider} from '@ui/tooltip';

import {createInitialState, type AppState} from '../../types.js';
import {Composer, type ComposerHandlers} from '../features/compact/composer.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});
function requireElement<T extends Element>(element: T | null, description: string): T {
  if (element) {
    return element;
  }

  throw new Error(`Missing ${description}`);
}

function createDummyHandlers(): ComposerHandlers {
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
  };
}

describe('Composer context picker trigger', () => {
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

  it('renders button forwarding popover trigger attributes and opens popover on click', async () => {
    // Given: a rendered Composer component.
    const state: AppState = createInitialState();
    const handlers = createDummyHandlers();

    act(() => {
      root.render(
        <TooltipProvider>
          <Composer handlers={handlers} state={state} />
        </TooltipProvider>
      );
    });

    // When: locating the trigger button by its aria-label.
    const triggerButton = requireElement(
      container.querySelector<HTMLButtonElement>(
        'button[aria-label="Add context or start a new session"]'
      ),
      'Add context trigger button'
    );

    // Then: Popover trigger attributes (aria-haspopup, aria-expanded) are present on the button.
    expect(triggerButton.getAttribute('aria-haspopup')).toBe('dialog');
    expect(triggerButton.getAttribute('aria-expanded')).toBe('false');

    // When: clicking the trigger button.
    await act(async () => {
      triggerButton.dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });

    // Then: aria-expanded becomes "true" on the trigger, popover content is rendered in DOM, and tab loading handler is invoked.
    expect(triggerButton.getAttribute('aria-expanded')).toBe('true');
    const popoverContent = requireElement(
      document.body.querySelector('[aria-label="Context picker"]'),
      'Context picker popover content'
    );
    expect(popoverContent).toBeDefined();
    expect(handlers.onLoadOpenTabs).toHaveBeenCalledTimes(1);
  });
});
