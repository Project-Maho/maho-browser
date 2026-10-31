import type {LiveCardDefinition, PaneDefinition, SettingScope} from '../models.js';

export type LivePaneSectionDefinition =
    {kind: 'live-card'; card: LiveCardDefinition};

export type SelectedProfilePaneDisposition = 'pane'|'limitation';
export type SelectedProfileLegacyRowDisposition = 'control'|'limitation';

// An active target is fail-closed until its browser-issued context proves that
// it is the host profile. This prevents loading/error/stale transitions from
// briefly exposing host-only pane data under a selected-profile context.
export function selectedProfilePaneDisposition(
    support: PaneDefinition['selectedProfileSupport'],
    hasSelectedTarget: boolean,
    isHostProfile: boolean|null|undefined): SelectedProfilePaneDisposition {
  return support === 'host-only' && hasSelectedTarget && isHostProfile !== true ?
      'limitation' : 'pane';
}

export const LEGACY_PROFILE_ROW_GUARD_PANE_KEYS = [
  'tabs',
  'advanced',
  'appearance',
] as const;

// Partial panes mix global rows, selected-profile-routable rows, and legacy
// host-only rows. Keep allowlisted rows actionable because the store routes
// them through typed target APIs. Gate unsupported profile rows for non-host or
// unresolved targets so they can never fall through to the host-bound path.
export function selectedProfileLegacyRowDisposition(
    scope: SettingScope|undefined,
    selectedProfileEligible: boolean|undefined,
    isGuardedPane: boolean,
    hasSelectedTarget: boolean,
    isHostProfile: boolean|null|undefined): SelectedProfileLegacyRowDisposition {
  if (!isGuardedPane || !hasSelectedTarget || isHostProfile === true) {
    return 'control';
  }
  if (scope === 'profile') {
    if (isHostProfile === null || isHostProfile === undefined) {
      return 'limitation';
    }
    return selectedProfileEligible === true ? 'control' : 'limitation';
  }
  return scope === 'device' || scope === 'process' || scope === 'account' ||
          scope === 'core-global' ?
      'control' : 'limitation';
}

export const LIVE_PANE_SECTION_MANIFEST: Record<string, LivePaneSectionDefinition[]> = {
  general: [
    {kind: 'live-card', card: {
      title: 'Daily browsing defaults',
      description: '',
      settingKeys: ['general.prompt_for_download', 'search.suggestions'],
      wide: true,
    }},
    {kind: 'live-card', card: {
      title: 'Languages',
      description: '',
      settingKeys: ['general.translate_target_language'],
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
      ],
      wide: true,
    }},
  ],
  advanced: [
    {kind: 'live-card', card: {
      title: 'Performance & browser behavior',
      description: '',
      settingKeys: [
        'advanced.memory_saver_mode',
        'advanced.memory_saver_timeout',
        'advanced.hardware_acceleration',
        'advanced.preload_pages',
      ],
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
      title: 'New tabs',
      description: '',
      settingKeys: ['sidebar.new_tab_position'],
      wide: true,
    }},
    {kind: 'live-card', card: {
      title: 'Tab lifecycle',
      description: '',
      settingKeys: ['tabs.archive_timeout', 'tabs.today_tab_timeout', 'tabs.pinned_close_behavior', 'tabs.auto_delete_empty_folders_on_tidy'],
      wide: true,
    }},
  ],
  'maho-ai': [
    {kind: 'live-card', card: {
      title: 'Conversations',
      description: '',
      settingKeys: ['conversation.auto_archive_after_days'],
      wide: true,
    }},
  ],
};

// Merged into the Content blocker pane; rendered there as toggles.
export const PRIVACY_SETTING_KEYS = [
  'privacy.do_not_track',
  'privacy.safe_browsing',
  'privacy.block_third_party_cookies',
] as const;

const BROWSING_PANES: PaneDefinition[] = [
  {
    key: 'general',
    scope: 'profile',
    selectedProfileSupport: 'supported',
    symbol: 'G',
    navTitle: 'General',
    title: 'General',
    kind: 'live',
    domain: 'browsing',
    groupKeys: ['general', 'search'],
  },
  {
    key: 'tabs',
    scope: 'core-global',
    selectedProfileSupport: 'partial',
    symbol: 'T',
    navTitle: 'Tabs',
    title: 'Tabs',
    kind: 'live',
    domain: 'browsing',
    groupKeys: ['tabs'],
  },
  {
    key: 'notifications',
    scope: 'profile',
    selectedProfileSupport: 'supported',
    symbol: 'N',
    navTitle: 'Notifications',
    title: 'Notifications',
    kind: 'live',
    domain: 'browsing',
    groupKeys: ['notifications'],
  },
  {
    key: 'advanced',
    scope: 'device',
    selectedProfileSupport: 'partial',
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
    scope: 'profile',
    selectedProfileSupport: 'partial',
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
    scope: 'account',
    selectedProfileSupport: 'not-applicable',
    symbol: 'AC',
    navTitle: 'Account',
    title: 'Account',
    kind: 'live',
    domain: 'identity',
    contentKind: 'account',
  },
  {
    key: 'profiles',
    scope: 'core-global',
    selectedProfileSupport: 'partial',
    symbol: 'P',
    navTitle: 'Profiles',
    title: 'Profiles',
    kind: 'live',
    domain: 'identity',
    contentKind: 'profiles',
  },
  {
    key: 'passwords',
    scope: 'core-global',
    selectedProfileSupport: 'host-only',
    symbol: 'PW',
    navTitle: 'Passwords',
    title: 'Passwords',
    kind: 'live',
    domain: 'identity',
    contentKind: 'passwords',
  },
  {
    key: 'saved-passwords',
    scope: 'core-global',
    selectedProfileSupport: 'host-only',
    symbol: 'SP',
    navTitle: 'Saved Passwords',
    title: 'Saved Passwords',
    kind: 'live',
    domain: 'identity',
    contentKind: 'saved-passwords',
  },

  {
    key: 'autofill',
    scope: 'core-global',
    selectedProfileSupport: 'host-only',
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
    scope: 'process',
    selectedProfileSupport: 'not-applicable',
    symbol: '\u2318',
    navTitle: 'Shortcuts',
    title: 'Keyboard shortcuts',
    kind: 'live',
    domain: 'productivity',
    contentKind: 'shortcuts',
  },
  {
    key: 'extensions',
    scope: 'profile',
    selectedProfileSupport: 'host-only',
    symbol: 'EX',
    navTitle: 'Extensions',
    title: 'Extensions',
    kind: 'live',
    domain: 'productivity',
    externalAction: 'openExtensions',
  },
  // Maho has no bookmark surface of its own, so Settings owns the import of
  // other browsers' bookmarks/profiles instead of linking to chrome://bookmarks.
  {
    key: 'import',
    scope: 'profile',
    selectedProfileSupport: 'host-only',
    symbol: 'IM',
    navTitle: 'Import data',
    title: 'Import browser data',
    kind: 'live',
    domain: 'productivity',
  },
  {
    key: 'maho-mini',
    scope: 'process',
    selectedProfileSupport: 'host-only',
    symbol: 'LA',
    navTitle: 'Maho Mini & routing',
    title: 'Maho Mini & routing',
    kind: 'live',
    domain: 'productivity',
    contentKind: 'maho-mini',
  },
  // Mail is a top-level navigation entry, not a row inside a Features pane.
  // It stays visible while Mail is disabled because it owns the enable toggle.
  // It belongs to the mail domain (alongside the mail detail panes), not
  // productivity, so the Mail section renders as its own nav section.
  {
    key: 'mail',
    scope: 'profile',
    selectedProfileSupport: 'host-only',
    symbol: 'M',
    navTitle: 'Mail',
    title: 'Mail',
    kind: 'live',
    domain: 'mail',
    contentKind: 'mail-overview',
  },
  {
    key: 'maho-ai',
    scope: 'profile',
    selectedProfileSupport: 'host-only',
    symbol: 'AI',
    navTitle: 'Models',
    title: 'Models',
    kind: 'live',
    domain: 'productivity',
    contentKind: 'ai',
  },
  {
    key: 'maho-ai-developers',
    scope: 'profile',
    selectedProfileSupport: 'host-only',
    symbol: 'DEV',
    navTitle: 'Developers',
    title: 'Developers',
    kind: 'live',
    domain: 'productivity',
    contentKind: 'ai-developers',
  },
  {
    key: 'billing',
    scope: 'account',
    selectedProfileSupport: 'not-applicable',
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
    scope: 'process',
    selectedProfileSupport: 'partial',
    symbol: 'C',
    navTitle: 'Privacy & content blocker',
    title: 'Privacy & content blocker',
    kind: 'live',
    domain: 'protection',
    contentKind: 'content-blocker',
  },
  {
    key: 'chromium-settings',
    scope: 'profile',
    selectedProfileSupport: 'host-only',
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
    scope: 'account',
    selectedProfileSupport: 'host-only',
    symbol: 'MA',
    navTitle: 'Mail accounts',
    title: 'Mail accounts',
    kind: 'live',
    domain: 'mail',
    contentKind: 'mail',
  },
  {
    key: 'mail-signatures',
    scope: 'account',
    selectedProfileSupport: 'host-only',
    symbol: 'MS',
    navTitle: 'Signatures',
    title: 'Email signatures',
    kind: 'live',
    domain: 'mail',
    contentKind: 'mail-signatures',
  },
  {
    key: 'mail-rules',
    scope: 'account',
    selectedProfileSupport: 'host-only',
    symbol: 'MR',
    navTitle: 'Rules & filters',
    title: 'Mail rules & filters',
    kind: 'live',
    domain: 'mail',
    contentKind: 'mail-rules',
  },
  {
    key: 'mail-calendar',
    scope: 'account',
    selectedProfileSupport: 'host-only',
    symbol: 'MC',
    navTitle: 'Calendar',
    title: 'Mail calendar settings',
    kind: 'live',
    domain: 'mail',
    contentKind: 'mail-calendar',
  },
  {
    key: 'mail-behavior',
    scope: 'account',
    selectedProfileSupport: 'host-only',
    symbol: 'MB',
    navTitle: 'Mail behavior',
    title: 'Mail behavior & layout',
    kind: 'live',
    domain: 'mail',
    contentKind: 'mail-behavior',
  },
  {
    key: 'mail-security',
    scope: 'account',
    selectedProfileSupport: 'host-only',
    symbol: 'MSY',
    navTitle: 'Mail security',
    title: 'PGP & S/MIME security',
    kind: 'live',
    domain: 'mail',
    contentKind: 'mail-security',
  },
];

const CORE_PANE_DEFINITIONS: PaneDefinition[] = [
  ...BROWSING_PANES,
  ...LOOK_AND_FEEL_PANES,
  ...IDENTITY_PANES,
  ...PRODUCTIVITY_PANES,
  ...PROTECTION_PANES,
];

// The "Maho Mail" settings section of detail panes only exists when the opt-in
// maho.mail.enabled pref is on. The always-visible top-level Mail pane owns its
// toggle; the sidebar button, chrome://maho-mail, and helper subprocess read
// the same pref. app.tsx computes the nav from live settings rather than a
// build-time constant and skips domains with no panes.
export function panesForSettings(mailEnabled: boolean): PaneDefinition[] {
  return mailEnabled ? [...CORE_PANE_DEFINITIONS, ...MAIL_PANES] :
                       CORE_PANE_DEFINITIONS;
}

// Every pane that can ever exist, used for key lookup and deep links. Callers
// that render navigation must use panesForSettings() so a deferred Mail does
// not appear.
export const ALL_PANE_DEFINITIONS: PaneDefinition[] = [
  ...CORE_PANE_DEFINITIONS,
  ...MAIL_PANES,
];

export const PANE_DEFINITIONS = CORE_PANE_DEFINITIONS;

export const PANE_DEFINITION_MAP: Record<string, PaneDefinition> =
    Object.fromEntries(ALL_PANE_DEFINITIONS.map(d => [d.key, d])) as
        Record<string, PaneDefinition>;

const PANE_ALIASES: Readonly<Record<string, string>> = {
  'account-sync': 'account',
  'privacy': 'content-blocker',
  // Mail used to be buried in the Features pane; keep old deep links working.
  'features': 'mail',
};

export function resolveReachablePaneKey(key: string, mailEnabled: boolean): string {
  const canonicalKey = PANE_ALIASES[key] ?? key;
  const pane = PANE_DEFINITION_MAP[canonicalKey];
  const reachable = panesForSettings(mailEnabled).some(candidate => candidate.key === canonicalKey);
  return pane && pane.kind !== 'hidden' && reachable ? canonicalKey : PANE_DEFINITIONS[0]!.key;
}
