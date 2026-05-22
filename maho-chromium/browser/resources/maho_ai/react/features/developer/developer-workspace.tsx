import type {AppState} from '../../../types.js';
import {getConnectionStateLabel, getStatusLabel} from '../../../types.js';
import {Button} from '../../ui/button.js';
import {Pill} from '../../ui/pill.js';
import {Inspector} from './inspector.js';
import {LeftRail} from './left-rail.js';
import {ModeToggle} from './mode-toggle.js';
import {Timeline} from './timeline.js';

export function DeveloperWorkspace(
    {
      onNewSession,
      onRespondToApproval,
      onResumeSession,
      onSearch,
      onSelectInspector,
      onSetDeveloperMode,
      state,
    }: {
      onNewSession: () => void;
      onRespondToApproval: (approvalId: string, approved: boolean) => void;
      onResumeSession: (sessionId: string) => void;
      onSearch: (query: string) => void;
      onSelectInspector: (kind: AppState['selectedInspector']['kind'], id?: string) => void;
      onSetDeveloperMode: (enabled: boolean) => void;
      state: AppState;
    }) {
  const session = state.currentSessionId ? state.sessionsById[state.currentSessionId] : null;

  return (
    <div className="developer-console">
      <header className="developer-console__topbar">
        <div className="developer-console__summary">
          <div className="developer-console__brand">
            <h1 className="developer-console__title">Maho AI Console</h1>
            <p className="developer-console__subtitle">
              {state.currentSessionId ?
                'Console views for sessions, event flow, and runtime inspection.' :
                'Switch back to Assistant for the compact single-thread surface.'}
            </p>
          </div>
          <div className="developer-console__pills">
            <Pill text={getConnectionStateLabel(state.connectionState)} tone="neutral" />
            <Pill text={state.activeAdapterName || 'OpenCode'} tone="accent" />
            {session ? <Pill text={getStatusLabel(session.status)} tone="success" /> : null}
          </div>
        </div>
        <div className="developer-console__actions">
          <ModeToggle
            developerMode={state.developerMode}
            onSetDeveloperMode={onSetDeveloperMode}
          />
          <Button variant="console-primary" onClick={onNewSession}>
            New session
          </Button>
        </div>
      </header>
      <div className="developer-console__workspace">
        <LeftRail
          onNewSession={onNewSession}
          onResumeSession={onResumeSession}
          onSearch={onSearch}
          state={state}
        />
        <Timeline state={state} />
        <Inspector
          onRespondToApproval={onRespondToApproval}
          onSelectInspector={onSelectInspector}
          state={state}
        />
      </div>
    </div>
  );
}
