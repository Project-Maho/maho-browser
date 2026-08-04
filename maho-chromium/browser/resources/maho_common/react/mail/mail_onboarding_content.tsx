// Copyright 2026 Maho Browser. All rights reserved.

import React from 'react';

import type {MailOnboardingProps} from './mail_onboarding_api.js';
import type {MailOnboardingViewModel} from './mail_onboarding_controller_types.js';
import {useMailOnboardingController} from './mail_onboarding_controller.js';
import {MailOnboardingAccountsList} from './mail_onboarding_accounts_list.js';
import {MailOnboardingCredentialsStep} from './mail_onboarding_credentials_step.js';
import {MailOnboardingOAuthWaiting} from './mail_onboarding_oauth_waiting.js';
import {MailOnboardingProviderStep} from './mail_onboarding_provider_step.js';
import {MailOnboardingStepper} from './mail_onboarding_stepper.js';
import {MailOnboardingSuccessStep} from './mail_onboarding_success_step.js';
import {MailOnboardingTranslationStep} from './mail_onboarding_translation_step.js';

export function MailOnboardingContent(props: MailOnboardingProps) {
  const model = useMailOnboardingController(props);
  const visualModel = props.visualFixture
    ? applyVisualFixture(model, props.visualFixture)
    : model;
  const welcomeSurfaceMarker = props.config.surface === 'welcome' ? props.config.surface : undefined;
  return (
    <div
      className="h-full w-full overflow-y-auto"
      data-mail-onboarding-surface={welcomeSurfaceMarker}
    >
      <div className="flex min-h-full w-full flex-col justify-center gap-4 py-2">
        <MailOnboardingAccountsList
          accounts={visualModel.accounts}
          busy={visualModel.busy}
          copy={visualModel.copy}
          onDeleteAccount={visualModel.onDeleteAccount}
        />
        <MailOnboardingStepper steps={visualModel.stepperSteps} step={visualModel.step} copy={visualModel.copy} />
        {renderCurrentStep(visualModel)}
      </div>
    </div>
  );
}

function applyVisualFixture(
    model: MailOnboardingViewModel,
    fixture: NonNullable<MailOnboardingProps['visualFixture']>): MailOnboardingViewModel {
  if (fixture === 'one-account') {
    return {...model, accounts: [{id: 'visual-mail-account', email: 'visual@example.invalid'}]};
  }
  if (fixture === 'helper') {
    return {
      ...model,
      step: 'provider',
      providerState: {...model.providerState, mailServiceStatus: 'preparing'},
    };
  }
  if (fixture === 'oauth') {
    return {...model, step: 'oauth', selectedProvider: 'gmail'};
  }
  if (fixture === 'translation') {
    return {
      ...model,
      step: 'translation',
      translationState: {
        ...model.translationState,
        checkingAiConfig: false,
        isAiConfigured: true,
      },
    };
  }
  return {...model, step: 'provider', accounts: []};
}

function renderCurrentStep(model: MailOnboardingViewModel) {
  switch (model.step) {
    case 'provider':
      return (
        <MailOnboardingProviderStep
          state={model.providerState}
          actions={model.providerActions}
          copy={model.copy}
        />
      );
    case 'oauth':
      return (
        <MailOnboardingOAuthWaiting
          copy={model.copy}
          selectedProvider={model.selectedProvider}
          onCancel={model.onCancelOAuth}
        />
      );
    case 'credentials':
      return (
        <MailOnboardingCredentialsStep
          state={model.credentialsState}
          actions={model.credentialsActions}
          copy={model.copy}
        />
      );
    case 'translation':
      return (
        <MailOnboardingTranslationStep
          state={model.translationState}
          copy={model.copy}
          onSelectProvider={model.onSelectTranslationProvider}
        />
      );
    case 'success':
      return <MailOnboardingSuccessStep copy={model.copy} onComplete={model.onComplete} />;
  }
}
