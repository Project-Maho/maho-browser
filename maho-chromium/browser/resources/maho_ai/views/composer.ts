import {AppState, getSessionTitle, isSessionReadOnly} from '../types.js';

function createFooterIcon(symbol: 'plus'|'stop'|'spark'): string {
  switch (symbol) {
    case 'plus':
      return '<svg viewBox="0 0 20 20" aria-hidden="true"><path d="M10 4.2c.44 0 .8.36.8.8v4.2H15c.44 0 .8.36.8.8s-.36.8-.8.8h-4.2V15c0 .44-.36.8-.8.8s-.8-.36-.8-.8v-4.2H5c-.44 0-.8-.36-.8-.8s.36-.8.8-.8h4.2V5c0-.44.36-.8.8-.8z" fill="currentColor"/></svg>';
    case 'stop':
      return '<svg viewBox="0 0 20 20" aria-hidden="true"><path d="M6.2 5.4h7.6c.44 0 .8.36.8.8v7.6c0 .44-.36.8-.8.8H6.2a.8.8 0 01-.8-.8V6.2c0-.44.36-.8.8-.8z" fill="currentColor"/></svg>';
    case 'spark':
      return '<svg viewBox="0 0 20 20" aria-hidden="true"><path d="M10 2.2l1.54 3.88 3.88 1.54-3.88 1.54L10 13.06 8.46 9.18 4.58 7.64l3.88-1.56L10 2.2zm4.76 10.18.78 1.94 1.94.78-1.94.8-.78 1.94-.78-1.94-1.94-.8 1.94-.78.78-1.94z" fill="currentColor"/></svg>';
  }
}

function createFooterButton(
    label: string,
    symbol: 'plus'|'stop'|'spark',
    className = 'compact-composer__icon-button',
): HTMLButtonElement {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = className;
  button.setAttribute('aria-label', label);
  button.innerHTML = createFooterIcon(symbol);
  return button;
}

export function renderComposer(state: AppState, handlers: {
  onPromptChange: (value: string) => void;
  onSubmit: () => void;
  onCancel: () => void;
  onStartSession: () => void;
}): HTMLElement {
  const session = state.currentSessionId ? state.sessionsById[state.currentSessionId] : null;
  const readOnly = isSessionReadOnly(session);

  const panel = document.createElement('section');
  panel.className = 'compact-composer';

  const pageRow = document.createElement('div');
  pageRow.className = 'compact-composer__page-row';

  const pageChip = document.createElement('div');
  pageChip.className = 'compact-composer__page-chip';

  const pageChipIcon = document.createElement('span');
  pageChipIcon.className = 'compact-composer__page-chip-icon';
  pageChipIcon.textContent = '↗';

  const pageChipCopy = document.createElement('div');
  pageChipCopy.className = 'compact-composer__page-chip-copy';

  const pageChipLabel = document.createElement('span');
  pageChipLabel.className = 'compact-composer__page-chip-label';
  pageChipLabel.textContent = 'Page';

  const pageChipTitle = document.createElement('span');
  pageChipTitle.className = 'compact-composer__page-chip-title';
  pageChipTitle.textContent = state.currentSessionId && session ?
      getSessionTitle(session) : 'Current page';

  pageChipCopy.append(pageChipLabel, pageChipTitle);
  pageChip.append(pageChipIcon, pageChipCopy);

  const contextChip = document.createElement('div');
  contextChip.className = 'compact-composer__context-chip';
  contextChip.textContent = readOnly ? 'History only' :
      (state.composer.attachBrowserContext ? 'Context on' : 'Context off');

  pageRow.append(pageChip, contextChip);

  const textarea = document.createElement('textarea');
  textarea.className = 'compact-composer__input';
  textarea.placeholder = readOnly ?
      'This archived session is view-only. Start a new live session to continue.' :
      (state.currentSessionId ? 'Ask anything about this page…' :
                                'Ask Maho to analyze this page…');
  textarea.value = state.composer.prompt;
  textarea.rows = 3;
  textarea.disabled = readOnly;
  textarea.addEventListener('input', () => handlers.onPromptChange(textarea.value));
  textarea.addEventListener('keydown', (event: KeyboardEvent) => {
    if (readOnly) {
      return;
    }

    if (event.key === 'Enter' && !event.shiftKey) {
      event.preventDefault();
      handlers.onSubmit();
    }
  });

  const footer = document.createElement('div');
  footer.className = 'compact-composer__footer';

  const footerActions = document.createElement('div');
  footerActions.className = 'compact-composer__footer-actions';

  const newSession = createFooterButton('Start new session', 'plus');
  newSession.addEventListener('click', () => handlers.onStartSession());

  const cancel = createFooterButton('Cancel current turn', 'stop');
  cancel.disabled =
      readOnly || !state.currentSessionId || !state.turnPendingBySessionId[state.currentSessionId];
  cancel.addEventListener('click', () => handlers.onCancel());

  const hint = document.createElement('span');
  hint.className = 'compact-composer__hint';
  hint.textContent = readOnly ? 'Start a new live session to continue' : 'Enter send · Shift+Enter newline';

  footerActions.append(newSession, cancel, hint);

  const submit = document.createElement('button');
  submit.type = 'button';
  submit.className = 'compact-composer__send';
  submit.disabled = readOnly || !state.composer.prompt.trim();
  submit.addEventListener('click', () => handlers.onSubmit());

  const submitSpark = document.createElement('span');
  submitSpark.className = 'compact-composer__send-icon';
  submitSpark.innerHTML = createFooterIcon('spark');

  const submitLabel = document.createElement('span');
  submitLabel.className = 'compact-composer__send-label';
  submitLabel.textContent = state.currentSessionId ? 'Send' : 'Start';

  submit.append(submitSpark, submitLabel);
  footer.append(footerActions, submit);
  panel.append(pageRow, textarea, footer);
  return panel;
}
