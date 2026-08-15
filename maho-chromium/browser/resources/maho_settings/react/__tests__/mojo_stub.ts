// Copyright 2026 Maho Browser. All rights reserved.

/**
 * Test-only stand-in for `../mojo.js`. The real module re-exports the generated
 * Mojo bindings, which are unavailable outside the browser build. Enum values
 * are mirrored verbatim from `maho_settings.mojom-webui.js.d.ts`; only the
 * shapes the password panes need are declared.
 */

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
