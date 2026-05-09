import {sanitizeInnerHtml} from 'chrome://resources/js/parse_html_subset.js';
import {
  RuntimeEventKind,
  ToolCallStatus,
} from '../maho_ai.mojom-webui.js';
import {
  getSessionAccessLabel,
  getEventText,
  getRuntimeStateLabel,
  getStatusLabel,
  isSessionReadOnly,
  RuntimeEventRecord,
  SessionRecord,
} from '../types.js';

function escapeHtml(text: string): string {
  const div = document.createElement('div');
  div.textContent = text;
  return div.innerHTML;
}

function renderInline(text: string): string {
  let html = escapeHtml(text);
  html = html.replace(/`([^`]+)`/g, '<kbd>$1</kbd>');
  html = html.replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>');
  html = html.replace(/\*([^*]+)\*/g, '<em>$1</em>');
  html = html.replace(/\[([^\]]+)\]\((https:\/\/[^)]+)\)/g, '<a href="$2" target="_blank">$1</a>');
  return html.replace(/\n/g, '<br>');
}

export function renderTrustedMarkdown(raw: string): TrustedHTML {
  return sanitizeInnerHtml(`<p>${renderInline(raw)}</p>`, {tags: ['kbd', 'p', 'br', 'strong', 'em', 'a']});
}

export function createSectionLabel(text: string): HTMLElement {
  const label = document.createElement('div');
  label.className = 'console-section-label';
  label.textContent = text;
  return label;
}

export function createPill(text: string, tone = 'neutral'): HTMLElement {
  const pill = document.createElement('span');
  pill.className = `console-pill console-pill--${tone}`;
  pill.textContent = text;
  return pill;
}

export function createKeyValueRow(label: string, value: string): HTMLElement {
  const row = document.createElement('div');
  row.className = 'inspector-row';

  const term = document.createElement('span');
  term.className = 'inspector-row__label';
  term.textContent = label;

  const detail = document.createElement('span');
  detail.className = 'inspector-row__value';
  detail.textContent = value;

  row.append(term, detail);
  return row;
}

export function createEmptyPanel(title: string, description: string): HTMLElement {
  const panel = document.createElement('section');
  panel.className = 'empty-panel';

  const heading = document.createElement('h2');
  heading.textContent = title;

  const body = document.createElement('p');
  body.textContent = description;

  panel.append(heading, body);
  return panel;
}

export function createSessionSummaryCard(session: SessionRecord): HTMLElement {
  const card = document.createElement('section');
  card.className = 'inspector-card';
  card.append(
      createSectionLabel('Session'),
      createKeyValueRow('Status', getStatusLabel(session.status)),
      createKeyValueRow('Access', getSessionAccessLabel(session)),
      createKeyValueRow('Adapter', session.adapterName),
      createKeyValueRow('Events', String(session.eventCount)),
      createKeyValueRow('Tools', String(session.toolCallCount)));
  if (isSessionReadOnly(session)) {
    card.appendChild(createPill('Historical session', 'warning'));
  }
  return card;
}

function getToolStatusLabel(status: ToolCallStatus): string {
  switch (status) {
    case ToolCallStatus.kPending:
      return 'Pending';
    case ToolCallStatus.kRunning:
      return 'Running';
    case ToolCallStatus.kCompleted:
      return 'Completed';
    case ToolCallStatus.kFailed:
      return 'Failed';
    case ToolCallStatus.kCancelled:
      return 'Cancelled';
  }
}

export function createEventCard(event: RuntimeEventRecord): HTMLElement {
  const card = document.createElement('article');
  card.className = 'timeline-card';

  const header = document.createElement('div');
  header.className = 'timeline-card__header';

  const title = document.createElement('h3');
  title.className = 'timeline-card__title';

  const meta = document.createElement('div');
  meta.className = 'timeline-card__meta';
  meta.textContent = new Date(event.timestamp * 1000).toLocaleTimeString([], {
    hour: 'numeric',
    minute: '2-digit',
  });

  const body = document.createElement('div');
  body.className = 'timeline-card__body';

  switch (event.kind) {
    case RuntimeEventKind.kUserPrompt:
      card.dataset['kind'] = 'user';
      title.textContent = 'User prompt';
      body.innerHTML = renderTrustedMarkdown(getEventText(event));
      break;
    case RuntimeEventKind.kSessionStatus:
      card.dataset['kind'] = 'session';
      title.textContent = 'Session status';
      body.textContent = getRuntimeStateLabel(getEventText(event));
      break;
    case RuntimeEventKind.kAssistantToken:
      card.dataset['kind'] = 'assistant';
      title.textContent = 'Assistant streaming';
      body.innerHTML = renderTrustedMarkdown(getEventText(event));
      break;
    case RuntimeEventKind.kTurnComplete:
      card.dataset['kind'] = 'assistant-final';
      title.textContent = 'Assistant response';
      body.innerHTML = renderTrustedMarkdown(getEventText(event));
      break;
    case RuntimeEventKind.kToolRequest:
      card.dataset['kind'] = 'tool';
      title.textContent = event.toolCall?.toolName || 'Tool request';
      header.appendChild(createPill(getToolStatusLabel(event.toolCall?.status || ToolCallStatus.kPending), 'accent'));
      body.textContent = event.toolCall?.argumentsJson || 'Waiting for tool arguments';
      break;
    case RuntimeEventKind.kToolResult:
      card.dataset['kind'] = 'tool-result';
      title.textContent = 'Tool result';
      header.appendChild(createPill(event.toolResult?.success ? 'Success' : 'Error', event.toolResult?.success ? 'success' : 'danger'));
      body.textContent = event.toolResult?.errorMessage || event.toolResult?.output || 'No tool output';
      break;
    case RuntimeEventKind.kApprovalRequest:
      card.dataset['kind'] = 'approval';
      title.textContent = 'Approval required';
      header.appendChild(createPill('Pending', 'warning'));
      body.textContent = event.approvalRequest?.description || 'The runtime is waiting for approval.';
      break;
    case RuntimeEventKind.kApprovalResult:
      card.dataset['kind'] = 'approval-result';
      title.textContent = 'Approval decision';
      header.appendChild(createPill(event.approvalResult?.approved ? 'Approved' : 'Denied', event.approvalResult?.approved ? 'success' : 'danger'));
      body.textContent = event.approvalResult?.reason || 'Approval response recorded';
      break;
    case RuntimeEventKind.kError:
      card.dataset['kind'] = 'error';
      title.textContent = 'Runtime error';
      header.appendChild(createPill('Error', 'danger'));
      body.textContent = getEventText(event) || 'The runtime reported an error.';
      break;
    case RuntimeEventKind.kConnectionStateChanged:
      card.dataset['kind'] = 'connection';
      title.textContent = 'Connection state';
      body.textContent = getRuntimeStateLabel(event.text);
      break;
  }

  header.prepend(title);
  header.append(meta);
  card.append(header, body);
  return card;
}
