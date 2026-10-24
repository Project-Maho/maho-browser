// Copyright 2026 Maho Browser. All rights reserved.

// Settings > Privacy > "Agent activity": the local-only journal of
// consequential agent actions (form submit, purchase, credential typing, mail
// send/delete, cross-origin navigation, file upload). Rows are kept on this
// device for 30 days, never synced, and never recorded from incognito.
// The journal is after-the-fact review only; it never gates an action.

import React, {useCallback, useEffect, useRef, useState} from 'react';

import {Button} from '@ui/button';
import {Input} from '@ui/input';

import type {AgentActionJournalEntry} from '../maho_settings.mojom-webui.js';

export interface AgentActivityHandler {
  getAgentActionJournal(actor: string, action: string, query: string):
      Promise<{entries: AgentActionJournalEntry[]}>;
  clearAgentActionJournal(): Promise<{success: boolean}>;
  // Undo window for agent-sent mail (agent.mail_send_delay_seconds > 0).
  // Optional so surfaces without a mail service still render the journal.
  listQueuedAgentMail?(): Promise<{success: boolean, queueJson: string}>;
  cancelQueuedAgentMail?(id: string): Promise<{cancelled: boolean}>;
}

export interface QueuedAgentMail {
  id: string;
  subject: string;
  to: string[];
  sendAtMs: number;
}

export function parseQueuedAgentMail(json: string): QueuedAgentMail[] {
  try {
    const parsed: unknown = JSON.parse(json);
    if (!Array.isArray(parsed)) return [];
    return parsed.flatMap(item => {
      if (!item || typeof item !== 'object') return [];
      const row = item as Record<string, unknown>;
      if (typeof row['id'] !== 'string' || row['status'] !== 'pending') return [];
      return [{
        id: row['id'],
        subject: typeof row['subject'] === 'string' ? row['subject'] : '',
        to: Array.isArray(row['to']) ? row['to'].filter((v): v is string => typeof v === 'string') : [],
        sendAtMs: typeof row['send_at_ms'] === 'number' ? row['send_at_ms'] : 0,
      }];
    });
  } catch {
    return [];
  }
}

export const AGENT_ACTIVITY_ACTION_LABELS: Readonly<Record<string, string>> = {
  form_submit: 'Form submit',
  purchase: 'Purchase',
  credential_typing: 'Credential typed',
  mail_send: 'Mail sent',
  mail_delete: 'Mail deleted',
  cross_origin_navigation: 'Cross-origin navigation',
  file_upload: 'File upload',
};

export const AGENT_ACTIVITY_ACTOR_LABELS: Readonly<Record<string, string>> = {
  panel: 'AI panel',
  mcp: 'MCP client',
  cli: 'Maho CLI',
};

type LoadStatus = 'loading' | 'loaded' | 'error';

function formatTimestamp(seconds: bigint): string {
  const date = new Date(Number(seconds) * 1000);
  return Number.isNaN(date.getTime()) ? '' : date.toLocaleString();
}

export function AgentActivityCard({handler}: {handler: AgentActivityHandler}) {
  const [actor, setActor] = useState('');
  const [action, setAction] = useState('');
  const [query, setQuery] = useState('');
  const [entries, setEntries] = useState<AgentActionJournalEntry[]>([]);
  const [status, setStatus] = useState<LoadStatus>('loading');
  const [clearing, setClearing] = useState(false);
  const reqSeqRef = useRef(0);
  const [queued, setQueued] = useState<QueuedAgentMail[]>([]);
  const [cancelNotice, setCancelNotice] = useState('');

  const loadQueued = useCallback(async () => {
    if (!handler.listQueuedAgentMail) return;
    try {
      const {success, queueJson} = await handler.listQueuedAgentMail();
      setQueued(success ? parseQueuedAgentMail(queueJson) : []);
    } catch {
      setQueued([]);
    }
  }, [handler]);

  useEffect(() => {
    void loadQueued();
  }, [loadQueued]);

  const onCancelQueued = useCallback(async (id: string) => {
    if (!handler.cancelQueuedAgentMail) return;
    const {cancelled} = await handler.cancelQueuedAgentMail(id);
    setCancelNotice(cancelled ? 'Mail cancelled. It was not sent.' :
                                'Too late to cancel: the mail already left the undo window.');
    await loadQueued();
  }, [handler, loadQueued]);

  const load = useCallback(async () => {
    const seq = ++reqSeqRef.current;
    setStatus('loading');
    try {
      const result = await handler.getAgentActionJournal(actor, action, query);
      if (seq !== reqSeqRef.current) return;
      setEntries(result.entries);
      setStatus('loaded');
    } catch {
      if (seq !== reqSeqRef.current) return;
      setStatus('error');
    }
  }, [handler, actor, action, query]);

  useEffect(() => {
    void load();
  }, [load]);

  const onClear = useCallback(async () => {
    setClearing(true);
    try {
      const {success} = await handler.clearAgentActionJournal();
      if (success) {
        reqSeqRef.current++;
        setEntries([]);
        setStatus('loaded');
      }
    } finally {
      setClearing(false);
    }
  }, [handler]);

  return (
    <div id="agent-activity" className="mb-6 space-y-3" data-agent-activity="true">
      <div className="flex flex-wrap items-start justify-between gap-4 px-1">
        <div className="space-y-1">
          <h3 className="text-base font-semibold tracking-tight">Agent activity</h3>
          <p className="text-sm text-muted-foreground">
            Consequential actions taken by Maho agents (AI panel, MCP clients, Maho CLI).
            Stored only on this device for 30 days, never synced, and never recorded in incognito.
          </p>
        </div>
        <Button
          variant="outline"
          size="sm"
          disabled={clearing || entries.length === 0}
          onClick={() => void onClear()}
          aria-label="Clear agent activity">
          Clear
        </Button>
      </div>
      {queued.length > 0 || cancelNotice ? (
        <div className="overflow-hidden rounded-xl border border-border/60 glass p-4 space-y-2"
             data-agent-mail-queue="true">
          <h4 className="text-sm font-semibold">Queued agent mail</h4>
          {cancelNotice ? (
            <p className="text-sm text-muted-foreground" role="status">{cancelNotice}</p>
          ) : null}
          <ul className="divide-y" aria-label="Queued agent mail">
            {queued.map(item => (
              <li key={item.id} className="flex flex-wrap items-center justify-between gap-2 py-2 text-sm"
                  data-agent-mail-queued-row="true">
                <span>
                  <span className="font-medium">{item.subject || '(no subject)'}</span>
                  {item.to.length > 0 ? ` · to ${item.to.join(', ')}` : ''}
                  {item.sendAtMs ? ` · sends at ${new Date(item.sendAtMs).toLocaleTimeString()}` : ''}
                </span>
                <Button
                  variant="outline"
                  size="sm"
                  onClick={() => void onCancelQueued(item.id)}
                  aria-label={`Cancel sending ${item.subject || 'mail'}`}>
                  Cancel
                </Button>
              </li>
            ))}
          </ul>
        </div>
      ) : null}
      <div className="overflow-hidden rounded-xl border border-border/60 glass p-4 space-y-3">
        <div className="flex flex-wrap gap-2">
          <select
            aria-label="Filter by agent"
            className="h-8 rounded-md border bg-background px-2 text-sm"
            value={actor}
            onChange={event => setActor(event.target.value)}>
            <option value="">All agents</option>
            {Object.entries(AGENT_ACTIVITY_ACTOR_LABELS).map(([value, label]) => (
              <option key={value} value={value}>{label}</option>
            ))}
          </select>
          <select
            aria-label="Filter by action"
            className="h-8 rounded-md border bg-background px-2 text-sm"
            value={action}
            onChange={event => setAction(event.target.value)}>
            <option value="">All actions</option>
            {Object.entries(AGENT_ACTIVITY_ACTION_LABELS).map(([value, label]) => (
              <option key={value} value={value}>{label}</option>
            ))}
          </select>
          <Input
            aria-label="Search agent activity"
            className="h-8 max-w-xs"
            placeholder="Search site or summary"
            value={query}
            onChange={event => setQuery(event.target.value)}
          />
        </div>
        {status === 'error' ? (
          <p className="text-sm text-destructive" role="alert">Agent activity is unavailable.</p>
        ) : null}
        {status === 'loaded' && entries.length === 0 ? (
          <p className="text-sm text-muted-foreground" data-agent-activity-empty="true">
            No agent activity recorded.
          </p>
        ) : null}
        {entries.length > 0 ? (
          <ul className="divide-y" aria-label="Agent activity entries">
            {entries.map(entry => (
              <li key={String(entry.id)} className="py-2 text-sm" data-agent-activity-row="true">
                <div className="flex flex-wrap justify-between gap-2">
                  <span className="font-medium">
                    {AGENT_ACTIVITY_ACTION_LABELS[entry.action] ?? entry.action}
                  </span>
                  <span className="text-muted-foreground">{formatTimestamp(entry.timestamp)}</span>
                </div>
                <div className="text-muted-foreground">
                  {AGENT_ACTIVITY_ACTOR_LABELS[entry.actor] ?? entry.actor}
                  {entry.origin ? ` · ${entry.origin}` : ''}
                  {entry.outcome ? ` · ${entry.outcome}` : ''}
                </div>
              </li>
            ))}
          </ul>
        ) : null}
      </div>
    </div>
  );
}
