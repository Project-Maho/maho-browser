import { BrainCircuit, CheckCircle, Search, Shield, Sparkles } from 'lucide-react';
import { useTranslation } from 'react-i18next';
import { Button } from '../../ui';

interface SuccessStepProps {
  onDone: () => void;
}

export function SuccessStep({ onDone }: SuccessStepProps) {
  const { t } = useTranslation();

  const features = [
    {
      key: 'smart-inbox',
      title: t('onboarding.featureSmartInbox'),
      description: t('onboarding.featureSmartInboxDesc'),
      icon: BrainCircuit,
    },
    {
      key: 'ai-assistant',
      title: t('onboarding.featureAiAssistant'),
      description: t('onboarding.featureAiAssistantDesc'),
      icon: Sparkles,
    },
    {
      key: 'quick-search',
      title: t('onboarding.featureQuickSearch'),
      description: t('onboarding.featureQuickSearchDesc'),
      icon: Search,
    },
    {
      key: 'privacy',
      title: t('onboarding.featurePrivacy'),
      description: t('onboarding.featurePrivacyDesc'),
      icon: Shield,
    },
  ];

  return (
    <div className="flex flex-col items-center py-4 text-center sm:py-6">
      <CheckCircle size={48} className="mb-4 text-success" />
      <h2 className="mb-2 text-xl font-semibold text-foreground">{t('onboarding.accountAdded')}</h2>
      <p className="mb-5 max-w-sm text-sm text-muted-foreground sm:mb-6">{t('onboarding.accountAddedDescription')}</p>

      <div className="mb-6 grid w-full gap-3 md:grid-cols-2 sm:mb-7">
        {features.map((feature) => {
          const Icon = feature.icon;

          return (
            <div key={feature.key} className="rounded-xl border border-border bg-card/60 p-3.5 text-left sm:p-4">
              <div className="mb-3 flex h-10 w-10 items-center justify-center rounded-lg bg-card text-primary">
                <Icon size={18} />
              </div>
              <h3 className="text-sm font-semibold text-foreground">{feature.title}</h3>
              <p className="mt-1 text-xs leading-5 text-muted-foreground">{feature.description}</p>
            </div>
          );
        })}
      </div>

      <Button variant="primary" size="lg" onClick={onDone}>{t('onboarding.getStarted')}</Button>
    </div>
  );
}
