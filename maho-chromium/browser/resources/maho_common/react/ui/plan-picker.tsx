// Copyright 2026 Maho Browser. All rights reserved.

import React from 'react';
import {Check} from 'lucide-react';

import {cn} from '@lib/utils';
import {Badge} from '@ui/badge';
import {Button} from '@ui/button';

export type PlanTier = 'free' | 'pro' | 'max';

interface PlanDef {
  tier: PlanTier;
  name: string;
  priceUsd: number;
  ceilingUsd: number;
  tagline: string;
  features: readonly string[];
  recommended?: boolean;
}

const FREE_CEILING_USD = 0.5;

export const PLANS: readonly PlanDef[] = [
  {
    tier: 'free',
    name: 'Free',
    priceUsd: 0,
    ceilingUsd: 0.5,
    tagline: 'Best for trying Maho AI',
    features: ['Bring your own API key (BYOK)', 'Basic browser AI'],
  },
  {
    tier: 'pro',
    name: 'Pro',
    priceUsd: 5,
    ceilingUsd: 2.1,
    tagline: 'Best for everyday AI use',
    features: ['Everything in Free', 'Cheap-fast models (Haiku / Flash / Mini class)'],
    recommended: true,
  },
  {
    tier: 'max',
    name: 'Max',
    priceUsd: 20,
    ceilingUsd: 13.3,
    tagline: 'Best for power users',
    features: ['Everything in Pro', 'Mid-tier models (Sonnet / Gemini Pro class)'],
  },
];

function usageLine(plan: PlanDef): string {
  if (plan.tier === 'free') {
    return `~$${plan.ceilingUsd.toFixed(2)}/mo usage included`;
  }
  return `~${Math.round(plan.ceilingUsd / FREE_CEILING_USD)}\u00d7 Free usage`;
}

const TIER_RANK: Record<PlanTier, number> = {free: 0, pro: 1, max: 2};

interface PlanPickerProps {
  currentTier?: PlanTier;
  busyTier?: PlanTier | null;
  disabled?: boolean;
  freeLabel?: string;
  allowCurrentSelection?: boolean;
  onSelect: (tier: PlanTier) => void;
}

export function PlanPicker({
  currentTier = 'free',
  busyTier = null,
  disabled = false,
  freeLabel = 'Select Free',
  allowCurrentSelection = false,
  onSelect,
}: PlanPickerProps) {
  return (
    <div className="grid w-full gap-4 sm:grid-cols-3">
      {PLANS.map((plan) => {
        const isCurrent = plan.tier === currentTier;
        const isSelectableCurrent = isCurrent && allowCurrentSelection;
        const isBusy = busyTier === plan.tier;
        const isLowerThanCurrent = TIER_RANK[plan.tier] < TIER_RANK[currentTier];
        const label = isCurrent && !isSelectableCurrent
          ? 'Current plan'
          : plan.tier === 'free'
          ? freeLabel
          : isLowerThanCurrent
          ? 'Switch'
          : 'Upgrade';

        return (
          <div
            key={plan.tier}
            className={cn(
              'flex flex-col rounded-2xl border p-5 text-left transition-colors',
              plan.recommended ? 'border-primary/60 bg-primary/5' : 'border-border bg-background/60',
            )}
          >
            <div className="flex items-center justify-between gap-2">
              <span className="text-base font-semibold text-foreground">{plan.name}</span>
              {plan.recommended ? (
                <Badge variant="secondary" className="border-transparent bg-primary/10 text-primary">
                  Recommended
                </Badge>
              ) : null}
            </div>
            <p className="mt-1 text-xs text-muted-foreground">{plan.tagline}</p>

            <div className="mt-4 flex items-baseline gap-1">
              <span className="text-3xl font-semibold tracking-tight text-foreground">
                ${plan.priceUsd}
              </span>
              <span className="text-sm text-muted-foreground">
                {plan.priceUsd === 0 ? '/ forever' : '/ month'}
              </span>
            </div>

            <ul className="mt-4 flex flex-1 flex-col gap-2">
              <li className="flex items-start gap-2 text-xs text-foreground">
                <Check className="mt-0.5 size-3.5 shrink-0 text-primary" />
                <span>{usageLine(plan)}</span>
              </li>
              {plan.features.map((feature) => (
                <li key={feature} className="flex items-start gap-2 text-xs text-muted-foreground">
                  <Check className="mt-0.5 size-3.5 shrink-0 text-primary" />
                  <span>{feature}</span>
                </li>
              ))}
            </ul>

            <Button
              type="button"
              variant={isCurrent && !isSelectableCurrent ? 'outline' : plan.recommended ? 'default' : 'secondary'}
              className="mt-5 w-full"
              disabled={disabled || (isCurrent && !isSelectableCurrent) || isBusy}
              onClick={() => onSelect(plan.tier)}
            >
              {isBusy ? 'Opening\u2026' : label}
            </Button>
          </div>
        );
      })}
    </div>
  );
}
