import {readFileSync} from 'node:fs';
import path from 'node:path';
import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import type {QuickAction} from '../../types.js';
import {SuggestionChips} from '../features/compact/compact-stage.js';

const appSourcePath = path.join(process.cwd(), 'react/app.tsx');
const EXPECTED_QUICK_ACTIONS = [
  {
    id: 'summarize',
    label: 'Summarize',
    userPrompt: 'Summarize this page.',
    intent: 1,
  },
  {
    id: 'quiz-me',
    label: 'Quiz me',
    userPrompt: 'Quiz me on this page.',
    intent: 2,
  },
] as const satisfies readonly QuickAction[];

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

function requireButton(
    container: HTMLElement,
    label: string): HTMLButtonElement {
  const button = Array.from(container.querySelectorAll('button'))
      .find(candidate => candidate.textContent === label);
  if (button) {
    return button;
  }

  throw new Error(`Missing ${label} button`);
}

interface QuickActionRenderOptions {
  readonly attachmentCount?: number;
  readonly developerMode?: boolean;
  readonly disabled?: boolean;
  readonly hasMessages?: boolean;
  readonly prompt?: string;
}

describe('Compact quick actions', () => {
  let container: HTMLDivElement;
  let root: Root;
  let submitQuickAction: ReturnType<typeof vi.fn<(action: QuickAction) => void>>;

  beforeEach(() => {
    container = document.createElement('div');
    document.body.appendChild(container);
    root = createRoot(container);
    submitQuickAction = vi.fn<(action: QuickAction) => void>();
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
    vi.restoreAllMocks();
  });

  function renderQuickActions(options: QuickActionRenderOptions = {}): void {
    act(() => root.render(
        <SuggestionChips
          attachmentCount={options.attachmentCount ?? 0}
          developerMode={options.developerMode ?? false}
          disabled={options.disabled ?? false}
          hasMessages={options.hasMessages ?? false}
          prompt={options.prompt ?? ''}
          onSelect={submitQuickAction}
        />));
  }

  it('renders exactly Summarize and Quiz me without Analyze or Explain', () => {
    // Given: an empty compact conversation.
    renderQuickActions();

    // When: the visible action labels are read.
    const labels = Array.from(container.querySelectorAll('button'))
        .map(button => button.textContent);

    // Then: only the two page quick actions are available.
    expect(labels).toEqual(['Summarize', 'Quiz me']);
    expect(container.textContent).not.toContain('Analyze');
    expect(container.textContent).not.toContain('Explain');
  });

  it('dispatches the exact typed action object selected by each button', async () => {
    // Given: both compact quick actions are visible.
    renderQuickActions();

    // When: each action is selected once.
    await act(async () => {
      requireButton(container, 'Summarize').dispatchEvent(
          new MouseEvent('click', {bubbles: true}));
      requireButton(container, 'Quiz me').dispatchEvent(
          new MouseEvent('click', {bubbles: true}));
    });

    // Then: the store-facing handler receives the immutable action contracts.
    expect(submitQuickAction).toHaveBeenNthCalledWith(1, EXPECTED_QUICK_ACTIONS[0]);
    expect(submitQuickAction).toHaveBeenNthCalledWith(2, EXPECTED_QUICK_ACTIONS[1]);
  });

  it.each([
    ['developer mode', {developerMode: true}],
    ['existing messages', {hasMessages: true}],
    ['a nonblank draft', {prompt: '  keep this draft  '}],
    ['a composer attachment', {attachmentCount: 1}],
  ] satisfies readonly [string, QuickActionRenderOptions][])(
      'hides actions without dispatching for %s', (_condition, options) => {
        // Given: a compact quick-action visibility blocker.
        renderQuickActions(options);

        // When: the action area is inspected.
        const buttons = container.querySelectorAll('button');

        // Then: no action can be selected or dispatched.
        expect(buttons).toHaveLength(0);
        expect(submitQuickAction).not.toHaveBeenCalled();
      });

  it('treats a whitespace-only composer prompt as empty', () => {
    // Given: the composer contains only whitespace.
    renderQuickActions({prompt: '  \n  '});

    // When: the action area is inspected.
    const buttons = container.querySelectorAll('button');

    // Then: both empty-state actions remain visible.
    expect(buttons).toHaveLength(2);
  });

  it('keeps both actions disabled without dispatching', async () => {
    // Given: the session is read-only or has a pending turn.
    renderQuickActions({disabled: true});
    const buttons = Array.from(container.querySelectorAll('button'));

    // When: disabled actions receive click events.
    await act(async () => {
      for (const button of buttons) {
        button.dispatchEvent(new MouseEvent('click', {bubbles: true}));
      }
    });

    // Then: both controls stay disabled and the handler is untouched.
    expect(buttons).toHaveLength(2);
    expect(buttons.every(button => button.disabled)).toBe(true);
    expect(submitQuickAction).not.toHaveBeenCalled();
  });

  it('wires the app handler only to store.submitQuickAction', () => {
    // Given: the app-level quick-action handler source.
    const appSource = readFileSync(appSourcePath, 'utf8');

    // When: the typed handler body is isolated.
    const handlerStart = appSource.indexOf('const handleQuickActionSelect');
    const handlerEnd = appSource.indexOf('\n\n', handlerStart);
    const handlerSource = appSource.slice(handlerStart, handlerEnd);

    // Then: it dispatches the selected action without mutating the composer.
    expect(handlerStart).toBeGreaterThanOrEqual(0);
    expect(handlerSource).toContain('store.submitQuickAction(action)');
    expect(handlerSource).not.toContain('setComposerPrompt');
    expect(handlerSource).not.toContain('startSessionThenSubmit');
    expect(handlerSource).not.toContain('submitPrompt');
  });
});
