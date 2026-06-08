import type {AppState, TimelineEntry, ViewMode} from '../../../types.js';
import {CompactStage} from './compact-stage.js';
import {CompactTopbar} from './compact-topbar.js';
import {ConversationThread} from './conversation-thread.js';

export function CompactShell(
    {
      entries,
      hasMessages,
      onClosePanel,
      onGetViewMode,
      onOpenSettings,
      onRespondToApproval,
      onSetViewMode,
      onStartSession,
      readOnly,
      state,
      thinkingLabel,
    }: {
      entries: TimelineEntry[];
      hasMessages: boolean;
      onClosePanel: () => void;
      onGetViewMode: () => Promise<ViewMode>;
      onOpenSettings: () => void;
      onRespondToApproval: (approvalId: string, approved: boolean) => void;
      onSetViewMode: (mode: ViewMode) => void;
      onStartSession: () => void;
      readOnly: boolean;
      state: AppState;
      thinkingLabel: string | null;
    }) {
  return (
    <>
      <CompactTopbar
        onClosePanel={onClosePanel}
        onGetViewMode={onGetViewMode}
        onOpenSettings={onOpenSettings}
        onSetViewMode={onSetViewMode}
        onStartSession={onStartSession}
        state={state}
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
