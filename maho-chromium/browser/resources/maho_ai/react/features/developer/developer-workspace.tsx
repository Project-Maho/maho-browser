import React from 'react';
import type {AppState} from '../../../types.js';
import {getConnectionStateLabel, getStatusLabel} from '../../../types.js';
import {Button} from '@ui/button';
import {Badge} from '@ui/badge';
import {Inspector} from './inspector.js';
import {LeftRail} from './left-rail.js';
import {ModeToggle} from './mode-toggle.js';
import {Timeline} from './timeline.js';
import {DismissibleMcpGuidanceCard} from '../dismissible-mcp-guidance-card.js';

export function DeveloperWorkspace(
    {
      onNewSession,
      onConnect,
      onOpenSettings,
      onRespondToApproval,
      onResumeSession,
      onSearch,
      onSelectInspector,
      onSetDeveloperMode,
      state,
    }: {
      onNewSession: () => void;
      onConnect?: () => void;
      onOpenSettings: (paneKey: string) => void;
      onRespondToApproval: (approvalId: string, approved: boolean) => void;
      onResumeSession: (sessionId: string) => void;
      onSearch: (query: string) => void;
      onSelectInspector: (kind: AppState['selectedInspector']['kind'], id?: string) => void;
      onSetDeveloperMode: (enabled: boolean) => void;
      state: AppState;
    }) {
  const session = state.currentSessionId ? state.sessionsById[state.currentSessionId] : null;

  return (
    <div className="grid h-full min-h-0 grid-rows-[auto_minmax(0,1fr)] gap-3">
      <header className="flex flex-wrap items-center justify-between gap-4 rounded-[1.5rem] border border-border/80 bg-card p-4 shadow-[var(--shadow-raised)]">
        <div className="min-w-0 flex-1">
          <div className="grid gap-1">
            <h1 className="text-lg font-semibold tracking-tight text-foreground">Maho AI Console</h1>
            <p className="text-sm leading-6 text-muted-foreground">
              {state.currentSessionId ?
                'Console views for sessions, event flow, and runtime inspection.' :
                'Switch back to Assistant for the compact single-thread surface.'}
            </p>
          </div>
          <div className="mt-3 flex flex-wrap gap-1.5">
            <Badge variant="secondary">{getConnectionStateLabel(state.connectionState)}</Badge>
            <Badge variant="secondary">{state.activeAdapterName || 'OpenCode'}</Badge>
            {session ? <Badge variant="success">{getStatusLabel(session.status)}</Badge> : null}
          </div>
          <DismissibleMcpGuidanceCard className="mt-3" onConnect={onConnect} />
        </div>
        <div className="flex flex-wrap items-center gap-3">
          <ModeToggle
            developerMode={state.developerMode}
            onSetDeveloperMode={onSetDeveloperMode}
          />
          <Button variant="default" onClick={onNewSession}>
            New session
          </Button>
        </div>
      </header>
      <div className="flex min-h-0 gap-3 max-[1160px]:flex-col">
        <LeftRail
          onNewSession={onNewSession}
          onResumeSession={onResumeSession}
          onSearch={onSearch}
          state={state}
        />
        <Timeline onOpenSettings={onOpenSettings} state={state} />
        <Inspector
          onRespondToApproval={onRespondToApproval}
          onSelectInspector={onSelectInspector}
          state={state}
        />
      </div>
    </div>
  );
}
