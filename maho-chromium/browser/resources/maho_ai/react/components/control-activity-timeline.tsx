import {Bot, CircleDot, Eye, PauseCircle, XCircle} from 'lucide-react';

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
      return 'Disconnected';
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

function isActive(state: ControlActivityState): boolean {
  return state === ControlActivityState.kReading ||
      state === ControlActivityState.kActing ||
      state === ControlActivityState.kWaitingApproval ||
      state === ControlActivityState.kPaused;
}

export function ControlActivityTimeline(): React.JSX.Element|null {
  const entries = useControlActivityTimeline();

  if (entries.length === 0 || !entries.some((entry) => isActive(entry.state))) {
    return null;
  }

  const latest = entries[entries.length - 1];
  if (!latest) {
    return null;
  }

  const history = entries.slice(0, -1);
  const HeaderIcon = stateIcon(latest.state);

  return (
    <div
      className="mb-2 rounded-lg border border-primary/25 bg-primary/5"
      data-testid="control-activity-timeline"
    >
      <div className={history.length === 0
          ? 'flex items-center gap-2.5 px-3 py-2'
          : 'flex items-center gap-2.5 border-b border-border/60 px-3 py-2'}>
        <HeaderIcon className="size-4 shrink-0 text-primary" aria-hidden="true" />
        <span className="min-w-0 truncate text-sm font-medium text-foreground">
          {latest.controllerName}
        </span>
        <span className="ml-auto shrink-0 rounded-full bg-primary/15 px-2 py-0.5 text-xs text-primary">
          {statePhrase(latest.state)}
        </span>
      </div>
      {history.length === 0 ? null : (
        <ol className="px-3 py-1.5">
          {history.map((entry, index) => (
            <li
              key={index}
              className="flex items-center gap-2 py-1 text-xs text-muted-foreground"
            >
              <span
                className="size-1.5 shrink-0 rounded-full bg-purple-500"
                aria-hidden="true"
              />
              <span className="min-w-0 truncate">
                {statePhrase(entry.state)}
                {entry.targetTitle ? ` · ${entry.targetTitle}` : ''}
              </span>
            </li>
          ))}
        </ol>
      )}
    </div>
  );
}
