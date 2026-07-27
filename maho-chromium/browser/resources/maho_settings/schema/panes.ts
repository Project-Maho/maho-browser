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
        'appearance.sidebar_width',
        'appearance.density',
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
      wide: true,
    }},
  ],
  tabs: [
    {kind: 'live-card', card: {
      title: 'Tab lifecycle',
      description: '',
      settingKeys: ['tabs.archive_timeout', 'tabs.today_tab_timeout', 'tabs.pinned_close_behavior', 'tabs.auto_delete_empty_folders_on_tidy'],
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
];

const IDENTITY_PANES: PaneDefinition[] = [
  {
    key: 'account',
    symbol: 'AC',
    navTitle: 'Account',
    title: 'Account',
    kind: 'live',
    domain: 'identity',
    contentKind: 'account',
  },
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
    navTitle: 'Account & sync',
    title: 'Account & sync',
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
    key: 'saved-passwords',
    symbol: 'SP',
    navTitle: 'Saved Passwords',
    title: 'Saved Passwords',
    kind: 'live',
    domain: 'identity',
    contentKind: 'saved-passwords',
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
    title: 'Keyboard shortcuts',
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
    externalAction: 'openExtensions',
  },
  {
    key: 'maho-mini',
    symbol: 'LA',
    navTitle: 'Maho Mini & routing',
    title: 'Maho Mini & routing',
    kind: 'live',
    domain: 'productivity',
    contentKind: 'maho-mini',
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
  {
    key: 'maho-ai-developers',
    symbol: 'DEV',
    navTitle: 'Developers',
    title: 'Developers',
    kind: 'live',
    domain: 'productivity',
    contentKind: 'ai-developers',
  },
  {
    key: 'billing',
    symbol: '$',
    navTitle: 'Billing',
    title: 'Billing',
    kind: 'live',
    domain: 'productivity',
    contentKind: 'billing',
  },
];

const PROTECTION_PANES: PaneDefinition[] = [
  {
    key: 'content-blocker',
    symbol: 'C',
    navTitle: 'Content blocker',
    title: 'Content blocker',
    kind: 'live',
    domain: 'protection',
    contentKind: 'content-blocker',
  },
  {
    key: 'chromium-settings',
    symbol: 'CR',
    navTitle: 'Chromium settings',
    title: 'Chromium settings',
    kind: 'embed',
    domain: 'protection',
    embedUrl: 'chrome://settings',
  },
];

const MAIL_PANES: PaneDefinition[] = [
  {
    key: 'mail-accounts',
    symbol: 'MA',
    navTitle: 'Mail accounts',
    title: 'Mail accounts',
    kind: 'live',
    domain: 'mail',
    contentKind: 'mail',
  },
  {
    key: 'mail-signatures',
    symbol: 'MS',
    navTitle: 'Signatures',
    title: 'Email signatures',
    kind: 'live',
    domain: 'mail',
    contentKind: 'mail-signatures',
  },
  {
    key: 'mail-rules',
    symbol: 'MR',
    navTitle: 'Rules & filters',
    title: 'Mail rules & filters',
    kind: 'live',
    domain: 'mail',
    contentKind: 'mail-rules',
  },
  {
    key: 'mail-calendar',
    symbol: 'MC',
    navTitle: 'Calendar',
    title: 'Mail calendar settings',
    kind: 'live',
    domain: 'mail',
    contentKind: 'mail-calendar',
  },
  {
    key: 'mail-behavior',
    symbol: 'MB',
    navTitle: 'Mail behavior',
    title: 'Mail behavior & layout',
    kind: 'live',
    domain: 'mail',
    contentKind: 'mail-behavior',
  },
  {
    key: 'mail-ai',
    symbol: 'MAI',
    navTitle: 'Mail AI',
    title: 'Mail AI behavior',
    kind: 'live',
    domain: 'mail',
    contentKind: 'mail-ai',
  },
  {
    key: 'mail-security',
    symbol: 'MSY',
    navTitle: 'Mail security',
    title: 'PGP & S/MIME security',
    kind: 'live',
    domain: 'mail',
    contentKind: 'mail-security',
  },
];

// Mail deferred from this ship: omit the mail settings panes from the nav so
// the whole "Maho Mail" section disappears (app.tsx skips empty domains). The
// MAIL_PANES definitions are kept for a one-line revert. Flip to false to
// restore mail settings.
const MAIL_DEFERRED = true;

export const ALL_PANE_DEFINITIONS: PaneDefinition[] = [
  ...BROWSING_PANES,
  ...LOOK_AND_FEEL_PANES,
  ...IDENTITY_PANES,
  ...PRODUCTIVITY_PANES,
  ...PROTECTION_PANES,
  ...(MAIL_DEFERRED ? [] : MAIL_PANES),
];

export const PANE_DEFINITIONS = ALL_PANE_DEFINITIONS;

export const PANE_DEFINITION_MAP: Record<string, PaneDefinition> =
    Object.fromEntries(ALL_PANE_DEFINITIONS.map(d => [d.key, d])) as
        Record<string, PaneDefinition>;
