import {RuntimeEventKind} from '../maho_ai.mojom-webui.js';
import {MahoAiStore} from '../store.js';
import {
  AppState,
  getConnectionStateLabel,
  getSessionAccessLabel,
  getSessionTitle,
  getStatusLabel,
  isSessionReadOnly,
  RuntimeEventRecord,
} from '../types.js';
import {createEmptyPanel} from './cards.js';
import {renderComposer} from './composer.js';

interface EditableSnapshot {
  selectionEnd: number;
  selectionStart: number;
  scrollTop: number;
}

const kPendingSelectionStart = 'pendingSelectionStart';
const kPendingSelectionEnd = 'pendingSelectionEnd';

function readPendingSelection(
    editable: HTMLInputElement|HTMLTextAreaElement,
): {selectionStart: number; selectionEnd: number;}|null {
  const start = editable.dataset[kPendingSelectionStart];
  const end = editable.dataset[kPendingSelectionEnd];
  if (start === undefined || end === undefined) {
    return null;
  }

  return {
    selectionEnd: Number(end),
    selectionStart: Number(start),
  };
}

function writePendingSelection(
    editable: HTMLInputElement|HTMLTextAreaElement,
    snapshot: EditableSnapshot,
) {
  editable.dataset[kPendingSelectionStart] = String(snapshot.selectionStart);
  editable.dataset[kPendingSelectionEnd] = String(snapshot.selectionEnd);
}

function clearPendingSelection(editable: HTMLInputElement|HTMLTextAreaElement) {
  delete editable.dataset[kPendingSelectionStart];
  delete editable.dataset[kPendingSelectionEnd];
}

function captureEditableState(
    host: HTMLElement,
    selector: string,
): EditableSnapshot|null {
  const editable = host.querySelector(selector);
  if (!(editable instanceof HTMLInputElement) &&
      !(editable instanceof HTMLTextAreaElement)) {
    return null;
  }

  const pendingSelection = readPendingSelection(editable);
  const selectionStart = editable.selectionStart;
  const selectionEnd = editable.selectionEnd;

  return {
    selectionEnd: selectionEnd ?? pendingSelection?.selectionEnd ??
        editable.value.length,
    selectionStart: selectionStart ?? pendingSelection?.selectionStart ??
        editable.value.length,
    scrollTop: editable.scrollTop,
  };
}

function applyEditableSelection(
    editable: HTMLInputElement|HTMLTextAreaElement,
    snapshot: EditableSnapshot,
): {selectionEnd: number; selectionStart: number;} {
  const selectionStart = Math.min(snapshot.selectionStart, editable.value.length);
  const selectionEnd = Math.min(snapshot.selectionEnd, editable.value.length);

  if (document.activeElement === editable) {
    editable.setSelectionRange(selectionStart, selectionEnd);
  } else {
    editable.selectionStart = selectionStart;
    editable.selectionEnd = selectionEnd;
  }

  editable.scrollTop = snapshot.scrollTop;
  return {selectionEnd, selectionStart};
}

function restoreEditableState(
    host: HTMLElement,
    selector: string,
    snapshot: EditableSnapshot|null,
    restoreFocus: boolean,
) {
  if (!snapshot) {
    return;
  }

  const editable = host.querySelector(selector);
  if (!(editable instanceof HTMLInputElement) &&
      !(editable instanceof HTMLTextAreaElement)) {
    return;
  }

  if (!restoreFocus) {
    writePendingSelection(editable, snapshot);
    applyEditableSelection(editable, snapshot);
    return;
  }

  clearPendingSelection(editable);
  editable.focus({preventScroll: true});
  applyEditableSelection(editable, snapshot);

  queueMicrotask(() => {
    if (document.activeElement !== editable) {
      return;
    }

    applyEditableSelection(editable, snapshot);
  });
}

function createIconMarkup(symbol: 'spark'|'plus'|'ellipsis'): string {
  switch (symbol) {
    case 'spark':
      return '<svg viewBox="0 0 20 20" aria-hidden="true"><path d="M10 1.8l1.82 4.58 4.38 1.82-4.38 1.8L10 14.58 8.18 10 3.8 8.2l4.38-1.82L10 1.8zm5.24 10.66.98 2.48 2.48.98-2.48 1-.98 2.48-.98-2.48-2.48-1 2.48-.98.98-2.48zm-10.78.6.82 2.06 2.06.82-2.06.84-.82 2.06-.82-2.06-2.06-.84 2.06-.82.82-2.06z" fill="currentColor"/></svg>';
    case 'plus':
      return '<svg viewBox="0 0 20 20" aria-hidden="true"><path d="M10 4.2c.44 0 .8.36.8.8v4.2H15c.44 0 .8.36.8.8s-.36.8-.8.8h-4.2V15c0 .44-.36.8-.8.8s-.8-.36-.8-.8v-4.2H5c-.44 0-.8-.36-.8-.8s.36-.8.8-.8h4.2V5c0-.44.36-.8.8-.8z" fill="currentColor"/></svg>';
    case 'ellipsis':
      return '<svg viewBox="0 0 20 20" aria-hidden="true"><path d="M4.5 10a1.25 1.25 0 112.5 0 1.25 1.25 0 01-2.5 0zm4.25 0a1.25 1.25 0 112.5 0 1.25 1.25 0 01-2.5 0zm4.25 0a1.25 1.25 0 112.5 0 1.25 1.25 0 01-2.5 0z" fill="currentColor"/></svg>';
  }
}

function createIconButton(className: string, label: string, symbol: 'plus'|'ellipsis'):
    HTMLButtonElement {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = className;
  button.setAttribute('aria-label', label);
  button.innerHTML = createIconMarkup(symbol);
  return button;
}

function getLatestEvent(state: AppState): RuntimeEventRecord|null {
  if (!state.currentSessionId) {
    return null;
  }

  const entries = state.eventsBySessionId[state.currentSessionId] || [];
  return entries.at(-1)?.event || null;
}

function getEventTitle(event: RuntimeEventRecord): string {
  switch (event.kind) {
    case RuntimeEventKind.kUserPrompt:
      return 'Prompt sent';
    case RuntimeEventKind.kSessionStatus:
      return 'Session update';
    case RuntimeEventKind.kAssistantToken:
      return 'Drafting response';
    case RuntimeEventKind.kTurnComplete:
      return 'Latest response';
    case RuntimeEventKind.kToolRequest:
      return event.toolCall?.toolName || 'Tool request';
    case RuntimeEventKind.kToolResult:
      return event.toolResult?.success ? 'Tool finished' : 'Tool error';
    case RuntimeEventKind.kApprovalRequest:
      return 'Approval needed';
    case RuntimeEventKind.kApprovalResult:
      return event.approvalResult?.approved ? 'Approval granted' : 'Approval denied';
    case RuntimeEventKind.kError:
      return 'Runtime error';
    case RuntimeEventKind.kConnectionStateChanged:
      return 'Connection changed';
  }
}

function getEventPreview(event: RuntimeEventRecord): string {
  const rawText = event.text?.trim();
  if (rawText) {
    return rawText;
  }

  switch (event.kind) {
    case RuntimeEventKind.kToolRequest:
      return event.toolCall?.argumentsJson || 'Waiting for tool arguments.';
    case RuntimeEventKind.kToolResult:
      return event.toolResult?.errorMessage || event.toolResult?.output ||
          'Tool completed without output.';
    case RuntimeEventKind.kApprovalRequest:
      return event.approvalRequest?.description ||
          'The runtime paused and is waiting for approval.';
    case RuntimeEventKind.kApprovalResult:
      return event.approvalResult?.reason || 'Approval response recorded.';
    default:
      return 'Maho AI is ready for the next prompt.';
  }
}

function summarizeText(text: string, maxLength: number): string {
  const normalized = text.replace(/\s+/g, ' ').trim();
  if (normalized.length <= maxLength) {
    return normalized;
  }

  return `${normalized.slice(0, maxLength - 1).trimEnd()}…`;
}

function renderHeader(state: AppState, handlers: {onStartSession: () => void}): HTMLElement {
  const header = document.createElement('header');
  header.className = 'compact-panel__header';

  const brand = document.createElement('div');
  brand.className = 'compact-panel__brand';

  const brandMark = document.createElement('div');
  brandMark.className = 'compact-panel__brand-mark';
  brandMark.innerHTML = createIconMarkup('spark');

  const brandCopy = document.createElement('div');
  brandCopy.className = 'compact-panel__brand-copy';

  const eyebrow = document.createElement('span');
  eyebrow.className = 'compact-panel__eyebrow';
  eyebrow.textContent = 'Maho AI';

  const title = document.createElement('span');
  title.className = 'compact-panel__title';
  title.textContent = state.currentSessionId ?
      getConnectionStateLabel(state.connectionState) : 'Ready';

  brandCopy.append(eyebrow, title);
  brand.append(brandMark, brandCopy);

  const actions = document.createElement('div');
  actions.className = 'compact-panel__header-actions';

  const newSession = createIconButton(
      'compact-icon-button compact-icon-button--accent', 'Start new session',
      'plus');
  newSession.addEventListener('click', () => handlers.onStartSession());

  const menu = createIconButton(
      'compact-icon-button', 'Current runtime status', 'ellipsis');
  menu.disabled = true;

  actions.append(newSession, menu);
  header.append(brand, actions);
  return header;
}

function renderStage(state: AppState): HTMLElement {
  const stage = document.createElement('main');
  stage.className = 'compact-panel__stage';

  const halo = document.createElement('div');
  halo.className = 'compact-panel__halo';

  const center = document.createElement('div');
  center.className = 'compact-panel__center';

  const centerMark = document.createElement('div');
  centerMark.className = 'compact-panel__center-mark';
  centerMark.innerHTML = createIconMarkup('spark');

  const centerCopy = document.createElement('div');
  centerCopy.className = 'compact-panel__center-copy';

  const headline = document.createElement('h1');
  headline.className = 'compact-panel__headline';
  headline.textContent = state.currentSessionId ?
      'Ask about the current page' : 'Open a compact AI workspace';

  const description = document.createElement('p');
  description.className = 'compact-panel__description';
  description.textContent = state.currentSessionId ?
      'Minimal shell, live runtime wiring, and a focused prompt surface.' :
      'Start a live session and keep the UI focused on one prompt at a time.';

  centerCopy.append(headline, description);
  center.append(centerMark, centerCopy);

  const footerCard = document.createElement('section');
  footerCard.className = 'compact-panel__status-card';

  if (!state.currentSessionId) {
    const label = document.createElement('span');
    label.className = 'compact-panel__status-label';
    label.textContent = 'No live session';

    const body = document.createElement('p');
    body.className = 'compact-panel__status-copy';
    body.textContent = 'Use Analyze, Explain, or Summarize to draft the next prompt.';

    footerCard.append(label, body);
  } else {
    const session = state.sessionsById[state.currentSessionId];
    const latestEvent = getLatestEvent(state);

    const meta = document.createElement('div');
    meta.className = 'compact-panel__status-meta';

    const sessionTitle = document.createElement('span');
    sessionTitle.className = 'compact-panel__status-title';
    sessionTitle.textContent = session ? getSessionTitle(session) : 'Active session';

    const sessionBadge = document.createElement('span');
    sessionBadge.className = 'compact-panel__status-badge';
    sessionBadge.textContent = session ? getStatusLabel(session.status) :
        getConnectionStateLabel(state.connectionState);

    meta.append(sessionTitle, sessionBadge);

    const label = document.createElement('span');
    label.className = 'compact-panel__status-label';
    label.textContent = latestEvent ? getEventTitle(latestEvent) :
        getSessionAccessLabel(session);

    const body = document.createElement('p');
    body.className = 'compact-panel__status-copy';
    body.textContent = latestEvent ? summarizeText(getEventPreview(latestEvent), 150) :
        (isSessionReadOnly(session) ?
             'This archived session is view-only. Start a new live session to continue.' :
             'Your next prompt will continue in this session.');

    footerCard.append(meta, label, body);
  }

  stage.append(halo, center, footerCard);
  return stage;
}

function renderQuickActions(handlers: {onPromptChange: (value: string) => void}): HTMLElement {
  const group = document.createElement('div');
  group.className = 'compact-quick-actions';

  const presets = [
    ['Analyze', 'Analyze this page and surface the most important takeaways.'],
    ['Explain', 'Explain what is happening on this page in plain language.'],
    ['Summarize', 'Summarize this page into a concise set of bullets.'],
  ] as const;

  for (const [label, prompt] of presets) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'compact-quick-action';
    button.textContent = label;
    button.addEventListener('click', () => handlers.onPromptChange(prompt));
    group.appendChild(button);
  }

  return group;
}

class ConsoleShellRenderer {
  private readonly shell = document.createElement('div');
  private readonly panel = document.createElement('section');
  private readonly stageHost = document.createElement('div');
  private readonly actionsHost = document.createElement('div');
  private readonly composerHost = document.createElement('div');

  constructor(
      private readonly root: HTMLElement,
      private readonly store: MahoAiStore,
  ) {
    this.shell.className = 'console-shell';
    this.panel.className = 'compact-panel';
    this.stageHost.className = 'compact-panel__stage-host';
    this.actionsHost.className = 'compact-panel__actions-host';
    this.composerHost.className = 'compact-panel__composer-host';

    this.panel.append(this.stageHost, this.actionsHost, this.composerHost);
    this.shell.appendChild(this.panel);
    this.root.replaceChildren(this.shell);
  }

  render(state: AppState) {
    const composerWasFocused =
        this.composerHost.querySelector('.compact-composer__input') ===
        document.activeElement;
    const composerSnapshot =
        captureEditableState(this.composerHost, '.compact-composer__input');

    if (state.bootError) {
      this.panel.replaceChildren(createEmptyPanel('Bootstrap failed', state.bootError));
      return;
    }

    const header = renderHeader(state, {
      onStartSession: () => void this.store.startSession(),
    });

    const stage = renderStage(state);
    const actions = renderQuickActions({
      onPromptChange: value => this.store.setComposerPrompt(value),
    });
    const currentSession =
        state.currentSessionId ? state.sessionsById[state.currentSessionId] : null;
    const readOnly = isSessionReadOnly(currentSession);
    const actionButtons = actions.querySelectorAll<HTMLButtonElement>('.compact-quick-action');
    actionButtons.forEach(button => {
      button.disabled = readOnly;
    });

    this.stageHost.replaceChildren(header, stage);
    this.actionsHost.replaceChildren(actions);
    this.composerHost.replaceChildren(renderComposer(state, {
      onPromptChange: value => this.store.setComposerPrompt(value),
      onCancel: () => void this.store.cancelTurn(),
      onStartSession: () => void this.store.startSession(),
      onSubmit: () => {
        if (!this.store.getSnapshot().currentSessionId) {
          void this.store.startSession();
          return;
        }
        void this.store.submitPrompt();
      },
    }));

    restoreEditableState(
        this.composerHost,
        '.compact-composer__input',
        composerSnapshot,
        composerWasFocused);
  }
}

export function renderConsoleShell(root: HTMLElement, store: MahoAiStore) {
  const renderer = new ConsoleShellRenderer(root, store);
  store.subscribe(state => renderer.render(state));
}
