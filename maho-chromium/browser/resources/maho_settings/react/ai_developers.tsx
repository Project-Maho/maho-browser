// Copyright 2026 Maho Browser. All rights reserved.

import React, {useEffect, useState} from 'react';

import {Button} from '@ui/button';
import {Input} from '@ui/input';
import type {MahoSettingsStore} from './store.js';
import type {PageHandlerRemote} from '../mojo.js';
import {Check, Code, Copy} from '@icons/lucide';
import {cn} from '@lib/utils';
import {MCP_AGENT_GUIDANCE_COMMAND} from '../../maho_common/react/mcp-agent-guidance-copy.js';
import type {SettingValue} from '../mojo.js';
import type {PaneDefinition} from '../models.js';
import {ManagedSettingRow, PaneShell, SectionCard, Toggle} from './domain_panes.js';
import type {MahoSettingsStore} from './store.js';

export const MCP_ENABLED_SETTING_KEY = 'developers.mcp_enabled';

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

function McpServerToggleCard(
    {settings, store}: {
      readonly settings: SettingValue[];
      readonly store: MahoSettingsStore;
    }) {
  const setting = settings.find(item => item.key === MCP_ENABLED_SETTING_KEY);
  // The browser registers maho.mcp.enabled with a default of true; treat a
  // missing row as on so the card never claims MCP is off when it is running.
  const enabled = setting ? setting.value === 'true' : true;
  return (
    <SectionCard
      title="Local MCP server"
      description="The MCP server accepts connections only from programs running as your user account on this computer. Any such program can control the browser while it is on; Maho labels its own CLI but cannot stop other software you run from using the same access.">
      <ManagedSettingRow
        title="Allow local MCP connections"
        description="Turn off to stop the maho CLI and MCP agents from controlling Maho. Takes effect after restarting Maho.">
        <Toggle
          enabled={enabled}
          label="Allow local MCP connections"
          onToggle={async next => {
            await store.commitSettingValue(
                MCP_ENABLED_SETTING_KEY, next ? 'true' : 'false');
          }}
        />
      </ManagedSettingRow>
    </SectionCard>
  );
}

export function AiDevelopersPane(
    {pane, settings, store}: {
      readonly pane: PaneDefinition;
      readonly settings: SettingValue[];
      readonly store: MahoSettingsStore;
    }) {
  return (
    <PaneShell pane={pane}>
      <div className="space-y-6">
        <McpServerToggleCard settings={settings} store={store} />
        <AgentChannels handler={store.getHandler()} />
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

type ChannelView = {
  readonly id: string;
  readonly provider: string;
  readonly name: string;
  readonly enabled: boolean;
  readonly opaque_auth_handle: string;
  readonly allowed_sender_ids: readonly string[];
  readonly paired_sender_ids: readonly string[];
  readonly running: boolean;
  readonly result: string;
  readonly reply_id: string;
};

export function parseChannelList(json: string): {channels: ChannelView[]; locked: boolean} {
  const value: unknown = JSON.parse(json);
  if (!value || typeof value !== 'object' || !('channels' in value) ||
      !Array.isArray(value.channels) || !('locked' in value) || typeof value.locked !== 'boolean') {
    throw new TypeError('Invalid channel response');
  }
  const channels = value.channels.map((item: unknown): ChannelView => {
    if (!item || typeof item !== 'object' ||
        !('id' in item) || typeof item.id !== 'string' ||
        !('provider' in item) || typeof item.provider !== 'string' ||
        !('name' in item) || typeof item.name !== 'string' ||
        !('enabled' in item) || typeof item.enabled !== 'boolean' ||
        !('opaque_auth_handle' in item) || typeof item.opaque_auth_handle !== 'string' ||
        !('allowed_sender_ids' in item) || !Array.isArray(item.allowed_sender_ids) ||
        !('paired_sender_ids' in item) || !Array.isArray(item.paired_sender_ids) ||
        !('running' in item) || typeof item.running !== 'boolean') {
      throw new TypeError('Invalid channel metadata');
    }
    return {
      id: item.id, provider: item.provider, name: item.name, enabled: item.enabled,
      opaque_auth_handle: item.opaque_auth_handle, running: item.running,
      allowed_sender_ids: item.allowed_sender_ids.filter((id: unknown): id is string => typeof id === 'string'),
      paired_sender_ids: item.paired_sender_ids.filter((id: unknown): id is string => typeof id === 'string'),
      result: 'result' in item && typeof item.result === 'string' ? item.result : '',
      reply_id: 'reply_id' in item && typeof item.reply_id === 'string' ? item.reply_id : '',
    };
  });
  return {channels, locked: value.locked};
}

type ChannelHandler = Pick<PageHandlerRemote, 'listAgentChannels' | 'configureAgentChannel' |
  'removeAgentChannel' | 'approveAgentChannelPairing' | 'revokeAgentChannelPairing' | 'sendAgentChannelReply'>;

export function AgentChannels({handler}: {readonly handler: ChannelHandler}) {
  const [channels, setChannels] = useState<readonly ChannelView[]>([]);
  const [locked, setLocked] = useState(true);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const [id, setId] = useState('');
  const [provider, setProvider] = useState('telegram');
  const [name, setName] = useState('');
  const [note, setNote] = useState('');
  const [senders, setSenders] = useState('');
  const [enabled, setEnabled] = useState(true);
  const [sender, setSender] = useState('');
  const [code, setCode] = useState('');

  const refresh = async () => {
    const response = parseChannelList((await handler.listAgentChannels()).json);
    setChannels(response.channels);
    setLocked(response.locked);
  };
  const run = async (operation: () => Promise<{json: string}>) => {
    setBusy(true);
    setError('');
    try {
      const result: unknown = JSON.parse((await operation()).json);
      if (!result || typeof result !== 'object' || !('success' in result) || result.success !== true) {
        setError(result && typeof result === 'object' && 'error' in result && typeof result.error === 'string'
          ? result.error : 'Channel operation failed. Refresh and try again.');
      }
      await refresh();
    } catch (failure) {
      if (!(failure instanceof Error)) throw failure;
      setError(failure.message);
    } finally {
      setBusy(false);
    }
  };
  useEffect(() => {
    let active = true;
    handler.listAgentChannels().then(({json}) => {
      if (!active) return;
      const result = parseChannelList(json);
      setChannels(result.channels);
      setLocked(result.locked);
    }).catch((failure: unknown) => {
      if (!(failure instanceof Error)) throw failure;
      if (active) setError(failure.message);
    });
    return () => { active = false; };
  }, [handler]);

  return <SectionCard title="Messaging channels" description="Connect allowlisted senders to separate agent conversations. Pairing never approves tools or sends.">
    <div className="space-y-4 px-4 py-4 sm:px-6">
      <p className="text-sm text-muted-foreground">Create a secure note in Vault containing the bot token, then enter its item ID here. Slack requires an app token and bot token in the note as JSON. Credentials are cleared and channels stop when Vault locks.</p>
      {locked && <p role="status">Unlock Vault to configure or pair channels.</p>}
      {error && <p role="alert" className="text-sm text-destructive">{error}</p>}
      <Button variant="outline" disabled={busy} onClick={() => void run(async () => {
        await refresh(); return {json: '{"success":true}'};
      })}>Refresh channels and replies</Button>
      {channels.length === 0 && <p className="text-sm text-muted-foreground">No channels configured.</p>}
      <ul className="divide-y divide-border">
        {channels.map(channel => <li key={channel.id} className="space-y-3 py-4">
          <div className="flex flex-wrap items-center gap-3">
            <span className="min-w-0 break-all font-medium">{channel.name || channel.id} ({channel.provider})</span>
            <span className="text-sm text-muted-foreground">{channel.running ? 'Running' : 'Stopped'}</span>
            <Button variant="outline" disabled={busy} onClick={() => {
              setId(channel.id); setName(channel.name); setProvider(channel.provider);
              setNote(channel.opaque_auth_handle); setSenders(channel.allowed_sender_ids.join(', '));
              setEnabled(channel.enabled);
            }}>Edit {channel.id}</Button>
            <Button variant="outline" disabled={busy} onClick={() => void run(() => handler.removeAgentChannel(channel.id))}>Remove {channel.id}</Button>
          </div>
          <p className="text-sm">Paired senders: {channel.paired_sender_ids.join(', ') || 'None'}</p>
          <div className="flex flex-wrap gap-2">
            <Button variant="outline" disabled={busy || locked || !sender || !code}
              onClick={() => void run(() => handler.approveAgentChannelPairing(channel.id, sender, code))}>Approve pairing for {channel.id}</Button>
            <Button variant="outline" disabled={busy || !sender}
              onClick={() => void run(() => handler.revokeAgentChannelPairing(channel.id, sender))}>Revoke pairing for {channel.id}</Button>
          </div>
          {channel.result && <pre className="whitespace-pre-wrap break-words text-sm">{channel.result}</pre>}
          {channel.reply_id && <Button disabled={busy || locked}
            onClick={() => void run(() => handler.sendAgentChannelReply(channel.id, channel.reply_id))}>Confirm and send this reply to {channel.id}</Button>}
        </li>)}
      </ul>
      <div className="grid gap-3 sm:grid-cols-2">
        <label className="space-y-1 text-sm">Sender ID to pair or revoke<Input value={sender} onChange={event => setSender(event.target.value)} /></label>
        <label className="space-y-1 text-sm">Pairing code<Input value={code} autoComplete="off" onChange={event => setCode(event.target.value)} /></label>
      </div>
      <form className="space-y-3 border-t border-border pt-4" onSubmit={event => {
        event.preventDefault();
        void run(() => handler.configureAgentChannel(JSON.stringify({
          id, provider, name, enabled, opaque_auth_handle: note,
          allowed_sender_ids: senders.split(',').map(value => value.trim()).filter(Boolean),
        })));
      }}>
        <h4 className="font-medium">Configure channel</h4>
        <div className="grid gap-3 sm:grid-cols-2">
          <label className="space-y-1 text-sm">Channel ID<Input required value={id} onChange={event => setId(event.target.value)} /></label>
          <label className="space-y-1 text-sm">Name<Input value={name} onChange={event => setName(event.target.value)} /></label>
          <label className="space-y-1 text-sm">Provider<select className="w-full rounded-md border border-input bg-background p-2" value={provider} onChange={event => setProvider(event.target.value)}>
            <option value="telegram">Telegram</option><option value="slack">Slack</option><option value="discord">Discord</option>
          </select></label>
          <label className="space-y-1 text-sm">Vault secure-note item ID<Input required value={note} onChange={event => setNote(event.target.value)} /></label>
          <label className="space-y-1 text-sm sm:col-span-2">Allowed sender IDs, comma separated<Input required value={senders} onChange={event => setSenders(event.target.value)} /></label>
        </div>
        <label className="flex items-center gap-2 text-sm"><input type="checkbox" checked={enabled} onChange={event => setEnabled(event.target.checked)} />Enabled</label>
        <Button type="submit" disabled={busy || locked}>{busy ? 'Saving...' : 'Save channel'}</Button>
      </form>
    </div>
  </SectionCard>;
}
