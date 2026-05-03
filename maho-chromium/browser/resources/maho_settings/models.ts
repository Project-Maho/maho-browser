export type ControlKind = 'text'|'number'|'toggle'|'select'|'color-swatch'|'slider'|'textarea';
export type PaneStatus = 'live-pref'|'mixed'|'placeholder'|'live-embed';
export type PaneKind = 'live'|'placeholder'|'embed';
export type BadgeTone = 'live'|'mixed'|'bridge'|'api';

export interface ControlOption {
  value: string;
  label: string;
  displayLabel?: string;
}

export interface AccentColor {
  r: number;
  g: number;
  b: number;
  label: string;
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

export interface StructuredPlaceholderRow {
  label: string;
  description: string;
  tone: 'live'|'bridge'|'api';
  badgeLabel: string;
  control: ControlKind;
  segmented?: boolean;
  value?: string;
  options?: ControlOption[];
  placeholder?: string;
  min?: number;
  max?: number;
  step?: number;
}

export interface PaneHeroBadge {
  label: string;
  tone: BadgeTone;
}

export interface PaneDefinition {
  key: string;
  symbol: string;
  navTitle: string;
  navMeta: string;
  eyebrow: string;
  title: string;
  summary: string;
  status: PaneStatus;
  kind: PaneKind;
  heroBadges?: PaneHeroBadge[];
  overviewTitle?: string;
  overviewCopy?: string;
  groupKeys?: string[];
  placeholderReason?: string;
  placeholderDetail?: string;
  placeholderHighlights?: string[];
}

export interface LiveCardDefinition {
  title: string;
  description: string;
  settingKeys: string[];
  badgeTone: BadgeTone;
  wide?: boolean;
}

export interface StructuredPlaceholderCardDefinition {
  title: string;
  description: string;
  rows: StructuredPlaceholderRow[];
  badgeTone: 'bridge'|'api';
}
