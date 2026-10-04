import { useCallback, useEffect, useRef, useState } from 'react';
import { listen } from '../../../events.js';
import { Cpu, Key, Loader2, SkipForward } from 'lucide-react';
import { useTranslation } from 'react-i18next';
import * as api from '../../../api';
import { SUPPORTED_LANGUAGES } from '../../../hooks/useTranslation';
import type {
  AiConfig,
  TranslationConfig,
  TranslationModelDownloadProgress,
} from '../../../types';
import { Badge, Button } from '../../ui';

interface TranslationSetupStepProps {
  onComplete: () => void;
  onSkip: () => void;
}

const STORAGE_KEY = 'maho-translation-config';

type ModelStatus = 'unknown' | 'not_downloaded' | 'downloading' | 'downloaded';
type TranslationChoice = TranslationConfig['provider'] | null;

function getDefaultTargetLang(): string {
  const localeCode = navigator.language.split('-')[0]?.toLowerCase() ?? 'en';
  return SUPPORTED_LANGUAGES.some((lang) => lang.code === localeCode) ? localeCode : 'en';
}

function buildConfig(provider: TranslationConfig['provider']): TranslationConfig {
  return {
    provider,
    defaultTargetLang: getDefaultTargetLang(),
    alwaysTranslateFrom: [],
  };
}

function loadConfig(): TranslationConfig | null {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    return raw ? JSON.parse(raw) as TranslationConfig : null;
  } catch {
    return null;
  }
}

function saveConfig(config: TranslationConfig) {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(config));
  window.dispatchEvent(new CustomEvent('maho-translation-config-changed'));
}

export function TranslationSetupStep({ onComplete, onSkip }: TranslationSetupStepProps) {
  const { t } = useTranslation();
  const [selectedProvider, setSelectedProvider] = useState<TranslationChoice>(null);
  const [checkingAiConfig, setCheckingAiConfig] = useState(true);
  const [hasAiConfig, setHasAiConfig] = useState<AiConfig | null | undefined>(undefined);
  const completedRef = useRef(false);

  const refreshConfig = useCallback(() => {
    const config = loadConfig();
    setSelectedProvider(config?.provider ?? null);
  }, []);

  const finish = useCallback((mode: 'complete' | 'skip') => {
    if (completedRef.current) {
      return;
    }

    completedRef.current = true;

    if (mode === 'skip') {
      onSkip();
      return;
    }

    onComplete();
  }, [onComplete, onSkip]);

  useEffect(() => {
    refreshConfig();

    let cancelled = false;

    api.getAiConfig().then((config) => {
      if (!cancelled) {
        setHasAiConfig(config);
      }
    }).catch(() => {
      if (!cancelled) {
        setHasAiConfig(null);
      }
    }).finally(() => {
      if (!cancelled) {
        setCheckingAiConfig(false);
      }
    });

    return () => {
      cancelled = true;
    };
  }, [refreshConfig]);

  useEffect(() => {
    const handleConfigChanged = () => {
      refreshConfig();
    };

    window.addEventListener('maho-translation-config-changed', handleConfigChanged);

    return () => {
      window.removeEventListener('maho-translation-config-changed', handleConfigChanged);
    };
  }, [refreshConfig]);

  function handleSelectByok() {
    setSelectedProvider('byok');
    saveConfig(buildConfig('byok'));
    finish('complete');
  }

  function handleSelectSkip() {
    setSelectedProvider('skip');
    saveConfig(buildConfig('skip'));
    finish('skip');
  }

  const isByokSelected = selectedProvider === 'byok';
  const isSkipSelected = selectedProvider === 'skip';
  const aiConfigMissing = !checkingAiConfig && hasAiConfig == null;

  return (
    <div className="w-full">
      <div className="mb-5 sm:mb-6">
        <h2 className="text-lg font-semibold text-foreground">
          {t('onboarding.translationStepTitle')}
        </h2>
        <p className="mt-1 text-sm text-muted-foreground">
          {t('onboarding.translationStepSubtitle')}
        </p>
      </div>

      <div className="grid gap-3">

        <button
          type="button"
          onClick={() => void handleSelectByok()}
          className={`rounded-xl border p-4 text-left transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary sm:p-5 ${
            isByokSelected
              ? 'border-primary bg-primary/5 shadow-sm'
              : 'border-border bg-card/40 hover:border-foreground/20 hover:bg-surface-hover'
          }`}
          aria-pressed={isByokSelected}
        >
          <div className="flex items-start gap-3">
            <div className="flex h-11 w-11 shrink-0 items-center justify-center rounded-xl bg-card text-primary">
              <Key size={20} />
            </div>
            <div className="min-w-0 flex-1">
              <div className="flex flex-wrap items-center gap-2">
                <span className="text-sm font-semibold text-foreground">
                  {t('onboarding.translationByokTitle')}
                </span>
                <Badge variant="secondary" className="border-transparent bg-primary/10 text-primary">
                  {t('onboarding.translationByokRecommended')}
                </Badge>
              </div>
              <p className="mt-1 text-xs leading-5 text-muted-foreground">
                {t('onboarding.translationByokSubtitle')}
              </p>
              {aiConfigMissing && (
                <p className="mt-3 text-xs text-muted-foreground">
                  {t('onboarding.translationByokNeedsConfig')}
                </p>
              )}
            </div>
          </div>
        </button>

        <button
          type="button"
          onClick={() => void handleSelectSkip()}
          className={`rounded-xl border p-4 text-left transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary sm:p-5 ${
            isSkipSelected
              ? 'border-primary bg-primary/5 shadow-sm'
              : 'border-border bg-card/40 hover:border-foreground/20 hover:bg-surface-hover'
          }`}
          aria-pressed={isSkipSelected}
        >
          <div className="flex items-start gap-3">
            <div className="flex h-11 w-11 shrink-0 items-center justify-center rounded-xl bg-card text-muted-foreground">
              <SkipForward size={20} />
            </div>
            <div className="min-w-0 flex-1">
              <div className="text-sm font-semibold text-foreground">
                {t('onboarding.translationSkipTitle')}
              </div>
              <p className="mt-1 text-xs leading-5 text-muted-foreground">
                {t('onboarding.translationSkipSubtitle')}
              </p>
            </div>
          </div>
        </button>
      </div>
    </div>
  );
}
