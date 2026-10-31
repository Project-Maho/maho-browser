// Copyright 2026 Maho Browser. All rights reserved.

import type {
  MailAccount,
  MailBeginOAuthResult,
  MailOnboardingApi,
} from './mail_onboarding_api';
import {
  isMailHelperUnavailableError,
  parseMailAccounts,
  toSafeMailErrorMessage,
} from './mail_onboarding_api';

const DEFAULT_OAUTH_ERROR = 'OAuth is unavailable. Try IMAP instead.';
const INVALID_OAUTH_STATE_ERROR = 'The mail service returned an invalid OAuth state.';
const AUTH_STATUS_ERROR = 'Unable to check authorization status.';
const READINESS_RETRY_DELAYS_MS = [250, 500, 1000] as const;

export type OAuthBeginResult =
  | { readonly ok: true; readonly state: string }
  | { readonly ok: false; readonly errorMessage: string; readonly cancelled?: boolean };

export interface LifecycleDelegate {
  onAccountsLoaded(accounts: readonly MailAccount[]): void;
  onAccountsChanged(accounts: readonly MailAccount[]): void;
  onOAuthComplete(): void;
  onOAuthFailed(errorMsg: string): void;
  onReadinessChanged(status: 'preparing' | 'ready' | 'unavailable'): void;
}

export class MailOnboardingLifecycle {
  private api: MailOnboardingApi;
  private delegate: LifecycleDelegate;
  private initialLoaded = false;
  private unsubscribe: (() => void) | null = null;
  private activeOAuthState: string | null = null;
  private oauthGeneration = 0;
  private destroyed = false;
  private oauthAccountIds = new Set<string>();
  private deliveredInitialAccounts = false;
  private readinessRetryIndex = 0;
  private readinessRetryTimer: ReturnType<typeof setTimeout> | null = null;

  constructor(api: MailOnboardingApi, delegate: LifecycleDelegate) {
    this.api = api;
    this.delegate = delegate;
  }

  public setOauthAccountIds(ids: ReadonlySet<string>): void {
    this.oauthAccountIds = new Set(ids);
  }

  public getActiveOAuthState(): string | null {
    return this.activeOAuthState;
  }

  public setOauthStarted(state: string): void {
    this.activeOAuthState = state;
  }

  public async beginOAuth(provider: string): Promise<OAuthBeginResult> {
    const generation = ++this.oauthGeneration;
    if (this.destroyed) return {ok: false, cancelled: true, errorMessage: DEFAULT_OAUTH_ERROR};
    const result = await this.api.beginOAuth(provider);
    if (this.destroyed || generation !== this.oauthGeneration) {
      const state = result.ok ? normalizedOAuthState(result) : null;
      if (state) await this.api.cancelOAuth(state);
      return {ok: false, cancelled: true, errorMessage: DEFAULT_OAUTH_ERROR};
    }
    if (!result.ok) {
      if (isMailHelperUnavailableError(result.errorJson)) {
        this.delegate.onReadinessChanged('preparing');
      }
      return {ok: false, errorMessage: oauthErrorMessage(result)};
    }

    const state = normalizedOAuthState(result);
    if (state === null) {
      return {ok: false, errorMessage: INVALID_OAUTH_STATE_ERROR};
    }

    this.setOauthStarted(state);
    return {ok: true, state};
  }

  public start(): void {
    this.destroyed = false;
    this.loadInitialAccounts();
    this.setupSubscription();
  }

  private async loadInitialAccounts(): Promise<void> {
    if (this.initialLoaded) return;
    this.initialLoaded = true;
    await this.fetchAndReconcile();
  }

  private setupSubscription(): void {
    if (this.unsubscribe) return;
    this.unsubscribe = this.api.subscribeAccountsChanged(async () => {
      await this.fetchAndReconcile();
    });
  }

  public async onVisibilityFocus(): Promise<void> {
    await this.fetchAndReconcile();
  }

  private async fetchAndReconcile(): Promise<void> {
    const result = await this.api.listAccounts();
    if (!result.ok) {
      if (isMailHelperUnavailableError(result.resultJson)) {
        const retryScheduled = this.scheduleReadinessRetry();
        this.delegate.onReadinessChanged(
          retryScheduled ? 'preparing' : 'unavailable',
        );
        return;
      }
      if (this.activeOAuthState) {
        this.delegate.onOAuthFailed(
          toSafeMailErrorMessage(result.resultJson, AUTH_STATUS_ERROR),
        );
      }
      return;
    }
    this.clearReadinessRetry();
    this.delegate.onReadinessChanged('ready');
    const parsed = parseMailAccounts(result.resultJson);
    if (parsed) {
      this.publishAccounts(parsed);

      if (this.activeOAuthState) {
        const connected = parsed.some(
          account => !this.oauthAccountIds.has(account.id)
        );
        if (connected) {
          this.activeOAuthState = null;
          this.delegate.onOAuthComplete();
        }
      }
    }
  }

  public cancelActiveOAuth(): void {
    this.oauthGeneration += 1;
    if (this.activeOAuthState) {
      const state = this.activeOAuthState;
      this.activeOAuthState = null;
      void this.api.cancelOAuth(state);
    }
  }

  public destroy(): void {
    this.destroyed = true;
    this.clearReadinessRetry();
    if (this.unsubscribe) {
      this.unsubscribe();
      this.unsubscribe = null;
    }
    this.cancelActiveOAuth();
  }

  private publishAccounts(accounts: readonly MailAccount[]): void {
    if (!this.deliveredInitialAccounts) {
      this.deliveredInitialAccounts = true;
      this.delegate.onAccountsLoaded(accounts);
      return;
    }
    this.delegate.onAccountsChanged(accounts);
  }

  private scheduleReadinessRetry(): boolean {
    if (this.readinessRetryTimer !== null) return true;
    const delay = READINESS_RETRY_DELAYS_MS[this.readinessRetryIndex];
    if (delay === undefined) return false;
    this.readinessRetryIndex += 1;
    this.readinessRetryTimer = setTimeout(() => {
      this.readinessRetryTimer = null;
      void this.fetchAndReconcile();
    }, delay);
    return true;
  }

  private clearReadinessRetry(): void {
    if (this.readinessRetryTimer !== null) {
      clearTimeout(this.readinessRetryTimer);
      this.readinessRetryTimer = null;
    }
    this.readinessRetryIndex = 0;
  }
}

function normalizedOAuthState(result: MailBeginOAuthResult): string | null {
  if (typeof result.state !== 'string') {
    return null;
  }
  const state = result.state.trim();
  return state.length > 0 ? state : null;
}

function oauthErrorMessage(result: MailBeginOAuthResult): string {
  if (typeof result.errorJson !== 'string') {
    return DEFAULT_OAUTH_ERROR;
  }
  const message = result.errorJson.trim();
  if (message.length === 0) return DEFAULT_OAUTH_ERROR;
  return toSafeMailErrorMessage(message, DEFAULT_OAUTH_ERROR);
}
