// Copyright 2026 Maho Browser. All rights reserved.

import React from 'react';
import {AlertTriangle, Loader2, Server, Sparkles} from 'lucide-react';

import {Alert, AlertDescription} from '@ui/alert';
import {Badge} from '@ui/badge';
import type {TranslationProviderChoice, TranslationStepState} from './mail_onboarding_controller_types.js';
import type {MailOnboardingStringLookup} from './mail_onboarding_api.js';

type MailOnboardingTranslationStepProps = {
  readonly state: TranslationStepState;
  readonly copy: MailOnboardingStringLookup;
  readonly onSelectProvider: (provider: TranslationProviderChoice) => Promise<void>;
};

export function MailOnboardingTranslationStep({state, copy, onSelectProvider}: MailOnboardingTranslationStepProps) {
  return (
    <div className="w-full space-y-4">
      <div>
        <h2 className="text-base font-semibold text-foreground">{copy('onboarding.translationStepTitle', 'Translation Setup')}</h2>
        <p className="mt-1 text-xs text-muted-foreground">
          {copy('onboarding.translationStepSubtitle', "Choose how you'd like to translate emails in other languages.")}
        </p>
      </div>

      <div className="grid gap-3">
        <button
          type="button"
          onClick={() => { void onSelectProvider('byok'); }}
          disabled={state.busy || state.checkingAiConfig}
          className="flex items-start gap-3 rounded-xl border border-border bg-background/60 p-4 text-left transition-colors hover:bg-muted/40 focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring disabled:opacity-50"
        >
          <div className="flex h-10 w-10 shrink-0 items-center justify-center rounded-xl bg-card border border-border text-primary">
            <Sparkles className="size-5" />
          </div>
          <div className="min-w-0 flex-1">
            <div className="flex flex-wrap items-center gap-2">
              <span className="text-xs font-semibold text-foreground">{copy('onboarding.translationByokTitle', 'Cloud AI (Your API Key)')}</span>
              <Badge variant="secondary" className="border-transparent bg-primary/10 text-primary py-0 px-1.5 text-[9px]">
                {copy('onboarding.translationByokRecommended', 'Recommended')}
              </Badge>
            </div>
            <p className="mt-1 text-[10px] leading-relaxed text-muted-foreground">
              {copy('onboarding.translationByokSubtitle', 'Use your existing OpenAI, Anthropic, or Ollama key for fast, high-quality translation.')}
            </p>
            {state.checkingAiConfig ? (
              <p className="mt-2 text-[9px] text-muted-foreground flex items-center gap-1">
                <Loader2 className="size-2.5 animate-spin" /> {copy('onboarding.translationCheckingConfig', 'Checking configuration...')}
              </p>
            ) : !state.isAiConfigured ? (
              <p className="mt-2 text-[9px] text-muted-foreground">
                {copy('onboarding.translationByokNeedsConfig', "You'll need to add an API key in Settings → AI after setup.")}
              </p>
            ) : null}
          </div>
        </button>

        <button
          type="button"
          onClick={() => { void onSelectProvider('skip'); }}
          disabled={state.busy}
          className="flex items-start gap-3 rounded-xl border border-border bg-background/60 p-4 text-left transition-colors hover:bg-muted/40 focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
        >
          <div className="flex h-10 w-10 shrink-0 items-center justify-center rounded-xl bg-card border border-border text-muted-foreground">
            <Server className="size-5" />
          </div>
          <div className="min-w-0 flex-1">
            <span className="text-xs font-semibold text-foreground">{copy('onboarding.translationSkipTitle', 'Skip for now')}</span>
            <p className="mt-1 text-[10px] leading-relaxed text-muted-foreground">
              {copy('onboarding.translationSkipSubtitle', 'You can enable translation later in Settings.')}
            </p>
          </div>
        </button>
      </div>

      {state.error && (
        <Alert variant="destructive">
          <AlertTriangle className="size-3.5" />
          <AlertDescription className="text-[10px]">{state.error}</AlertDescription>
        </Alert>
      )}
    </div>
  );
}
