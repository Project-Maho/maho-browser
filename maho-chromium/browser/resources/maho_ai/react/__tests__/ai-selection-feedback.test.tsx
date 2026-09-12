import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';
import {toast} from 'sonner';

import {AISelectionLevels, type ComposerHandlers} from '../features/compact/composer.js';
import {
  AI_REASONING_EFFORT,
  createInitialState,
  type AISelectionRequest,
  type AppState,
} from '../../types.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

function createHandlers(
    onSetAISelection: (selection: AISelectionRequest) => Promise<boolean>):
    ComposerHandlers {
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
    onSetAISelection,
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

describe('AI selection rejection feedback', () => {
  let container: HTMLDivElement;
  let root: Root;

  beforeEach(() => {
    globalThis.ResizeObserver = class {
      observe(): void {}
      unobserve(): void {}
      disconnect(): void {}
    } as unknown as typeof ResizeObserver;
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

  it('toasts when the provider rejects the selection instead of dying silently',
      async () => {
        const toastErrorSpy = vi.spyOn(toast, 'error');
        const onSetAISelection =
            vi.fn<(selection: AISelectionRequest) => Promise<boolean>>()
                .mockResolvedValue(false);
        const state = createConfiguredState();
        act(() => root.render(
            <AISelectionLevels
              handlers={createHandlers(onSetAISelection)}
              state={state}
            />));

        act(() => {
          container.querySelector<HTMLButtonElement>(
              'button[data-ai-selection-level="provider"]')!
              .dispatchEvent(new MouseEvent('click', {bubbles: true}));
        });
        await act(async () => {
          document.querySelector<HTMLButtonElement>(
              'button[data-ai-selection-provider="anthropic"]')!
              .dispatchEvent(new MouseEvent('click', {bubbles: true}));
        });

        expect(onSetAISelection).toHaveBeenCalledOnce();
        expect(toastErrorSpy).toHaveBeenCalledTimes(1);
        expect(toastErrorSpy.mock.calls[0]![0]).toContain('not applied');
      });

  it('stays quiet when the selection is accepted', async () => {
    const toastErrorSpy = vi.spyOn(toast, 'error');
    const onSetAISelection =
        vi.fn<(selection: AISelectionRequest) => Promise<boolean>>()
            .mockResolvedValue(true);
    const state = createConfiguredState();
    act(() => root.render(
        <AISelectionLevels
          handlers={createHandlers(onSetAISelection)}
          state={state}
        />));

    act(() => {
      container.querySelector<HTMLButtonElement>(
          'button[data-ai-selection-level="model"]')!
          .dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    await act(async () => {
      document.querySelector<HTMLButtonElement>(
          'button[data-ai-selection-model="gpt-4.1"]')!
          .dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });

    expect(onSetAISelection).toHaveBeenCalledOnce();
    expect(toastErrorSpy).not.toHaveBeenCalled();
  });
});
