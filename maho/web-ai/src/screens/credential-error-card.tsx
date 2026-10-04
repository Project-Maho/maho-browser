import { type JSX } from 'preact';
import { Icon } from '../ui/icon';
import { getCredentialErrorPresentation, type CredentialErrorCode } from '../screens/credential-error';

interface CredentialErrorCardProps {
  readonly code: CredentialErrorCode;
}

function openSettings(target: 'ai-settings' | 'account'): void {
  if (typeof window === 'undefined') {
    return;
  }
  window.location.hash = target === 'account' ? '#onboarding?step=auth' : '#byok';
}

export function CredentialErrorCard({ code }: CredentialErrorCardProps): JSX.Element {
  const presentation = getCredentialErrorPresentation(code);
  return (
    <article class="chat-credential-card" role="alert" data-testid="credential-error-card">
      <div class="chat-credential-card-icon" aria-hidden>
        <Icon name="key-round" size={16} />
      </div>
      <div class="chat-credential-card-body">
        <h3 class="chat-credential-card-title">{presentation.title}</h3>
        <p class="chat-credential-card-description">{presentation.description}</p>
        <button
          type="button"
          class="chat-credential-card-action"
          data-testid="credential-error-action"
          onClick={() => openSettings(presentation.target)}
        >
          <Icon name="settings" size={14} aria-hidden />
          <span>{presentation.actionLabel}</span>
        </button>
      </div>
    </article>
  );
}
