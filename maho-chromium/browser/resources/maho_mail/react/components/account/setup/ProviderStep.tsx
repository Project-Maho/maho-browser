import { useState } from 'react';
import { ChevronDown, ChevronUp } from 'lucide-react';
import { useTranslation } from 'react-i18next';
import type { ProviderId } from './AccountSetupFlow';
import { GmailIcon, OutlookIcon, YahooIcon, ICloudIcon, FastmailIcon, ZohoIcon, AolIcon, GmxIcon, YandexIcon, NaverIcon, OtherProviderIcon } from './ProviderIcons';
import { Button } from '../../ui';

interface ProviderStepProps {
  onSelect: (provider: ProviderId) => void;
}

// name/description are onboarding.* locale keys.
const PROVIDERS: { id: ProviderId; name: string; description: string; icon: React.ReactNode }[] = [
  { id: 'gmail', name: 'providerGmail', description: 'providerGmailDesc', icon: <GmailIcon size={28} /> },
  { id: 'outlook', name: 'providerOutlook', description: 'providerOutlookDesc', icon: <OutlookIcon size={28} /> },
  { id: 'yahoo', name: 'providerYahoo', description: 'providerAppPasswordDesc', icon: <YahooIcon size={28} /> },
  { id: 'icloud', name: 'providerIcloud', description: 'providerAppPasswordDesc', icon: <ICloudIcon size={28} /> },
  { id: 'fastmail', name: 'providerFastmail', description: 'providerAppPasswordDesc', icon: <FastmailIcon size={28} /> },
  { id: 'zoho', name: 'providerZoho', description: 'providerAppPasswordDesc', icon: <ZohoIcon size={28} /> },
  { id: 'aol', name: 'providerAol', description: 'providerAppPasswordDesc', icon: <AolIcon size={28} /> },
  { id: 'gmx', name: 'providerGmx', description: 'providerGmxDesc', icon: <GmxIcon size={28} /> },
  { id: 'yandex', name: 'providerYandex', description: 'providerAppPasswordDesc', icon: <YandexIcon size={28} /> },
  { id: 'naver', name: 'providerNaver', description: 'providerNaverDesc', icon: <NaverIcon size={28} /> },
  { id: 'other', name: 'providerOther', description: 'providerOtherDesc', icon: <OtherProviderIcon size={28} /> },
];

const PRIMARY_PROVIDER_IDS: ProviderId[] = ['gmail', 'outlook', 'yahoo', 'icloud', 'other'];

function isPrimaryProvider(providerId: ProviderId) {
  return PRIMARY_PROVIDER_IDS.includes(providerId);
}

export function ProviderStep({ onSelect }: ProviderStepProps) {
  const { t } = useTranslation();
  const [showAllProviders, setShowAllProviders] = useState(false);

  const visibleProviders = showAllProviders
    ? PROVIDERS
    : PROVIDERS.filter((provider) => isPrimaryProvider(provider.id));

  const hasHiddenProviders = visibleProviders.length < PROVIDERS.length;

  return (
    <div className="w-full">
      <h2 className="mb-4 text-lg font-semibold text-foreground">
        {t('onboarding.chooseProvider')}
      </h2>
      <div className="grid grid-cols-2 gap-2.5 sm:gap-3">
        {visibleProviders.map((provider) => (
          <button
            key={provider.id}
            onClick={() => onSelect(provider.id)}
            className={`flex items-center gap-3 rounded-lg border border-border p-3 text-left transition-colors hover:border-foreground/20 hover:bg-surface-hover active:scale-[0.99] focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary sm:p-4 ${
              provider.id === 'other' ? 'col-span-2' : ''
            }`}
          >
            <div className="shrink-0">{provider.icon}</div>
            <div>
              <div className="text-sm font-medium text-foreground">{t(`onboarding.${provider.name}`)}</div>
              <div className="text-xs text-muted-foreground">{t(`onboarding.${provider.description}`)}</div>
            </div>
          </button>
        ))}

        {hasHiddenProviders && (
          <Button
            type="button"
            variant="ghost"
            size="sm"
            onClick={() => setShowAllProviders(true)}
            className="col-span-2 h-auto rounded-lg border border-dashed border-border px-4 py-3 text-muted-foreground hover:bg-surface-hover hover:text-foreground"
            aria-expanded={showAllProviders}
          >
            <ChevronDown size={16} />
            {t('onboarding.moreProviders')}
          </Button>
        )}

        {showAllProviders && (
          <Button
            type="button"
            variant="ghost"
            size="sm"
            onClick={() => setShowAllProviders(false)}
            className="col-span-2 h-auto rounded-lg px-4 py-2 text-muted-foreground hover:bg-surface-hover hover:text-foreground"
            aria-expanded={showAllProviders}
          >
            <ChevronUp size={16} />
            {t('onboarding.showFewer')}
          </Button>
        )}
      </div>
    </div>
  );
}
