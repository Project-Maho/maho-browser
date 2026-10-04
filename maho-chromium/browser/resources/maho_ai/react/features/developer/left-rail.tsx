import React from 'react';
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
import {cn} from '@lib/utils';
import {Button} from '@ui/button';
import {Badge} from '@ui/badge';
import {Input} from '@ui/input';

const mapToneToVariant = (tone: string): any => {
  if (tone === 'danger') return 'destructive';
  if (tone === 'neutral') return 'secondary';
  return tone;
};

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
    <aside className="flex min-h-0 w-[280px] shrink-0 flex-col gap-4 rounded-[1.5rem] border border-border/80 bg-card p-4 shadow-[var(--shadow-raised)] max-[1160px]:w-full max-[1160px]:max-h-[280px]">
      <div className="grid gap-3">
        <h1 className="text-lg font-semibold tracking-tight text-foreground">Maho AI Console</h1>
        <div className="flex flex-wrap gap-1.5">
          <Badge variant={mapToneToVariant(getConnectionStateTone(state.connectionState))}>
            {getConnectionStateLabel(state.connectionState)}
          </Badge>
          <Badge variant="secondary">
            {state.activeAdapterName || 'OpenCode'}
          </Badge>
        </div>
        <Button className="w-full" variant="default" onClick={onNewSession}>
          New session
        </Button>
        <div className="text-[11px] font-semibold uppercase tracking-[0.14em] text-muted-foreground">
          Sessions
        </div>
        <Input
          className="border-border bg-input/90 shadow-none"
          placeholder="Search sessions"
          value={state.sessionSearchQuery}
          onChange={event => onSearch(event.currentTarget.value)}
        />
      </div>
      <div className="flex min-h-0 flex-col gap-2 overflow-y-auto pr-1">
        {!filteredSessions.length ? (
          <p className="text-sm leading-6 text-muted-foreground">
            No sessions yet. Start a task to create one.
          </p>
        ) : filteredSessions.map(({sessionId, session}) => (
          <Button
            key={sessionId}
            className={cn(
                'h-auto w-full items-start justify-start gap-2 rounded-xl border border-border/80 bg-secondary/35 px-3 py-3 text-left text-foreground shadow-none hover:-translate-y-px hover:border-border hover:bg-surface-hover',
                sessionId === state.currentSessionId && 'border-primary/30 bg-primary/10 text-foreground')}
            variant="ghost"
            onClick={() => onResumeSession(sessionId)}>
            <div className="text-sm font-semibold">{getSessionTitle(session)}</div>
            <div className="text-xs text-muted-foreground">{getSessionSubtitle(session)}</div>
            <div className="text-[10px] text-muted-foreground">
              {`${getStatusLabel(session.status)} • ${session.eventCount} events • ${session.toolCallCount} tools`}
            </div>
            <div className="mt-1 flex flex-wrap gap-2">
              <Badge variant={isSessionReadOnly(session) ? 'warning' : 'success'}>
                {getSessionAccessLabel(session)}
              </Badge>
            </div>
          </Button>
        ))}
      </div>
    </aside>
  );
}
