import React from 'react';

import {MahoAiStore} from '../../../store.js';
import type {RuntimeConfigInfo} from '../../../types.js';
import {RUNTIME_TIER_OPTIONS, getRuntimeTierOption} from '../../../views/runtime_config.js';
import {useAppState} from '../../hooks/use-app-state.js';

interface RuntimeToggleDescriptor {
  readonly key: 'finalConfirm'|'proactiveMode';
  readonly label: string;
  readonly description: string;
}

const RUNTIME_TOGGLES: readonly RuntimeToggleDescriptor[] = [
  {
    key: 'finalConfirm',
    label: 'Final confirmation',
    description: 'Ask before risky actions',
  },
  {
    key: 'proactiveMode',
    label: 'Proactive mode',
    description: 'Act on goals without waiting',
  },
] as const;

function toggleAriaLabel(toggle: RuntimeToggleDescriptor, enabled: boolean):
    string {
  return `${toggle.label}: ${enabled ? 'on' : 'off'}`;
}

// Session runtime-config controls (plan row 4): permission tier selector plus
// final-confirm and proactive-mode toggles. Pure display + dispatch; the
// CapabilityBroker remains the only enforcement point. Hidden entirely when
// the page handler does not expose the runtime config surface.
export function RuntimeConfigCard({store}: {store: MahoAiStore}) {
  const state = useAppState(store);
  if (!state.runtimeConfigSupported) {
    return null;
  }

  const config: RuntimeConfigInfo = state.runtimeConfig;
  const activeTier = getRuntimeTierOption(config.permissionTier);

  const changeTier = (tier: string) => {
    void store.setRuntimeConfig({permissionTier: tier});
  };

  const toggleFlag = (key: 'finalConfirm'|'proactiveMode') => {
    void store.setRuntimeConfig({[key]: !config[key]});
  };

  return (
    <section
      aria-label="Agent session controls"
      className="rounded-xl border border-border/80 bg-card/90 p-3 text-xs shadow-[var(--shadow-raised)]">
      <div className="flex flex-col gap-2.5">
        <div
          aria-label="Permission tier"
          className="grid grid-cols-3 gap-1 rounded-lg bg-surface-hover/60 p-1"
          role="radiogroup">
          {RUNTIME_TIER_OPTIONS.map(option => {
            const selected = option.value === activeTier.value;
            return (
              <button
                aria-checked={selected}
                className={
                  'rounded-md px-2 py-1 font-medium transition-colors ' +
                  (selected ?
                    'bg-card text-foreground shadow-sm' :
                    'text-muted-foreground hover:bg-surface-hover hover:text-foreground')
                }
                key={option.value}
                onClick={() => changeTier(option.value)}
                role="radio"
                title={option.description}
                type="button">
                {option.label}
              </button>
            );
          })}
        </div>
        <p className="text-[11px] leading-4 text-muted-foreground">
          {activeTier.description}
        </p>
        <div className="flex flex-col gap-1.5">
          {RUNTIME_TOGGLES.map(toggle => {
            const enabled = config[toggle.key] === true;
            return (
              <button
                aria-checked={enabled}
                aria-label={toggleAriaLabel(toggle, enabled)}
                className={
                  'flex items-center justify-between gap-2 rounded-lg px-2 py-1.5 text-left transition-colors hover:bg-surface-hover ' +
                  (enabled ? 'text-foreground' : 'text-muted-foreground')
                }
                key={toggle.key}
                onClick={() => toggleFlag(toggle.key)}
                role="switch"
                type="button">
                <span className="flex min-w-0 flex-col">
                  <span className="font-medium">{toggle.label}</span>
                  <span className="truncate text-[11px] text-muted-foreground">
                    {toggle.description}
                  </span>
                </span>
                <span
                  className={
                    'relative inline-flex h-4 w-7 shrink-0 items-center rounded-full transition-colors ' +
                    (enabled ? 'bg-primary' : 'bg-border')
                  }>
                  <span
                    className={
                      'absolute size-3 rounded-full bg-background shadow transition-all ' +
                      (enabled ? 'left-[14px]' : 'left-0.5')
                    } />
                </span>
              </button>
            );
          })}
        </div>
      </div>
    </section>
  );
}
