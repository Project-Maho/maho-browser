import React from 'react';
import {CompactStage} from './compact-stage.js';
import {CompactTopbar} from './compact-topbar.js';
import {ConversationThread} from './conversation-thread.js';
import type {MahoAiStore} from '../../../store.js';
import type {AppState, TimelineEntry, ViewMode} from '../../../types.js';

import {TrustCeremony} from '../trust-ceremony/trust-ceremony.js';

export function CompactShell(
    {
      store,
      entries,
      hasMessages,
      onClosePanel,
      onGetViewMode,
      onOpenSettings,
      onRespondToApproval,
      onSetViewMode,
      onStartSession,
      onTidyTabs,
      readOnly,
      state,
      thinkingLabel,
    }: {
      store: MahoAiStore;
      entries: TimelineEntry[];
      hasMessages: boolean;
      onClosePanel: () => void;
      onGetViewMode: () => Promise<ViewMode>;
      onOpenSettings: () => void;
      onRespondToApproval: (approvalId: string, approved: boolean) => void;
      onSetViewMode: (mode: ViewMode) => void;
      onStartSession: () => void;
      onTidyTabs?: () => void;
      readOnly: boolean;
      state: AppState;
      thinkingLabel: string | null;
    }) {
  return (
    <>
      <CompactTopbar
        store={store}
        onClosePanel={onClosePanel}
        onGetViewMode={onGetViewMode}
        onOpenSettings={onOpenSettings}
        onSetViewMode={onSetViewMode}
        onStartSession={onStartSession}
        onTidyTabs={onTidyTabs}
      />
      <TrustCeremony store={store} />
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
