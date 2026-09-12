import {useMemo, useState} from 'react';

import {cn} from '@lib/utils';
import {ChevronDown} from '@icons/lucide';
import {useAutoScroll} from '../../hooks/use-auto-scroll.js';
import type {TimelineEntry} from '../../../types.js';
import {collectConversationItems} from '../../../views/conversation_thread.js';
import type {ConversationItem} from '../../../views/conversation_thread.js';
import {Markdown} from '../../components/markdown.js';
import {ArtifactCard} from '../../components/artifact-card.js';
import {ToolExecutionGroup, ToolStep} from '../../components/tool-execution-group.js';
import {ConversationMessage} from './conversation-message.js';
import {ConversationSystemItem} from './conversation-system-item.js';
import {PendingThought} from './pending-thought.js';

function ThinkingBubble({item}: {item: ConversationItem}) {
  const [expanded, setExpanded] = useState(false);

  return (
    <article className="w-full motion-safe:animate-in motion-safe:fade-in motion-safe:slide-in-from-bottom-1 motion-reduce:animate-none">
      <div className="overflow-hidden rounded-xl border border-border/70 bg-secondary/35">
        <button
          type="button"
          aria-expanded={expanded}
          className="flex w-full items-center gap-2 px-3 py-2 text-left text-muted-foreground transition-colors hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-inset focus-visible:ring-ring"
          onClick={() => setExpanded(!expanded)}>
          <span className="min-w-0 flex-1 text-[11px] font-semibold uppercase tracking-[0.12em]">
            Thinking
          </span>
          <ChevronDown className={cn(
              'size-3.5 shrink-0 transition-transform',
              expanded && 'rotate-180')} />
        </button>
        {expanded ? (
          <Markdown
            className="min-w-0 border-t border-border/60 px-3 py-3 text-xs leading-5 text-muted-foreground [overflow-wrap:anywhere] [&_p]:m-0 [&_p:not(:last-child)]:mb-3 [&_ul]:m-0 [&_ul]:list-disc [&_ul]:pl-5 [&_ol]:m-0 [&_ol]:list-decimal [&_ol]:pl-5 [&_li+li]:mt-2 [&_code]:rounded-md [&_code]:border [&_code]:border-border [&_code]:bg-background/70 [&_code]:px-1.5 [&_code]:py-0.5 [&_code]:[overflow-wrap:anywhere] [&_pre]:m-0 [&_pre]:overflow-x-auto [&_pre]:whitespace-pre-wrap [&_pre]:[overflow-wrap:anywhere] [&_pre]:rounded-xl [&_pre]:bg-background/70 [&_pre]:p-3"
            text={item.text}
          />
        ) : null}
      </div>
    </article>
  );
}

export function ConversationThread(
    {
      bottomPadding,
      entries,
      onOpenSettings,
      onRegenerate,
      onRespondToApproval,
      onRenameArtifact,
      onDeleteArtifact,
      onOpenArtifactPreview,
      onGetArtifactPreviewUrl,
      onGetArtifactExportUrl,
      readOnly,
      thinkingLabel,
      topPadding,
    }: {
      bottomPadding: number;
      entries: TimelineEntry[];
      onOpenSettings: (paneKey: string) => void;
      onRegenerate?: () => void;
      onRespondToApproval: (approvalId: string, approved: boolean) => void;
      onRenameArtifact?: (artifactId: string, displayName: string) => Promise<string | null>;
      onDeleteArtifact?: (artifactId: string) => boolean | Promise<boolean>;
      onOpenArtifactPreview?: (artifactId: string) => void;
      onGetArtifactPreviewUrl?: (artifactId: string) => Promise<string | null>;
      onGetArtifactExportUrl?: (artifactId: string) => Promise<string | null>;
      readOnly: boolean;
      thinkingLabel: string | null;
      topPadding: number;
    }) {
  const items = useMemo(() => collectConversationItems(entries), [entries]);
  // Only the newest assistant answer can be regenerated; older turns are
  // history and resubmitting their prompt would not replace them.
  const lastAssistantKey = useMemo(() => {
    for (let i = items.length - 1; i >= 0; --i) {
      if (items[i].role === 'assistant' && !items[i].kind) {
        return items[i].key;
      }
    }
    return null;
  }, [items]);
  // Only the latest failed turn can be retried; older failures are historical
  // and resubmitting would erroneously replay the newest prompt instead (M3).
  const lastErrorKey = useMemo(() => {
    for (let i = items.length - 1; i >= 0; --i) {
      const it = items[i];
      if (it.role === 'system' && (it.credentialError || it.tone === 'danger')) {
        return it.key;
      }
    }
    return null;
  }, [items]);
  const autoScrollDependency = useMemo(
      () => ({bottomPadding, items, thinkingLabel, topPadding}),
      [bottomPadding, items, thinkingLabel, topPadding]);
  const listRef = useAutoScroll<HTMLDivElement>(autoScrollDependency);

  const groupedItems = useMemo(() => {
    type GroupedThreadItem =
      | { kind: 'item'; item: ConversationItem }
      | { kind: 'tool_group'; groupKey: string; steps: ToolStep[]; startTime: number; endTime: number; isRunning: boolean };

    const result: GroupedThreadItem[] = [];
    let currentToolSteps: ToolStep[] = [];
    let groupStartTime = 0;
    let groupEndTime = 0;
    let groupIsRunning = false;
    let groupKey = '';

    const flushToolGroup = () => {
      if (currentToolSteps.length === 0) return;
      result.push({
        kind: 'tool_group',
        groupKey,
        steps: [...currentToolSteps],
        startTime: groupStartTime,
        endTime: groupEndTime,
        isRunning: groupIsRunning,
      });
      currentToolSteps = [];
      groupIsRunning = false;
    };

    for (const item of items) {
      if (item.kind === 'activity') {
        const isError = item.tone === 'danger' || item.chips?.some(c => c.tone === 'danger') || false;
        const step: ToolStep = {
          key: item.key,
          text: item.text,
          isError,
          toolName: item.toolName,
          note: item.note,
          errorDetail: item.errorDetail,
          timestamp: item.timestamp,
        };
        if (currentToolSteps.length === 0) {
          groupKey = `tool-group-${item.key}`;
          groupStartTime = item.timestamp;
          groupEndTime = item.timestamp;
        } else {
          groupEndTime = Math.max(groupEndTime, item.timestamp);
        }
        if (item.tone === 'accent' && (!item.chips || item.chips.length === 0)) {
          groupIsRunning = true;
        }
        currentToolSteps.push(step);
      } else {
        flushToolGroup();
        result.push({kind: 'item', item});
      }
    }
    flushToolGroup();
    return result;
  }, [items]);

  return (
    <main className="flex min-h-0 flex-1 flex-col">
      <div
          ref={listRef}
          className="flex min-h-0 flex-1 flex-col justify-start gap-4 overflow-y-auto px-4 [scrollbar-width:none] [&::-webkit-scrollbar]:hidden max-[520px]:gap-3 max-[520px]:px-3"
          style={{paddingBottom: bottomPadding, paddingTop: topPadding}}>
        {groupedItems.map(entry => {
          if (entry.kind === 'tool_group') {
            return (
              <ToolExecutionGroup
                key={entry.groupKey}
                steps={entry.steps}
                startTime={entry.startTime}
                endTime={entry.endTime}
                isRunning={entry.isRunning}
              />
            );
          }
          const item = entry.item!;
          if (item.kind === 'thinking') {
            return <ThinkingBubble key={item.key} item={item} />;
          }
          if (item.kind === 'artifact' && item.artifact) {
            const artifact = item.artifact;
            return (
              <ArtifactCard
                key={item.key}
                artifact={artifact}
                onOpenPreview={onOpenArtifactPreview ?
                    () => onOpenArtifactPreview(artifact.artifactId) : undefined}
                onRename={onRenameArtifact ?
                    name => onRenameArtifact(artifact.artifactId, name) : undefined}
                onDelete={onDeleteArtifact ?
                    () => onDeleteArtifact(artifact.artifactId) : undefined}
                getPreviewUrl={onGetArtifactPreviewUrl ?
                    () => onGetArtifactPreviewUrl(artifact.artifactId) : undefined}
                getExportUrl={onGetArtifactExportUrl ?
                    () => onGetArtifactExportUrl(artifact.artifactId) : undefined}
              />
            );
          }
          if (item.role === 'system') {
            const isLatestError = item.key === lastErrorKey;
            return (
              <ConversationSystemItem
                key={item.key}
                item={item}
                readOnly={readOnly}
                onOpenSettings={onOpenSettings}
                onRespondToApproval={onRespondToApproval}
                onRetry={isLatestError && !readOnly && !thinkingLabel ? onRegenerate : undefined}
              />
            );
          }
          const isLastAssistant =
              item.role === 'assistant' && item.key === lastAssistantKey;
          return (
            <ConversationMessage
              key={item.key}
              item={item}
              onRegenerate={
                  isLastAssistant && !readOnly && !thinkingLabel ? onRegenerate :
                                                                   undefined}
            />
          );
        })}
        {thinkingLabel ? <PendingThought label={thinkingLabel} /> : null}
      </div>
      <div
          aria-live="polite"
          className="sr-only"
          data-turn-status
          role="status">
        {thinkingLabel ? `Maho is working: ${thinkingLabel}` : ''}
      </div>
    </main>
  );
}
