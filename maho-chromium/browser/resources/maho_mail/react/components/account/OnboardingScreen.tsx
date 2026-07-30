import { Mail } from 'lucide-react';
import { useTranslation } from 'react-i18next';
import { AccountSetupFlow } from './setup/AccountSetupFlow';

interface OnboardingScreenProps {
  onAccountAdded: () => void;
}

export function OnboardingScreen({ onAccountAdded }: OnboardingScreenProps) {
  const { t } = useTranslation();

  return (
    <main className="relative flex min-h-screen bg-background text-foreground">
      <div className="absolute inset-x-0 top-0 h-8 shrink-0" data-tauri-drag-region />
      <div className="flex w-full justify-center overflow-y-auto px-4 pb-6 pt-12 sm:px-6 sm:pb-8 sm:pt-14">
        <div className="flex w-full max-w-md flex-col justify-center gap-6">
          <div className="text-center">
            <div className="mx-auto mb-3 flex h-12 w-12 items-center justify-center rounded-full bg-card">
            <Mail size={24} className="text-muted-foreground" />
            </div>
            <h1 className="text-xl font-semibold text-foreground">{t('onboarding.welcome')}</h1>
            <p className="mt-1 text-sm text-muted-foreground">{t('onboarding.subtitle')}</p>
          </div>
          <AccountSetupFlow onComplete={() => onAccountAdded()} />
        </div>
      </div>
    </main>
  );
}
