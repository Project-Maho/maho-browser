import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {AISelectionLevels, type ComposerHandlers} from '../features/compact/composer.js';
import {
  AI_REASONING_EFFORT,
  createInitialState,
  type AppState,
} from '../../types.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

function createHandlers(): ComposerHandlers {
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

function createConfiguredState(): AppState {
  const state = createInitialState();
  state.aiSettings = {
    activeModelId: 'gpt-4o-mini',
    activeProviderId: 'openai',
    activeReasoningEffort: AI_REASONING_EFFORT.kMedium,
    providerOptions: [
      {
        id: 'openai',
        label: 'OpenAI',
        modelOptions: [
          {id: 'gpt-4o-mini', label: 'GPT-4o mini'},
          {id: 'gpt-4.1', label: 'GPT-4.1'},
        ],
      },
      {
        id: 'anthropic',
        label: 'Anthropic',
        modelOptions: [{id: 'claude-3-5-sonnet', label: 'Claude 3.5 Sonnet'}],
      },
    ],
    reasoningOptions: [
      {effort: AI_REASONING_EFFORT.kLow, label: 'Low'},
      {effort: AI_REASONING_EFFORT.kMedium, label: 'Medium'},
      {effort: AI_REASONING_EFFORT.kHigh, label: 'High'},
    ],
  };
  return state;
}

// Radix popovers use floating-ui, which requires ResizeObserver in the
// environment under test.
function installResizeObserverStub(): void {
  globalThis.ResizeObserver = class {
    observe(): void {}
    unobserve(): void {}
    disconnect(): void {}
  } as unknown as typeof ResizeObserver;
}

describe('AISelectionLevels keyboard/ARIA contract', () => {
  let container: HTMLDivElement;
  let root: Root;

  beforeEach(() => {
    installResizeObserverStub();
    window.HTMLElement.prototype.scrollIntoView = vi.fn();
    const appRoot = document.createElement('div');
    appRoot.id = 'app';
    document.body.appendChild(appRoot);
    container = document.createElement('div');
    document.body.appendChild(container);
    root = createRoot(container);
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    document.getElementById('app')?.remove();
    (globalThis as Record<string, unknown>)['ResizeObserver'] = undefined;
    vi.restoreAllMocks();
  });

  it('wires the trigger to a labelled popup with aria-controls and closes it on Escape',
      () => {
        const state = createConfiguredState();
        act(() => root.render(
            <AISelectionLevels handlers={createHandlers()} state={state} />));

        const trigger = container.querySelector<HTMLButtonElement>(
            'button[data-ai-selection-level="provider"]');
        expect(trigger).not.toBeNull();
        expect(trigger?.getAttribute('aria-expanded')).toBe('false');

        act(() => {
          trigger!.dispatchEvent(new MouseEvent('click', {bubbles: true}));
        });

        expect(trigger?.getAttribute('aria-expanded')).toBe('true');
        const controlsId = trigger?.getAttribute('aria-controls');
        expect(controlsId).toBeTruthy();
        const popup = document.getElementById(controlsId!);
        expect(popup).not.toBeNull();
        expect(popup?.getAttribute('role')).toBe('dialog');
        expect(popup?.textContent).toContain('Anthropic');

        act(() => {
          popup!.dispatchEvent(new KeyboardEvent('keydown', {
            key: 'Escape',
            bubbles: true,
          }));
        });

        expect(trigger?.getAttribute('aria-expanded')).toBe('false');
        expect(document.getElementById(controlsId!)).toBeNull();
      });
});
