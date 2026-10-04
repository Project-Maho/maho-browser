// Copyright 2026 Maho Browser. All rights reserved.

import {useEffect, useRef, useState} from 'react';
import type {Dispatch, MutableRefObject, SetStateAction} from 'react';

import type {
  MailAccount,
  MailOnboardingAction,
  MailOnboardingApi,
  MailOnboardingConfig,
} from './mail_onboarding_api.js';
import {MailOnboardingLifecycle} from './mail_onboarding_lifecycle.js';
import type {MailServiceStatus} from './mail_onboarding_controller_types.js';
import type {SetupStep} from './mail_onboarding_config.js';

type MailOnboardingLifecycleHookInput = {
  readonly api: MailOnboardingApi;
  readonly accounts: readonly MailAccount[];
  readonly onAccountsChange: (accounts: readonly MailAccount[]) => void;
  readonly config: MailOnboardingConfig;
  readonly hasTranslationStep: boolean;
  readonly setAction: Dispatch<SetStateAction<MailOnboardingAction | null>>;
  readonly setStep: Dispatch<SetStateAction<SetupStep>>;
  readonly setError: Dispatch<SetStateAction<string | null>>;
};

type MailOnboardingLifecycleHookResult = {
  readonly lifecycleRef: MutableRefObject<MailOnboardingLifecycle | null>;
  readonly oauthAccountIds: MutableRefObject<ReadonlySet<string>>;
  readonly mailServiceStatus: MailServiceStatus;
  readonly setMailServiceStatus: Dispatch<SetStateAction<MailServiceStatus>>;
};

export function useMailOnboardingLifecycle(input: MailOnboardingLifecycleHookInput): MailOnboardingLifecycleHookResult {
  const [mailServiceStatus, setMailServiceStatus] = useState<MailServiceStatus>('ready');
  const oauthAccountIds = useRef<ReadonlySet<string>>(new Set());
  const lifecycleRef = useRef<MailOnboardingLifecycle | null>(null);

  if (!lifecycleRef.current) {
    lifecycleRef.current = new MailOnboardingLifecycle(input.api, {
      onAccountsLoaded: (parsed) => {
        input.onAccountsChange(parsed);
        if (parsed.length > 0 && input.config.initialExistingAccountsStep === 'success') input.setStep('success');
      },
      onAccountsChanged: (parsed) => input.onAccountsChange(parsed),
      onOAuthComplete: () => {
        input.setAction(null);
        input.setStep(input.hasTranslationStep ? 'translation' : 'success');
      },
      onOAuthFailed: (errorMsg) => {
        input.setAction(null);
        input.setStep('provider');
        input.setError(errorMsg);
      },
      onReadinessChanged: (status) => setMailServiceStatus(status),
    });
  }

  useEffect(() => {
    lifecycleRef.current?.setOauthAccountIds(oauthAccountIds.current);
  }, [input.accounts]);

  useEffect(() => {
    lifecycleRef.current?.start();
    return () => lifecycleRef.current?.destroy();
  }, []);

  useEffect(() => {
    const handleVisibilityOrFocus = () => {
      if ((document.visibilityState === 'visible' || document.hasFocus()) && lifecycleRef.current) {
        void lifecycleRef.current.onVisibilityFocus();
      }
    };
    window.addEventListener('visibilitychange', handleVisibilityOrFocus);
    window.addEventListener('focus', handleVisibilityOrFocus);
    return () => {
      window.removeEventListener('visibilitychange', handleVisibilityOrFocus);
      window.removeEventListener('focus', handleVisibilityOrFocus);
    };
  }, []);

  return {lifecycleRef, oauthAccountIds, mailServiceStatus, setMailServiceStatus};
}

export function useMailOnboardingAiConfig(step: SetupStep, api: MailOnboardingApi) {
  const [checkingAiConfig, setCheckingAiConfig] = useState(true);
  const [isAiConfigured, setIsAiConfigured] = useState(false);

  useEffect(() => {
    if (step !== 'translation') return;
    if (!api.getAiProviderConfigured) {
      setIsAiConfigured(false);
      setCheckingAiConfig(false);
      return;
    }
    let active = true;
    setCheckingAiConfig(true);
    api.getAiProviderConfigured().then(configured => {
      if (!active) return;
      setIsAiConfigured(configured);
      setCheckingAiConfig(false);
    }).catch(error => {
      if (!(error instanceof Error)) throw error;
      if (!active) return;
      setIsAiConfigured(false);
      setCheckingAiConfig(false);
    });
    return () => {
      active = false;
    };
  }, [step, api]);

  return {checkingAiConfig, isAiConfigured};
}
