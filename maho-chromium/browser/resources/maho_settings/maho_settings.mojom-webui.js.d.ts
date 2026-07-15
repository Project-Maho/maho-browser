// Copyright 2026 Maho Browser. All rights reserved.
// Ambient type declarations for maho_settings.mojom-webui.js.
// Generated at build time; this file provides tsc with the shape of the Mojo
// bindings so the React bundle can type-check without touching generated code.

export interface SettingValue {
  key: string;
  value: string;
}

export interface ProfileInfo {
  id: string;
  name: string;
  isDefault: boolean;
  isActive: boolean;
  spaceIds: string[];
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
  hasByokOpenai: boolean;
  hasByokAnthropic: boolean;
  hasRelaySession: boolean;
  relayTier: string;
  relaySubscriptionStatus: string;
  creditBalanceUsd: number;
  tierCeilingUsd: number;
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

export enum PasswordProviderKind {
  kMahoNative = 0,
  kBitwarden = 1,
  kOnePassword = 2,
}

export interface PasswordProviderCapabilities {
  canListSavedPasswords: boolean;
  canSearchSavedPasswords: boolean;
  canAddSavedPasswords: boolean;
  canDeleteSavedPasswords: boolean;
  canEditSavedPasswords: boolean;
  canRevealPasswords: boolean;
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


export interface SpaceBasicInfo {
  id: string;
  name: string;
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

export interface FilterListInfo {
  id: string;
  name: string;
  url: string;
  enabled: boolean;
  ruleCount: number;
}

export interface ContentBlockerStats {
  totalRuleCount: number;
  filterListCount: number;
}

export interface InstalledExtension {
  id: string;
  name: string;
  version: string;
  description: string;
  enabled: boolean;
  manifestVersion: number;
}

export enum RemoveExtensionResult {
  kSuccess = 0,
  kCancelled = 1,
  kBlockedByPolicy = 2,
  kNotFound = 3,
  kBusy = 4,
  kUninstallFailed = 5,
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

export class PageCallbackRouter {
  $: {
    bindNewPipeAndPassRemote(): unknown;
  };
  settingsChanged: {
    addListener(listener: (settings: SettingValue[]) => void): number;
  };
  installedExtensionsChanged: {
    addListener(listener: (extensions: InstalledExtension[]) => void): number;
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
  removeListener(id: number): boolean;
}

export class PageHandlerRemote {
  $: {
    bindNewPipeAndPassReceiver(): unknown;
  };
  getSettings(): Promise<{settings: SettingValue[]}>;
  setSetting(key: string, value: string): Promise<{success: boolean}>;
  getProfiles(): Promise<{profiles: ProfileInfo[]}>;
  createProfile(name: string): Promise<{profile?: ProfileInfo | null}>;
  deleteProfile(profileId: string): Promise<{success: boolean}>;
  switchProfile(profileId: string): Promise<{success: boolean}>;
  getATCRules(): Promise<{rules: ATCRule[]}>;
  addATCRule(urlPattern: string, targetSpaceId: string): Promise<{rule?: ATCRule | null}>;
  removeATCRule(ruleId: string): Promise<void>;
  toggleATCRule(ruleId: string, enabled: boolean): Promise<void>;
  getAISettings(): Promise<{settings: AISettings}>;
  setAIApiKey(key: string): Promise<void>;
  setAIProvider(provider: string): Promise<void>;
  setAIBaseUrl(url: string): Promise<void>;
  setAIModel(model: string): Promise<void>;
  setAIApprovalPolicy(policy: string): Promise<void>;
  setAISessionPersistence(enabled: boolean): Promise<void>;
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
  getPasswordProviderStatus(): Promise<{status: PasswordProviderStatus}>;
  getPasswordProviderOptions(): Promise<{options: PasswordProviderOption[]}>;
  getSavedPasswords(): Promise<{passwords: SavedPassword[]}>;
  searchPasswords(query: string): Promise<{passwords: SavedPassword[]}>;
  addPassword(domain: string, username: string, password: string): Promise<{success: boolean}>;
  updatePasswordUsername(passwordId: string, username: string): Promise<{success: boolean}>;
  deletePassword(passwordId: string): Promise<{success: boolean}>;
  revealPassword(passwordId: string): Promise<{password: string | null}>;
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
  addFilterList(id: string, name: string, url: string): Promise<{success: boolean}>;
  toggleFilterList(id: string, enabled: boolean): Promise<void>;
  removeFilterList(id: string): Promise<void>;
  rebuildContentRules(): Promise<{stats: ContentBlockerStats}>;
  getContentBlockerStats(): Promise<{stats: ContentBlockerStats}>;
  getInstalledExtensions(): Promise<{extensions: InstalledExtension[]}>;
  toggleExtension(id: string): Promise<{enabled: boolean}>;
  removeExtension(id: string): Promise<{result: RemoveExtensionResult}>;
  openExtensionsPage(): Promise<void>;
  setBYOKKey(provider: string, key: string): Promise<{success: boolean}>;
  clearBYOKKey(provider: string): Promise<{success: boolean}>;
  login(email: string, password: string): Promise<{success: boolean; errorMessage: string}>;
  logout(): Promise<void>;
  getAccountStatus(): Promise<{status: AccountStatus}>;
  getBillingInfo(): Promise<{info: BillingInfo}>;
  getInvoices(): Promise<{invoices: Invoice[]}>;
  getBillingPortalUrl(): Promise<{url: string}>;
  getBuyCreditsUrl(packSizeUsd: number): Promise<{checkoutUrl: string}>;
}

export class PageHandlerFactory {
  static getRemote(): PageHandlerFactory;
  createPageHandler(page: unknown, handler: unknown): void;
}
