import type {RefObject} from 'react';
import {CompactStage} from './compact-stage.js';
import {CompactTopbar} from './compact-topbar.js';
import {ConversationThread} from './conversation-thread.js';
import type {MahoAiStore} from '../../../store.js';
import type {TimelineEntry, ViewMode} from '../../../types.js';

import {TrustCeremony} from '../trust-ceremony/trust-ceremony.js';

export function CompactShell(
    {
      store,
      bottomPadding = 0,
      entries,
      hasMessages,
      onClosePanel,
      onConnect,
      onGetViewMode,
      onOpenSettings,
      onRegenerate,
      onRespondToApproval,
      onSetViewMode,
      onStartSession,
      readOnly,
      thinkingLabel,
      topOverlayRef,
      topPadding = 0,
    }: {
      store: MahoAiStore;
      bottomPadding?: number;
      entries: TimelineEntry[];
      hasMessages: boolean;
      onClosePanel: () => void;
      onConnect?: () => void;
      onGetViewMode: () => Promise<ViewMode>;
      onOpenSettings: (paneKey?: string) => void;
      onRegenerate?: () => void;
      onRespondToApproval: (approvalId: string, approved: boolean) => void;
      onSetViewMode: (mode: ViewMode) => void;
      onStartSession: () => void;
      readOnly: boolean;
      thinkingLabel: string | null;
      topOverlayRef?: RefObject<HTMLDivElement | null>;
      topPadding?: number;
    }) {
  if (hasMessages || thinkingLabel) {
    return (
      <div className="flex h-full min-h-0 flex-col">
        <div
            ref={topOverlayRef}
            className="relative z-20 grid shrink-0 gap-3 px-3 pb-2 pt-3 max-[520px]:gap-2 max-[520px]:px-2 max-[520px]:pb-2 max-[520px]:pt-2">
          <CompactTopbar
            store={store}
            onClosePanel={onClosePanel}
            onGetViewMode={onGetViewMode}
            onOpenSettings={onOpenSettings}
            onSetViewMode={onSetViewMode}
            onStartSession={onStartSession}
          />
          <TrustCeremony store={store} />
        </div>
        <ConversationThread
          bottomPadding={bottomPadding}
          entries={entries}
          onOpenSettings={onOpenSettings}
          onRegenerate={onRegenerate}
          onRespondToApproval={onRespondToApproval}
          readOnly={readOnly}
          thinkingLabel={thinkingLabel}
          topPadding={topPadding}
        />
      </div>
    );
  }

  return (
    <div className="flex h-full min-h-0 flex-col gap-3 max-[520px]:gap-2">
      <CompactTopbar
        store={store}
        onClosePanel={onClosePanel}
        onGetViewMode={onGetViewMode}
        onOpenSettings={onOpenSettings}
        onSetViewMode={onSetViewMode}
        onStartSession={onStartSession}
      />
      <TrustCeremony store={store} />
      <CompactStage onConnect={onConnect} />
    </div>
  );
}
