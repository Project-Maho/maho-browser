export type ControlKind = 'text'|'toggle'|'select'|'slider'|'textarea';

export type PaneKind = 'live'|'hidden'|'embed';

export type SettingScope =
    | 'profile'
    | 'device'
    | 'process'
    | 'account'
    | 'core-global';

export type SelectedProfileSupport =
    | 'supported'
    | 'partial'
    | 'host-only'
    | 'not-applicable';

export const GLOBAL_APPLICABILITY_LABEL = 'Applies to all profiles.';

export function scopeApplicabilityLabel(scope: SettingScope): string | null {
  return scope === 'profile' ? null : GLOBAL_APPLICABILITY_LABEL;
}

export type OwnershipMetadataRecord = {
  readonly key: string;
  readonly scope?: SettingScope;
  readonly selectedProfile?: boolean;
  readonly selectedProfileSupport?: SelectedProfileSupport;
};

export function assertExhaustiveOwnershipMetadata(
    records: readonly OwnershipMetadataRecord[],
    eligibility: 'selectedProfile'|'selectedProfileSupport'): void {
  const seen = new Set<string>();
  for (const record of records) {
    if (!record.scope) {
      throw new Error(`Missing scope for ${record.key}`);
    }
    if (record[eligibility] === undefined) {
      throw new Error(`Missing ${eligibility} eligibility for ${record.key}`);
    }
    if (seen.has(record.key)) {
      throw new Error(`Duplicate ownership metadata for ${record.key}`);
    }
    seen.add(record.key);
  }
}

export type DomainId =
    | 'browsing'
    | 'look-and-feel'
    | 'identity'
    | 'productivity'
    | 'protection'
    | 'mail';

interface ControlOption {
  value: string;
  label: string;
  displayLabel?: string;
}

export interface SettingMetadata {
  scope: SettingScope;
  selectedProfile: boolean;
  label: string;
  description: string;
  control: ControlKind;
  options?: ControlOption[];
  segmented?: boolean;
  placeholder?: string;
  min?: number;
  max?: number;
  step?: number;
  unit?: string;
}

export interface SettingDefinition {
  key: string;
  metadata: SettingMetadata;
}

export interface ServiceMetadata {
  key: string;
  scope: SettingScope;
  selectedProfile: boolean;
}

export interface PaneDefinition {
  key: string;
  scope: SettingScope;
  selectedProfileSupport: SelectedProfileSupport;
  symbol: string;
  navTitle: string;
  title: string;
  kind: PaneKind;
  domain: DomainId;
  groupKeys?: string[];
  embedUrl?: string;
  contentKind?: 'profiles'|'atc'|'ai'|'ai-developers'|'billing'|'account'|'shortcuts'|'sync'|'autofill'|'passwords'|'saved-passwords'|'mail'|'mail-overview'|'content-blocker'|'extensions'|'maho-mini'|'mail-signatures'|'mail-rules'|'mail-calendar'|'mail-behavior'|'mail-security';
  externalAction?: 'openExtensions';
}

export interface LiveCardDefinition {
  title: string;
  description: string;
  settingKeys: string[];
  wide?: boolean;
}

export interface DomainDefinition {
  id: DomainId;
  label: string;
  order: number;
}
