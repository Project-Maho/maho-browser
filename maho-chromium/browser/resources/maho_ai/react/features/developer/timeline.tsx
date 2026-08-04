import type {AppState} from '../../../types.js';
import {
  getSessionAccessLabel,
  getStatusLabel,
  isSessionReadOnly,
} from '../../../types.js';
import {useAutoScroll} from '../../hooks/use-auto-scroll.js';
import {EmptyState} from '../../components/empty-state.js';
import {EventCard} from './event-card.js';

export function Timeline(
    {
      onOpenSettings,
      state,
    }: {
      onOpenSettings: (paneKey: string) => void;
      state: AppState;
    }) {
  const events = state.currentSessionId ?
      (state.eventsBySessionId[state.currentSessionId] || []) :
      [];
  const session = state.currentSessionId ? state.sessionsById[state.currentSessionId] : null;
  const streamRef = useAutoScroll<HTMLDivElement>(events);

  return (
    <main className="grid min-h-0 flex-1 grid-rows-[auto_minmax(0,1fr)] gap-3 rounded-[1.5rem] border border-border/80 bg-card p-4 shadow-[var(--shadow-raised)]">
      <div className="grid gap-2">
        <h2 className="text-lg font-semibold tracking-tight text-foreground">Timeline</h2>
        <p className="text-sm leading-6 text-muted-foreground">
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
        <div ref={streamRef} className="flex min-h-0 flex-col gap-3 overflow-y-auto pr-1">
          {session ? (
            <div className="flex flex-wrap gap-2 text-[11px] font-semibold uppercase tracking-[0.14em] text-muted-foreground">
              <span className="rounded-full bg-secondary px-3 py-1 text-secondary-foreground">Session state</span>
              <span className="rounded-full bg-secondary px-3 py-1 text-secondary-foreground">{getStatusLabel(session.status)}</span>
              <span className="rounded-full bg-secondary px-3 py-1 text-secondary-foreground">{getSessionAccessLabel(session)}</span>
            </div>
          ) : null}
          <div className="text-[11px] font-semibold uppercase tracking-[0.14em] text-muted-foreground">
            Event stream
          </div>
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
              <EventCard
                key={`${entry.event.sequence}-${entry.event.timestamp}`}
                event={entry.event}
                onOpenSettings={onOpenSettings}
              />
            ))
          )}
        </div>
      )}
    </main>
  );
}
