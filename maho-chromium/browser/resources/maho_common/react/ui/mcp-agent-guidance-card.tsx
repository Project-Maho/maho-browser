import React from 'react';

import {cn} from '../lib/utils';
import {AgentBrandIcon, type AgentBrandName} from './agent-brand-icons.js';
import {Button} from './button';

import {
  MCP_AGENT_GUIDANCE_CLIENTS,
  MCP_AGENT_GUIDANCE_CTA_LABEL,
  MCP_AGENT_GUIDANCE_VISIBLE_TEXT,
} from '../mcp-agent-guidance-copy.js';

interface AgentIconTile {
  readonly animationClassName: string;
  readonly name: AgentBrandName;
}

const TILE_SHUFFLE_STYLES = `
@keyframes maho-mcp-agent-codex {
  0%, 100% { opacity: 1; transform: translate3d(0, 4px, 0) rotate(-4deg) scale(1); }
  25% { opacity: 0.68; transform: translate3d(18px, 0, 0) rotate(5deg) scale(0.82); }
  50% { opacity: 0.48; transform: translate3d(22px, 8px, 0) rotate(-2deg) scale(0.74); }
  75% { opacity: 0.34; transform: translate3d(18px, 16px, 0) rotate(4deg) scale(0.66); }
}
@keyframes maho-mcp-agent-claude {
  0%, 100% { opacity: 0.68; transform: translate3d(18px, 0, 0) rotate(5deg) scale(0.82); }
  25% { opacity: 1; transform: translate3d(0, 4px, 0) rotate(-4deg) scale(1); }
  50% { opacity: 0.68; transform: translate3d(18px, 0, 0) rotate(5deg) scale(0.82); }
  75% { opacity: 0.48; transform: translate3d(22px, 8px, 0) rotate(-2deg) scale(0.74); }
}
@keyframes maho-mcp-agent-opencode {
  0%, 100% { opacity: 0.48; transform: translate3d(22px, 8px, 0) rotate(-2deg) scale(0.74); }
  25% { opacity: 0.34; transform: translate3d(18px, 16px, 0) rotate(4deg) scale(0.66); }
  50% { opacity: 1; transform: translate3d(0, 4px, 0) rotate(-4deg) scale(1); }
  75% { opacity: 0.68; transform: translate3d(18px, 0, 0) rotate(5deg) scale(0.82); }
}
@keyframes maho-mcp-agent-pi {
  0%, 100% { opacity: 0.34; transform: translate3d(18px, 16px, 0) rotate(4deg) scale(0.66); }
  25% { opacity: 0.48; transform: translate3d(22px, 8px, 0) rotate(-2deg) scale(0.74); }
  50% { opacity: 0.68; transform: translate3d(18px, 0, 0) rotate(5deg) scale(0.82); }
  75% { opacity: 1; transform: translate3d(0, 4px, 0) rotate(-4deg) scale(1); }
}
.maho-mcp-agent-tile {
  animation-duration: 7.2s;
  animation-iteration-count: infinite;
  animation-timing-function: cubic-bezier(0.4, 0, 0.2, 1);
  will-change: transform, opacity;
}
.maho-mcp-agent-codex { animation-name: maho-mcp-agent-codex; }
.maho-mcp-agent-claude { animation-name: maho-mcp-agent-claude; }
.maho-mcp-agent-opencode { animation-name: maho-mcp-agent-opencode; }
.maho-mcp-agent-pi { animation-name: maho-mcp-agent-pi; }
.maho-mcp-agent-guidance:is(:hover, :focus-within) .maho-mcp-agent-tile {
  animation-play-state: paused;
}
@media (prefers-reduced-motion: reduce) {
  .maho-mcp-agent-tile {
    animation: none;
    will-change: auto;
  }
  .maho-mcp-agent-codex { opacity: 1; transform: translate3d(0, 4px, 0) rotate(-4deg) scale(1); }
  .maho-mcp-agent-claude { opacity: 0.68; transform: translate3d(18px, 0, 0) rotate(5deg) scale(0.82); }
  .maho-mcp-agent-opencode { opacity: 0.48; transform: translate3d(22px, 8px, 0) rotate(-2deg) scale(0.74); }
  .maho-mcp-agent-pi { opacity: 0.34; transform: translate3d(18px, 16px, 0) rotate(4deg) scale(0.66); }
}`;

const AGENT_ICON_TILES: readonly AgentIconTile[] = [
  {animationClassName: 'maho-mcp-agent-codex', name: MCP_AGENT_GUIDANCE_CLIENTS[0]},
  {animationClassName: 'maho-mcp-agent-claude', name: MCP_AGENT_GUIDANCE_CLIENTS[1]},
  {animationClassName: 'maho-mcp-agent-opencode', name: MCP_AGENT_GUIDANCE_CLIENTS[2]},
  {animationClassName: 'maho-mcp-agent-pi', name: MCP_AGENT_GUIDANCE_CLIENTS[3]},
] as const;

interface McpAgentGuidanceCardProps {
  readonly className?: string;
  readonly onConnect?: () => void;
  readonly onDismiss?: () => void;
}

function AgentTileStack() {
  return (
    <div className="relative h-10 w-14 shrink-0" aria-hidden="true">
      <style>{TILE_SHUFFLE_STYLES}</style>
      {AGENT_ICON_TILES.map(tile => (
        <span
          key={tile.name}
          className={cn(
              'maho-mcp-agent-tile absolute left-0 top-0 flex size-8 items-center justify-center overflow-hidden rounded-xl border border-border/80 bg-background shadow-sm ring-1 ring-background/70',
              tile.animationClassName)}
          data-mcp-agent-tile={tile.name}>
          <AgentBrandIcon name={tile.name} />
          <span className="pointer-events-none absolute inset-0 rounded-xl ring-1 ring-inset ring-foreground/10" />
        </span>
      ))}
    </div>
  );
}

export function McpAgentGuidanceCard({className, onConnect, onDismiss}: McpAgentGuidanceCardProps) {
  return (
    <div
      role="note"
      className={cn(
          'maho-mcp-agent-guidance grid min-h-14 w-full max-w-[23.5rem] items-center gap-3 rounded-xl border border-border/70 bg-card/95 px-3 py-2 shadow-sm ring-1 ring-background/40',
          onDismiss ?
            'grid-cols-[auto_minmax(0,1fr)_auto_auto]' :
            'grid-cols-[auto_minmax(0,1fr)_auto]',
          className)}>
      <AgentTileStack />
      <p className="min-w-0 text-xs font-medium leading-4 text-foreground">
        {MCP_AGENT_GUIDANCE_VISIBLE_TEXT}
      </p>
      <Button
          aria-label="Open Maho MCP setup guide"
          className="h-8 shrink-0 rounded-lg border-border bg-background/70 px-2.5 text-xs font-semibold text-foreground shadow-none hover:bg-surface-hover"
          size="sm"
          type="button"
          variant="outline"
          onClick={onConnect}>
        {MCP_AGENT_GUIDANCE_CTA_LABEL}
      </Button>
      {onDismiss ? (
        <button
            aria-label="Dismiss"
            className="flex size-6 shrink-0 items-center justify-center rounded-md text-muted-foreground transition-colors hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
            type="button"
            onClick={onDismiss}>
          <span aria-hidden="true" className="text-sm leading-none">✕</span>
        </button>
      ) : null}
    </div>
  );
}
