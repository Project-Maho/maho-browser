import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { OAuthStep } from './OAuthStep';
import { CredentialsStep } from './CredentialsStep';
import { SuccessStep } from './SuccessStep';
import { ProviderStep } from './ProviderStep';
import { TranslationSetupStep } from './TranslationSetupStep';

export type SetupStep = 'provider' | 'oauth' | 'credentials' | 'translation' | 'success';
export type OAuthProviderId = 'gmail' | 'outlook';
export type ProviderId = OAuthProviderId | 'yahoo' | 'icloud' | 'fastmail' | 'zoho' | 'aol' | 'gmx' | 'yandex' | 'naver' | 'other';

function isOAuthProvider(p: ProviderId): p is OAuthProviderId {
  return p === 'gmail' || p === 'outlook';
}

interface AccountSetupFlowProps {
  onComplete: (accountId: string) => void;
  onCancel?: () => void;
}

export function AccountSetupFlow({ onComplete }: AccountSetupFlowProps) {
  const { t } = useTranslation();
  const [step, setStep] = useState<SetupStep>('provider');
  const [selectedProvider, setSelectedProvider] = useState<ProviderId | null>(null);
  const [accountId, setAccountId] = useState<string | null>(null);

  const steps = [
    {
      key: 'provider',
      label: t('onboarding.stepProvider'),
      active: step === 'provider',
    },
    {
      key: 'connect',
      label: t('onboarding.stepConnect'),
      active: step === 'oauth' || step === 'credentials',
    },
    {
      key: 'translation',
      label: t('onboarding.stepTranslation'),
      active: step === 'translation',
    },
    {
      key: 'done',
      label: t('onboarding.stepDone'),
      active: step === 'success',
    },
  ];

  function handleProviderSelect(provider: ProviderId) {
    setSelectedProvider(provider);
    if (isOAuthProvider(provider)) {
      setStep('oauth');
    } else {
      setStep('credentials');
    }
  }

  function handleSuccess(id: string) {
    setAccountId(id);
    setStep('translation');
  }

  function handleTranslationDone() {
    setStep('success');
  }

  function handleBack() {
    setStep('provider');
    setSelectedProvider(null);
  }

  function handleDone() {
    if (accountId) onComplete(accountId);
  }

  return (
    <div className="flex w-full flex-col rounded-2xl border border-border bg-card/40 p-5 shadow-sm backdrop-blur-sm sm:p-6">
      <div className="mb-6 flex items-start sm:mb-7">
        {steps.map((stepItem, index) => {
          const isActive = stepItem.active;

          return (
            <div key={stepItem.key} className="flex min-w-0 flex-1 items-center last:flex-none">
              <div className="flex flex-col items-center gap-2">
                <div
                  className={`flex h-8 w-8 items-center justify-center rounded-full border text-sm font-semibold transition-colors ${
                    isActive
                      ? 'border-primary bg-primary text-primary-foreground'
                      : 'border-border bg-card text-muted-foreground'
                  }`}
                >
                  {index + 1}
                </div>
                <span className={`text-xs font-medium ${isActive ? 'text-primary' : 'text-muted-foreground'}`}>
                  {stepItem.label}
                </span>
              </div>

              {index < steps.length - 1 && (
                <div className="mx-3 mt-4 h-px min-w-3 flex-1 bg-border" aria-hidden="true" />
              )}
            </div>
          );
        })}
      </div>

      <div>
        {step === 'provider' && <ProviderStep onSelect={handleProviderSelect} />}
        {step === 'oauth' && selectedProvider && isOAuthProvider(selectedProvider) && (
          <OAuthStep
            provider={selectedProvider}
            onBack={handleBack}
            onSuccess={handleSuccess}
          />
        )}
        {step === 'credentials' && (
          <CredentialsStep
            providerHint={selectedProvider}
            onSuccess={handleSuccess}
            onBack={handleBack}
          />
        )}
        {step === 'translation' && (
          <TranslationSetupStep
            onComplete={handleTranslationDone}
            onSkip={handleTranslationDone}
          />
        )}
        {step === 'success' && <SuccessStep onDone={handleDone} />}
      </div>
    </div>
  );
}
