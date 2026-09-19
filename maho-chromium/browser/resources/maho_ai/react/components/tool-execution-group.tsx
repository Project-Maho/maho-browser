import React, {useEffect, useMemo, useState} from 'react';
import {ChevronDown} from '@icons/lucide';
import {cn} from '@lib/utils';

export interface ToolStep {
  key: string;
  text: string;
  isError: boolean;
  toolName?: string;
  note?: string;
  errorDetail?: string;
  timestamp: number;
}

export interface ToolExecutionGroupProps {
  steps: ToolStep[];
  startTime: number;
  endTime: number;
  isRunning?: boolean;
}

function formatDuration(ms: number): string {
  const totalSec = Math.max(1, Math.floor(ms / 1000));
  if (totalSec < 60) {
    return `${totalSec}s`;
  }
  const minutes = Math.floor(totalSec / 60);
  const seconds = totalSec % 60;
  return `${minutes}m ${seconds}s`;
}

function getToolCategoryLabel(steps: ToolStep[]): string {
  const allNames = steps.map(s => (s.toolName || '').toLowerCase());
  if (allNames.some(n => n.includes('page') || n.includes('tab') || n.includes('browser') || n.includes('navigate'))) {
    return 'Browser actions';
  }
  if (allNames.some(n => n.includes('read') || n.includes('write') || n.includes('edit') || n.includes('patch'))) {
    return 'Edited files';
  }
  return 'Ran commands';
}

export const ToolExecutionGroup = React.memo(function ToolExecutionGroup({
  steps,
  startTime,
  endTime,
  isRunning = false,
}: ToolExecutionGroupProps) {
  const [isExpanded, setIsExpanded] = useState(true);
  const [isCommandsExpanded, setIsCommandsExpanded] = useState(true);
  const [now, setNow] = useState(() => Date.now());

  useEffect(() => {
    if (!isRunning) {
      return undefined;
    }
    const timer = setInterval(() => {
      setNow(Date.now());
    }, 1000);
    return () => clearInterval(timer);
  }, [isRunning]);

  const durationText = useMemo(() => {
    if (isRunning) {
      const elapsedMs = Math.max(1000, now - (startTime || now));
      return `Working... (${formatDuration(elapsedMs)})`;
    }
    const diff = Math.max(1000, (endTime || startTime) - startTime);
    return `Worked for ${formatDuration(diff)}`;
  }, [startTime, endTime, isRunning, now]);

  const categoryLabel = useMemo(() => getToolCategoryLabel(steps), [steps]);

  if (!steps.length) {
    return null;
  }

  return (
    <div className="w-full max-w-full text-left font-sans select-text my-1.5" data-testid="tool-execution-group">
      <button
        type="button"
        onClick={() => setIsExpanded(prev => !prev)}
        className="group/header flex items-center gap-1.5 text-xs font-medium text-muted-foreground hover:text-foreground transition-colors cursor-pointer select-none py-1 focus:outline-none"
        aria-expanded={isExpanded}
      >
        <span>{durationText}</span>
        <ChevronDown
          className={cn(
            'size-3.5 text-muted-foreground transition-transform duration-200',
            !isExpanded && '-rotate-90'
          )}
        />
      </button>

      {isExpanded && (
        <div className="flex flex-col mt-0.5 pl-0.5">
          <button
            type="button"
            onClick={() => setIsCommandsExpanded(prev => !prev)}
            className="group/sub flex items-center gap-1.5 text-xs font-medium text-muted-foreground hover:text-foreground transition-colors cursor-pointer select-none py-1 focus:outline-none"
            aria-expanded={isCommandsExpanded}
          >
            <span className="font-mono text-xs text-muted-foreground select-none">&gt;_</span>
            <span>{categoryLabel}</span>
            <ChevronDown
              className={cn(
                'size-3 text-muted-foreground transition-transform duration-200',
                !isCommandsExpanded && '-rotate-90'
              )}
            />
          </button>

          {isCommandsExpanded && (
            <div className="flex flex-col mt-0.5 pl-0.5">
              {steps.map((step, idx) => (
                <div key={step.key} className="flex flex-col">
                  <div className="flex items-center gap-2 py-0.5 text-[12.5px] leading-5 text-zinc-300">
                    <span className="font-mono text-xs text-zinc-500 select-none shrink-0">&gt;_</span>
                    <span className="font-normal truncate flex-1 min-w-0" title={step.text}>
                      {step.text}
                    </span>
                    {step.isError && (
                      <span className="shrink-0 rounded bg-zinc-800/90 px-1.5 py-0.2 text-[10.5px] font-medium text-zinc-300 border border-zinc-700/60">
                        Error
                      </span>
                    )}
                  </div>
                  {step.isError && (step.errorDetail || step.note) && (
                    <div
                      className="ml-5 my-1 px-2.5 py-1.5 rounded-lg bg-zinc-900/90 border border-destructive/30 font-mono text-[11px] leading-4 text-destructive/90 select-text whitespace-pre-wrap [overflow-wrap:anywhere]"
                      data-testid="tool-step-error-detail"
                    >
                      {step.errorDetail || step.note}
                    </div>
                  )}
                  {idx < steps.length - 1 && (
                    <div className="ml-[5px] my-0.5 h-2 w-px bg-zinc-700/60" />
                  )}
                </div>
              ))}
            </div>
          )}
        </div>
      )}
    </div>
  );
});
