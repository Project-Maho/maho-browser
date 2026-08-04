// Copyright 2026 Maho Browser. All rights reserved.

import React from 'react';

import {cn} from '@lib/utils';
import type {MailOnboardingStepKey, MailOnboardingStringLookup} from './mail_onboarding_api.js';
import type {SetupStep} from './mail_onboarding_config.js';
import {isStepperStepActive, stepLabel} from './mail_onboarding_strings.js';

type MailOnboardingStepperProps = {
  readonly steps: readonly MailOnboardingStepKey[];
  readonly step: SetupStep;
  readonly copy: MailOnboardingStringLookup;
};

export function MailOnboardingStepper({steps, step, copy}: MailOnboardingStepperProps) {
  return (
    <div className="mx-auto mb-4 flex w-full max-w-lg items-start py-2">
      {steps.map((stepKey, index) => {
        const isActive = isStepperStepActive(stepKey, step);
        return (
          <div key={stepKey} className="flex min-w-0 flex-1 items-center last:flex-none">
            <div className="flex shrink-0 flex-col items-center gap-1.5">
              <div
                className={cn(
                  'flex size-7 aspect-square shrink-0 items-center justify-center rounded-full border text-xs font-semibold transition-colors',
                  isActive
                    ? 'border-primary bg-primary text-primary-foreground'
                    : 'border-border bg-card text-muted-foreground',
                )}
              >
                {index + 1}
              </div>
              <span className={cn('text-[10px] font-medium', isActive ? 'text-primary font-semibold' : 'text-muted-foreground')}>
                {stepLabel(stepKey, copy)}
              </span>
            </div>
            {index < steps.length - 1 && (
              <div className="mx-2 mt-3.5 h-px min-w-2 flex-1 bg-border" aria-hidden="true" />
            )}
          </div>
        );
      })}
    </div>
  );
}
