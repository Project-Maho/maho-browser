import type {ServiceMetadata, SettingDefinition, SettingMetadata} from '../models.js';

export const RESTORE_ON_STARTUP_OPTIONS = [
  {value: '5', label: 'Open the New Tab page', displayLabel: 'New tab'},
  {value: '1', label: 'Continue where you left off', displayLabel: 'Resume'},
  {value: '4', label: 'Open a specific set of pages', displayLabel: 'Specific pages'},
];

export const THEME_OPTIONS = [
  {value: '0', label: 'Match system', displayLabel: 'System'},
  {value: '1', label: 'Light'},
  {value: '2', label: 'Dark'},
];

export const ARCHIVE_TIMEOUT_OPTIONS = [
  {value: '-1', label: 'Off'},
  {value: '12', label: '12 hours'},
  {value: '24', label: '24 hours'},
  {value: '168', label: '7 days'},
  {value: '720', label: '30 days'},
  {value: '1440', label: '60 days'},
  {value: '2160', label: '90 days'},
];

export const TODAY_TAB_TIMEOUT_OPTIONS = [
  {value: '6', label: '6 hours'},
  {value: '12', label: '12 hours'},
  {value: '24', label: '24 hours'},
  {value: '48', label: '48 hours'},
];

export const PINNED_CLOSE_BEHAVIOR_OPTIONS = [
  {value: 'switch', label: 'Switch to the next tab'},
  {value: 'reset', label: 'Reset the pinned tab'},
  {value: 'reset-switch', label: 'Reset, then switch to the next tab'},
  {value: 'unload-switch', label: 'Unload, then switch to the next tab'},
  {value: 'reset-unload-switch', label: 'Reset, unload, then switch to the next tab'},
  {value: 'close', label: 'Close the pinned tab'},
];

export const TRANSLATE_LANGUAGE_OPTIONS = [
  {value: '', label: 'Automatic (match your preferred language)'},
  {value: 'en', label: 'English'},
  {value: 'ko', label: 'Korean'},
  {value: 'ja', label: 'Japanese'},
  {value: 'zh-CN', label: 'Chinese (Simplified)'},
  {value: 'zh-TW', label: 'Chinese (Traditional)'},
  {value: 'es', label: 'Spanish'},
  {value: 'fr', label: 'French'},
  {value: 'de', label: 'German'},
  {value: 'it', label: 'Italian'},
  {value: 'pt', label: 'Portuguese'},
  {value: 'ru', label: 'Russian'},
  {value: 'ar', label: 'Arabic'},
  {value: 'hi', label: 'Hindi'},
  {value: 'vi', label: 'Vietnamese'},
  {value: 'th', label: 'Thai'},
  {value: 'id', label: 'Indonesian'},
  {value: 'nl', label: 'Dutch'},
  {value: 'pl', label: 'Polish'},
  {value: 'tr', label: 'Turkish'},
  {value: 'uk', label: 'Ukrainian'},
];

export const SETTING_DEFINITIONS: SettingDefinition[] = [
  {
    key: 'general.homepage',
    metadata: {
      scope: 'profile',
      selectedProfile: false,
      label: 'Home page',
      description: 'The page Maho should treat as your default starting point.',
      control: 'text',
      placeholder: 'https://example.com or chrome://newtab',
    },
  },
  {
    key: 'general.restore_on_startup',
    metadata: {
      scope: 'profile',
      selectedProfile: false,
      label: 'On startup',
      description: 'Choose whether Maho restores your last session or opens the pages saved for startup.',
      control: 'select',
      options: RESTORE_ON_STARTUP_OPTIONS,
      segmented: true,
    },
  },
  {
    key: 'general.prompt_for_download',
    metadata: {
      scope: 'profile',
      selectedProfile: true,
      label: 'Ask where to save each file before downloading',
      description: 'Show a save dialog for each download instead of automatically using the default download location.',
      control: 'toggle',
    },
  },
  {
    key: 'general.translate_target_language',
    metadata: {
      scope: 'profile',
      selectedProfile: true,
      label: 'Translate pages into',
      description: 'The language Maho translates web pages into. Right-click a page and choose "Translate this page" to translate it into this language.',
      control: 'select',
      options: TRANSLATE_LANGUAGE_OPTIONS,
    },
  },
  {
    key: 'search.suggestions',
    metadata: {
      scope: 'profile',
      selectedProfile: true,
      label: 'Search suggestions',
      description: 'Show suggested queries while typing in supported search surfaces.',
      control: 'toggle',
    },
  },
  {
    key: 'appearance.theme',
    metadata: {
      scope: 'profile',
      selectedProfile: false,
      label: 'Browser theme',
      description: 'Choose whether the browsing environment — the content area and web pages — follows the system appearance or stays light or dark. The sidebar follows its Space theme separately.',
      control: 'select',
      options: THEME_OPTIONS,
      segmented: true,
    },
  },
  {
    key: 'appearance.sidebar_width',
    metadata: {
      scope: 'profile',
      selectedProfile: false,
      label: 'Sidebar width',
      description: 'Adjust the default width of the sidebar panel in pixels.',
      control: 'slider',
      min: 180,
      max: 400,
      step: 1,
      unit: 'px',
    },
  },
  {
    key: 'appearance.density',
    metadata: {
      scope: 'core-global',
      selectedProfile: false,
      label: 'Interface density',
      description: 'Adjust the padding and layout spacing of browser controls.',
      control: 'select',
      options: [
        {value: 'comfortable', label: 'Comfortable'},
        {value: 'compact', label: 'Compact'},
      ],
      segmented: true,
    },
  },
  {
    key: 'privacy.do_not_track',
    metadata: {
      scope: 'profile',
      selectedProfile: false,
      label: 'Send “Do Not Track”',
      description: 'Ask sites not to track browsing activity where they choose to honor the request.',
      control: 'toggle',
    },
  },
  {
    key: 'privacy.safe_browsing',
    metadata: {
      scope: 'profile',
      selectedProfile: false,
      label: 'Safe browsing',
      description: 'Warn about known dangerous sites and downloads before they become a problem.',
      control: 'toggle',
    },
  },
  {
    key: 'privacy.block_third_party_cookies',
    metadata: {
      scope: 'profile',
      selectedProfile: false,
      label: 'Block third-party cookies',
      description: 'Block cookies set by other sites while you browse. When off, standard cookie controls stay disabled.',
      control: 'toggle',
    },
  },
  {
    key: 'advanced.hardware_acceleration',
    metadata: {
      scope: 'device',
      selectedProfile: false,
      label: 'Use hardware acceleration when available',
      description: 'Let Maho use your GPU for supported tasks. A restart may be required for changes to fully apply.',
      control: 'toggle',
    },
  },
  {
    key: 'advanced.preload_pages',
    metadata: {
      scope: 'profile',
      selectedProfile: false,
      label: 'Preload pages',
      description: 'Choose how aggressively Maho preloads pages to make navigation feel faster.',
      control: 'select',
      options: [
        {value: '2', label: 'Disabled'},
        {value: '0', label: 'Standard'},
        {value: '3', label: 'Extended'},
      ],
    },
  },
  {
    key: 'notifications.quiet_permission_ui',
    metadata: {
      scope: 'profile',
      selectedProfile: false,
      label: 'Use quieter notification permission prompts',
      description: 'Reduce interruptions by showing less intrusive notification permission requests on supported sites.',
      control: 'toggle',
    },
  },
  {
    key: 'tabs.archive_timeout',
    metadata: {
      scope: 'profile',
      selectedProfile: true,
      label: 'Auto-archive inactive tabs after',
      description: 'Tabs left inactive for this long are moved to the archive automatically. Set to Off to disable auto-archiving.',
      control: 'select',
      options: ARCHIVE_TIMEOUT_OPTIONS,
    },
  },
  {
    key: 'tabs.today_tab_timeout',
    metadata: {
      scope: 'core-global',
      selectedProfile: false,
      label: 'Keep tabs in Today for',
      description: 'How long recently active tabs stay in the Today section before ageing out.',
      control: 'select',
      options: TODAY_TAB_TIMEOUT_OPTIONS,
    },
  },
  {
    key: 'tabs.pinned_close_behavior',
    metadata: {
      scope: 'core-global',
      selectedProfile: false,
      label: 'When closing a pinned tab',
      description: 'Choose whether closing a pinned tab should switch away, reset its state, unload it, or fully close it.',
      control: 'select',
      options: PINNED_CLOSE_BEHAVIOR_OPTIONS,
    },
  },
  {
    key: 'tabs.auto_delete_empty_folders_on_tidy',
    metadata: {
      scope: 'core-global',
      selectedProfile: false,
      label: 'Automatically remove empty folders after Tab Tidy',
      description: 'When Tab Tidy runs, folders that end up with zero tabs are deleted.',
      control: 'toggle',
    },
  },
  {
    key: 'atc.maho_mini_global_shortcut_enabled',
    metadata: {
      scope: 'process',
      selectedProfile: false,
      label: 'Option+Command+N shortcut',
      description: 'Open a blank Maho Mini window with Option+Command+N.',
      control: 'toggle',
    },
  },
  {
    key: 'atc.maho_mini_click_override_enabled',
    metadata: {
      scope: 'process',
      selectedProfile: false,
      label: 'Command+Option+Click override',
      description: 'Command+Option+Click on links to open them in Maho Mini instead of a new tab.',
      control: 'toggle',
    },
  },
  {
    key: 'atc.open_external_links_in_maho_mini',
    metadata: {
      scope: 'process',
      selectedProfile: false,
      label: 'Open external links in Maho Mini',
      description: 'Open external links in Maho Mini from other applications.',
      control: 'toggle',
    },
  },
  {
    key: 'autofill.passwords_enabled',
    metadata: {
      scope: 'core-global',
      selectedProfile: false,
      label: 'Passwords',
      description: 'Enable password management to show saved passwords.',
      control: 'toggle',
    },
  },
  {
    key: 'autofill.password_provider',
    metadata: {
      scope: 'core-global',
      selectedProfile: false,
      label: 'Password provider',
      description: 'Choose the password provider used by Maho.',
      control: 'select',
    },
  },
  {
    key: 'mail.enabled',
    metadata: {
      scope: 'profile',
      selectedProfile: false,
      label: 'Maho Mail enabled',
      description: 'Internal runtime gate for the deferred Maho Mail surface.',
      control: 'toggle',
    },
  },
];

export const SETTING_METADATA: Record<string, SettingMetadata> = Object.fromEntries(
    SETTING_DEFINITIONS.map(({key, metadata}) => [key, metadata])) as Record<string, SettingMetadata>;

// Typed operations that are not represented by SettingValue rows still need
// authoritative ownership and selected-profile eligibility.
export const SERVICE_DEFINITIONS: readonly ServiceMetadata[] = [
  {key: 'profile.name', scope: 'profile', selectedProfile: true},
  {key: 'profile.avatar', scope: 'profile', selectedProfile: true},
  {key: 'profile.archive_timeout', scope: 'profile', selectedProfile: true},
  {key: 'search.default_engine', scope: 'profile', selectedProfile: true},
  {key: 'downloads.default_directory', scope: 'profile', selectedProfile: true},
  {key: 'updates.browser', scope: 'process', selectedProfile: false},
  {key: 'shortcuts.recording', scope: 'process', selectedProfile: false},
  {key: 'account.session', scope: 'account', selectedProfile: false},
  {key: 'billing.subscription', scope: 'account', selectedProfile: false},
  {key: 'content_blocker.engine', scope: 'process', selectedProfile: false},
  {key: 'passwords.vault', scope: 'core-global', selectedProfile: false},
  {key: 'autofill.data', scope: 'core-global', selectedProfile: false},
  {key: 'browser.site_data', scope: 'profile', selectedProfile: false},
  {key: 'browser.history', scope: 'profile', selectedProfile: false},
  {key: 'browser.extensions', scope: 'profile', selectedProfile: false},
  {key: 'browser.chromium_settings', scope: 'profile', selectedProfile: false},
] as const;

export const SERVICE_METADATA: Readonly<Record<string, ServiceMetadata>> =
    Object.fromEntries(SERVICE_DEFINITIONS.map(service => [service.key, service]));
