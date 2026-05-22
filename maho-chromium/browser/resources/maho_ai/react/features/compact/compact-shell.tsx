import type {AppState, TimelineEntry} from '../../../types.js';
import {CompactHeader} from './compact-header.js';
import {CompactStage} from './compact-stage.js';
import {ConversationThread} from './conversation-thread.js';

export function CompactShell(
    {
      entries,
      hasMessages,
      onRespondToApproval,
      onSetDeveloperMode,
      onStartSession,
      readOnly,
      state,
      thinkingLabel,
    }: {
      entries: TimelineEntry[];
      hasMessages: boolean;
      onRespondToApproval: (approvalId: string, approved: boolean) => void;
      onSetDeveloperMode: (enabled: boolean) => void;
      onStartSession: () => void;
      readOnly: boolean;
      state: AppState;
      thinkingLabel: string | null;
    }) {
  return (
    <>
      <CompactHeader
        hasMessages={hasMessages}
        onSetDeveloperMode={onSetDeveloperMode}
        onStartSession={onStartSession}
      />
      {hasMessages || thinkingLabel ? (
        <ConversationThread
          entries={entries}
          onRespondToApproval={onRespondToApproval}
          readOnly={readOnly}
          thinkingLabel={thinkingLabel}
        />
      ) : <CompactStage state={state} />}
    </>
  );
}
