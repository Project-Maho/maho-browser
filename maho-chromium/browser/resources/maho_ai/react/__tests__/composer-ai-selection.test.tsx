import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {
  AISelectionDropdownContent,
  AISelectionLevels,
  type ComposerHandlers,
} from '../features/compact/composer.js';
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

const originalResizeObserver = globalThis.ResizeObserver;
const originalScrollIntoView = window.HTMLElement.prototype.scrollIntoView;

// The selection popups are Radix popovers; opening one needs ResizeObserver
// and renders its options into the portal target rather than the test root.
function installPopoverEnvironment(): void {
  globalThis.ResizeObserver = class {
    observe(): void {}
    unobserve(): void {}
    disconnect(): void {}
  } as unknown as typeof ResizeObserver;
  window.HTMLElement.prototype.scrollIntoView = vi.fn();
  if (!document.getElementById('app')) {
    const appRoot = document.createElement('div');
    appRoot.id = 'app';
    document.body.appendChild(appRoot);
  }
}

function cleanupPopoverEnvironment(): void {
  globalThis.ResizeObserver = originalResizeObserver;
  window.HTMLElement.prototype.scrollIntoView = originalScrollIntoView;
  document.querySelectorAll('#app').forEach(el => el.remove());
}

function requireElement<T extends Element>(element: T | null, description: string): T {
  if (element) {
    return element;
  }

  throw new Error(`Missing ${description}`);
}

function createHandlers(
    onSetAISelection: (selection: AISelectionRequest) => Promise<boolean> =
        vi.fn<() => Promise<boolean>>().mockResolvedValue(true)):
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
        modelOptions: [
          {id: 'claude-3-5-sonnet', label: 'Claude 3.5 Sonnet'},
        ],
      },
      {
        id: 'empty-provider',
        label: 'Empty Provider',
        modelOptions: [],
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

describe('AISelectionDropdownContent', () => {
  let container: HTMLDivElement;
  let root: Root;

  beforeEach(() => {
    installPopoverEnvironment();
    container = document.createElement('div');
    document.body.append(container);
    root = createRoot(container);
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    cleanupPopoverEnvironment();
  });

  it('shows provider, model, and thinking level as three compact controls', () => {
    const state = createConfiguredState();
    act(() => root.render(
        <AISelectionLevels handlers={createHandlers()} state={state} />));

    expect(container.querySelector('[data-ai-selection-level="provider"]')?.textContent)
        .toContain('OpenAI');
    expect(container.querySelector('[data-ai-selection-level="model"]')?.textContent)
        .toContain('GPT-4o mini');
    expect(container.querySelector('[data-ai-selection-level="thinking"]')?.textContent)
        .toContain('Medium');
    expect(container.textContent).not.toContain('PROVIDER');
    expect(container.textContent).not.toContain('MODEL');
    expect(container.textContent).not.toContain('REASONING');
  });

  it.each(['model', 'provider'])('preserves Low when changing %s', async level => {
    const state = createConfiguredState();
    state.aiSettings.activeReasoningEffort = AI_REASONING_EFFORT.kLow;
    const onSetAISelection = vi.fn().mockResolvedValue(true);
    act(() => root.render(<AISelectionLevels handlers={createHandlers(onSetAISelection)} state={state} />));
    act(() => container.querySelector<HTMLButtonElement>(`[data-ai-selection-level="${level}"]`)!.click());
    const option = level === 'model' ? 'gpt-4.1' : 'anthropic';
    await act(async () => document.querySelector<HTMLButtonElement>(`[data-ai-selection-${level}="${option}"]`)!.click());
    expect(onSetAISelection).toHaveBeenCalledWith(expect.objectContaining({reasoningEffort: AI_REASONING_EFFORT.kLow}));
  });

  it('calls SetAISelection-shaped handler for provider, model, and thinking picks', async () => {
    // Given
    const onSetAISelection = vi.fn<(selection: AISelectionRequest) => Promise<boolean>>()
                               .mockResolvedValue(true);
    const state = createConfiguredState();
    act(() => root.render(
        <AISelectionLevels
          handlers={createHandlers(onSetAISelection)}
          state={state}
        />));

    // When
    act(() => {
      requireElement(
          container.querySelector('button[data-ai-selection-level="provider"]'),
          'provider level')
          .dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    await act(async () => {
      requireElement(
          document.querySelector('button[data-ai-selection-provider="anthropic"]'),
          'Anthropic provider option')
          .dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    act(() => {
      requireElement(
          container.querySelector('button[data-ai-selection-level="model"]'),
          'model level')
          .dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    await act(async () => {
      requireElement(
          document.querySelector('button[data-ai-selection-model="gpt-4.1"]'),
          'GPT-4.1 model option')
          .dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    act(() => {
      requireElement(
          container.querySelector('button[data-ai-selection-level="thinking"]'),
          'thinking level')
          .dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    await act(async () => {
      requireElement(
          document.querySelector(`button[data-ai-selection-reasoning="${AI_REASONING_EFFORT.kHigh}"]`),
          'high reasoning option')
          .dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });

    // Then
    expect(onSetAISelection).toHaveBeenNthCalledWith(1, {
      modelId: 'claude-3-5-sonnet',
      providerId: 'anthropic',
      reasoningEffort: AI_REASONING_EFFORT.kMedium,
    });
    expect(onSetAISelection).toHaveBeenNthCalledWith(2, {
      modelId: 'gpt-4.1',
      providerId: 'openai',
      reasoningEffort: AI_REASONING_EFFORT.kMedium,
    });
    expect(onSetAISelection).toHaveBeenNthCalledWith(3, {
      modelId: 'gpt-4o-mini',
      providerId: 'openai',
      reasoningEffort: AI_REASONING_EFFORT.kHigh,
    });
  });

  it('filters models across providers by search and switches provider on pick', async () => {
    // Given
    const onSetAISelection = vi.fn<(selection: AISelectionRequest) => Promise<boolean>>()
                               .mockResolvedValue(true);
    const state = createConfiguredState();
    act(() => root.render(
        <AISelectionLevels handlers={createHandlers(onSetAISelection)} state={state} />));
    act(() => {
      requireElement(
          container.querySelector('button[data-ai-selection-level="model"]'), 'model level')
          .dispatchEvent(new MouseEvent('click', {bubbles: true}));
    });
    const search = requireElement(
        document.querySelector<HTMLInputElement>('input[data-ai-model-search]'), 'model search');
    const visibleModels = () => Array.from(
        document.querySelectorAll('[data-ai-selection-model]'),
        el => el.getAttribute('data-ai-selection-model'));
    expect(visibleModels()).toEqual(['gpt-4o-mini', 'gpt-4.1', 'claude-3-5-sonnet']);

    // When: typing a multi-token query that only the Anthropic model matches.
    act(() => {
      const setValue = Object.getOwnPropertyDescriptor(
          HTMLInputElement.prototype, 'value')!.set!;
      setValue.call(search, 'sonnet 3');
      search.dispatchEvent(new Event('input', {bubbles: true}));
    });
    expect(visibleModels()).toEqual(['claude-3-5-sonnet']);
    await act(async () => {
      search.dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', bubbles: true}));
    });

    // Then
    expect(onSetAISelection).toHaveBeenCalledWith({
      modelId: 'claude-3-5-sonnet',
      providerId: 'anthropic',
      reasoningEffort: AI_REASONING_EFFORT.kMedium,
    });
    expect(document.querySelector('input[data-ai-model-search]')).toBeNull();
  });

  it('shows a graceful no-provider state without opening settings when no providers exist', () => {
    // Given
    const state = createInitialState();
    state.aiSettings = {
      ...state.aiSettings,
      activeModelId: null,
      activeProviderId: null,
      providerOptions: [],
    };

    // When
    act(() => root.render(
        <AISelectionDropdownContent handlers={createHandlers()} state={state} />));

    // Then
    expect(container.textContent).toContain('No AI providers available');
    expect(container.textContent).not.toContain('Configure in Settings');
  });

  it('disables provider options that cannot produce a selectable model', () => {
    // Given
    const state = createConfiguredState();

    // When
    act(() => root.render(
        <AISelectionDropdownContent handlers={createHandlers()} state={state} />));

    // Then
    const emptyProvider = requireElement(
        container.querySelector<HTMLButtonElement>(
            'button[data-ai-selection-provider="empty-provider"]'),
        'empty provider option');
    expect(emptyProvider.disabled).toBe(true);
    expect(emptyProvider.getAttribute('aria-disabled')).toBe('true');
  });

  it('does not render the raw Configure in Settings fallback when provider options exist', () => {
    // Given
    const state = createConfiguredState();
    state.aiSettings = {
      ...state.aiSettings,
      activeProviderId: 'missing-provider',
    };

    // When
    act(() => root.render(
        <AISelectionDropdownContent handlers={createHandlers()} state={state} />));

    // Then
    expect(container.textContent).toContain('OpenAI');
    expect(container.textContent).not.toContain('Current:');
    expect(container.textContent).not.toContain('Configure in Settings');
  });
});
