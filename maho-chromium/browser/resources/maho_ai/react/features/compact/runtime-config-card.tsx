import React, {useEffect, useId, useRef, useState} from 'react';
import {toast} from 'sonner';

import {ChevronDown, Eye, ShieldCheck, Zap} from '@icons/lucide';
import {cn} from '@lib/utils';
import {MahoAiStore} from '../../../store.js';
import type {PermissionTierValue, RuntimeConfigInfo} from '../../../types.js';
import {RUNTIME_TIER_OPTIONS, getRuntimeTierOption} from '../../../views/runtime_config.js';
import {useAppState} from '../../hooks/use-app-state.js';

interface RuntimeToggleDescriptor {
  readonly key: 'finalConfirm'|'proactiveMode'|'mailReadAllowed';
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
  {
    key: 'mailReadAllowed',
    label: 'Read Mail',
    description: 'Allow Mail reads for this profile',
  },
] as const;

const TIER_ICON: Record<PermissionTierValue, typeof Eye> = {
  read_only: Eye,
  guard: ShieldCheck,
  full_access: Zap,
};

const TIER_TONE: Record<PermissionTierValue, string> = {
  read_only: 'text-muted-foreground',
  guard: 'text-warning',
  full_access: 'text-destructive',
};

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
  const [statusMessage, setStatusMessage] = useState<string|null>(null);
  const [open, setOpen] = useState(false);
  const groupRef = useRef<HTMLDivElement>(null);
  const rootRef = useRef<HTMLDivElement>(null);
  const triggerRef = useRef<HTMLButtonElement>(null);
  const panelId = useId();

  useEffect(() => {
    if (!open) {
      return;
    }
    const handlePointerDown = (event: MouseEvent) => {
      if (!rootRef.current?.contains(event.target as Node)) {
        setOpen(false);
      }
    };
    const handleKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        setOpen(false);
        triggerRef.current?.focus();
      }
    };
    document.addEventListener('mousedown', handlePointerDown);
    document.addEventListener('keydown', handleKeyDown);
    return () => {
      document.removeEventListener('mousedown', handlePointerDown);
      document.removeEventListener('keydown', handleKeyDown);
    };
  }, [open]);

  if (!state.runtimeConfigSupported) {
    return null;
  }

  const config: RuntimeConfigInfo = state.runtimeConfig;
  const activeTier = getRuntimeTierOption(config.permissionTier);
  const ActiveTierIcon = TIER_ICON[activeTier.value] ?? ShieldCheck;

  const applyChange =
      async (change: Partial<RuntimeConfigInfo>): Promise<void> => {
        const accepted = await store.setRuntimeConfig(change);
        if (accepted) {
          setStatusMessage(null);
          return;
        }
        // The store already rolled the optimistic patch back; make the
        // refusal visible instead of snapping back silently (P1-2).
        const refusal = 'Change was not applied — the broker refused it.';
        setStatusMessage(refusal);
        toast.error(refusal);
      };

  const changeTier = (tier: string) => {
    void applyChange({permissionTier: tier});
  };

  const toggleFlag = (key: RuntimeToggleDescriptor['key']) => {
    void applyChange({[key]: !config[key]});
  };

  // Roving-tabindex arrow-key contract for the tier radiogroup: selection
  // follows focus, wrapping at both ends.
  const handleTierKeyDown = (event: React.KeyboardEvent<HTMLDivElement>) => {
    if (event.key !== 'ArrowRight' && event.key !== 'ArrowDown' &&
        event.key !== 'ArrowLeft' && event.key !== 'ArrowUp') {
      return;
    }
    event.preventDefault();
    const forward = event.key === 'ArrowRight' || event.key === 'ArrowDown';
    const currentIndex = RUNTIME_TIER_OPTIONS.findIndex(
        option => option.value === activeTier.value);
    const next = RUNTIME_TIER_OPTIONS[
        (currentIndex + (forward ? 1 : -1) + RUNTIME_TIER_OPTIONS.length) %
        RUNTIME_TIER_OPTIONS.length]!;
    changeTier(next.value);
    groupRef.current?.querySelector<HTMLButtonElement>(
        `[data-tier="${next.value}"]`)?.focus();
  };

  return (
    <div className="relative min-w-0 shrink-0" ref={rootRef}>
      <button
        aria-controls={panelId}
        aria-expanded={open}
        aria-label={`Permissions: ${activeTier.label}`}
        className={cn(
            'inline-flex h-6 items-center gap-0.5 rounded-md panel-chip pl-[5px] pr-[7px]',
            'outline-none focus-visible:ring-1 focus-visible:ring-ring',
            open && 'bg-surface-selected')}
        data-runtime-tier-trigger
        onClick={() => setOpen(value => !value)}
        ref={triggerRef}
        title={`${activeTier.label} — ${activeTier.description}`}
        type="button">
        <ActiveTierIcon
          aria-hidden="true"
          className={cn('size-3.5 shrink-0', TIER_TONE[activeTier.value])} />
        <ChevronDown aria-hidden="true" className="size-2.5 shrink-0 opacity-55" />
      </button>

      {/* Anchored, not portaled: the radiogroup and both switches must stay
          inside this subtree for assistive tech. */}
      <div
        className="absolute bottom-full left-0 z-30 mb-1.5 w-[16.5rem] rounded-xl panel-card-raised p-2 text-xs"
        hidden={!open}
        id={panelId}>
        <div
          aria-label="Permission tier"
          className="grid grid-cols-3 gap-0.5 rounded-lg bg-surface-hover/70 p-0.5"
          onKeyDown={handleTierKeyDown}
          ref={groupRef}
          role="radiogroup">
          {RUNTIME_TIER_OPTIONS.map(option => {
            const selected = option.value === activeTier.value;
            return (
              <button
                aria-checked={selected}
                className={cn(
                    'min-w-0 truncate rounded-[7px] px-1 py-1 text-[10.5px] font-medium outline-none transition-colors',
                    'focus-visible:ring-1 focus-visible:ring-ring',
                    selected
                        ? 'bg-card text-foreground shadow-[var(--panel-shadow-flat)]'
                        : 'text-muted-foreground hover:text-foreground')}
                data-tier={option.value}
                key={option.value}
                onClick={() => changeTier(option.value)}
                role="radio"
                tabIndex={selected ? 0 : -1}
                title={option.description}
                type="button">
                {option.label}
              </button>
            );
          })}
        </div>

        {statusMessage ? (
          <p
            className="mt-1.5 text-[11px] leading-4 text-destructive"
            data-config-status
            role="status">
            {statusMessage}
          </p>
        ) : null}

        <p className="mt-1.5 px-0.5 text-[11px] leading-4 text-muted-foreground">
          {activeTier.description}
        </p>

        <div className="mt-1 grid gap-0.5">
          {RUNTIME_TOGGLES.map(toggle => {
            const enabled = config[toggle.key] === true;
            return (
              <button
                aria-checked={enabled}
                aria-label={toggleAriaLabel(toggle, enabled)}
                className={cn(
                    'flex items-center justify-between gap-3 rounded-lg px-1.5 py-1.5 text-left outline-none transition-colors',
                    'hover:bg-surface-hover focus-visible:ring-1 focus-visible:ring-ring',
                    enabled ? 'text-foreground' : 'text-muted-foreground')}
                data-mail-read-consent={
                  toggle.key === 'mailReadAllowed' ? '' : undefined}
                key={toggle.key}
                onClick={() => toggleFlag(toggle.key)}
                role="switch"
                type="button">
                <span className="min-w-0 truncate text-[11.5px] font-medium">
                  {toggle.label}
                </span>
                <span
                  className={cn(
                      'relative inline-flex h-[15px] w-[26px] shrink-0 rounded-full transition-colors duration-200',
                      enabled ? 'bg-panel-accent' : 'bg-border')}>
                  <span
                    className={cn(
                        'absolute top-[2px] size-[11px] rounded-full bg-white shadow-sm transition-all duration-200',
                        enabled ? 'left-[13px]' : 'left-[2px]')} />
                </span>
              </button>
            );
          })}
        </div>
      </div>
    </div>
  );
}
