// Copyright 2026 Maho Browser. All rights reserved.

/**
 * Test-only stand-in for `../mojo.js`. The real module re-exports the generated
 * Mojo bindings, which are unavailable outside the browser build. Enum values
 * are mirrored verbatim from `maho_settings.mojom-webui.js.d.ts`; only the
 * shapes the password panes need are declared.
 */

type StubListener = (...args: never[]) => void;

class StubCallbackRoute {
  private readonly listeners = new Map<number, StubListener>();
  private nextId = 1;

  addListener(listener: StubListener): number {
    const id = this.nextId++;
    this.listeners.set(id, listener);
    return id;
  }

  removeListener(id: number): boolean {
    return this.listeners.delete(id);
  }
}

// `bindMojoPageHandler` calls `new RouterClass()` / `new HandlerClass()` and
// then `$.bindNewPipeAndPassRemote()` / `$.bindNewPipeAndPassReceiver()`, so the
// stub must supply real constructors with a `$` handle or the store cannot be
// built under vitest.
export class PageCallbackRouter {
  readonly $ = {
    bindNewPipeAndPassRemote: () => ({}),
    close: () => {},
  };

  readonly settingsChanged = new StubCallbackRoute();
  readonly accountStatusChanged = new StubCallbackRoute();
  readonly onBrowserUpdateStateChanged = new StubCallbackRoute();

  removeListener(_id: number): boolean {
    return true;
  }
}

export class PageHandlerRemote {
  readonly $ = {
    bindNewPipeAndPassReceiver: () => ({}),
    close: () => {},
  };
}

export const PageHandlerFactory = {
  getRemote: () => ({
    createPageHandler: (_router: unknown, _handler: unknown) => {},
  }),
};

export enum SettingScope {
  kProfile = 0,
  kDevice = 1,
  kProcess = 2,
  kAccount = 3,
  kCoreGlobal = 4,
}

export enum MailNotificationPermission {
  kNotDetermined = 0,
  kPromptPending = 1,
  kDenied = 2,
  kGranted = 3,
  kUnsupported = 4,
}

export enum MailBehaviorUpdateStatus {
  kApplied = 0,
  kConflict = 1,
  kRejected = 2,
  kInvalid = 3,
}

export enum ProfileLifecycleState {
  kLoading = 0,
  kReady = 1,
  kDeleting = 2,
  kUnavailable = 3,
}

export enum ProfileTargetErrorCode {
  kNone = 0,
  kInvalidProfileId = 1,
  kProfileNotFound = 2,
  kProfileDeleting = 3,
  kProfileUnavailable = 4,
  kStaleContext = 5,
  kStaleProfileRevision = 6,
  kUnsupportedScope = 7,
  kInvalidArgument = 8,
  kInternal = 9,
}

export enum ProfileObservableLifecycleState {
  kProvisioning = 0,
  kReady = 1,
  kDeleting = 2,
  kRepairRequired = 3,
}

export enum DeletedProfileHistoryAvailability {
  kUnavailable = 0,
}

export enum VaultLockState {
  kUninitialized = 0,
  kLocked = 1,
  kUnlocked = 2,
  kAutoLocked = 3,
}

export enum VaultPreflightState {
  kUnavailable = 0,
  kHealthy = 1,
  kLocked = 2,
  kUnrecoverableKey = 3,
  kStructuralCorruption = 4,
  kPlaintextResidue = 5,
}

export enum VaultAgentPolicy {
  kDeny = 0,
  kAskEveryUse = 1,
  kAllowForTask = 2,
  kWhileUnlocked = 3,
  kAlwaysAllow = 4,
}

export enum VaultItemKind {
  kLogin = 0,
  kTotp = 1,
  kPasskey = 2,
  kSecureItem = 3,
  kUnknown = 4,
}

export enum PasswordProviderKind {
  kMahoNative = 0,
  kBitwarden = 1,
  kOnePassword = 2,
  kDisabled = 3,
}

export enum SecretAction {
  kCopy = 0,
  kReveal = 1,
}

export enum PasswordImportSourceFormat {
  kOnePasswordCsv = 0,
  kOnePasswordPux = 1,
  kBitwardenIndividualCsv = 2,
  kBitwardenOrganizationCsv = 3,
  kBitwardenJson = 4,
  kApplePasswordsCsv = 5,
  kKeePassXcCsv = 6,
  kKeePassClassicCsv = 7,
}

export enum ContentBlockingMode {
  kNative = 0,
  kExtension = 1,
  kDisabled = 2,
  kUnknown = 3,
}

export enum BrowserUpdateState {
  kIdle = 0,
  kChecking = 1,
  kUpdateAvailable = 2,
  kDownloading = 3,
  kVerifying = 4,
  kReadyToInstall = 5,
  kUpToDate = 6,
  kError = 7,
}

export enum AIConnectionState {
  kDisconnected = 0,
  kConnected = 1,
  kNeedsAuth = 2,
  kOffline = 3,
  kError = 4,
}

export enum AIModelCapability {
  kUnknown = 0,
  kSupported = 1,
  kUnsupported = 2,
}

export interface AISettings {
  hasApiKey: boolean;
  provider: string;
  baseUrl: string;
  model: string;
  approvalPolicy: string;
  sessionPersistenceEnabled: boolean;
  mailReadAllowed: boolean;
  hasByokOpenai: boolean;
  hasByokAnthropic: boolean;
  hasRelaySession: boolean;
  relayTier: string;
  relaySubscriptionStatus: string;
  creditBalanceUsd: number;
  tierCeilingUsd: number;
}

export interface VaultItem {
  id: string;
  revision: bigint;
  provider: PasswordProviderKind;
  itemKind: VaultItemKind;
  title: string;
  origins: string[];
  usernameHint: string;
  createdAt: string;
  updatedAt: string;
  lastUsedAt: string | null;
  hasTotp: boolean;
  hasPasskey: boolean;
  favorite: boolean;
  trashedAt: string | null;
  hasNotes: boolean;
}

export interface VaultTotpCodeResult {
  success: boolean;
  code: string | null;
  secondsRemaining: number;
  period: number;
  errorCode: string | null;
}

export interface PasswordGeneratorOptions {
  mode?: string;
  length?: number;
  includeLowercase?: boolean;
  includeUppercase?: boolean;
  includeDigits?: boolean;
  includeSymbols?: boolean;
  useLowercase?: boolean;
  useUppercase?: boolean;
  useDigits?: boolean;
  useSymbols?: boolean;
  avoidAmbiguous?: boolean;
  passphraseMode?: boolean;
  wordCount?: number;
  separator?: string;
  capitalize?: boolean;
  includeNumber?: boolean;
}

export interface GeneratedPasswordResult {
  success: boolean;
  password: string | null;
  strengthScore: number;
  entropyBits: number;
}

export interface VaultHealthReport {
  success: boolean;
  weakItemIds?: string[];
  weak?: string[];
  reusedGroups?: string[][];
  reused?: string[][];
  totalLogins: number;
  errorCode: string | null;
}

export interface VaultStatus {
  lockState: VaultLockState;
  selectedProvider: PasswordProviderKind;
  effectiveProvider: PasswordProviderKind;
  agentPolicyDefault: VaultAgentPolicy;
  itemCount: bigint;
  autoLockMinutes: number;
  failedUnlockCount: number;
  retryAtTimestamp: string | null;
}

export interface VaultOperationResult {
  success: boolean;
  errorCode: string | null;
  errorMessage: string | null;
  status: VaultStatus | null;
  item: VaultItem | null;
}
