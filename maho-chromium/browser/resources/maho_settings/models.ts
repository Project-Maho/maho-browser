export type ControlKind = 'text'|'toggle'|'select'|'slider'|'textarea';

export type PaneKind = 'live'|'hidden'|'embed';

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

export interface PaneDefinition {
  key: string;
  symbol: string;
  navTitle: string;
  title: string;
  kind: PaneKind;
  domain: DomainId;
  groupKeys?: string[];
  embedUrl?: string;
  contentKind?: 'profiles'|'atc'|'ai'|'ai-developers'|'billing'|'account'|'shortcuts'|'sync'|'autofill'|'passwords'|'saved-passwords'|'mail'|'content-blocker'|'extensions'|'maho-mini'|'mail-signatures'|'mail-rules'|'mail-calendar'|'mail-behavior'|'mail-ai'|'mail-security';
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
