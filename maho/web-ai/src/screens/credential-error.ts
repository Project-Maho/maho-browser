/**
 * Typed credential-error presentation shared by the chat and agent WebViews.
 *
 * Mirrors the desktop maho_ai reference (maho-chromium/browser/resources/
 * maho_ai/views/credential_error.ts): when the native backend surfaces a typed
 * credential failure it emits a structured envelope and the UI renders an
 * actionable card ("open AI settings" / "reconnect your Maho account") instead
 * of the raw diagnostic string (e.g. "Secure storage callback is missing...").
 *
 * The envelope is produced by the maho-agent / maho-ffi / maho-jni layers:
 *   {"version":1,"kind":"credential_error","code":"secure_store_unavailable"}
 */

export type CredentialErrorCode =
  | 'provider_not_configured'
  | 'credential_unusable'
  | 'secure_store_unavailable'
  | 'credential_decrypt_failed'
  | 'managed_auth_unavailable'
  | 'unsupported_provider';

export type CredentialTarget = 'ai-settings' | 'account';

export interface CredentialErrorPresentation {
  readonly title: string;
  readonly description: string;
  readonly actionLabel: string;
  readonly target: CredentialTarget;
}

interface CredentialEnvelope {
  readonly version?: unknown;
  readonly kind?: unknown;
  readonly code?: unknown;
}

const KNOWN_CODES: ReadonlySet<CredentialErrorCode> = new Set<CredentialErrorCode>([
  'provider_not_configured',
  'credential_unusable',
  'secure_store_unavailable',
  'credential_decrypt_failed',
  'managed_auth_unavailable',
  'unsupported_provider',
]);

/**
 * Parse the typed credential envelope out of a raw error string. Returns the
 * credential code when `text` is a credential_error envelope; otherwise null so
 * callers fall back to the regular error presentation.
 */
export function parseCredentialErrorCode(text: string | null | undefined): CredentialErrorCode | null {
  const raw = (text ?? '').trim();
  if (!raw.startsWith('{') || !raw.endsWith('}')) {
    return null;
  }
  let parsed: CredentialEnvelope;
  try {
    parsed = JSON.parse(raw) as CredentialEnvelope;
  } catch {
    return null;
  }
  const keys = Object.keys(parsed);
  if (
    keys.length !== 3
    || !keys.includes('version')
    || !keys.includes('kind')
    || !keys.includes('code')
    || parsed.version !== 1
    || parsed.kind !== 'credential_error'
  ) {
    return null;
  }
  const code = parsed.code;
  if (typeof code !== 'string' || !KNOWN_CODES.has(code as CredentialErrorCode)) {
    return null;
  }
  return code as CredentialErrorCode;
}

export function getCredentialErrorPresentation(
  code: CredentialErrorCode,
): CredentialErrorPresentation {
  switch (code) {
    case 'provider_not_configured':
      return {
        title: 'Choose an AI provider',
        description: 'Select an AI provider, then try again.',
        actionLabel: 'Open AI settings',
        target: 'ai-settings',
      };
    case 'credential_unusable':
      return {
        title: 'Update your AI credential',
        description: 'Enter a valid AI credential, then try again.',
        actionLabel: 'Open AI settings',
        target: 'ai-settings',
      };
    case 'secure_store_unavailable':
      return {
        title: 'AI credentials are unavailable',
        description:
          'Your AI credential could not be read securely. Enter a valid credential, then try again.',
        actionLabel: 'Open AI settings',
        target: 'ai-settings',
      };
    case 'credential_decrypt_failed':
      return {
        title: 'Re-enter your AI credential',
        description: 'Your saved credential could not be decrypted. Re-enter it, then try again.',
        actionLabel: 'Open AI settings',
        target: 'ai-settings',
      };
    case 'managed_auth_unavailable':
      return {
        title: 'Reconnect your Maho account',
        description: 'Your Maho account connection needs attention. Sign in again, then try again.',
        actionLabel: 'Open account settings',
        target: 'account',
      };
    case 'unsupported_provider':
      return {
        title: 'Choose a supported AI provider',
        description: 'That AI provider is not supported. Select a supported provider, then try again.',
        actionLabel: 'Open AI settings',
        target: 'ai-settings',
      };
  }
}
