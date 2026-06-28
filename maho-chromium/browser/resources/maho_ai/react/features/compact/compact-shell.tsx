import type {AppState, CreditBalanceInfo, TimelineEntry, ViewMode} from '../../../types.js';
import type {MahoAiStore} from '../../../store.js';
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
      onTidyTabs,
      onOpenBuyCredits,
      readOnly,
      state,
      store,
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
      onTidyTabs?: () => void;
      onOpenBuyCredits: () => void;
      readOnly: boolean;
      state: AppState;
      store: MahoAiStore;
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
        onTidyTabs={onTidyTabs}
        onOpenBuyCredits={onOpenBuyCredits}
        store={store}
        creditBalance={state.creditBalance}
        state={state}
      />
      {hasMessages || thinkingLabel ? (
        <ConversationThread
          entries={entries}
          onRespondToApproval={onRespondToApproval}
          readOnly={readOnly}
          thinkingLabel={thinkingLabel}
          chatBlockedReason={state.chatBlockedReason}
          onOpenBuyCredits={onOpenBuyCredits}
        />
      ) : <CompactStage state={state} />}
    </>
  );
}
