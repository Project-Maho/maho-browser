// Copyright 2026 Maho Browser. All rights reserved.
// Ambient type declarations for maho_settings.mojom-webui.js.
// Generated at build time; this file provides tsc with the shape of the Mojo
// bindings so the React bundle can type-check without touching generated code.

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

export interface MailBehaviorSnapshot {
  version: number;
  revision: bigint;
  valueJson: string;
  notificationPermission: MailNotificationPermission;
}

export interface MailBehaviorUpdateResult {
  status: MailBehaviorUpdateStatus;
  snapshot: MailBehaviorSnapshot;
}

export interface SettingValue {
  key: string;
  value: string;
}

export interface SearchEngineInfo {
  keyword: string;
  name: string;
  iconUrl: string;
  isDefault: boolean;
}

export enum SettingScope {
  kProfile = 0,
  kDevice = 1,
  kProcess = 2,
  kAccount = 3,
  kCoreGlobal = 4,
}

export interface ScopedSettingValue {
  key: string;
  value: string;
  scope: SettingScope;
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

export interface ProfileTarget {
  profileId: string;
  targetToken: string;
}

export interface ProfileTargetError {
  code: ProfileTargetErrorCode;
  message: string;
  currentContextRevision: bigint;
  currentProfileRevision: bigint | null;
}

export interface SelectedProfileContext {
  profileId: string;
  targetToken: string;
  contextRevision: bigint;
  profileRevision: bigint;
  lifecycleState: ProfileLifecycleState;
  isHostProfile: boolean;
  isActiveMahoProfile: boolean;
}

export interface SelectedProfileContextResult {
  context: SelectedProfileContext | null;
  error: ProfileTargetError | null;
}

export interface ProfileMetadata {
  name: string;
  avatarColor: string;
}

export interface ProfileMetadataUpdate {
  name: string;
  avatarColor: string;
  expectedProfileRevision: bigint;
}

export interface ProfileMetadataResult {
  context: SelectedProfileContext | null;
  metadata: ProfileMetadata | null;
  error: ProfileTargetError | null;
}

export interface ProfileSearchSettings {
  engines: SearchEngineInfo[];
  suggestionsEnabled: boolean;
}

export interface ProfileSearchSettingsResult {
  context: SelectedProfileContext | null;
  search: ProfileSearchSettings | null;
  error: ProfileTargetError | null;
}

export interface ProfileDownloadSettings {
  directoryDisplayPath: string;
  promptForDownload: boolean;
}

export interface ProfileDownloadSettingsResult {
  context: SelectedProfileContext | null;
  download: ProfileDownloadSettings | null;
  error: ProfileTargetError | null;
}

export interface ProfileArchiveSettings {
  timeoutHours: number;
}

export interface ProfileArchiveSettingsResult {
  context: SelectedProfileContext | null;
  archive: ProfileArchiveSettings | null;
  error: ProfileTargetError | null;
}

export interface ProfileMutationResult {
  context: SelectedProfileContext | null;
  error: ProfileTargetError | null;
}

export interface ProfileInfo {
  id: string;
  name: string;
  isDefault: boolean;
  isActive: boolean;
  spaceIds: string[];
}

export enum ProfileObservableLifecycleState {
  kProvisioning = 0,
  kReady = 1,
  kDeleting = 2,
  kRepairRequired = 3,
}

export interface ProfileObservableLifecycleEntry {
  profileId: string;
  lifecycleState: ProfileObservableLifecycleState;
}

export enum DeletedProfileHistoryAvailability {
  kUnavailable = 0,
}

export interface ProfileObservablesSnapshot {
  activeBrowserProfileId: string | null;
  activeMahoProfileId: string | null;
  registryRevision: bigint;
  lifecycleEntries: ProfileObservableLifecycleEntry[];
  pendingDeletionProfileIds: string[];
  deletedHistoryAvailability: DeletedProfileHistoryAvailability;
}

export interface ATCRule {
  id: string;
  urlPattern: string;
  targetSpaceId: string;
  enabled: boolean;
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
  hasOauthOpenai: boolean;
  hasOauthAnthropic: boolean;
  oauthClientIdOpenai: string;
  oauthClientIdAnthropic: string;
  hasRelaySession: boolean;
  relayTier: string;
  relaySubscriptionStatus: string;
  creditBalanceUsd: number;
  tierCeilingUsd: number;
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

export interface AIModelDescriptor {
  id: string;
  label: string;
  vision: AIModelCapability;
  tools: AIModelCapability;
}

export interface AIProviderDescriptor {
  id: string;
  label: string;
  authLabel: string;
  state: AIConnectionState;
  isDefaultProvider: boolean;
  baseUrl?: string;
  lastModelId?: string;
  models: AIModelDescriptor[];
  modelsLoading: boolean;
  modelsError?: string;
}

export interface AITaskModelRoute {
  taskId: string;
  inheritsDefault: boolean;
  effectiveProviderId: string;
  effectiveModelId: string;
  available: boolean;
  unavailableReason?: string;
  configuredProviderId?: string;
  configuredModelId?: string;
}

export interface AIModelsSettings {
  providers: AIProviderDescriptor[];
  defaultProviderId: string;
  defaultModelId: string;
  taskRoutes: AITaskModelRoute[];
  hasRelaySession: boolean;
}

export interface AIProviderTestResult {
  ok: boolean;
  message: string;
}

export interface AITaskReplacement {
  taskId: string;
  inheritDefault: boolean;
  providerId?: string;
  modelId?: string;
}

export interface MojoKeyCombo {
  key: string;
  modifiers: string[];
}

export interface ShortcutBinding {
  action: string;
  label: string;
  category: string;
  keyCombo: MojoKeyCombo;
  isCustom: boolean;
  enabled: boolean;
  defaultKeyCombo: MojoKeyCombo;
}

export interface SetShortcutResult {
  success: boolean;
  conflictAction: string | null;
}

export interface SyncStatus {
  isSyncing: boolean;
  relayUrl: string;
  roomId: string;
  statusLabel: string;
  errorMessage: string;
  lastSyncTimestamp: bigint;
}

export interface SyncDeviceInfo {
  id: string;
  name: string;
  deviceType: string;
  isOnline: boolean;
}

export interface SyncKeyInfo {
  syncKey: string;
  roomId: string;
  recoveryPhrase: string;
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

export interface PasswordProviderCapabilities {
  canListSavedPasswords: boolean;
  canSearchSavedPasswords: boolean;
  canAddSavedPasswords: boolean;
  canDeleteSavedPasswords: boolean;
  canEditSavedPasswords: boolean;
}

export interface PasswordProviderStatus {
  provider: PasswordProviderKind;
  displayName: string;
  description: string;
  isAvailable: boolean;
  isEnabled: boolean;
  capabilities: PasswordProviderCapabilities;
}

export interface PasswordProviderOption {
  provider: PasswordProviderKind;
  displayName: string;
  description: string;
  isAvailable: boolean;
}

export interface SavedPassword {
  id: string;
  domain: string;
  username: string;
  createdAt: string;
  lastUsed?: string | null;
}

export interface VaultStatus {
  lockState: VaultLockState;
  selectedProvider: PasswordProviderKind;
  effectiveProvider: PasswordProviderKind;
  agentPolicyDefault: VaultAgentPolicy;
  itemCount: bigint;
  autoLockMinutes: number;
  failedUnlockCount: number;
  retryAtTimestamp: bigint | null;
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

export enum SecretAction {
  kCopy = 0,
  kReveal = 1,
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

export interface VaultOperationResult {
  success: boolean;
  errorCode: string | null;
  errorMessage: string | null;
  status: VaultStatus | null;
  item: VaultItem | null;
}

export interface VaultItemListResult {
  success: boolean;
  items: VaultItem[];
  nextCursor: string | null;
  errorCode: string | null;
  errorMessage: string | null;
}

export interface VaultProviderOption {
  provider: PasswordProviderKind;
  isAvailable: boolean;
  displayName: string;
  capabilities: PasswordProviderCapabilities;
}

export interface VaultProviderStatus {
  selectedProvider: PasswordProviderKind;
  effectiveProvider: PasswordProviderKind;
  selectedProviderIsAvailable: boolean;
  providers: VaultProviderOption[];
}

export interface VaultPolicyStatus {
  isAvailable: boolean;
  policy: VaultAgentPolicy;
  itemId: string | null;
  origin: string | null;
  expiresAt: string | null;
  unavailableReason: string | null;
}

export interface VaultAuditEntry {
  timestamp: string;
  taskId: string | null;
  origin: string | null;
  itemAlias: string | null;
  operation: string;
  policy: VaultAgentPolicy | null;
  decision: string;
  reason: string | null;
  deviceName: string;
}

export interface VaultAuditPage {
  isAvailable: boolean;
  entries: VaultAuditEntry[];
  nextCursor: string | null;
  errorCode: string | null;
  unavailableReason: string | null;
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

export interface PasswordImportPreview {
  previewToken: string;
  sourceFormat: PasswordImportSourceFormat;
  sourceLabel: string;
  imported: number;
  skipped: number;
  duplicates: number;
  blankPasswords: number;
  unsupportedFields: number;
  safeMessages: string[];
  safeErrors: string[];
}

export interface PasswordImportOperationResult {
  success: boolean;
  errorCode: string | null;
  errorMessage: string | null;
  preview: PasswordImportPreview | null;
  committed: number;
  failed: number;
  terminalResultCount: number;
}

export interface SpaceBasicInfo {
  id: string;
  name: string;
}

export interface CurrentBrowserSpaceSnapshot {
  focusedBrowserSessionId: number;
  selectedSpace: SpaceBasicInfo;
  assignedTabIds: string[];
  assignedWindowIds: string[];
}

export interface AutofillAddress {
  id: string;
  name: string;
  addressLine1: string;
  addressLine2: string;
  city: string;
  state: string;
  postalCode: string;
  country: string;
  phone: string;
  email: string;
}

export interface AutofillPayment {
  id: string;
  cardNetwork: string;
  lastFour: string;
  expiration: string;
  cardholderName: string;
}

export enum ContentBlockingMode {
  kNative = 0,
  kExtension = 1,
  kDisabled = 2,
  kUnknown = 3,
}

export interface FilterListInfo {
  id: string;
  name: string;
  url: string;
  enabled: boolean;
  ruleCount: number;
  etag: string | null;
  lastModified: string | null;
  sha256: string | null;
  lastAttemptTimestamp: bigint | null;
  lastSuccessTimestamp: bigint | null;
  failureCount: number;
  lastStatus: number | null;
  lastError: string | null;
  nextRetryTimestamp: bigint | null;
}

export interface ContentBlockerStats {
  mode: ContentBlockingMode;
  totalRuleCount: number;
  filterListCount: number;
  engineGeneration: bigint;
  healthStatus: string | null;
  overallError: string | null;
  lastUpdateTimestamp: bigint;
}

export interface ContentBlockerMutationResult {
  success: boolean;
  errorCode: string | null;
  errorMessage: string | null;
  compileRequired: boolean;
  compileScheduled: boolean;
  stats: ContentBlockerStats | null;
}

export interface AccountStatus {
  signedIn: boolean;
  email: string;
  displayName: string;
  userId: string;
  tier: string;
  subscriptionStatus: string;
  subscriptionExpiresAt: bigint;
}

export interface BillingInfo {
  tier: string;
  subscriptionStatus: string;
  subscriptionExpiresAt: bigint;
  periodStartAt: bigint;
  anniversaryResetDay: number;
  tierCeilingUsd: number;
  creditBalanceUsd: number;
  lifetimePurchasedUsd: number;
  hasPaymentMethod: boolean;
}

export interface Invoice {
  id: string;
  createdAt: bigint;
  amountUsd: number;
  currency: string;
  status: string;
  pdfUrl: string;
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

export interface BrowserVersionInfo {
  version: string;
  channel: string;
  updateState: BrowserUpdateState;
  updateSupported: boolean;
  updateGuidance: string;
}

export class PageCallbackRouter {
  $: {
    bindNewPipeAndPassRemote(): unknown;
  };
  settingsChanged: {
    addListener(listener: (settings: SettingValue[]) => void): number;
  };
  onBrowserUpdateStateChanged: {
    addListener(listener: (state: BrowserUpdateState) => void): number;
  };
  accountStatusChanged: {
    addListener(listener: (status: AccountStatus) => void): number;
  };
  onShortcutRecorded: {
    addListener(listener: (keyCombo: MojoKeyCombo) => void): number;
  };
  providerModelsRefreshed: {
    addListener(listener: (providerId: string, modelIds: string[]) => void): number;
  };
  onAIModelsSettingsChanged: {
    addListener(listener: (settings: AIModelsSettings) => void): number;
  };
  onVaultLockStateChanged: {
    addListener(listener: (locked: boolean) => void): number;
  };
  onMailAccountsChanged: {
    addListener(listener: () => void): number;
  };
  onMailSignaturesChanged: {
    addListener(listener: () => void): number;
  };
  onMailTemplatesChanged: {
    addListener(listener: () => void): number;
  };
  onMailLabelsChanged: {
    addListener(listener: () => void): number;
  };
  onMailRulesChanged: {
    addListener(listener: () => void): number;
  };
  onMailCalendarChanged: {
    addListener(listener: () => void): number;
  };
  onMailBehaviorChanged: {
    addListener(listener: () => void): number;
  };
  onMailAIConfigChanged: {
    addListener(listener: () => void): number;
  };
  onMailSecurityChanged: {
    addListener(listener: () => void): number;
  };
  removeListener(id: number): boolean;
}

export class PageHandlerRemote {
  $: {
    bindNewPipeAndPassReceiver(): unknown;
  };
  getSettings(): Promise<{settings: SettingValue[]}>;
  setSetting(key: string, value: string): Promise<{success: boolean}>;
  getSearchEngines(): Promise<{engines: SearchEngineInfo[]}>;
  setDefaultSearchEngine(keyword: string): Promise<{success: boolean}>;
  getGlobalSettingsSnapshot(): Promise<{settings: ScopedSettingValue[]}>;
  getSelectedProfileContext(
      target: ProfileTarget): Promise<{result: SelectedProfileContextResult}>;
  getSelectedProfileMetadata(
      target: ProfileTarget): Promise<{result: ProfileMetadataResult}>;
  updateSelectedProfileMetadata(
      target: ProfileTarget,
      expectedContextRevision: bigint,
      update: ProfileMetadataUpdate): Promise<{result: ProfileMetadataResult}>;
  getSelectedProfileSearchSettings(
      target: ProfileTarget): Promise<{result: ProfileSearchSettingsResult}>;
  setSelectedProfileDefaultSearchEngine(
      target: ProfileTarget,
      expectedContextRevision: bigint,
      keyword: string,
      expectedProfileRevision: bigint): Promise<{result: ProfileSearchSettingsResult}>;
  setSelectedProfileSearchSuggestionsEnabled(
      target: ProfileTarget,
      expectedContextRevision: bigint,
      enabled: boolean,
      expectedProfileRevision: bigint): Promise<{result: ProfileSearchSettingsResult}>;
  getSelectedProfileDownloadSettings(
      target: ProfileTarget): Promise<{result: ProfileDownloadSettingsResult}>;
  setSelectedProfileDownloadPrompt(
      target: ProfileTarget,
      expectedContextRevision: bigint,
      promptForDownload: boolean,
      expectedProfileRevision: bigint): Promise<{result: ProfileDownloadSettingsResult}>;
  selectSelectedProfileDownloadDirectory(
      target: ProfileTarget,
      expectedContextRevision: bigint,
      expectedProfileRevision: bigint): Promise<{result: ProfileDownloadSettingsResult}>;
  getSelectedProfileArchiveSettings(
      target: ProfileTarget): Promise<{result: ProfileArchiveSettingsResult}>;
  setSelectedProfileArchiveTimeout(
      target: ProfileTarget,
      expectedContextRevision: bigint,
      timeoutHours: number,
      expectedProfileRevision: bigint): Promise<{result: ProfileArchiveSettingsResult}>;
  getBrowserVersionInfo(): Promise<{info: BrowserVersionInfo}>;
  checkForBrowserUpdates(): Promise<void>;
  applyBrowserUpdateAndRestart(): Promise<void>;
  getProfiles(): Promise<{profiles: ProfileInfo[]}>;
  getProfileObservablesSnapshot(): Promise<{
    snapshot: ProfileObservablesSnapshot | null;
  }>;
  createProfile(name: string): Promise<{profile?: ProfileInfo | null}>;
  deleteProfile(profileId: string): Promise<{success: boolean}>;
  switchProfile(profileId: string): Promise<{success: boolean}>;
  getATCRules(): Promise<{rules: ATCRule[]}>;
  addATCRule(urlPattern: string, targetSpaceId: string): Promise<{rule?: ATCRule | null}>;
  removeATCRule(ruleId: string): Promise<void>;
  toggleATCRule(ruleId: string, enabled: boolean): Promise<void>;
  getAISettings(): Promise<{settings: AISettings}>;
  getAIModelsSettings(): Promise<{settings: AIModelsSettings}>;
  setDefaultAIModel(providerId: string, modelId: string): Promise<{accepted: boolean; error?: string}>;
  setTaskAIModel(taskId: string, providerId?: string, modelId?: string): Promise<{accepted: boolean; error?: string}>;
  setAIProviderBaseUrl(providerId: string, baseUrl: string): Promise<{accepted: boolean; error?: string}>;
  testAIProvider(providerId: string): Promise<{result: AIProviderTestResult}>;
  disconnectAIProvider(
      providerId: string,
      replacementDefaultProviderId?: string,
      replacementDefaultModelId?: string,
      taskReplacements?: AITaskReplacement[]): Promise<{accepted: boolean; affectedTaskIds: string[]; error?: string}>;
  setAIApiKey(key: string): Promise<void>;
  setAIProvider(provider: string): Promise<void>;
  setAIBaseUrl(url: string): Promise<void>;
  setAIModel(model: string): Promise<void>;
  setAIApprovalPolicy(policy: string): Promise<void>;
  setAISessionPersistence(enabled: boolean): Promise<void>;
  setAIMailReadAllowed(allowed: boolean): Promise<{success: boolean}>;
  fetchProviderModels(providerId: string): Promise<{modelIds: string[]; fromCache: boolean}>;
  refreshProviderModels(providerId: string): Promise<{modelIds: string[]}>;
  testManagedConnection(): Promise<{success: boolean; message: string}>;
  getShortcuts(): Promise<{shortcuts: ShortcutBinding[]}>;
  setShortcut(action: string, keyCombo: MojoKeyCombo): Promise<{result: SetShortcutResult}>;
  checkShortcutConflict(keyCombo: MojoKeyCombo): Promise<{conflictAction: string | null}>;
  resetShortcut(action: string): Promise<void>;
  resetAllShortcuts(): Promise<void>;
  toggleShortcut(action: string, enabled: boolean): Promise<void>;
  setRecordingMode(enabled: boolean): Promise<void>;
  exportShortcuts(): Promise<{jsonData: string}>;
  importShortcuts(jsonData: string): Promise<{success: boolean}>;
  getSyncStatus(): Promise<{status: SyncStatus}>;
  getSyncDevices(): Promise<{devices: SyncDeviceInfo[]}>;
  generateSyncKey(): Promise<{keyInfo: SyncKeyInfo}>;
  configureSyncEncryption(recoveryPhrase: string): Promise<{status: SyncStatus}>;
  joinSync(recoveryPhrase: string): Promise<{status: SyncStatus}>;
  stopSync(): Promise<void>;
  disconnectSyncDevice(deviceId: string): Promise<{success: boolean}>;
  renameSyncDevice(deviceId: string, newName: string): Promise<{success: boolean}>;
  getSpaces(): Promise<{spaces: SpaceBasicInfo[]}>;
  getCurrentBrowserSpaceSnapshot(): Promise<{
    snapshot: CurrentBrowserSpaceSnapshot | null;
  }>;
  getPasswordProviderStatus(): Promise<{status: PasswordProviderStatus}>;
  getPasswordProviderOptions(): Promise<{options: PasswordProviderOption[]}>;
  getSavedPasswords(): Promise<{passwords: SavedPassword[]}>;
  searchPasswords(query: string): Promise<{passwords: SavedPassword[]}>;
  addPassword(domain: string, username: string, password: string): Promise<{success: boolean}>;
  updatePasswordUsername(passwordId: string, username: string): Promise<{success: boolean}>;
  deletePassword(passwordId: string): Promise<{success: boolean}>;
  getVaultStatus(): Promise<{result: VaultOperationResult}>;
  getVaultPreflightState(): Promise<{state: VaultPreflightState}>;
  getVaultProviderStatus(): Promise<{status: VaultProviderStatus}>;
  initializeVault(masterPassphrase: string, recoverySecret: string): Promise<{
    result: VaultOperationResult;
  }>;
  unlockVault(masterPassphrase: string): Promise<{result: VaultOperationResult}>;
  unlockVaultWithRecovery(recoverySecret: string): Promise<{
    result: VaultOperationResult;
  }>;
  lockVault(): Promise<{result: VaultOperationResult}>;
  lockNow(): Promise<{result: VaultOperationResult}>;
  listVaultItems(
      provider: PasswordProviderKind | null,
      kinds: VaultItemKind[],
      cursor: string | null,
      limit: number,
      trashOnly?: boolean,
      favoritesOnly?: boolean): Promise<{result: VaultItemListResult}>;
  searchVaultItems(
      origin: string,
      provider: PasswordProviderKind | null,
      kinds: VaultItemKind[]): Promise<{result: VaultItemListResult}>;
  addVaultLogin(
      title: string,
      origins: string[],
      username: string,
      password: string,
      notes?: string | null): Promise<{result: VaultOperationResult}>;
  updateVaultLogin(
      itemId: string,
      expectedRevision: bigint,
      title: string,
      origins: string[],
      username: string,
      password: string | null,
      notes?: string | null): Promise<{result: VaultOperationResult}>;
  deleteVaultItem(
      itemId: string,
      expectedRevision: bigint): Promise<{result: VaultOperationResult}>;
  trashVaultItem(
      itemId: string,
      expectedRevision: bigint): Promise<{result: VaultOperationResult}>;
  restoreVaultItem(
      itemId: string,
      expectedRevision: bigint): Promise<{result: VaultOperationResult}>;
  emptyVaultTrash(): Promise<{result: VaultOperationResult}>;
  setVaultItemFavorite(
      itemId: string,
      expectedRevision: bigint,
      favorite: boolean): Promise<{result: VaultOperationResult}>;
  getVaultItemNotes(
      itemId: string): Promise<{
        success: boolean;
        notes: string | null;
        errorCode: string | null;
        username: string | null;
      }>;
  addVaultSecureNote(
      title: string,
      notes: string): Promise<{result: VaultOperationResult}>;
  updateVaultSecureNote(
      itemId: string,
      expectedRevision: bigint,
      title: string,
      notes: string): Promise<{result: VaultOperationResult}>;
  setVaultLoginTotp(
      itemId: string,
      expectedRevision: bigint,
      secret: string): Promise<{result: VaultOperationResult}>;
  getVaultTotpCode(
      itemId: string): Promise<{result: VaultTotpCodeResult}>;
  generatePassword(
      options: PasswordGeneratorOptions): Promise<{result: GeneratedPasswordResult}>;
  estimatePasswordStrength(
      password: string): Promise<{score: number; entropyBits: number}>;
  getVaultHealthReport(): Promise<{report: VaultHealthReport}>;
  useVaultSecret(
      itemId: string,
      expectedRevision: bigint,
      action: SecretAction): Promise<{result: VaultOperationResult}>;
  getVaultPolicyStatus(): Promise<{status: VaultPolicyStatus}>;
  setVaultPolicy(
      policy: VaultAgentPolicy,
      itemId: string | null,
      origin: string | null,
      expiresAt: string | null): Promise<{status: VaultPolicyStatus}>;
  getVaultAuditPage(
      cursor: string | null,
      limit: number): Promise<{page: VaultAuditPage}>;
  selectPasswordImportFile(
      sourceFormat: PasswordImportSourceFormat): Promise<{filePath: string | null}>;
  previewPasswordImport(
      sourceFormat: PasswordImportSourceFormat,
      filePath: string): Promise<{result: PasswordImportOperationResult}>;
  cancelPasswordImport(): Promise<{result: PasswordImportOperationResult}>;
  commitPasswordImport(
      previewToken: string): Promise<{result: PasswordImportOperationResult}>;
  getAutofillAddresses(): Promise<{addresses: AutofillAddress[]}>;
  addAutofillAddress(
      id: string,
      name: string,
      addressLine1: string,
      addressLine2: string,
      city: string,
      state: string,
      postalCode: string,
      country: string,
      phone: string,
      email: string): Promise<{address?: AutofillAddress | null}>;
  deleteAutofillAddress(id: string): Promise<{success: boolean}>;
  getAutofillPayments(): Promise<{payments: AutofillPayment[]}>;
  addAutofillPayment(
      id: string,
      cardNetwork: string,
      lastFour: string,
      expiration: string,
      cardholderName: string): Promise<{payment?: AutofillPayment | null}>;
  deleteAutofillPayment(id: string): Promise<{success: boolean}>;
  getFilterLists(): Promise<{lists: FilterListInfo[]}>;
  addFilterList(id: string, name: string, url: string): Promise<{
    success: boolean;
    mutation: ContentBlockerMutationResult;
  }>;
  toggleFilterList(id: string, enabled: boolean): Promise<{
    success: boolean;
    mutation: ContentBlockerMutationResult;
  }>;
  removeFilterList(id: string): Promise<{
    success: boolean;
    mutation: ContentBlockerMutationResult;
  }>;
  rebuildContentRules(): Promise<{
    success: boolean;
    stats: ContentBlockerStats;
    mutation: ContentBlockerMutationResult;
  }>;
  getContentBlockerStats(): Promise<{stats: ContentBlockerStats}>;
  setContentBlockingMode(mode: ContentBlockingMode): Promise<{
    success: boolean;
    mutation: ContentBlockerMutationResult;
  }>;
  triggerFilterUpdate(listId: string | null): Promise<{
    success: boolean;
    mutation: ContentBlockerMutationResult;
  }>;
  openExtensionsPage(): void;
  openChromiumSettingsPage(): void;
  openMigrationDialog(): void;
  setBYOKKey(provider: string, key: string): Promise<{success: boolean}>;
  clearBYOKKey(provider: string): Promise<{success: boolean}>;
  setAIProviderOAuthClientId(provider: string, clientId: string): Promise<{success: boolean}>;
  signInToAIProvider(provider: string): Promise<{success: boolean; errorMessage: string}>;
  signOutOfAIProvider(provider: string): Promise<{success: boolean}>;
  login(email: string, password: string): Promise<{success: boolean; errorMessage: string}>;
  signInWithGoogle(): Promise<{success: boolean; errorMessage: string}>;
  logout(): Promise<void>;
  getAccountStatus(): Promise<{status: AccountStatus}>;
  getBillingInfo(): Promise<{info: BillingInfo | null; error: string}>;
  getInvoices(): Promise<{invoices: Invoice[]}>;
  getBillingPortalUrl(): Promise<{url: string}>;
  getBuyCreditsUrl(packSizeUsd: number): Promise<{checkoutUrl: string}>;
  getSubscriptionCheckoutUrl(tier: string): Promise<{checkoutUrl: string}>;
  mailListAccounts(): Promise<{ok: boolean; resultJson: string}>;
  mailAddAccount(requestJson: string): Promise<{ok: boolean; resultJson: string}>;
  mailTestConnection(requestJson: string): Promise<{ok: boolean; resultJson: string}>;
  mailDeleteAccount(accountId: string): Promise<{ok: boolean; resultJson: string}>;
  mailBeginOAuth(provider: string): Promise<{ok: boolean; resultJson: string}>;
  mailOAuthComplete(state: string, code: string): Promise<{ok: boolean; resultJson: string}>;
  mailReconnectAccount(accountId: string): Promise<{ok: boolean; resultJson: string}>;
  mailOAuthCancel(state: string): Promise<{ accepted: boolean }>;
  mailListSignatures(accountId: string | null): Promise<{ok: boolean; resultJson: string}>;
  mailUpsertSignature(signatureJson: string): Promise<{ok: boolean; error: string}>;
  mailDeleteSignature(id: string): Promise<{ok: boolean}>;
  mailListRules(accountId: string): Promise<{ok: boolean; resultJson: string}>;
  mailUpsertRule(ruleJson: string): Promise<{ok: boolean; error: string}>;
  mailDeleteRule(id: string): Promise<{ok: boolean}>;
  mailReorderRules(accountId: string, ruleIdsJson: string): Promise<{ok: boolean}>;
  mailGetCalendarPrefs(): Promise<{ok: boolean; resultJson: string}>;
  mailSetCalendarPrefs(prefsJson: string): Promise<{ok: boolean}>;
  mailListCalendarCategories(accountId: string): Promise<{ok: boolean; resultJson: string}>;
  mailSetCalendarVisibility(id: string, visible: boolean): Promise<{ok: boolean}>;
  mailListAccountCalendars(accountId: string): Promise<{ok: boolean; resultJson: string}>;
  mailGetBehaviorPrefs(): Promise<{ok: boolean; snapshot: MailBehaviorSnapshot}>;
  mailSetBehaviorPref(expectedRevision: bigint, key: string, value: string): Promise<{result: MailBehaviorUpdateResult}>;
  mailRequestNotificationPermission(): Promise<{permission: MailNotificationPermission}>;
  mailGetAIConfig(feature: string): Promise<{ok: boolean; resultJson: string}>;
  mailSetAIConfig(feature: string, configJson: string): Promise<{ok: boolean}>;
  mailSetAIKey(provider: string, apiKey: string): Promise<{ok: boolean}>;
  mailGeneratePgpKey(accountId: string, email: string, passphrase: string): Promise<{ok: boolean; error: string}>;
  mailImportPgpKey(accountId: string, keyBlock: string, passphrase: string | null): Promise<{ok: boolean; error: string}>;
  mailListPgpKeys(accountId: string): Promise<{ok: boolean; resultJson: string}>;
  mailDeletePgpKey(keyId: string): Promise<{ok: boolean}>;
  mailImportSmimeIdentity(accountId: string, p12DataBase64: string, passphrase: string): Promise<{ok: boolean; error: string}>;
  mailListSmimeIdentities(accountId: string): Promise<{ok: boolean; resultJson: string}>;
  mailDeleteSmimeIdentity(id: string): Promise<{ok: boolean}>;
  mailListTemplates(accountId: string | null): Promise<{ok: boolean; resultJson: string}>;
  mailUpsertTemplate(templateJson: string): Promise<{ok: boolean; error: string}>;
  mailDeleteTemplate(id: string): Promise<{ok: boolean}>;
  mailListLabels(accountId: string | null): Promise<{ok: boolean; resultJson: string}>;
  mailCreateLabel(accountId: string, name: string, color: string): Promise<{ok: boolean; error: string}>;
  mailDeleteLabel(id: string): Promise<{ok: boolean}>;
  mailCreateCalendarCategory(name: string, color: string): Promise<{ok: boolean; error: string}>;
  mailUpdateCalendarCategory(id: string, name: string, color: string): Promise<{ok: boolean; error: string}>;
  mailDeleteCalendarCategory(id: string): Promise<{ok: boolean}>;
  mailSetDefaultPgpKey(keyId: string): Promise<{ok: boolean}>;
  mailSetDefaultSmimeIdentity(id: string): Promise<{ok: boolean}>;
}

export class PageHandlerFactory {
  static getRemote(): PageHandlerFactory;
  createPageHandler(page: unknown, handler: unknown): void;
}
