// Copyright 2026 Maho Browser. All rights reserved.

import React, {useState} from 'react';
import {ArrowLeft, Sparkles} from 'lucide-react';

import {PlanPicker} from '@ui/plan-picker';
import type {PlanTier} from '@ui/plan-picker';
import {Button} from '@ui/button';

import type {AiSetupState} from '../types.js';
import type {MahoWelcomeStore} from '../store.js';

interface PlanSelectProps {
  store: MahoWelcomeStore;
  aiSetup: AiSetupState;
}

export function PlanSelect({store, aiSetup}: PlanSelectProps) {
  const [busyTier, setBusyTier] = useState<PlanTier | null>(null);

  function selectPlan(tier: PlanTier) {
    if (tier === 'free') {
      store.continueWithFreePlan();
      return;
    }

    setBusyTier(tier);
    void store.startSubscriptionCheckout(tier).finally(() => setBusyTier(null));
  }

  return (
    <main className="fixed inset-0 isolate flex min-h-screen w-full items-center justify-center overflow-auto bg-background px-6 py-10 text-foreground">
      <div aria-hidden="true" className="absolute inset-0 bg-gradient-to-br from-background via-background to-muted" />
      <div aria-hidden="true" className="absolute left-[18%] top-[12%] h-72 w-72 rounded-full bg-primary/10 blur-3xl" />
      <div aria-hidden="true" className="absolute bottom-0 right-0 h-96 w-96 translate-x-1/4 translate-y-1/4 rounded-full bg-muted/60 blur-3xl" />
      <section className="relative z-10 w-full max-w-5xl rounded-2xl border border-border bg-card p-6 shadow-2xl sm:p-10">
        <Button
          type="button"
          variant="ghost"
          className="mb-8 gap-2 text-muted-foreground"
          onClick={() => store.prevPage()}
        >
          <ArrowLeft className="size-4" />
          Back
        </Button>
        <div className="mx-auto max-w-2xl text-center">
          <div className="mx-auto mb-4 flex size-11 items-center justify-center rounded-xl bg-primary/10 text-primary">
            <Sparkles className="size-5" />
          </div>
          <h1 className="text-3xl font-semibold tracking-tight">Choose your Managed AI plan</h1>
          <p className="mt-3 text-sm leading-6 text-muted-foreground">
            Start free or choose a monthly plan. You can manage your plan anytime in Settings.
          </p>
        </div>
        <div className="mx-auto mt-10 max-w-4xl">
          <PlanPicker
            allowCurrentSelection
            busyTier={busyTier}
            freeLabel="Continue with Free"
            onSelect={selectPlan}
          />
        </div>
        {aiSetup.status === 'error' ? (
          <p role="alert" className="mt-5 text-center text-sm text-destructive">
            {aiSetup.errorMessage}
          </p>
        ) : null}
      </section>
    </main>
  );
}
