import {
  AppState,
  getConnectionStateTone,
  getSessionAccessLabel,
  getConnectionStateLabel,
  getSessionSubtitle,
  getStatusLabel,
  getSessionTitle,
  isSessionReadOnly,
} from '../types.js';
import {createPill, createSectionLabel} from './cards.js';

export function renderLeftRail(state: AppState, handlers: {
  onNewSession: () => void;
  onSearch: (query: string) => void;
  onResumeSession: (sessionId: string) => void;
}): HTMLElement {
  const rail = document.createElement('aside');
  rail.className = 'console-left-rail';

  const top = document.createElement('div');
  top.className = 'console-left-rail__top';

  const title = document.createElement('h1');
  title.className = 'console-app-title';
  title.textContent = 'Maho AI Console';

  const status = document.createElement('div');
  status.className = 'runtime-status';
  const tone = getConnectionStateTone(state.connectionState);
  status.append(
      createPill(getConnectionStateLabel(state.connectionState), tone),
      createPill(state.activeAdapterName || 'OpenCode', 'neutral'));

  const newSession = document.createElement('button');
  newSession.className = 'console-button console-button--primary';
  newSession.textContent = 'New session';
  newSession.addEventListener('click', () => handlers.onNewSession());

  const search = document.createElement('input');
  search.className = 'console-input';
  search.placeholder = 'Search sessions';
  search.value = state.sessionSearchQuery;
  search.addEventListener('input', () => handlers.onSearch(search.value));

  top.append(title, status, newSession, createSectionLabel('Sessions'), search);

  const list = document.createElement('div');
  list.className = 'session-list';

  const query = state.sessionSearchQuery.trim().toLowerCase();
  const filteredSessions: Array<{
    sessionId: string;
    session: AppState['sessionsById'][string];
  }> = [];
  for (const sessionId of state.sessionOrder) {
    const session = state.sessionsById[sessionId];
    if (!session) {
      continue;
    }
    if (query && !getSessionTitle(session).toLowerCase().includes(query) &&
        !getSessionSubtitle(session).toLowerCase().includes(query)) {
      continue;
    }
    filteredSessions.push({sessionId, session});
  }

  if (!filteredSessions.length) {
    const empty = document.createElement('p');
    empty.className = 'session-list__empty';
    empty.textContent = 'No sessions yet. Start a task to create one.';
    list.appendChild(empty);
  }

  for (const {sessionId, session} of filteredSessions) {
    const button = document.createElement('button');
    button.className = 'session-list__item';
    if (sessionId === state.currentSessionId) {
      button.classList.add('is-active');
    }

    const heading = document.createElement('div');
    heading.className = 'session-list__title';
    heading.textContent = getSessionTitle(session);

    const summary = document.createElement('div');
    summary.className = 'session-list__summary';
    summary.textContent = getSessionSubtitle(session);

    const meta = document.createElement('div');
    meta.className = 'session-list__meta';
    meta.textContent =
        `${getStatusLabel(session.status)} • ${session.eventCount} events • ${session.toolCallCount} tools`;

    const access = document.createElement('div');
    access.className = 'session-list__access';
    access.appendChild(createPill(
        getSessionAccessLabel(session),
        isSessionReadOnly(session) ? 'warning' : 'success'));

    button.append(heading, summary, meta, access);
    button.addEventListener('click', () => handlers.onResumeSession(sessionId));
    list.appendChild(button);
  }

  rail.append(top, list);
  return rail;
}
