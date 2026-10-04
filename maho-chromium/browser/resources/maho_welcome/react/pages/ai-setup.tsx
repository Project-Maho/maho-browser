// Copyright 2026 Maho Browser. All rights reserved.

import React from 'react';
import {Check, Clock, ExternalLink, Key, Loader2, Server, Sparkles} from 'lucide-react';

import {cn} from '@lib/utils';
import {Badge} from '@ui/badge';

import type {MahoWelcomeStore} from '../store.js';

export function AiSetupSidebar({store}: {store: MahoWelcomeStore}) {
  return (
    <>
      <h1>Choose Your AI Experience</h1>
      <p>Maho AI is built into your browser — no extensions needed.</p>
      <p>Choose how you want to power it.</p>
    </>
  );
}

type AiProviderChoice = 'maho-managed' | 'byok' | 'skip';

interface Props {
  store: MahoWelcomeStore;
  provider: string;
  status: 'idle' | 'submitting' | 'success' | 'error';
  errorMessage: string;
  isSignedIn: boolean;
}

export function AiSetupContent({store, provider, status, errorMessage, isSignedIn}: Props) {
  const handleSelect = (choice: AiProviderChoice) => {
    store.setAiProvider(choice);
  };

  return (
    <div className="w-full py-4">
      <div>
        <h2 className="text-base font-semibold text-foreground">AI Provider</h2>
        <p className="mt-1 text-xs text-muted-foreground">
          Maho Browser comes with AI built in. Choose how you'd like to power it.
        </p>
      </div>

      <div className="mt-5 grid gap-3">
        <button
          type="button"
          onClick={() => handleSelect('maho-managed')}
          disabled={status === 'submitting'}
          className={cn(
            'flex items-start gap-3 rounded-xl border p-4 text-left transition-colors hover:bg-muted/40 focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring disabled:opacity-50 maho-managed-glow',
            provider === 'maho-managed'
              ? 'border-primary bg-primary/5 ring-1 ring-primary/20'
              : 'border-border bg-background/60'
          )}
        >
          <div className="flex h-10 w-10 shrink-0 items-center justify-center rounded-xl bg-card border border-border text-primary">
            <Sparkles className="size-5" />
          </div>
          <div className="min-w-0 flex-1">
            <div className="flex flex-wrap items-center gap-2">
              <span className="text-xs font-semibold text-foreground">Maho Managed AI Service</span>
              <Badge variant="secondary" className="border-transparent bg-primary/10 text-primary py-0 px-1.5 text-[9px]">
                Recommended
              </Badge>
            </div>
            <p className="mt-1 text-[10px] leading-relaxed text-muted-foreground">
              Simple, private, no API keys needed. Sign in with your Maho account to get started. Free tier available.
            </p>
          </div>
        </button>

        <button
          type="button"
          onClick={() => handleSelect('byok')}
          disabled={status === 'submitting'}
          className={cn(
            'flex items-start gap-3 rounded-xl border p-4 text-left transition-colors hover:bg-muted/40 focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring disabled:opacity-50',
            provider === 'byok'
              ? 'border-primary bg-primary/5 ring-1 ring-primary/20'
              : 'border-border bg-background/60'
          )}
        >
          <div className="flex h-10 w-10 shrink-0 items-center justify-center rounded-xl bg-card border border-border text-muted-foreground">
            <Server className="size-5" />
          </div>
          <div className="min-w-0 flex-1">
            <span className="text-xs font-semibold text-foreground">Bring Your Own Key</span>
            <p className="mt-1 text-[10px] leading-relaxed text-muted-foreground">
              Use your existing OpenAI or Anthropic API key. Full control over model selection and spending.
            </p>
          </div>
        </button>

        <button
          type="button"
          onClick={() => handleSelect('skip')}
          disabled={status === 'submitting'}
          className="flex items-start gap-3 rounded-xl border border-border bg-background/60 p-4 text-left transition-colors hover:bg-muted/40 focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring disabled:opacity-50"
        >
          <div className="flex h-10 w-10 shrink-0 items-center justify-center rounded-xl bg-card border border-border text-muted-foreground">
            <Clock className="size-5" />
          </div>
          <div className="min-w-0 flex-1">
            <span className="text-xs font-semibold text-foreground">Skip for now</span>
            <p className="mt-1 text-[10px] leading-relaxed text-muted-foreground">
              You can set up AI later in Settings.
            </p>
          </div>
        </button>
      </div>

      {provider === 'maho-managed' && !isSignedIn ? (
        <div className="mt-4 rounded-xl border border-border/70 bg-card/50 p-4">
          <p className="mb-3 text-[11px] text-muted-foreground">
            Maho Managed AI requires a Maho account. Sign in to access the managed AI service with free tier credits.
          </p>
          <div className="flex gap-2">
            <button
              type="button"
              onClick={() => store.goToAuth()}
              className="inline-flex items-center gap-1.5 rounded-lg bg-primary px-3.5 py-2 text-xs font-semibold text-primary-foreground shadow-sm hover:bg-primary/90 transition-colors"
            >
              Sign In
            </button>
            <button
              type="button"
              onClick={() => store.goToAuthSignup()}
              className="inline-flex items-center gap-1.5 rounded-lg border border-border bg-background px-3.5 py-2 text-xs font-semibold text-foreground shadow-sm hover:bg-muted/50 transition-colors"
            >
              Create Account
            </button>
          </div>
        </div>
      ) : provider === 'maho-managed' && isSignedIn ? (
        <div className="mt-4 rounded-xl border border-primary/30 bg-primary/5 p-4">
          <div className="flex items-start gap-2">
            <Check className="mt-0.5 size-4 shrink-0 text-primary" />
            <div className="min-w-0">
              <p className="text-[11px] font-semibold text-foreground">Maho Managed AI is ready</p>
              <p className="mt-1 text-[10px] leading-relaxed text-muted-foreground">
                You're on the Free tier with monthly AI credits included. Upgrade or manage billing anytime from Settings. Click Next to continue.
              </p>
            </div>
          </div>
        </div>
      ) : provider === 'byok' ? (
        <div
          className="mt-4 rounded-xl border border-border/70 bg-card/50 p-4"
          data-testid="byok-setup-card"
        >
          <div className="flex items-start justify-between gap-3">
            <div className="flex min-w-0 items-start gap-2.5">
              {status === 'success' ? (
                <Check className="mt-0.5 size-4 shrink-0 text-primary" />
              ) : (
                <Key className="mt-0.5 size-4 shrink-0 text-muted-foreground" />
              )}
              <div className="min-w-0">
                <p className="text-[11px] font-semibold text-foreground">
                  {status === 'success'
                    ? 'BYOK credentials configured'
                    : 'Provider configuration required'}
                </p>
                <p className="mt-1 text-[10px] leading-relaxed text-muted-foreground">
                  {status === 'success'
                    ? 'Your selected provider configuration is saved. Click Next to continue.'
                    : 'Choose OpenAI, Anthropic, or a compatible endpoint in Settings and save your key before continuing.'}
                </p>
              </div>
            </div>
            <button
              type="button"
              disabled={status === 'submitting'}
              onClick={() => { void store.openByokSettings(); }}
              className="flex shrink-0 items-center gap-1.5 rounded-lg border border-border bg-card px-3 py-1.5 text-[11px] font-medium text-foreground transition-colors hover:bg-accent disabled:opacity-50"
            >
              <span>{status === 'success' ? 'Edit in Settings' : 'Configure in Settings'}</span>
              <ExternalLink className="size-3 text-muted-foreground" />
            </button>
          </div>
        </div>
      ) : null}

      {errorMessage ? (
        <p className="mt-3 text-[10px] text-destructive">{errorMessage}</p>
      ) : null}

      {status === 'submitting' ? (
        <div className="mt-4 flex items-center gap-2 text-[10px] text-muted-foreground">
          <Loader2 className="size-3 animate-spin" />
          Saving your AI setup…
        </div>
      ) : null}
    </div>
  );
}
