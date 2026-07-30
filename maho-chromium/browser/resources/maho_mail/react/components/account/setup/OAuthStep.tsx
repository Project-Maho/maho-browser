import { useState } from 'react';
import * as api from '../../../api';
import { useToast } from '../../ui/Toast';
import { Button } from '../../ui';
import { ArrowLeft, AlertTriangle } from 'lucide-react';

interface OAuthStepProps {
  readonly provider: 'gmail' | 'outlook';
  readonly onBack: () => void;
}

export function OAuthStep({ provider, onBack }: OAuthStepProps) {
  const [authenticating, setAuthenticating] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const { toast } = useToast();

  const providerName = provider === 'gmail' ? 'Google' : 'Microsoft';
  const heading = provider === 'gmail' ? 'Sign in with Google' : 'Sign in with Microsoft';

  async function handleSignIn(): Promise<void> {
    try {
      setAuthenticating(true);
      setError(null);
      await api.startOAuth2(provider);
    } catch (err) {
      const message = err instanceof Error ? err.message : 'Failed to sign in';
      setError(message);
      toast('error', message);
    } finally {
      setAuthenticating(false);
    }
  }

  return (
    <div>
      <button
        type="button"
        onClick={onBack}
        disabled={authenticating}
        className="flex items-center gap-1.5 text-sm text-muted-foreground hover:text-foreground mb-6 disabled:opacity-50 disabled:cursor-not-allowed"
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
