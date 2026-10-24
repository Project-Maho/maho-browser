import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { listen } from '../../../events.js';
import * as api from '../../../api';
import { useToast } from '../../ui/Toast';
import { Button } from '../../ui';
import { ArrowLeft, AlertTriangle } from 'lucide-react';

interface OAuthStepProps {
  readonly provider: 'gmail' | 'outlook';
  readonly onBack: () => void;
  readonly onSuccess?: (accountId: string) => void;
}

export function OAuthStep({ provider, onBack, onSuccess }: OAuthStepProps) {
  const { t } = useTranslation();
  const [authenticating, setAuthenticating] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const { toast } = useToast();
  const onSuccessRef = useRef(onSuccess);
  onSuccessRef.current = onSuccess;
  // Accounts that existed before this sign-in started. accounts-changed also
  // fires for unrelated updates (token refresh, re-auth), so only an account
  // that is not in this set completes the step.
  const existingIdsRef = useRef<ReadonlySet<string> | null>(null);
  // State of the in-flight OAuth flow, so leaving the step can stop it.
  const oauthStateRef = useRef<string | null>(null);

  const cancelPendingOAuth = (): void => {
    const state = oauthStateRef.current;
    oauthStateRef.current = null;
    if (!state) return;
    api.cancelOAuth(state).catch((err: unknown) => {
      console.warn('[OAuth] cancel failed:', err);
    });
  };

  useEffect(() => cancelPendingOAuth, []);

  // The loopback listener completes onboarding in the background after the
  // user approves in the browser tab. Wait for the resulting
  // accounts-changed event and advance the flow with the new account.
  useEffect(() => {
    if (!authenticating) return;
    let cancelled = false;
    let unlisten: (() => void) | undefined;
    listen('accounts-changed', async () => {
      if (cancelled) return;
      try {
        const accounts = await api.listAccounts();
        if (cancelled) return;
        const existingIds = existingIdsRef.current ?? new Set<string>();
        const added = accounts.find((account) => !existingIds.has(account.id));
        if (added && onSuccessRef.current) {
          oauthStateRef.current = null;
          onSuccessRef.current(added.id);
        }
      } catch (err) {
        if (cancelled) return;
        setError(err instanceof Error ? err.message : t('onboarding.addFailed'));
        setAuthenticating(false);
      }
    }).then((fn) => {
      if (cancelled) {
        fn();
      } else {
        unlisten = fn;
      }
    });
    return () => {
      cancelled = true;
      if (unlisten) unlisten();
    };
  }, [authenticating, t]);

  const providerName = provider === 'gmail' ? 'Google' : 'Microsoft';

  async function handleSignIn(): Promise<void> {
    try {
      setAuthenticating(true);
      setError(null);
      try {
        existingIdsRef.current = new Set((await api.listAccounts()).map((account) => account.id));
      } catch {
        existingIdsRef.current = new Set();
      }
      oauthStateRef.current = await api.startOAuth2(provider);
      // Keep authenticating=true: the loopback listener completes onboarding
      // in the background after browser approval, and the accounts-changed
      // effect above advances the flow. Resetting here would tear down the
      // listener before the user even sees the consent screen.
    } catch (err) {
      const message = err instanceof Error ? err.message : t('onboarding.oauthFailed');
      setError(message);
      toast('error', message);
      setAuthenticating(false);
    }
  }

  // The user may close the consent tab; waiting must never lock the wizard.
  function handleCancel(): void {
    cancelPendingOAuth();
    setAuthenticating(false);
  }

  function handleBack(): void {
    cancelPendingOAuth();
    onBack();
  }

  return (
    <div>
      <button
        type="button"
        onClick={handleBack}
        className="-ml-2 mb-6 flex h-9 items-center gap-1.5 rounded-lg px-2 text-sm text-muted-foreground transition-colors hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring disabled:opacity-50 disabled:cursor-not-allowed"
      >
        <ArrowLeft className="w-4 h-4" />
        {t('onboarding.back')}
      </button>

      <h2 className="text-lg font-semibold text-foreground mb-2">{t('onboarding.oauthHeading', { provider: providerName })}</h2>

      <p className="text-sm text-muted-foreground mb-6">
        {t('onboarding.oauthDescription', { provider: providerName })}
      </p>

      <Button
        variant="primary"
        size="lg"
        loading={authenticating}
        onClick={handleSignIn}
      >
        {t('onboarding.oauthButton', { provider: providerName })}
      </Button>

      {authenticating && (
        <div className="mt-3 flex flex-col items-center gap-2">
          <p className="text-xs text-muted-foreground text-center">
            {t('onboarding.oauthWaitingBody', { provider: providerName })}
          </p>
          <button
            type="button"
            onClick={handleCancel}
            className="rounded-lg px-3 py-1.5 text-xs font-medium text-muted-foreground transition-colors hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
          >
            {t('common.cancel')}
          </button>
        </div>
      )}

      {error && (
        <div className="flex items-center gap-2 rounded-lg bg-destructive/10 px-3 py-2 text-sm text-destructive mt-4">
          <AlertTriangle className="w-4 h-4" />
          {error}
        </div>
      )}
    </div>
  );
}
