// Copyright 2026 Maho Browser. All rights reserved.

import type {
  ConfirmDeleteAccount,
  MailOnboardingStepKey,
  MailOnboardingStringLookup,
} from './mail_onboarding_api.js';
import type {SetupStep} from './mail_onboarding_config.js';

export function createMailOnboardingCopy(
  t: MailOnboardingStringLookup,
): MailOnboardingStringLookup {
  return (key, fallback, values) => t(key, fallback, values);
}

export function stepLabel(
  stepKey: MailOnboardingStepKey,
  copy: MailOnboardingStringLookup,
): string {
  switch (stepKey) {
    case 'provider':
      return copy('onboarding.stepProvider', 'Provider');
    case 'connect':
      return copy('onboarding.stepConnect', 'Connect');
    case 'translation':
      return copy('onboarding.stepTranslation', 'Translation');
    case 'done':
      return copy('onboarding.stepDone', 'Done');
  }
}

export function isStepperStepActive(stepKey: MailOnboardingStepKey, step: SetupStep): boolean {
  switch (stepKey) {
    case 'provider':
      return step === 'provider';
    case 'connect':
      return step === 'oauth' || step === 'credentials';
    case 'translation':
      return step === 'translation';
    case 'done':
      return step === 'success';
  }
}

export function defaultConfirmDeleteAccount(copy: MailOnboardingStringLookup): ConfirmDeleteAccount {
  return (account) => window.confirm(
    copy('onboarding.deleteConfirm', 'Delete {{email}} from Maho Mail?', {email: account.email}),
  );
}
