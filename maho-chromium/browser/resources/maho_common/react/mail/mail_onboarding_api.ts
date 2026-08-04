// Copyright 2026 Maho Browser. All rights reserved.

export interface MailHandlerResult { readonly ok: boolean; readonly resultJson: string; }
export interface MailAccount { readonly id: string; readonly email: string; }

export interface MailBeginOAuthResult {
  readonly ok: boolean;
  readonly state: string;
  readonly errorJson: string;
}

export interface MailOnboardingApi {
  // Required on both surfaces
  listAccounts(): Promise<MailHandlerResult>;
  addAccount(requestJson: string): Promise<MailHandlerResult>;
  testConnection(paramsJson: string): Promise<MailHandlerResult>;
  deleteAccount(accountId: string): Promise<MailHandlerResult>;
  beginOAuth(provider: string): Promise<MailBeginOAuthResult>;
  // Optional — welcome only (translation step); maho_mail omits → step skipped
  getAiProviderConfigured?: () => Promise<boolean>;
  setTranslationProvider?: (provider: string) => Promise<boolean>;
  // Completion — welcome: store.nextPage(); maho_mail: close/hide onboarding
  onComplete: () => void;

  subscribeAccountsChanged(cb: () => void): () => void;
  cancelOAuth(state: string): Promise<boolean>;
}

export type MailOnboardingSurface = 'welcome' | 'mail';
export type MailOnboardingVisualFixture =
  'zero-account' | 'one-account' | 'helper' | 'oauth' | 'translation';
export type MailOnboardingStepKey = 'provider' | 'connect' | 'translation' | 'done';
export type MailOnboardingExistingAccountsStep = 'provider' | 'success';

export interface MailOnboardingConfig {
  readonly surface: MailOnboardingSurface;
  readonly steps: readonly MailOnboardingStepKey[];
  readonly initialExistingAccountsStep: MailOnboardingExistingAccountsStep;
}

export const WELCOME_MAIL_ONBOARDING_CONFIG = {
  surface: 'welcome',
  steps: ['provider', 'connect', 'translation', 'done'],
  initialExistingAccountsStep: 'provider',
} as const satisfies MailOnboardingConfig;

export const MAIL_APP_ONBOARDING_CONFIG = {
  surface: 'mail',
  steps: ['provider', 'connect', 'done'],
  initialExistingAccountsStep: 'success',
} as const satisfies MailOnboardingConfig;

export type MailOnboardingStringKey =
  `${'account' | 'common' | 'onboarding'}.${string}`;

export type MailOnboardingStringValues = Readonly<Record<string, string | number>>;

export type MailOnboardingStringLookup = (
  key: MailOnboardingStringKey,
  fallback: string,
  values?: MailOnboardingStringValues,
) => string;

export type ConfirmDeleteAccount = (account: MailAccount) => boolean | Promise<boolean>;

export interface MailOnboardingProps {
  readonly api: MailOnboardingApi;
  readonly accounts: readonly MailAccount[];
  readonly onAccountsChange: (accounts: readonly MailAccount[]) => void;
  readonly config: MailOnboardingConfig;
  readonly t?: MailOnboardingStringLookup;
  readonly confirmDeleteAccount?: ConfirmDeleteAccount;
  readonly visualFixture?: MailOnboardingVisualFixture;
}

export type MailOnboardingAction =
  | 'oauth'
  | 'testing'
  | 'adding'
  | 'translation'
  | 'deleting';

export interface MailOnboardingActionFlags {
  readonly testing: boolean;
  readonly adding: boolean;
}

export type MailServerPortParseResult =
  | { readonly ok: true; readonly value: number }
  | { readonly ok: false; readonly reason: 'empty' | 'integer' | 'range' };

export type DeleteMailAccountResult =
  | { readonly kind: 'cancelled' }
  | { readonly kind: 'deleted'; readonly accounts: readonly MailAccount[] }
  | { readonly kind: 'failed'; readonly accounts: readonly MailAccount[]; readonly errorMessage: string };

export interface DeleteMailAccountRequest {
  readonly account: MailAccount;
  readonly accounts: readonly MailAccount[];
  readonly confirmDelete: ConfirmDeleteAccount;
  readonly deleteAccount: (accountId: string) => Promise<MailHandlerResult>;
  readonly failureMessage: string;
}

const HELPER_UNAVAILABLE_PATTERNS = [
  'mail helper unavailable',
  'mail service unavailable',
] as const;

export const defaultMailOnboardingStringLookup: MailOnboardingStringLookup = (
  _key,
  fallback,
  values,
) => formatMailOnboardingString(fallback, values);

export function formatMailOnboardingString(
  template: string,
  values?: MailOnboardingStringValues,
): string {
  if (!values) return template;
  return template.replace(/\{\{\s*([A-Za-z0-9_]+)\s*\}\}/g, (match, key: string) => {
    const value = values[key];
    return value === undefined ? match : String(value);
  });
}

export function parseMailAccounts(resultJson: string): readonly MailAccount[] | null {
  let parsed: unknown;
  try {
    parsed = JSON.parse(resultJson);
  } catch (error) {
    if (error instanceof SyntaxError) return null;
    throw error;
  }
  if (!Array.isArray(parsed)) return null;

  const accounts: MailAccount[] = [];
  for (const value of parsed) {
    if (typeof value !== 'object' || value === null ||
        !('id' in value) || typeof value.id !== 'string' ||
        !('email' in value) || typeof value.email !== 'string') {
      return null;
    }
    const id = value.id.trim();
    const email = value.email.trim();
    if (id.length === 0 || email.length === 0) return null;
    accounts.push({id, email});
  }
  return accounts;
}

export function isMailHelperUnavailableError(errorText: string): boolean {
  const normalized = normalizeErrorText(errorText).toLowerCase();
  return HELPER_UNAVAILABLE_PATTERNS.some(pattern => normalized.includes(pattern));
}

export function toSafeMailErrorMessage(_errorText: string, fallback: string): string {
  return fallback;
}

export function parseMailServerPort(value: string): MailServerPortParseResult {
  const trimmed = value.trim();
  if (trimmed.length === 0) return {ok: false, reason: 'empty'};
  if (!/^\d+$/.test(trimmed)) return {ok: false, reason: 'integer'};
  const port = Number(trimmed);
  if (!Number.isInteger(port)) return {ok: false, reason: 'integer'};
  if (port < 1 || port > 65535) return {ok: false, reason: 'range'};
  return {ok: true, value: port};
}

export function getMailOnboardingActionFlags(
  action: MailOnboardingAction | null,
): MailOnboardingActionFlags {
  return {
    testing: action === 'testing',
    adding: action === 'adding',
  };
}

export async function deleteMailAccountWithConfirmation(
  request: DeleteMailAccountRequest,
): Promise<DeleteMailAccountResult> {
  const confirmed = await request.confirmDelete(request.account);
  if (!confirmed) return {kind: 'cancelled'};

  const result = await request.deleteAccount(request.account.id);
  if (!result.ok) {
    return {
      kind: 'failed',
      accounts: request.accounts,
      errorMessage: toSafeMailErrorMessage(result.resultJson, request.failureMessage),
    };
  }

  return {
    kind: 'deleted',
    accounts: request.accounts.filter(account => account.id !== request.account.id),
  };
}

function normalizeErrorText(errorText: string): string {
  let parsed: unknown;
  try {
    parsed = JSON.parse(errorText);
  } catch (error) {
    if (error instanceof SyntaxError) return errorText;
    throw error;
  }

  const structured = collectStructuredErrorText(parsed);
  return structured.length > 0 ? structured : errorText;
}

function collectStructuredErrorText(value: unknown): string {
  if (typeof value === 'string') return value;
  if (typeof value !== 'object' || value === null) return '';

  const parts: string[] = [];
  if ('error' in value && typeof value.error === 'string') parts.push(value.error);
  if ('message' in value && typeof value.message === 'string') parts.push(value.message);
  if ('detail' in value && typeof value.detail === 'string') parts.push(value.detail);
  if ('resultJson' in value && typeof value.resultJson === 'string') parts.push(value.resultJson);
  return parts.join(' ');
}
