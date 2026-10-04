// Copyright 2026 Maho Browser. All rights reserved.

import React from 'react';
import {AlertTriangle, ArrowLeft, CheckCircle, ChevronDown, ChevronRight, Loader2} from 'lucide-react';

import {Alert, AlertDescription} from '@ui/alert';
import {Button} from '@ui/button';
import {Input} from '@ui/input';
import {Label} from '@ui/label';
import {Select, SelectContent, SelectItem, SelectTrigger, SelectValue} from '@ui/select';
import type {CredentialsStepActions, CredentialsStepState} from './mail_onboarding_controller_types.js';
import {isMailServerEncryption, PROVIDER_SETUP_GUIDES} from './mail_onboarding_config.js';
import type {MailOnboardingStringLookup} from './mail_onboarding_api.js';

type MailOnboardingCredentialsStepProps = {
  readonly state: CredentialsStepState;
  readonly actions: CredentialsStepActions;
  readonly copy: MailOnboardingStringLookup;
};

export function MailOnboardingCredentialsStep({state, actions, copy}: MailOnboardingCredentialsStepProps) {
  const setupGuide = state.selectedProvider ? PROVIDER_SETUP_GUIDES[state.selectedProvider] : undefined;
  return (
    <div className="w-full">
      <Button
        type="button"
        variant="ghost"
        size="sm"
        className="w-fit px-0 text-muted-foreground hover:bg-transparent hover:text-foreground mb-3 text-xs"
        onClick={actions.onBack}
      >
        <ArrowLeft className="mr-1.5 size-3.5" />
        {copy('onboarding.back', 'Back')}
      </Button>

      <h2 className="text-base font-semibold text-foreground mb-3">{copy('onboarding.credentialsTitle', 'Enter your credentials')}</h2>

      {setupGuide && (
        <div className="mb-4 rounded-lg border border-primary/20 bg-primary/5 px-3 py-2">
          <div className="flex items-center gap-2 mb-1.5">
            <CheckCircle className="size-3.5 text-primary shrink-0" aria-hidden="true" />
            <p className="text-xs font-semibold text-foreground">
              {copy(setupGuide.titleKey, setupGuide.titleFallback)}
            </p>
          </div>
          <ol className="ml-5 list-decimal space-y-1">
            {setupGuide.steps.map(guideStep => (
              <li key={guideStep[0]} className="text-[10px] text-muted-foreground leading-relaxed">{copy(guideStep[0], guideStep[1])}</li>
            ))}
          </ol>
        </div>
      )}

      <div className="space-y-3">
        <div className="space-y-1.5">
          <Label htmlFor="mail-email" className="text-xs">{copy('account.emailAddress', 'Email address')}</Label>
          <Input
            id="mail-email"
            type="email"
            value={state.email}
            onChange={(event) => actions.onEmailChange(event.target.value)}
            onBlur={actions.onEmailBlur}
            placeholder={copy('onboarding.emailPlaceholder', 'you@example.com')}
            disabled={state.busy}
            className="h-9 bg-background text-xs"
          />
        </div>

        <div className="space-y-1.5">
          <Label htmlFor="mail-display-name" className="text-xs">{copy('account.displayName', 'Display name')}</Label>
          <Input
            id="mail-display-name"
            type="text"
            value={state.displayName}
            onChange={(event) => actions.onDisplayNameChange(event.target.value)}
            placeholder={copy('onboarding.displayNamePlaceholder', 'Your Name')}
            disabled={state.busy}
            className="h-9 bg-background text-xs"
          />
        </div>

        <div className="space-y-1.5">
          <Label htmlFor="mail-password" className="text-xs">{copy('account.password', 'Password')}</Label>
          <Input
            id="mail-password"
            type="password"
            value={state.password}
            onChange={(event) => actions.onPasswordChange(event.target.value)}
            placeholder={copy('onboarding.passwordPlaceholder', 'App password or account password')}
            disabled={state.busy}
            className="h-9 bg-background text-xs tracking-wide"
          />
        </div>

        <button
          type="button"
          onClick={actions.onToggleAdvanced}
          className="flex items-center gap-1 text-xs text-muted-foreground hover:text-foreground transition-colors mt-2"
        >
          {state.showAdvanced ? <ChevronDown className="size-3.5" /> : <ChevronRight className="size-3.5" />}
          {copy('onboarding.advancedSettings', 'Advanced server settings')}
        </button>

        {state.showAdvanced && <MailServerAdvancedSettings state={state} actions={actions} copy={copy} />}

        {state.error && (
          <Alert variant="destructive" className="mt-2.5">
            <AlertTriangle className="size-3.5" />
            <AlertDescription className="text-[10px]">{state.error}</AlertDescription>
          </Alert>
        )}

        {state.testSuccess && (
          <Alert variant="success" className="mt-2.5">
            <CheckCircle className="size-3.5" />
            <AlertDescription className="text-[10px]">{copy('account.connectionSuccess', 'Connection successful!')}</AlertDescription>
          </Alert>
        )}

        <div className="flex items-center gap-3 mt-4">
          <Button
            type="button"
            variant="outline"
            size="sm"
            onClick={actions.onTestConnection}
            disabled={state.busy || !state.email || !state.password || !state.imapHost}
          >
            {state.actionFlags.testing ? (
              <>
                <Loader2 className="mr-1.5 size-3 animate-spin" />
                {copy('onboarding.testingConnection', 'Testing...')}
              </>
            ) : (
              copy('account.testConnection', 'Test Connection')
            )}
          </Button>
          <div className="flex-1" />
          <Button
            type="button"
            variant="default"
            size="sm"
            onClick={actions.onAddAccount}
            disabled={state.busy || !state.email || !state.password || !state.imapHost || !state.smtpHost}
          >
            {state.actionFlags.adding ? (
              <>
                <Loader2 className="mr-1.5 size-3 animate-spin" />
                {copy('onboarding.addingAccount', 'Adding...')}
              </>
            ) : (
              copy('account.addAccount', 'Add Account')
            )}
          </Button>
        </div>
      </div>
    </div>
  );
}

type MailServerAdvancedSettingsProps = {
  readonly state: CredentialsStepState;
  readonly actions: CredentialsStepActions;
  readonly copy: MailOnboardingStringLookup;
};

function MailServerAdvancedSettings({state, actions, copy}: MailServerAdvancedSettingsProps) {
  return (
    <div className="space-y-4 mt-3 border-l border-border pl-3">
      <div>
        <h3 className="text-[10px] font-semibold uppercase tracking-wider text-muted-foreground mb-1.5">{copy('account.imapIncoming', 'IMAP (Incoming)')}</h3>
        <div className="grid grid-cols-3 gap-2">
          <div className="col-span-3 space-y-1">
            <Label htmlFor="imap-host" className="text-[10px]">{copy('account.host', 'Host')}</Label>
            <Input id="imap-host" className="h-8 bg-background text-xs" value={state.imapHost} onChange={(event) => actions.onImapHostChange(event.target.value)} placeholder={copy('onboarding.imapHostPlaceholder', 'imap.example.com')} disabled={state.busy} />
          </div>
          <div className="space-y-1">
            <Label htmlFor="imap-port" className="text-[10px]">{copy('account.port', 'Port')}</Label>
            <Input id="imap-port" type="number" className="h-8 bg-background text-xs" value={state.imapPortText} onChange={(event) => actions.onImapPortTextChange(event.target.value)} disabled={state.busy} />
          </div>
          <div className="col-span-2 space-y-1">
            <Label htmlFor="imap-encryption" className="text-[10px]">{copy('account.encryption', 'Encryption')}</Label>
            <Select value={state.imapEncryption} onValueChange={(value) => { if (isMailServerEncryption(value)) actions.onImapEncryptionChange(value); }} disabled={state.busy}>
              <SelectTrigger id="imap-encryption" className="h-8 bg-background text-xs">
                <SelectValue placeholder="Select" />
              </SelectTrigger>
              <SelectContent>
                <SelectItem value="Tls">TLS</SelectItem>
                <SelectItem value="StartTls">STARTTLS</SelectItem>
                <SelectItem value="None">None</SelectItem>
              </SelectContent>
            </Select>
          </div>
        </div>
      </div>

      <div>
        <h3 className="text-[10px] font-semibold uppercase tracking-wider text-muted-foreground mb-1.5">{copy('account.smtpOutgoing', 'SMTP (Outgoing)')}</h3>
        <div className="grid grid-cols-3 gap-2">
          <div className="col-span-3 space-y-1">
            <Label htmlFor="smtp-host" className="text-[10px]">{copy('account.host', 'Host')}</Label>
            <Input id="smtp-host" className="h-8 bg-background text-xs" value={state.smtpHost} onChange={(event) => actions.onSmtpHostChange(event.target.value)} placeholder={copy('onboarding.smtpHostPlaceholder', 'smtp.example.com')} disabled={state.busy} />
          </div>
          <div className="space-y-1">
            <Label htmlFor="smtp-port" className="text-[10px]">{copy('account.port', 'Port')}</Label>
            <Input id="smtp-port" type="number" className="h-8 bg-background text-xs" value={state.smtpPortText} onChange={(event) => actions.onSmtpPortTextChange(event.target.value)} disabled={state.busy} />
          </div>
          <div className="col-span-2 space-y-1">
            <Label htmlFor="smtp-encryption" className="text-[10px]">{copy('account.encryption', 'Encryption')}</Label>
            <Select value={state.smtpEncryption} onValueChange={(value) => { if (isMailServerEncryption(value)) actions.onSmtpEncryptionChange(value); }} disabled={state.busy}>
              <SelectTrigger id="smtp-encryption" className="h-8 bg-background text-xs">
                <SelectValue placeholder="Select" />
              </SelectTrigger>
              <SelectContent>
                <SelectItem value="Tls">TLS</SelectItem>
                <SelectItem value="StartTls">STARTTLS</SelectItem>
                <SelectItem value="None">None</SelectItem>
              </SelectContent>
            </Select>
          </div>
        </div>
      </div>
    </div>
  );
}
