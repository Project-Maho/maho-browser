import type {LiveCardDefinition, PaneDefinition} from '../models.js';

export type LivePaneSectionDefinition =
    {kind: 'live-card'; card: LiveCardDefinition};

export const LIVE_PANE_SECTION_MANIFEST: Record<string, LivePaneSectionDefinition[]> = {
  general: [
    {kind: 'live-card', card: {
      title: 'Daily browsing defaults',
      description: '',
      settingKeys: ['general.homepage', 'general.restore_on_startup', 'general.prompt_for_download', 'search.suggestions'],
      wide: true,
    }},
  ],
  appearance: [
    {kind: 'live-card', card: {
      title: 'Browser chrome',
      description: '',
      settingKeys: [
        'appearance.theme',
        'appearance.accent_color',
        'appearance.density',
        'appearance.show_home_button',
        'appearance.font_size',
        'appearance.minimum_font_size',
        'appearance.sidebar_width',
        'appearance.window_transparency',
      ],
      wide: true,
    }},
  ],
  privacy: [
    {kind: 'live-card', card: {
      title: 'Safety & tracking',
      description: '',
      settingKeys: ['privacy.do_not_track', 'privacy.safe_browsing', 'privacy.block_third_party_cookies'],
      wide: true,
    }},
  ],
  advanced: [
    {kind: 'live-card', card: {
      title: 'Performance & browser behavior',
      description: '',
      settingKeys: ['advanced.hardware_acceleration', 'advanced.preload_pages'],
      wide: true,
    }},
  ],
  notifications: [
    {kind: 'live-card', card: {
      title: 'Notification prompts',
      description: '',
      settingKeys: ['notifications.quiet_permission_ui'],
    }},
  ],
  tabs: [
    {kind: 'live-card', card: {
      title: 'Tab lifecycle',
      description: '',
      settingKeys: ['tabs.archive_timeout', 'tabs.today_tab_timeout'],
      wide: true,
    }},
  ],
};

const BROWSING_PANES: PaneDefinition[] = [
  {
    key: 'general',
    symbol: 'G',
    navTitle: 'General',
    title: 'General',
    kind: 'live',
    domain: 'browsing',
    groupKeys: ['general', 'search'],
  },
  {
    key: 'tabs',
    symbol: 'T',
    navTitle: 'Tabs',
    title: 'Tabs',
    kind: 'live',
    domain: 'browsing',
    groupKeys: ['tabs'],
  },
  {
    key: 'privacy',
    symbol: 'P',
    navTitle: 'Privacy',
    title: 'Privacy',
    kind: 'live',
    domain: 'browsing',
    groupKeys: ['privacy'],
  },
  {
    key: 'notifications',
    symbol: 'N',
    navTitle: 'Notifications',
    title: 'Notifications',
    kind: 'live',
    domain: 'browsing',
    groupKeys: ['notifications'],
  },
  {
    key: 'advanced',
    symbol: 'A',
    navTitle: 'Advanced',
    title: 'Advanced',
    kind: 'live',
    domain: 'browsing',
    groupKeys: ['advanced'],
  },
];

const LOOK_AND_FEEL_PANES: PaneDefinition[] = [
  {
    key: 'appearance',
    symbol: 'A',
    navTitle: 'Appearance',
    title: 'Appearance',
    kind: 'live',
    domain: 'look-and-feel',
    groupKeys: ['appearance'],
  },
  {
    key: 'theme-store',
    symbol: 'TS',
    navTitle: 'Theme Store',
    title: 'Theme Store',
    kind: 'live',
    domain: 'look-and-feel',
    contentKind: 'theme-store',
  },
];

const IDENTITY_PANES: PaneDefinition[] = [
  {
    key: 'profiles',
    symbol: 'P',
    navTitle: 'Profiles',
    title: 'Profiles',
    kind: 'live',
    domain: 'identity',
    contentKind: 'profiles',
  },
  {
    key: 'account-sync',
    symbol: 'AS',
    navTitle: 'Account & Sync',
    title: 'Account & Sync',
    kind: 'live',
    domain: 'identity',
    contentKind: 'sync',
  },
  {
    key: 'passwords',
    symbol: 'PW',
    navTitle: 'Passwords',
    title: 'Passwords',
    kind: 'live',
    domain: 'identity',
    contentKind: 'passwords',
  },
  {
    key: 'autofill',
    symbol: 'A',
    navTitle: 'Autofill',
    title: 'Autofill',
    kind: 'live',
    domain: 'identity',
    contentKind: 'autofill',
  },
];

const PRODUCTIVITY_PANES: PaneDefinition[] = [
  {
    key: 'shortcuts',
    symbol: '\u2318',
    navTitle: 'Shortcuts',
    title: 'Shortcuts',
    kind: 'live',
    domain: 'productivity',
    contentKind: 'shortcuts',
  },
  {
    key: 'extensions',
    symbol: 'EX',
    navTitle: 'Extensions',
    title: 'Extensions',
    kind: 'live',
    domain: 'productivity',
    contentKind: 'extensions',
  },
  {
    key: 'maho-ai',
    symbol: 'AI',
    navTitle: 'Maho AI',
    title: 'Maho AI',
    kind: 'live',
    domain: 'productivity',
    contentKind: 'ai',
  },
];

const PROTECTION_PANES: PaneDefinition[] = [
  {
    key: 'content-blocker',
    symbol: 'C',
    navTitle: 'Content Blocker',
    title: 'Content Blocker',
    kind: 'live',
    domain: 'protection',
    contentKind: 'content-blocker',
  },
  {
    key: 'atc',
    symbol: 'AT',
    navTitle: 'Air Traffic Control',
    title: 'Air Traffic Control',
    kind: 'live',
    domain: 'protection',
    contentKind: 'atc',
  },
];

export const ALL_PANE_DEFINITIONS: PaneDefinition[] = [
  ...BROWSING_PANES,
  ...LOOK_AND_FEEL_PANES,
  ...IDENTITY_PANES,
  ...PRODUCTIVITY_PANES,
  ...PROTECTION_PANES,
];

export const VISIBLE_PANE_DEFINITIONS: PaneDefinition[] =
    ALL_PANE_DEFINITIONS.filter(p => p.kind !== 'hidden');

export const PANE_DEFINITIONS = VISIBLE_PANE_DEFINITIONS;

export const PANE_DEFINITION_MAP: Record<string, PaneDefinition> =
    Object.fromEntries(ALL_PANE_DEFINITIONS.map(d => [d.key, d])) as
        Record<string, PaneDefinition>;
