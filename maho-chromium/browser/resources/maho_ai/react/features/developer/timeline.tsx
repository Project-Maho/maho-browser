import type {AppState} from '../../../types.js';
import {
  getSessionAccessLabel,
  getStatusLabel,
  isSessionReadOnly,
} from '../../../types.js';
import {useAutoScroll} from '../../hooks/use-auto-scroll.js';
import {EmptyState} from '../../ui/empty-state.js';
import {EventCard} from './event-card.js';

export function Timeline({state}: {state: AppState}) {
  const events = state.currentSessionId ?
      (state.eventsBySessionId[state.currentSessionId] || []) :
      [];
  const session = state.currentSessionId ? state.sessionsById[state.currentSessionId] : null;
  const streamRef = useAutoScroll<HTMLDivElement>(events);

  return (
    <main className="console-timeline">
      <div className="console-panel-header">
        <h2>Timeline</h2>
        <p>
          {state.currentSessionId ?
            'Live runtime events, tool activity, and approval flow.' :
            'Create or resume a session to begin streaming runtime activity.'}
        </p>
      </div>
      {!state.currentSessionId ? (
        <EmptyState
          description="Use the left rail to start a session or reopen recent work."
          title="No active session"
        />
      ) : (
        <>
          {session ? (
            <div className="console-panel-status-row">
              <div className="console-section-label">Session state</div>
              <div className="console-section-label">{getStatusLabel(session.status)}</div>
              <div className="console-section-label">{getSessionAccessLabel(session)}</div>
            </div>
          ) : null}
          <div ref={streamRef} className="timeline-stream">
            <div className="console-section-label">Event stream</div>
            {!events.length ? (
              (() => {
                const history =
                    state.currentSessionId ? state.historiesBySessionId[state.currentSessionId] : null;
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
                return <EmptyState description={description} title={title} />;
              })()
            ) : (
              events.map(entry => (
                <EventCard key={`${entry.event.sequence}-${entry.event.timestamp}`} event={entry.event} />
              ))
            )}
          </div>
        </>
      )}
    </main>
  );
}
