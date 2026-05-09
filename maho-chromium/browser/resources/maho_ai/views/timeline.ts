import {
  AppState,
  getSessionAccessLabel,
  getStatusLabel,
  isSessionReadOnly,
} from '../types.js';
import {createEmptyPanel, createEventCard, createSectionLabel} from './cards.js';

export function renderTimeline(state: AppState): HTMLElement {
  const panel = document.createElement('main');
  panel.className = 'console-timeline';

  const header = document.createElement('div');
  header.className = 'console-panel-header';

  const title = document.createElement('h2');
  title.textContent = 'Timeline';

  const subtitle = document.createElement('p');
  subtitle.textContent = state.currentSessionId ?
      'Live runtime events, tool activity, and approval flow.' :
      'Create or resume a session to begin streaming runtime activity.';

  header.append(title, subtitle);
  panel.append(header);

  if (!state.currentSessionId) {
    panel.appendChild(createEmptyPanel(
        'No active session',
        'Use the left rail to start a session or reopen recent work.'));
    return panel;
  }

  const session = state.sessionsById[state.currentSessionId];
  if (session) {
    const statusRow = document.createElement('div');
    statusRow.className = 'console-panel-status-row';
    statusRow.append(
        createSectionLabel('Session state'),
        createSectionLabel(getStatusLabel(session.status)),
        createSectionLabel(getSessionAccessLabel(session)));
    panel.appendChild(statusRow);
  }

  const stream = document.createElement('div');
  stream.className = 'timeline-stream';
  stream.appendChild(createSectionLabel('Event stream'));

  const events = state.eventsBySessionId[state.currentSessionId] || [];
  if (!events.length) {
    const history = state.historiesBySessionId[state.currentSessionId];
    const isReadOnlySession = isSessionReadOnly(session);
    const title = history?.loaded ?
        (isReadOnlySession ? 'No historical events stored' : 'No runtime events yet') :
        (isReadOnlySession ? 'Loading historical session' : 'Connecting to session stream');
    const description = history?.loaded ?
        (isReadOnlySession ?
             'This session is available as stored history only. The runtime did not return any persisted events.' :
             'The session exists, but the runtime has not emitted any renderable events yet.') :
        (isReadOnlySession ?
             'Fetching persisted events for this read-only session.' :
             'Waiting for replayed history or live runtime events.');
    stream.appendChild(createEmptyPanel(title, description));
  } else {
    for (const entry of events) {
      stream.appendChild(createEventCard(entry.event));
    }
  }

  panel.appendChild(stream);
  return panel;
}
