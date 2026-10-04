// Copyright 2026 Maho Browser. All rights reserved.

import React from 'react';
import type {LucideIcon} from 'lucide-react';
import {BrainCircuit, CheckCircle, Search, Server, Shield, Sparkles} from 'lucide-react';

import {Button} from '@ui/button';
import type {MailOnboardingStringLookup} from './mail_onboarding_api.js';

type MailOnboardingSuccessStepProps = {
  readonly copy: MailOnboardingStringLookup;
  readonly onComplete: () => void;
};

type SuccessFeature = {
  readonly key: string;
  readonly title: string;
  readonly description: string;
  readonly icon: LucideIcon;
};

export function MailOnboardingSuccessStep({copy, onComplete}: MailOnboardingSuccessStepProps) {
  const features: readonly SuccessFeature[] = [
    {
      key: 'smart-inbox',
      title: copy('onboarding.featureSmartInbox', 'Smart Inbox'),
      description: copy('onboarding.featureSmartInboxDesc', 'AI categorizes your emails automatically'),
      icon: BrainCircuit,
    },
    {
      key: 'ai-assistant',
      title: copy('onboarding.featureAiAssistant', 'AI Assistant'),
      description: copy('onboarding.featureAiAssistantDesc', 'Summarize, reply, and translate with AI'),
      icon: Sparkles,
    },
    {
      key: 'quick-search',
      title: copy('onboarding.featureQuickSearch', 'Quick Search'),
      description: copy('onboarding.featureQuickSearchDesc', 'Find emails with keyword or AI search'),
      icon: Search,
    },
    {
      key: 'privacy',
      title: copy('onboarding.featurePrivacy', 'Privacy First'),
      description: copy('onboarding.featurePrivacyDesc', 'Your data stays on your device'),
      icon: Shield,
    },
  ];

  return (
    <div className="flex flex-col items-center py-4 text-center sm:py-6">
      <CheckCircle className="size-12 mb-3 text-success" />
      <h2 className="mb-1 text-base font-semibold text-foreground">{copy('onboarding.accountAdded', 'Account added!')}</h2>
      <p className="mb-5 max-w-sm text-xs text-muted-foreground">
        {copy('onboarding.accountAddedDescription', 'Your email account has been set up successfully.')}
      </p>

      <div className="mb-6 grid w-full gap-3 md:grid-cols-2">
        {features.map((feature) => {
          const Icon = feature.icon;
          return (
            <div key={feature.key} className="rounded-xl border border-border bg-card/60 p-3 text-left">
              <div className="mb-2.5 flex h-9 w-9 items-center justify-center rounded-lg bg-muted text-primary border border-border">
                <Icon className="size-4.5" />
              </div>
              <h3 className="text-xs font-semibold text-foreground">{feature.title}</h3>
              <p className="mt-1 text-[10px] leading-relaxed text-muted-foreground">{feature.description}</p>
            </div>
          );
        })}
      </div>

      <Button type="button" variant="default" size="lg" className="w-fit" onClick={onComplete}>
        {copy('onboarding.getStarted', 'Get Started')}
      </Button>
    </div>
  );
}
