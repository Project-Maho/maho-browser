import { useState } from 'react';
import { ChevronDown, ChevronUp } from 'lucide-react';
import { useTranslation } from 'react-i18next';
import type { ProviderId } from './AccountSetupFlow';
import { GmailIcon, OutlookIcon, YahooIcon, ICloudIcon, FastmailIcon, ZohoIcon, AolIcon, GmxIcon, YandexIcon, NaverIcon, OtherProviderIcon } from './ProviderIcons';
import { Button } from '../../ui';

interface ProviderStepProps {
  onSelect: (provider: ProviderId) => void;
}

const PROVIDERS: { id: ProviderId; name: string; description: string; icon: React.ReactNode }[] = [
  { id: 'gmail', name: 'Gmail', description: 'Sign in with Google', icon: <GmailIcon size={28} /> },
  { id: 'outlook', name: 'Outlook', description: 'Sign in with Microsoft', icon: <OutlookIcon size={28} /> },
  { id: 'yahoo', name: 'Yahoo Mail', description: 'App password required', icon: <YahooIcon size={28} /> },
  { id: 'icloud', name: 'iCloud Mail', description: 'App password required', icon: <ICloudIcon size={28} /> },
  { id: 'fastmail', name: 'Fastmail', description: 'App password required', icon: <FastmailIcon size={28} /> },
  { id: 'zoho', name: 'Zoho Mail', description: 'App password required', icon: <ZohoIcon size={28} /> },
  { id: 'aol', name: 'AOL', description: 'App password required', icon: <AolIcon size={28} /> },
  { id: 'gmx', name: 'GMX', description: 'Free email service', icon: <GmxIcon size={28} /> },
  { id: 'yandex', name: 'Yandex', description: 'App password required', icon: <YandexIcon size={28} /> },
  { id: 'naver', name: 'Naver', description: 'Enable IMAP and use an app password', icon: <NaverIcon size={28} /> },
  { id: 'other', name: 'Other', description: 'IMAP/SMTP manual setup', icon: <OtherProviderIcon size={28} /> },
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
              <div className="text-sm font-medium text-foreground">{provider.name}</div>
              <div className="text-xs text-muted-foreground">{provider.description}</div>
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
            More providers
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
            Show fewer
          </Button>
        )}
      </div>
    </div>
  );
}
