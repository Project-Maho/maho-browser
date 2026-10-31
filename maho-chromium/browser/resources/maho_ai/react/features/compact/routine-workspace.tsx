import React, {useCallback, useEffect, useRef, useState} from 'react';

import {Badge} from '@ui/badge';
import {Button} from '@ui/button';
import {Input} from '@ui/input';
import {Textarea} from '@ui/textarea';
import {Plus, X, Check, ShieldAlert} from '@icons/lucide';
import type {
  RoutineInfo,
  RoutineOperationsClient,
  RoutineRunRecord,
  RoutineRunStatus,
} from '../../../routine-client.js';

type TriggerMode = 'manual' | 'schedule' | 'event';

const STATUS_PRESENTATION = {
  queued: {label: 'Queued', variant: 'secondary'},
  running: {label: 'Running', variant: 'warning'},
  awaiting_approval: {label: 'Awaiting approval', variant: 'warning'},
  succeeded: {label: 'Succeeded', variant: 'success'},
  failed: {label: 'Failed', variant: 'destructive'},
} as const;

function formatSchedule(schedule: string | null): string {
  if (!schedule) return 'Manual';
  const trimmed = schedule.trim();
  if (trimmed === '0 9 * * 1-5' || trimmed === '0 9 * * 1-5 *') {
    return 'Every weekday at 9:00 AM';
  }
  if (trimmed === '0 9 * * *' || trimmed === '0 9 * * * *') {
    return 'Every day at 9:00 AM';
  }
  if (trimmed === '0 0 * * 0') {
    return 'Every Sunday at midnight';
  }
  return trimmed;
}

function formatTrigger(trigger: string | null): string {
  if (!trigger) return 'Manual';
  if (trigger === 'on_startup') return 'On browser startup';
  if (trigger.startsWith('on_many_tabs')) {
    const count = trigger.split(':')[1] || '20';
    return `When ${count}+ tabs are open`;
  }
  return trigger.replace(/[_.-]+/g, ' ').replace(/^./, c => c.toUpperCase());
}

function formatRunSource(source: string): string {
  if (source === 'manual') return 'Manual run';
  if (source === 'schedule') return 'Scheduled run';
  if (source === 'event') return 'Event-triggered run';
  if (!source) return 'Run';
  return source.replace(/[_.-]+/g, ' ').replace(/^./, c => c.toUpperCase());
}

// Human reason for a failed routines call; the raw backend text stays in the
// console instead of the panel.
function humanizeReason(reason: unknown, fallback: string): string {
  console.error('[maho-ai] routines operation failed:', reason);
  return fallback;
}

function latestStatus(
    statuses: RoutineRunStatus[], routineId: string): RoutineRunStatus | null {
  return statuses
             .filter(status => status.routineId === routineId)
             .sort((left, right) => Number(right.revision - left.revision))[0] ??
      null;
}

export function mergeRoutineStatuses(
    current: RoutineRunStatus[],
    incoming: RoutineRunStatus[]): RoutineRunStatus[] {
  const merged = new Map(current.map(status => [status.runId, status]));
  for (const status of incoming) {
    const existing = merged.get(status.runId);
    if (!existing || status.revision > existing.revision) {
      merged.set(status.runId, status);
    }
  }
  return [...merged.values()];
}

export function RoutineWorkspace(
    {
      client,
      onBackToChat = () => {},
    }: {
      client: RoutineOperationsClient | null;
      onBackToChat?: () => void;
    }) {
  const [routines, setRoutines] = useState<RoutineInfo[]>([]);
  const [statuses, setStatuses] = useState<RoutineRunStatus[]>([]);
  const [history, setHistory] = useState<RoutineRunRecord[]>([]);
  const [eligible, setEligible] = useState<boolean | null>(null);
  const [eligibilityAttempt, setEligibilityAttempt] = useState(0);
  const [name, setName] = useState('');
  const [prompt, setPrompt] = useState('');
  const [triggerMode, setTriggerMode] = useState<TriggerMode>('manual');
  const [schedule, setSchedule] = useState('');
  const [trigger, setTrigger] = useState('');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [recording, setRecording] = useState(false);
  const clientGeneration = useRef(0);
  const refreshRequest = useRef(0);

  const refresh = useCallback(async (generation: number) => {
    if (!client) {
      return;
    }
    const request = ++refreshRequest.current;
    const [nextRoutines, nextStatuses, history] = await Promise.all([
      client.list(),
      client.statuses(),
      client.history(null, 5),
    ]);
    if (generation !== clientGeneration.current ||
        request !== refreshRequest.current) {
      return;
    }
    setRoutines(nextRoutines);
    setStatuses(current => mergeRoutineStatuses(current, nextStatuses));
    setHistory(history);
    setError(null);
  }, [client]);

  useEffect(() => {
    const generation = ++clientGeneration.current;
    ++refreshRequest.current;
    let active = true;
    setRoutines([]);
    setStatuses([]);
    setHistory([]);
    setBusy(false);
    setError(null);
    if (!client) {
      setEligible(false);
      return () => {
        active = false;
      };
    }
    setEligible(null);
    void client.isEligible()
        .then(async nextEligible => {
          if (!active) {
            return;
          }
          setEligible(nextEligible);
          if (nextEligible) {
            await refresh(generation);
          }
        })
        .catch(reason => {
          if (active) {
            setError(humanizeReason(
                reason, 'Routines could not be loaded. Check your connection and try again.'));
          }
        });
    return () => {
      active = false;
      if (clientGeneration.current === generation) {
        ++clientGeneration.current;
        ++refreshRequest.current;
      }
    };
  }, [client, refresh, eligibilityAttempt]);

  useEffect(() => {
    if (!client) {
      return;
    }
    const generation = clientGeneration.current;
    return client.subscribe(status => {
      if (generation !== clientGeneration.current) {
        return;
      }
      setStatuses(current => {
        return mergeRoutineStatuses(current, [status]);
      });
      if (status.state === 'succeeded' || status.state === 'failed') {
        void refresh(generation);
      }
    });
  }, [client, refresh, eligibilityAttempt]);

  const runMutation = useCallback(async (mutation: () => Promise<unknown>) => {
    const generation = clientGeneration.current;
    setBusy(true);
    try {
      if (await mutation() === false) {
        throw new Error('That action was not accepted. Try again.');
      }
      await refresh(generation);
    } catch (reason) {
      if (generation === clientGeneration.current) {
        setError(humanizeReason(
            reason, 'That action could not be completed. Try again.'));
      }
    } finally {
      if (generation === clientGeneration.current) {
        setBusy(false);
      }
    }
  }, [refresh]);

  const workspaceHeader = (
    <div className="mb-3 flex items-center justify-between gap-2 border-b border-border pb-2.5">
      <div>
        <h2 className="text-sm font-semibold tracking-tight text-foreground">Routines</h2>
        <p className="text-xs text-muted-foreground">
          Automate recurring browser work. Recent runs: {history.length}
        </p>
      </div>
      <Button
        aria-label="Back to current chat"
        className="h-7 px-2.5 text-xs font-medium text-muted-foreground hover:text-foreground"
        onClick={onBackToChat}
        size="sm"
        type="button"
        variant="ghost">
        Back to chat
      </Button>
    </div>
  );

  if (eligible === null) {
    return (
      <section aria-busy={!error} aria-label="Routines" className="p-3.5">
        {workspaceHeader}
        {error ? (
          <div role="alert" data-routine-error className="text-xs text-destructive">
            <p>{error}</p>
            <Button data-routine-retry onClick={() => setEligibilityAttempt(attempt => attempt + 1)} type="button" variant="outline">
              Retry
            </Button>
          </div>
        ) : (
          <p className="mt-4 text-center text-xs text-muted-foreground">Loading routines...</p>
        )}
      </section>
    );
  }
  if (!client || eligible === false) {
    return (
      <section aria-label="Routines" className="p-3.5">
        {workspaceHeader}
        <div className="mt-6 flex flex-col items-center justify-center p-6 text-center">
          <h3 className="text-sm font-medium text-foreground">Maho Max Required</h3>
          <p className="mt-1 text-xs text-muted-foreground">
            Routines allow automating recurring browser work and require a Maho Max subscription.
          </p>
        </div>
      </section>
    );
  }
  return (
    <section aria-label="Routines" className="flex h-full flex-col min-h-0 overflow-auto p-3.5">
      {workspaceHeader}

      {error ? (
        <div role="alert" data-routine-error className="mb-3 flex items-center gap-2 rounded-lg border border-destructive/30 bg-destructive/10 p-2.5 text-xs text-destructive">
          <ShieldAlert className="size-4 shrink-0" />
          <p className="flex-1">{error}</p>
          <Button
            className="h-6 shrink-0 px-2 text-2xs"
            data-routine-retry
            disabled={busy}
            onClick={() => void runMutation(async () => {})}
            size="sm"
            type="button"
            variant="outline">
            Retry
          </Button>
        </div>
      ) : null}

      {history.length > 0 ? (
        <div
          aria-label="Recent Routine history"
          className="mb-3 space-y-1.5">
          {history.map(record => (
            <div
              className="flex items-start justify-between gap-2 rounded-lg border border-border/70 bg-card px-2.5 py-1.5 text-2xs shadow-2xs"
              data-routine-history-run={record.resultId.toString()}
              key={record.resultId.toString()}>
              <div className="min-w-0 flex-1">
                <span className="font-medium text-foreground">
                  {routines.find(routine => routine.id === record.routineId)?.name ??
                      record.routineId}
                </span>
                <span className="ml-2 text-muted-foreground">
                  {formatRunSource(record.source)} · {record.success ? 'Succeeded' : 'Failed'}
                </span>
                <p className="mt-0.5 line-clamp-1 text-muted-foreground">{record.content}</p>
              </div>
            </div>
          ))}
        </div>
      ) : null}

      <form
        className="mb-4 grid gap-2.5 rounded-xl border border-border bg-card p-3.5 shadow-xs"
        onSubmit={event => {
          event.preventDefault();
          const selectedSchedule =
              triggerMode === 'schedule' ? schedule.trim() : null;
          const selectedTrigger =
              triggerMode === 'event' ? trigger.trim() : null;
          void runMutation(async () => {
            const created = await client.create(
                name.trim(), prompt.trim(), selectedSchedule, selectedTrigger);
            if (!created) {
              throw new Error('Routine creation failed');
            }
            setName('');
            setPrompt('');
            setSchedule('');
            setTrigger('');
          });
        }}>
        <div className="flex items-center justify-between border-b border-border pb-1">
          <h3 className="text-xs font-semibold text-foreground">Create routine</h3>
          {client && (
            <Button
              type="button"
              size="sm"
              variant={recording ? "destructive" : "outline"}
              className="h-6 text-2xs px-2"
              onClick={async () => {
                if (recording) {
                  const res = await client.stopTraceRecording();
                  setRecording(false);
                  if (res.traceJson) {
                    setPrompt(prev => prev ? `${prev}\n\n${res.traceJson}` : res.traceJson);
                  }
                } else {
                  const ok = await client.startTraceRecording(0n);
                  if (ok) {
                    setRecording(true);
                  }
                }
              }}>
              {recording ? "🔴 Stop Recording" : "Record Actions (Trace v3)"}
            </Button>
          )}
        </div>
        <Input
          aria-label="Routine name"
          className="text-xs"
          onChange={event => setName(event.target.value)}
          placeholder="Routine name (e.g., Morning Briefing)"
          required
          value={name}
        />
        <Textarea
          aria-label="Routine prompt"
          className="min-h-[60px] text-xs"
          onChange={event => setPrompt(event.target.value)}
          placeholder="What should Maho do?"
          required
          value={prompt}
        />
        <fieldset className="grid gap-1.5">
          <legend className="text-xs font-medium text-muted-foreground">Run mode</legend>
          <div className="flex flex-wrap gap-1.5">
            {([
              ['manual', 'Manual'],
              ['schedule', 'Schedule'],
              ['event', 'Event'],
            ] as const).map(([value, label]) => (
              <Button
                aria-pressed={triggerMode === value}
                className="h-7 text-xs"
                data-state={triggerMode === value ? 'active' : 'inactive'}
                key={value}
                onClick={() => setTriggerMode(value)}
                size="sm"
                type="button"
                variant={triggerMode === value ? 'default' : 'outline'}>
                {label}
              </Button>
            ))}
          </div>
        </fieldset>
        {triggerMode === 'schedule' ? (
          <Input
            aria-label="Cron schedule"
            className="text-xs font-mono"
            onChange={event => setSchedule(event.target.value)}
            placeholder="0 9 * * 1-5"
            required
            value={schedule}
          />
        ) : null}
        {triggerMode === 'event' ? (
          <select
            aria-label="Event trigger"
            className="h-8 rounded-md border border-input bg-background px-2.5 text-xs shadow-2xs focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
            onChange={event => setTrigger(event.target.value)}
            required
            value={trigger}>
            <option value="">Choose event</option>
            <option value="on_startup">Browser startup</option>
            <option value="on_many_tabs:20">20 open tabs</option>
          </select>
        ) : null}
        <Button disabled={busy} size="sm" type="submit" variant="secondary">
          {busy ? 'Saving...' : 'Create routine'}
        </Button>
      </form>

      {!routines.length ? (
        <p className="rounded-xl border border-dashed border-border p-4 text-xs text-muted-foreground text-center">
          No routines yet. Create your first routine above.
        </p>
      ) : (
        <div className="space-y-2">
          {routines.map(routine => {
            const status = latestStatus(statuses, routine.id);
            const isAwaitingApproval = status?.state === 'awaiting_approval' && Boolean(status?.approvalId);
            return (
              <div
                className="rounded-xl border border-border bg-card p-3 shadow-xs"
                key={routine.id}>
                <div className="flex items-start justify-between gap-2">
                  <div className="min-w-0 flex-1">
                    <div className="font-medium text-xs text-foreground truncate">{routine.name}</div>
                    <div className="text-2xs text-muted-foreground">
                      {routine.schedule ? (
                        <span>{formatSchedule(routine.schedule)}</span>
                      ) : routine.trigger ? (
                        <span>{formatTrigger(routine.trigger)}</span>
                      ) : (
                        <span>Manual</span>
                      )}
                    </div>
                  </div>
                  {status ? (
                    <Badge
                      aria-live="polite"
                      className="shrink-0 text-3xs font-medium"
                      data-routine-status={status.state}
                      role="status"
                      variant={STATUS_PRESENTATION[status.state].variant}>
                      {STATUS_PRESENTATION[status.state].label}
                    </Badge>
                  ) : null}
                </div>

                <div className="mt-2.5 flex flex-wrap items-center gap-1.5">
                  <Button
                    className="h-6 px-2.5 text-2xs"
                    disabled={busy}
                    onClick={() => void runMutation(() => client!.run(routine.id))}
                    size="sm"
                    type="button"
                    variant="outline">
                    Run
                  </Button>
                  {isAwaitingApproval ? (
                    <>
                      <Button
                        className="h-6 px-2 text-2xs"
                        disabled={busy}
                        onClick={() => void runMutation(() => client!.approve(
                            status!.runId, status!.approvalId!, true))}
                        size="sm"
                        type="button">
                        <Check className="mr-1 size-3" />
                        Approve
                      </Button>
                      <Button
                        className="h-6 px-2 text-2xs"
                        disabled={busy}
                        onClick={() => void runMutation(() => client!.approve(
                            status!.runId, status!.approvalId!, false))}
                        size="sm"
                        type="button"
                        variant="outline">
                        Deny
                      </Button>
                    </>
                  ) : null}
                </div>
              </div>
            );
          })}
        </div>
      )}
    </section>
  );
}
