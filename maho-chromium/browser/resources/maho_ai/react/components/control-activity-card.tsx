import {Bot, CircleDot, Eye, PauseCircle, XCircle} from 'lucide-react';

import {cn} from '@lib/utils';

import {
  ControlActivityState,
} from '../../maho_ai.mojom-webui.js';
import type {ControlActivitySnapshot} from '../../maho_ai.mojom-webui.js';
import {useControlActivityTimeline} from '../hooks/use-control-activity.js';

function statePhrase(state: ControlActivityState): string {
  switch (state) {
    case ControlActivityState.kReading:
      return 'Reading this tab';
    case ControlActivityState.kActing:
      return 'Acting on this tab';
    case ControlActivityState.kWaitingApproval:
      return 'Waiting for approval';
    case ControlActivityState.kPaused:
      return 'Paused';
    case ControlActivityState.kFailed:
      return 'Action failed';
    default:
      return 'Working in this browser';
  }
}

function stateIcon(state: ControlActivityState): typeof Eye {
  switch (state) {
    case ControlActivityState.kReading:
      return Eye;
    case ControlActivityState.kActing:
      return CircleDot;
    case ControlActivityState.kFailed:
      return XCircle;
    case ControlActivityState.kPaused:
      return PauseCircle;
    default:
      return Bot;
  }
}

export function ControlActivityCard(): React.JSX.Element|null {
  const snapshots = useControlActivityTimeline();
  const snapshot: ControlActivitySnapshot|null = snapshots.at(-1) ?? null;

  if (!snapshot || snapshot.state === ControlActivityState.kIdle ||
      snapshot.state === ControlActivityState.kDisconnected) {
    return null;
  }

  const Icon = stateIcon(snapshot.state);
  const target =
      snapshot.targetTitle ? ` · ${snapshot.targetTitle}` : '';

  const failed = snapshot.state === ControlActivityState.kFailed;
  const waiting = snapshot.state === ControlActivityState.kWaitingApproval;

  return (
    <div
      className={cn(
          'flex min-w-0 items-center gap-2 rounded-xl panel-card px-2.5 py-2 text-[12px] tracking-[-0.005em] text-foreground',
          failed && 'border-destructive/30',
          waiting && 'border-warning/30')}
      data-testid="control-activity-card"
    >
      <Icon
        aria-hidden="true"
        className={cn(
            'size-3.5 shrink-0',
            failed ? 'text-destructive'
                   : waiting ? 'text-warning' : 'text-panel-accent')} />
      <span className="min-w-0 truncate">
        <span className="font-medium">{snapshot.controllerName}</span>
        <span className="text-muted-foreground">
          {' — '}{statePhrase(snapshot.state)}{target}
        </span>
      </span>
    </div>
  );
}
