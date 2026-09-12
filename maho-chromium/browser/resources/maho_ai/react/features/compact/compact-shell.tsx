import type {RefObject} from 'react';
import {CompactStage} from './compact-stage.js';
import {CompactTopbar} from './compact-topbar.js';
import {ConversationThread} from './conversation-thread.js';
import type {MahoAiStore} from '../../../store.js';
import type {TimelineEntry, ViewMode} from '../../../types.js';

import {TrustCeremony} from '../trust-ceremony/trust-ceremony.js';
import {InteractionApprovalCard} from './interaction-approval-card.js';

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
      onOpenRoutines = () => {},
      onRegenerate,
      onRespondToApproval,
      onResumeSession,
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
      onOpenRoutines?: () => void;
      onRegenerate?: () => void;
      onRespondToApproval: (approvalId: string, approved: boolean) => void;
      onResumeSession?: (sessionId: string) => void;
      onSetViewMode: (mode: ViewMode) => void;
      onStartSession: () => void;
      readOnly: boolean;
      thinkingLabel: string | null;
      topOverlayRef?: RefObject<HTMLDivElement | null>;
      topPadding?: number;
    }) {
  const renameArtifact = (artifactId: string, displayName: string): Promise<string | null> => {
    const sessionId = store.getSnapshot().currentSessionId;
    return sessionId ? store.renameArtifact(sessionId, artifactId, displayName)
                     : Promise.resolve(null);
  };
  const deleteArtifact = (artifactId: string): Promise<boolean> => {
    const sessionId = store.getSnapshot().currentSessionId;
    return sessionId ? store.deleteArtifact(sessionId, artifactId) : Promise.resolve(false);
  };
  const previewArtifactUrl = (artifactId: string): Promise<string | null> =>
      store.getArtifactPreviewUrl(artifactId);
  const exportArtifactUrl = (artifactId: string): Promise<string | null> =>
      store.getArtifactExportUrl(artifactId);
  if (hasMessages || thinkingLabel) {
    return (
      <div className="flex h-full min-h-0 flex-col">
        <div
            ref={topOverlayRef}
            className="relative z-20 grid shrink-0 gap-2 px-1 pb-2 pt-1">
          <CompactTopbar
            store={store}
            onClosePanel={onClosePanel}
            onGetViewMode={onGetViewMode}
            onOpenSettings={onOpenSettings}
            onOpenRoutines={onOpenRoutines}
            onResumeSession={onResumeSession}
            onSetViewMode={onSetViewMode}
            onStartSession={onStartSession}
          />
          <TrustCeremony store={store} />
        </div>
        <div
            className="flex min-h-0 min-w-0 flex-1 flex-col overflow-y-auto [scrollbar-width:none] [&::-webkit-scrollbar]:hidden"
            data-testid="runtime-flow">
          <ConversationThread
            bottomPadding={bottomPadding}
            entries={entries}
            onOpenSettings={onOpenSettings}
            onRegenerate={onRegenerate}
            onRespondToApproval={onRespondToApproval}
            onRenameArtifact={renameArtifact}
            onDeleteArtifact={deleteArtifact}
            onGetArtifactPreviewUrl={previewArtifactUrl}
            onGetArtifactExportUrl={exportArtifactUrl}
            readOnly={readOnly}
            thinkingLabel={thinkingLabel}
            topPadding={topPadding}
          />
          <div
              className="grid min-w-0 shrink-0 gap-3 px-4 pb-3 max-[520px]:gap-2 max-[520px]:px-3"
              data-testid="runtime-footer">
            <InteractionApprovalCard store={store} />
          </div>
        </div>
      </div>
    );
  }

  return (
    <div className="flex h-full min-h-0 min-w-0 flex-col gap-2">
      <CompactTopbar
        store={store}
        onClosePanel={onClosePanel}
        onGetViewMode={onGetViewMode}
        onOpenSettings={onOpenSettings}
        onOpenRoutines={onOpenRoutines}
        onResumeSession={onResumeSession}
        onSetViewMode={onSetViewMode}
        onStartSession={onStartSession}
      />
      <TrustCeremony store={store} />
      <InteractionApprovalCard store={store} />
      <CompactStage />
    </div>
  );
}
