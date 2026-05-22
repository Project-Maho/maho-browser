import type {AppState} from '../../../types.js';
import {
  getConnectionStateLabel,
  getConnectionStateTone,
  getSessionAccessLabel,
  getSessionSubtitle,
  getSessionTitle,
  getStatusLabel,
  isSessionReadOnly,
} from '../../../types.js';
import {cn} from '../../lib/cn.js';
import {Button} from '../../ui/button.js';
import {Pill} from '../../ui/pill.js';

export function LeftRail(
    {
      onNewSession,
      onResumeSession,
      onSearch,
      state,
    }: {
      onNewSession: () => void;
      onResumeSession: (sessionId: string) => void;
      onSearch: (query: string) => void;
      state: AppState;
    }) {
  const query = state.sessionSearchQuery.trim().toLowerCase();
  const filteredSessions = state.sessionOrder.flatMap(sessionId => {
    const session = state.sessionsById[sessionId];
    if (!session) {
      return [];
    }
    if (query && !getSessionTitle(session).toLowerCase().includes(query) &&
        !getSessionSubtitle(session).toLowerCase().includes(query)) {
      return [];
    }
    return [{sessionId, session}];
  });

  return (
    <aside className="console-left-rail">
      <div className="console-left-rail__top">
        <h1 className="console-app-title">Maho AI Console</h1>
        <div className="runtime-status">
          <Pill text={getConnectionStateLabel(state.connectionState)} tone={getConnectionStateTone(state.connectionState) as 'danger' | 'success' | 'warning'} />
          <Pill text={state.activeAdapterName || 'OpenCode'} tone="neutral" />
        </div>
        <Button variant="console-primary" onClick={onNewSession}>
          New session
        </Button>
        <div className="console-section-label">Sessions</div>
        <input
          className="console-input"
          placeholder="Search sessions"
          value={state.sessionSearchQuery}
          onChange={event => onSearch(event.currentTarget.value)}
        />
      </div>
      <div className="session-list">
        {!filteredSessions.length ? (
          <p className="session-list__empty">No sessions yet. Start a task to create one.</p>
        ) : filteredSessions.map(({sessionId, session}) => (
          <Button
            key={sessionId}
            className={cn(sessionId === state.currentSessionId && 'is-active')}
            variant="session-list-item"
            onClick={() => onResumeSession(sessionId)}>
            <div className="session-list__title">{getSessionTitle(session)}</div>
            <div className="session-list__summary">{getSessionSubtitle(session)}</div>
            <div className="session-list__meta">
              {`${getStatusLabel(session.status)} • ${session.eventCount} events • ${session.toolCallCount} tools`}
            </div>
            <div className="session-list__access">
              <Pill
                text={getSessionAccessLabel(session)}
                tone={isSessionReadOnly(session) ? 'warning' : 'success'}
              />
            </div>
          </Button>
        ))}
      </div>
    </aside>
  );
}
