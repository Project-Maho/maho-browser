// Copyright 2026 Maho Browser. All rights reserved.

import React, {useEffect, useState} from 'react';

import {Button} from '@ui/button';
import {Check, Code, Copy} from '@icons/lucide';
import {cn} from '@lib/utils';
import {MCP_AGENT_GUIDANCE_COMMAND} from '../../maho_common/react/mcp-agent-guidance-copy.js';
import type {PaneDefinition} from '../models.js';
import {PaneShell, SectionCard} from './domain_panes.js';

const CLI_COMMANDS = [
  {
    command: 'maho run "Open localhost:3000 and run smoke test"',
    description: 'Run a browser automation prompt from your terminal.',
    title: 'Run a task',
  },
  {
    command: MCP_AGENT_GUIDANCE_COMMAND,
    description: 'Start the local MCP server for agents that support MCP.',
    title: 'Connect MCP',
  },
  {
    command: 'maho browser repl',
    description: 'Open an interactive browser REPL for local debugging.',
    title: 'Browser REPL',
  },
] as const;

type CopyStatus = 'idle' | 'copied' | 'failed';

function CommandPill({command, className}: {readonly className?: string; readonly command: string}) {
  return (
    <code className={cn(
        'inline-flex max-w-full items-center overflow-hidden rounded-lg border border-border bg-background/70 px-2.5 py-1.5 font-mono text-xs font-medium leading-none text-foreground shadow-sm',
        className)}>
      <span className="truncate">{command}</span>
    </code>
  );
}

function CopyCommandButton({command}: {readonly command: string}) {
  const [status, setStatus] = useState<CopyStatus>('idle');

  useEffect(() => {
    if (status === 'idle') {
      return undefined;
    }

    const timer = window.setTimeout(() => setStatus('idle'), 1800);
    return () => window.clearTimeout(timer);
  }, [status]);

  const copyCommand = async () => {
    const clipboard = navigator.clipboard;
    if (!clipboard) {
      setStatus('failed');
      return;
    }

    try {
      await clipboard.writeText(command);
      setStatus('copied');
    } catch (error) {
      if (error instanceof Error) {
        setStatus('failed');
        return;
      }
      throw error;
    }
  };

  const isCopied = status === 'copied';
  const label = isCopied ? 'Copied' : 'Copy command';

  return (
    <Button
      aria-label={`Copy command: ${command}`}
      className="h-8 shrink-0 rounded-lg border-border bg-background/70 px-2.5 text-xs font-semibold text-foreground shadow-none hover:bg-surface-hover"
      size="sm"
      type="button"
      variant="outline"
      onClick={() => void copyCommand()}>
      {isCopied ? <Check aria-hidden="true" className="size-3.5" /> : <Copy aria-hidden="true" className="size-3.5" />}
      {status === 'failed' ? 'Copy unavailable' : label}
    </Button>
  );
}

function CliHeroCard() {
  return (
    <div className="grid gap-5 rounded-2xl border border-border/70 bg-card/80 p-4 shadow-sm ring-1 ring-background/40 sm:grid-cols-[minmax(0,0.9fr)_minmax(0,1.1fr)] sm:p-5">
      <div className="overflow-hidden rounded-xl border border-border bg-background/70 shadow-sm">
        <div className="flex items-center gap-1.5 border-b border-border bg-muted/40 px-3 py-2" aria-hidden="true">
          <span className="size-2 rounded-full bg-muted-foreground/45" />
          <span className="size-2 rounded-full bg-muted-foreground/35" />
          <span className="size-2 rounded-full bg-muted-foreground/25" />
        </div>
        <div className="space-y-2 p-3 font-mono text-xs leading-5 text-muted-foreground">
          <p><span className="text-foreground">$</span> maho mcp</p>
          <p>starting local server…</p>
          <p className="text-foreground">ready for agent connections</p>
        </div>
      </div>
      <div className="flex min-w-0 flex-col justify-center gap-3">
        <div className="flex size-9 items-center justify-center rounded-xl border border-border bg-background/70 text-muted-foreground shadow-sm">
          <Code aria-hidden="true" className="size-4" />
        </div>
        <div>
          <h3 className="text-base font-semibold tracking-tight text-foreground">Maho from your agent stack</h3>
          <p className="mt-1 text-sm leading-6 text-muted-foreground">
            Use the local CLI for browser tasks, MCP setup, and interactive debugging without changing AI settings automatically.
          </p>
        </div>
      </div>
    </div>
  );
}

function CommandRow(
    {command, description, title}: {
      readonly command: string;
      readonly description: string;
      readonly title: string;
    }) {
  return (
    <div className="grid gap-3 border-t border-border px-4 py-4 sm:grid-cols-[minmax(0,1fr)_minmax(220px,320px)] sm:items-center sm:px-6">
      <div className="min-w-0">
        <h4 className="text-sm font-medium text-foreground">{title}</h4>
        <p className="mt-1 text-xs leading-5 text-muted-foreground">{description}</p>
      </div>
      <CommandPill command={command} className="w-full" />
    </div>
  );
}

export function AiDevelopersPane({pane}: {readonly pane: PaneDefinition}) {
  return (
    <PaneShell pane={pane}>
      <div className="space-y-6">
        <section className="space-y-3">
          <h3 className="px-1 text-base font-semibold tracking-tight text-foreground">CLI</h3>
          <CliHeroCard />
          <SectionCard title="Installation" description="Install the maho CLI tool to your terminal.">
            <div className="grid gap-3 border-t-0 px-4 py-4 sm:grid-cols-[minmax(0,1fr)_auto] sm:items-center sm:px-6">
              <div className="min-w-0">
                <h4 className="text-sm font-medium text-foreground">Install CLI</h4>
                <p className="mt-1 text-xs leading-5 text-muted-foreground">Run this script in your terminal to download and install the Maho CLI.</p>
              </div>
              <div className="flex min-w-0 flex-wrap items-center gap-2 sm:justify-end">
                <CommandPill command="curl -fsSL https://mahobrowser.com/install-cli.sh | sh" />
                <CopyCommandButton command="curl -fsSL https://mahobrowser.com/install-cli.sh | sh" />
              </div>
            </div>
          </SectionCard>
          <SectionCard title="Commands" description="Supported local commands for browser agents and developer workflows.">
            {CLI_COMMANDS.map(item => (
              <CommandRow key={item.command} {...item} />
            ))}
          </SectionCard>
        </section>

        <section className="space-y-3">
          <h3 className="px-1 text-base font-semibold tracking-tight text-foreground">Skill and MCP</h3>
          <SectionCard title="Agent setup" description="Connect local agent tools to Maho without writing browser configuration automatically.">
            <div className="grid gap-3 border-t-0 px-4 py-4 sm:grid-cols-[minmax(0,1fr)_auto] sm:items-center sm:px-6">
              <div className="min-w-0">
                <h4 className="text-sm font-medium text-foreground">Maho MCP server</h4>
                <p className="mt-1 text-xs leading-5 text-muted-foreground">Add this to agents that support MCP servers.</p>
              </div>
              <div className="flex min-w-0 flex-wrap items-center gap-2 sm:justify-end">
                <CommandPill command={MCP_AGENT_GUIDANCE_COMMAND} />
                <CopyCommandButton command={MCP_AGENT_GUIDANCE_COMMAND} />
              </div>
            </div>
          </SectionCard>
        </section>
      </div>
    </PaneShell>
  );
}
