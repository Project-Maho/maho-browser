export type ControlKind = 'text'|'number'|'toggle'|'select'|'slider'|'textarea';

export type PaneKind = 'live'|'hidden'|'embed';

export type DomainId =
    | 'browsing'
    | 'look-and-feel'
    | 'identity'
    | 'productivity'
    | 'protection';

export interface ControlOption {
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
  contentKind?: 'settings'|'profiles'|'atc'|'ai'|'billing'|'account'|'shortcuts'|'sync'|'autofill'|'passwords'|'content-blocker'|'extensions';
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
