import type {AccentColor, SettingDefinition, SettingMetadata} from '../models.js';

export const RESTORE_ON_STARTUP_OPTIONS = [
  {value: '5', label: 'Open the New Tab page', displayLabel: 'New Tab'},
  {value: '1', label: 'Continue where you left off', displayLabel: 'Resume'},
  {value: '4', label: 'Open a specific set of pages', displayLabel: 'Specific Pages'},
];

export const THEME_OPTIONS = [
  {value: '0', label: 'Match system', displayLabel: 'System'},
  {value: '1', label: 'Light'},
  {value: '2', label: 'Dark'},
];

export const DENSITY_OPTIONS = [
  {value: 'compact', label: 'Compact'},
  {value: 'comfortable', label: 'Comfortable'},
];

export const ACCENT_COLORS: AccentColor[] = [
  {r: 0, g: 122, b: 255, label: 'Blue'},
  {r: 255, g: 59, b: 48, label: 'Red'},
  {r: 52, g: 199, b: 89, label: 'Green'},
  {r: 255, g: 204, b: 0, label: 'Yellow'},
  {r: 175, g: 82, b: 222, label: 'Purple'},
  {r: 255, g: 149, b: 0, label: 'Orange'},
];

export const SETTING_DEFINITIONS: SettingDefinition[] = [
  {
    key: 'general.homepage',
    metadata: {
      label: 'Home page',
      description: 'The page Maho should treat as your default starting point.',
      control: 'text',
      placeholder: 'https://example.com or chrome://newtab',
    },
  },
  {
    key: 'general.restore_on_startup',
    metadata: {
      label: 'On startup',
      description: 'Choose whether Maho opens the New Tab page, restores your last session, or opens the pages saved for startup.',
      control: 'select',
      options: RESTORE_ON_STARTUP_OPTIONS,
      segmented: true,
    },
  },
  {
    key: 'general.prompt_for_download',
    metadata: {
      label: 'Ask where to save each file before downloading',
      description: 'Show a save dialog for each download instead of automatically using the default download location.',
      control: 'toggle',
    },
  },
  {
    key: 'search.suggestions',
    metadata: {
      label: 'Search suggestions',
      description: 'Show suggested queries while typing in supported search surfaces.',
      control: 'toggle',
    },
  },
  {
    key: 'appearance.show_home_button',
    metadata: {
      label: 'Show Home button',
      description: 'Display a Home button in the toolbar so you can open your chosen home page with one click.',
      control: 'toggle',
    },
  },
  {
    key: 'appearance.font_size',
    metadata: {
      label: 'Default font size',
      description: 'Sets the baseline text size used when pages rely on browser defaults.',
      control: 'number',
      min: 9,
      max: 72,
      step: 1,
    },
  },
  {
    key: 'appearance.minimum_font_size',
    metadata: {
      label: 'Minimum font size',
      description: 'Prevent pages from rendering text smaller than this size, measured in pixels.',
      control: 'number',
      min: 1,
      max: 24,
      step: 1,
    },
  },
  {
    key: 'appearance.theme',
    metadata: {
      label: 'Browser theme',
      description: 'Choose whether the browser chrome follows the system appearance or stays light or dark.',
      control: 'select',
      options: THEME_OPTIONS,
      segmented: true,
    },
  },
  {
    key: 'appearance.accent_color',
    metadata: {
      label: 'Accent color',
      description: 'Choose a color to personalize the browser chrome and interactive elements.',
      control: 'color-swatch',
    },
  },
  {
    key: 'appearance.density',
    metadata: {
      label: 'Display density',
      description: 'Switch between a tighter or more spacious arrangement of controls and spacing.',
      control: 'select',
      options: DENSITY_OPTIONS,
    },
  },
  {
    key: 'appearance.sidebar_width',
    metadata: {
      label: 'Sidebar width',
      description: 'Adjust the default width of the sidebar panel in pixels.',
      control: 'slider',
      min: 180,
      max: 400,
      step: 1,
    },
  },
  {
    key: 'appearance.window_transparency',
    metadata: {
      label: 'Window transparency',
      description: 'Allow subtle transparency effects on the browser window background.',
      control: 'toggle',
    },
  },
  {
    key: 'appearance.custom_chrome_css',
    metadata: {
      label: 'Custom chrome CSS',
      description: 'Apply custom CSS rules to browser chrome surfaces. Changes take effect on reload.',
      control: 'textarea',
      placeholder: '/* e.g. --toolbar-bg: #1a1a2e; */',
    },
  },
  {
    key: 'privacy.do_not_track',
    metadata: {
      label: 'Send “Do Not Track”',
      description: 'Ask sites not to track browsing activity where they choose to honor the request.',
      control: 'toggle',
    },
  },
  {
    key: 'privacy.safe_browsing',
    metadata: {
      label: 'Safe Browsing',
      description: 'Warn about known dangerous sites and downloads before they become a problem.',
      control: 'toggle',
    },
  },
  {
    key: 'privacy.block_third_party_cookies',
    metadata: {
      label: 'Block third-party cookies',
      description: 'Block cookies set by other sites while you browse. When off, standard cookie controls stay disabled.',
      control: 'toggle',
    },
  },
  {
    key: 'advanced.hardware_acceleration',
    metadata: {
      label: 'Use hardware acceleration when available',
      description: 'Let Maho use your GPU for supported tasks. A restart may be required for changes to fully apply.',
      control: 'toggle',
    },
  },
  {
    key: 'advanced.preload_pages',
    metadata: {
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
      label: 'Use quieter notification permission prompts',
      description: 'Reduce interruptions by showing less intrusive notification permission requests on supported sites.',
      control: 'toggle',
    },
  },
];

export const SETTING_METADATA: Record<string, SettingMetadata> = Object.fromEntries(
    SETTING_DEFINITIONS.map(({key, metadata}) => [key, metadata])) as Record<string, SettingMetadata>;
