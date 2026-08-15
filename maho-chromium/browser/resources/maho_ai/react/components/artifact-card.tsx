import {useEffect, useRef, useState} from 'react';

import {ChevronDown, Code, Edit, Eye, GripVertical, MoreHorizontal, Paperclip, Trash2} from '@icons/lucide';
import {cn} from '@lib/utils';
import {
  ContextMenu,
  ContextMenuContent,
  ContextMenuItem,
  ContextMenuSeparator,
  ContextMenuTrigger,
} from '@ui/context-menu';
import type {ArtifactInfo} from '../../maho_ai.mojom-webui.js';
import {formatByteSize} from '../lib/format-size.js';
import {formatTimestamp} from '../lib/format-timestamp.js';

function iconForMime(mimeType: string) {
  if (mimeType.startsWith('text/') || mimeType === 'application/json' ||
      mimeType.includes('html') || mimeType.includes('xml') ||
      mimeType.includes('javascript')) {
    return Code;
  }
  return Paperclip;
}

function formatDownloadUrl(mimeType: string, displayName: string, url: string): string {
  return `${mimeType}:${displayName}:${url}`;
}

type PreviewStatus = 'idle' | 'loading' | 'ready' | 'error';

export interface ArtifactCardProps {
  artifact: ArtifactInfo;
  onOpenPreview?: () => void;
  onRename?: (displayName: string) => void | Promise<string | null>;
  onDelete?: () => boolean | Promise<boolean>;
  getPreviewUrl?: () => Promise<string | null>;
  getExportUrl?: () => Promise<string | null>;
}

export function ArtifactCard(
    {artifact, onOpenPreview, onRename, onDelete, getPreviewUrl, getExportUrl}: ArtifactCardProps) {
  const [renaming, setRenaming] = useState(false);
  const [draftName, setDraftName] = useState(artifact.displayName);
  const [error, setError] = useState<string | null>(null);
  const [previewOpen, setPreviewOpen] = useState(false);
  const [previewUrl, setPreviewUrl] = useState<string | null>(null);
  const [previewStatus, setPreviewStatus] = useState<PreviewStatus>('idle');
  const [exportUrl, setExportUrl] = useState<string | null>(null);
  const [deletePending, setDeletePending] = useState(false);
  const [iconFailed, setIconFailed] = useState(false);
  const renameSubmitting = useRef(false);
  const renameMenuClosing = useRef(false);
  const triggerRef = useRef<HTMLElement>(null);
  const renameInputRef = useRef<HTMLInputElement>(null);
  const closeFocusTarget = useRef<'rename'|'trigger'>('trigger');
  const Icon = iconForMime(artifact.mimeType);
  const hasPreviewAction = !!(onOpenPreview || getPreviewUrl);
  const hasMenuActions = !deletePending && !!(hasPreviewAction || onRename || onDelete);

  useEffect(() => {
    if (renaming) {
      renameInputRef.current?.focus();
    }
  }, [renaming]);

  useEffect(() => {
    if (deletePending) {
      triggerRef.current?.focus();
    }
  }, [deletePending]);

  useEffect(() => {
    if (!getExportUrl) {
      setExportUrl(null);
      return;
    }
    let active = true;
    void getExportUrl().then(url => {
      if (active) {
        setExportUrl(url);
      }
    });
    return () => {
      active = false;
    };
  }, [getExportUrl, artifact.artifactId]);

  const submitRename = async () => {
    if (renameSubmitting.current) {
      return;
    }
    const name = draftName.trim();
    if (!name || !onRename) {
      setRenaming(false);
      return;
    }
    renameSubmitting.current = true;
    const result = await onRename(name);
    renameSubmitting.current = false;
    if (typeof result === 'string') {
      setError(result);
      return;
    }
    setError(null);
    setRenaming(false);
  };

  const startRename = () => {
    if (deletePending) {
      return;
    }
    closeFocusTarget.current = 'rename';
    renameMenuClosing.current = true;
    setDraftName(artifact.displayName);
    setError(null);
    setRenaming(true);
  };

  const cancelRename = () => {
    renameSubmitting.current = false;
    renameMenuClosing.current = false;
    setRenaming(false);
    setError(null);
    setDraftName(artifact.displayName);
    queueMicrotask(() => triggerRef.current?.focus());
  };

  const deleteArtifact = async () => {
    if (!onDelete || deletePending) {
      return;
    }
    closeFocusTarget.current = 'trigger';
    setDeletePending(true);
    setError(null);
    try {
      const deleted = await onDelete();
      if (!deleted) {
        setError('Delete failed');
        setDeletePending(false);
      }
    } catch {
      setError('Delete failed');
      setDeletePending(false);
    }
  };

  const openContextMenuFromKeyboard = (event: React.KeyboardEvent<HTMLElement>) => {
    if (renaming || deletePending || !hasMenuActions ||
        !(event.key === 'ContextMenu' || (event.key === 'F10' && event.shiftKey))) {
      return;
    }
    event.preventDefault();
    const bounds = event.currentTarget.getBoundingClientRect();
    event.currentTarget.dispatchEvent(new MouseEvent('contextmenu', {
      bubbles: true,
      button: 2,
      clientX: bounds.left + Math.min(16, bounds.width / 2),
      clientY: bounds.top + Math.min(16, bounds.height / 2),
    }));
  };

  const openActionsMenu = (event: React.MouseEvent<HTMLElement>) => {
    event.preventDefault();
    event.stopPropagation();
    if (renaming || deletePending || !hasMenuActions) {
      return;
    }
    const bounds = event.currentTarget.getBoundingClientRect();
    triggerRef.current?.dispatchEvent(new MouseEvent('contextmenu', {
      bubbles: true,
      button: 2,
      clientX: bounds.left,
      clientY: bounds.bottom,
    }));
  };

  const togglePreview = async () => {
    if (!getPreviewUrl || deletePending) {
      return;
    }
    if (previewOpen) {
      setPreviewOpen(false);
      return;
    }
    setPreviewOpen(true);
    setPreviewStatus('loading');
    setPreviewUrl(null);
    const url = await getPreviewUrl();
    if (url) {
      setPreviewUrl(url);
      setPreviewStatus('ready');
    } else {
      setPreviewStatus('error');
    }
  };

  return (
    <ContextMenu>
      <ContextMenuTrigger asChild disabled={!hasMenuActions}>
        <article
        ref={triggerRef}
        aria-haspopup={hasMenuActions ? 'menu' : undefined}
        aria-label={`Actions for ${artifact.displayName}`}
        aria-disabled={deletePending ? true : undefined}
        tabIndex={0}
        className={`w-full motion-safe:animate-in motion-safe:fade-in motion-safe:slide-in-from-bottom-1 focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring motion-reduce:animate-none${
          exportUrl ? ' cursor-grab active:cursor-grabbing' : ''
        }`}
        draggable={!!exportUrl}
        title={
          exportUrl ? 'Drag this card to Finder to save the file' : undefined
        }
        onKeyDown={openContextMenuFromKeyboard}
        onDragStart={event => {
          if (!exportUrl) {
            return;
          }
          event.dataTransfer.setData(
              'DownloadURL',
              formatDownloadUrl(artifact.mimeType, artifact.displayName, exportUrl));
          event.dataTransfer.effectAllowed = 'copy';
        }}>
      <div className="overflow-hidden rounded-xl border border-border/70 bg-secondary/35">
        <div className="flex items-center gap-3 px-3 py-2">
          {exportUrl ? (
            <GripVertical
              className="-ml-1 size-4 shrink-0 text-muted-foreground/50"
              aria-hidden="true" />
          ) : null}
          {iconFailed ? (
            <Icon className="size-4 shrink-0 text-muted-foreground" aria-hidden="true" />
          ) : (
            <img
              src={`chrome://fileicon/${encodeURIComponent(artifact.displayName)}?scale=2x`}
              alt=""
              aria-hidden="true"
              className="size-4 shrink-0"
              onError={() => setIconFailed(true)} />
          )}
          <div className="min-w-0 flex-1">
            {renaming ? (
              <div className="flex flex-col gap-1">
                <input
                  ref={renameInputRef}
                  aria-label="Artifact name"
                  className="w-full rounded-md border border-border bg-background px-2 py-1 text-xs text-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
                  value={draftName}
                  onChange={event => setDraftName(event.target.value)}
                  onKeyDown={event => {
                    if (event.key === 'Enter') {
                      event.preventDefault();
                      void submitRename();
                    } else if (event.key === 'Escape') {
                      event.preventDefault();
                      cancelRename();
                    }
                  }}
                  onBlur={() => {
                    if (renaming && !renameSubmitting.current && !renameMenuClosing.current) {
                      void submitRename();
                    }
                  }}
                />
                {error ? (
                  <span role="alert" className="text-[11px] text-destructive">{error}</span>
                ) : null}
              </div>
            ) : (
              <>
                <div className="truncate text-xs font-medium text-foreground">
                  {artifact.displayName}
                </div>
                <div className="text-[11px] text-muted-foreground">
                  {formatByteSize(artifact.sizeBytes)} · {formatTimestamp(artifact.createdAt)}
                </div>
                {error ? (
                  <span role="alert" className="text-[11px] text-destructive">{error}</span>
                ) : null}
              </>
            )}
          </div>
          {getPreviewUrl ? (
            <button
              type="button"
              aria-label="Toggle preview"
              aria-expanded={previewOpen}
              className="shrink-0 rounded-md p-1 text-muted-foreground hover:bg-surface-hover hover:text-foreground"
              onClick={() => void togglePreview()}>
              <ChevronDown className={cn(
                  'size-3.5 transition-transform', previewOpen && 'rotate-180')} />
            </button>
          ) : null}
          {!renaming && hasMenuActions ? (
            <button
              type="button"
              aria-label={`Actions for ${artifact.displayName}`}
              aria-haspopup="menu"
              className="shrink-0 rounded-md p-1 text-muted-foreground hover:bg-surface-hover hover:text-foreground"
              onClick={openActionsMenu}>
              <MoreHorizontal className="size-3.5" aria-hidden="true" />
            </button>
          ) : null}
        </div>
        {previewOpen ? (
          <div className="border-t border-border/60">
            {previewStatus === 'ready' && previewUrl ? (
              <iframe
                title="Artifact preview"
                sandbox=""
                referrerPolicy="no-referrer"
                src={previewUrl}
                className="h-64 w-full border-0 bg-background"
              />
            ) : previewStatus === 'error' ? (
              <p role="alert" className="px-3 py-3 text-xs text-muted-foreground">
                Preview unavailable
              </p>
            ) : (
              <p className="px-3 py-3 text-xs text-muted-foreground">Loading preview…</p>
            )}
          </div>
        ) : null}
      </div>
        </article>
      </ContextMenuTrigger>
      {hasMenuActions ? (
        <ContextMenuContent
          className="w-44"
          onCloseAutoFocus={event => {
            event.preventDefault();
            const focusTarget = closeFocusTarget.current;
            closeFocusTarget.current = 'trigger';
            queueMicrotask(() => {
              if (focusTarget === 'rename') {
                renameMenuClosing.current = false;
                renameInputRef.current?.focus();
              } else {
                triggerRef.current?.focus();
              }
            });
          }}>
          {hasPreviewAction ? (
            <ContextMenuItem disabled={deletePending} onSelect={() => {
              if (deletePending) {
                return;
              }
              closeFocusTarget.current = 'trigger';
              if (onOpenPreview) {
                onOpenPreview();
              } else {
                void togglePreview();
              }
            }}>
              <Eye className="mr-2 size-3.5" aria-hidden="true" />
              Open preview
            </ContextMenuItem>
          ) : null}
          {onRename ? (
            <ContextMenuItem disabled={deletePending} onSelect={startRename}>
              <Edit className="mr-2 size-3.5" aria-hidden="true" />
              Rename
            </ContextMenuItem>
          ) : null}
          {onDelete ? (
            <>
              {(hasPreviewAction || onRename) ? <ContextMenuSeparator /> : null}
              <ContextMenuItem
                disabled={deletePending}
                className="text-destructive focus:bg-destructive/10 focus:text-destructive"
                onSelect={() => void deleteArtifact()}>
                <Trash2 className="mr-2 size-3.5" aria-hidden="true" />
                Delete
              </ContextMenuItem>
            </>
          ) : null}
        </ContextMenuContent>
      ) : null}
    </ContextMenu>
  );
}
