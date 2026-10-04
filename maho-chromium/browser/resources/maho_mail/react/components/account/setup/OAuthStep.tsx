import { useEffect, useRef, useState } from 'react';
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
  const [authenticating, setAuthenticating] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const { toast } = useToast();
  const onSuccessRef = useRef(onSuccess);
  onSuccessRef.current = onSuccess;

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
        const latest = accounts[accounts.length - 1];
        if (latest && onSuccessRef.current) {
          onSuccessRef.current(latest.id);
        }
      } catch {
        // Leave the spinner running; the user can retry or go back.
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
  }, [authenticating]);

  const providerName = provider === 'gmail' ? 'Google' : 'Microsoft';
  const heading = provider === 'gmail' ? 'Sign in with Google' : 'Sign in with Microsoft';

  async function handleSignIn(): Promise<void> {
    try {
      setAuthenticating(true);
      setError(null);
      await api.startOAuth2(provider);
      // Keep authenticating=true: the loopback listener completes onboarding
      // in the background after browser approval, and the accounts-changed
      // effect above advances the flow. Resetting here would tear down the
      // listener before the user even sees the consent screen.
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Failed to sign in';
      setError(message);
      toast('error', message);
      setAuthenticating(false);
    }
  }

  return (
    <div>
      <button
        type="button"
        onClick={onBack}
        disabled={authenticating}
        className="-ml-2 mb-6 flex h-9 items-center gap-1.5 rounded-lg px-2 text-sm text-muted-foreground transition-colors hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring disabled:opacity-50 disabled:cursor-not-allowed"
      >
        <ArrowLeft className="w-4 h-4" />
        Back
      </button>

      <h2 className="text-lg font-semibold text-foreground mb-2">{heading}</h2>

      <p className="text-sm text-muted-foreground mb-6">
        You'll be redirected to {providerName} to authorize access to your email.
      </p>

      <Button
        variant="primary"
        size="lg"
        loading={authenticating}
        onClick={handleSignIn}
      >
        Sign in with {providerName}
      </Button>

      {authenticating && (
        <p className="text-xs text-muted-foreground mt-3 text-center">
          Complete sign-in in your browser...
        </p>
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
