import type {AppState, TimelineEntry} from '../../../types.js';
import {CompactTopbar} from './compact-topbar.js';
import {CompactStage} from './compact-stage.js';
import {ConversationThread} from './conversation-thread.js';
import {ViewMode} from '../../maho_ai.mojom-webui.js';

export function CompactShell(
    {
      entries,
      hasMessages,
      onRespondToApproval,
      onSetDeveloperMode,
      onStartSession,
      onClosePanel,
      onOpenSettings,
      onSetViewMode,
      onGetViewMode,
      readOnly,
      state,
      thinkingLabel,
    }: {
      entries: TimelineEntry[];
      hasMessages: boolean;
      onRespondToApproval: (approvalId: string, approved: boolean) => void;
      onSetDeveloperMode: (enabled: boolean) => void;
      onStartSession: () => void;
      onClosePanel: () => void;
      onOpenSettings: () => void;
      onSetViewMode: (mode: ViewMode) => void;
      onGetViewMode: () => Promise<ViewMode>;
      readOnly: boolean;
      state: AppState;
      thinkingLabel: string | null;
    }) {
  return (
    <>
      <CompactTopbar
        onClosePanel={onClosePanel}
        onOpenSettings={onOpenSettings}
        onSetViewMode={onSetViewMode}
        onGetViewMode={onGetViewMode}
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

