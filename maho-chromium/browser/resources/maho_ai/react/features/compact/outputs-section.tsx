import {useState} from 'react';

import {ChevronDown} from '@icons/lucide';
import {cn} from '@lib/utils';
import type {MahoAiStore} from '../../../store.js';
import {useAppState} from '../../hooks/use-app-state.js';
import {ArtifactCard} from '../../components/artifact-card.js';

export function OutputsSection(
    {store, onOpenPreview}: {
      store: MahoAiStore;
      onOpenPreview?: (artifactId: string) => void;
    }) {
  const state = useAppState(store);
  const [expanded, setExpanded] = useState(true);
  const sessionId = state.currentSessionId;
  if (!sessionId) {
    return null;
  }
  const artifacts = state.artifactsBySessionId[sessionId] || [];

  return (
    <section className="shrink-0 border-t border-border/60 px-4 py-2 max-[520px]:px-3">
      <button
        type="button"
        aria-expanded={expanded}
        className="flex w-full items-center gap-2 text-left text-muted-foreground transition-colors hover:text-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-inset focus-visible:ring-ring"
        onClick={() => setExpanded(value => !value)}>
        <span className="min-w-0 flex-1 text-[11px] font-semibold uppercase tracking-[0.12em]">
          Outputs{artifacts.length ? ` (${artifacts.length})` : ''}
        </span>
        <ChevronDown
          className={cn('size-3.5 shrink-0 transition-transform', expanded && 'rotate-180')} />
      </button>
      {expanded ? (
        artifacts.length ? (
          <div className="mt-2 flex flex-col gap-2">
            {artifacts.map(artifact => (
              <ArtifactCard
                key={artifact.artifactId}
                artifact={artifact}
                getPreviewUrl={() => store.getArtifactPreviewUrl(artifact.artifactId)}
                getExportUrl={() => store.getArtifactExportUrl(artifact.artifactId)}
                onOpenPreview={onOpenPreview ?
                    () => onOpenPreview(artifact.artifactId) : undefined}
                onRename={name => store.renameArtifact(sessionId, artifact.artifactId, name)}
                onDelete={() => store.deleteArtifact(sessionId, artifact.artifactId)}
              />
            ))}
          </div>
        ) : (
          <p className="mt-2 text-xs text-muted-foreground">No outputs yet</p>
        )
      ) : null}
    </section>
  );
}
