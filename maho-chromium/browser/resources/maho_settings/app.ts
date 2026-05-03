import {PageHandlerFactory, PageHandlerRemote, SettingValue} from './maho_settings.mojom-webui.js';
import {PageCallbackRouter} from './maho_settings.mojom-webui.js';
import {
  ACCENT_COLORS,
  SETTING_METADATA,
} from './schema/setting_schema.js';
import {
  LIVE_PANE_SECTION_MANIFEST,
  PANE_DEFINITIONS,
  PANE_DEFINITION_MAP,
} from './schema/panes.js';
import type {LivePaneSectionDefinition} from './schema/panes.js';
import type {
  ControlKind,
  PaneDefinition,
  PaneStatus,
  StructuredPlaceholderRow,
  SettingMetadata,
} from './models.js';

let pageHandler: PageHandlerRemote;
let callbackRouter: PageCallbackRouter;
let currentPaneKey = 'general';
let currentSettings: SettingValue[] = [];

const BROWSER_SETTINGS_URL = 'chrome://settings/';

function initialize() {
  callbackRouter = new PageCallbackRouter();
  pageHandler = new PageHandlerRemote();

  const factory = PageHandlerFactory.getRemote();
  factory.createPageHandler(
      callbackRouter.$.bindNewPipeAndPassRemote(),
      pageHandler.$.bindNewPipeAndPassReceiver());

  callbackRouter.settingsChanged.addListener((settings: SettingValue[]) => {
    currentSettings = settings;
    renderSettings(currentSettings);
  });

  pageHandler.getSettings().then(({settings}: {settings: SettingValue[]}) => {
    currentSettings = settings;
    renderSettings(currentSettings);
  });
}

function renderSettings(settings: SettingValue[]) {
  const container = document.querySelector<HTMLElement>('maho-settings-app');
  if (!container) {
    return;
  }

  currentPaneKey = getInitialPaneKey();
  container.replaceChildren(createPreferencesShell(settings));
}

function getInitialPaneKey(): string {
  return PANE_DEFINITION_MAP[currentPaneKey] ? currentPaneKey : PANE_DEFINITIONS[0]!.key;
}

function createPreferencesShell(settings: SettingValue[]): HTMLElement {
  const shell = document.createElement('div');
  shell.className = 'preferences-shell';

  const windowShell = document.createElement('div');
  windowShell.className = 'preferences-window';

  const workspace = document.createElement('div');
  workspace.className = 'preferences-workspace';
  workspace.append(createSidebar(), createContentPane(settings));

  windowShell.appendChild(workspace);
  shell.appendChild(windowShell);
  return shell;
}

function createSidebar(): HTMLElement {
  const sidebar = document.createElement('aside');
  sidebar.className = 'preferences-sidebar';

  const scroll = document.createElement('div');
  scroll.className = 'preferences-sidebar__scroll';

  const brand = document.createElement('section');
  brand.className = 'sidebar-brand-card';

  const brandKicker = document.createElement('div');
  brandKicker.className = 'sidebar-brand-kicker';
  brandKicker.textContent = 'Hybrid direction';

  const brandTitle = document.createElement('h1');
  brandTitle.className = 'sidebar-title';
  brandTitle.textContent = 'Settings';

  brand.append(brandKicker, brandTitle);

  const nav = document.createElement('nav');
  nav.className = 'sidebar-navigation';
  nav.setAttribute('aria-label', 'Settings panes');

  for (const pane of PANE_DEFINITIONS) {
    nav.appendChild(createPaneButton(pane));
  }

  scroll.append(brand, nav);
  sidebar.appendChild(scroll);
  return sidebar;
}

function createPaneButton(pane: PaneDefinition): HTMLButtonElement {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'sidebar-nav-button';
  button.dataset['pane'] = pane.key;
  if (pane.key === currentPaneKey) {
    button.classList.add('is-active');
    button.setAttribute('aria-current', 'page');
  }

  const badge = document.createElement('span');
  badge.className = 'sidebar-nav-button__badge';
  badge.textContent = pane.symbol;

  const text = document.createElement('span');
  text.className = 'sidebar-nav-button__text';

  const label = document.createElement('span');
  label.className = 'sidebar-nav-button__label';
  label.textContent = pane.navTitle;

  const detail = document.createElement('span');
  detail.className = 'sidebar-nav-button__detail';
  detail.textContent = pane.navMeta;

  text.append(label, detail);
  button.append(badge, text);
  button.addEventListener('click', () => {
    if (currentPaneKey === pane.key) {
      return;
    }
    currentPaneKey = pane.key;
    renderSettings(currentSettings);
  });
  return button;
}

function createContentPane(settings: SettingValue[]): HTMLElement {
  const content = document.createElement('main');
  content.className = 'preferences-content';

  const scroll = document.createElement('div');
  scroll.className = 'preferences-content__scroll';

  const pane = PANE_DEFINITION_MAP[currentPaneKey] || PANE_DEFINITIONS[0]!;
  if (pane.kind === 'embed') {
    scroll.appendChild(createBrowserPane(pane));
  } else if (pane.kind === 'placeholder') {
    scroll.appendChild(createPlaceholderPane(pane));
  } else {
    scroll.appendChild(createLivePane(pane, settings));
  }

  content.appendChild(scroll);
  return content;
}

function createLivePane(pane: PaneDefinition, settings: SettingValue[]): HTMLElement {
  const section = createPaneSectionShell(pane);
  const card = section.querySelector('.preferences-card');
  if (!card) {
    return section;
  }

  const grouped = groupSettings(settings);
  const relevantSettings = (pane.groupKeys || [pane.key])
                               .flatMap(groupKey => grouped[groupKey] || []);

  card.appendChild(createOverviewBanner(pane));

  const groupedCards = buildLivePaneCards(pane, relevantSettings);
  const grid = document.createElement('div');
  grid.className = 'cards-grid';
  for (const paneCard of groupedCards) {
    grid.appendChild(paneCard);
  }
  card.appendChild(grid);

  return section;
}

function buildLivePaneCards(pane: PaneDefinition, settings: SettingValue[]): HTMLElement[] {
  const sections = LIVE_PANE_SECTION_MANIFEST[pane.key];
  if (!sections) {
    return createSingleLiveCard(pane.title, pane.summary, settings);
  }

  const cards: HTMLElement[] = [];
  for (const section of sections) {
    cards.push(createLivePaneCardFromSection(section, settings));
  }
  return cards;
}

function createLivePaneCardFromSection(
    section: LivePaneSectionDefinition, settings: SettingValue[]): HTMLElement {
  if (section.kind === 'live-card') {
    const card = createContentCard(
        section.card.title,
        section.card.description,
        selectSettings(settings, section.card.settingKeys),
        section.card.badgeTone);
    if (section.card.wide) {
      card.classList.add('content-card--wide');
    }
    return card;
  }

  return createStructuredPlaceholderCard(
      section.card.title,
      section.card.description,
      section.card.rows,
      section.card.badgeTone);
}

function createSingleLiveCard(
    title: string, description: string, settings: SettingValue[]): HTMLElement[] {
  return [createContentCard(title, description, settings, 'live')];
}

function selectSettings(settings: SettingValue[], keys: string[]): SettingValue[] {
  const byKey = new Map(settings.map(setting => [setting.key, setting]));
  return keys.map(key => byKey.get(key)).filter(Boolean) as SettingValue[];
}

function createOverviewBanner(pane: PaneDefinition): HTMLElement {
  const banner = document.createElement('div');
  banner.className = 'overview-banner';

  const title = document.createElement('div');
  title.className = 'overview-banner__title';
  title.textContent = pane.overviewTitle || 'How this pane should work';

  const copy = document.createElement('p');
  copy.className = 'overview-banner__copy';
  copy.textContent = pane.overviewCopy || pane.summary;

  banner.append(title, copy);
  return banner;
}

function createPaneIntroCluster(...elements: HTMLElement[]): HTMLElement {
  const cluster = document.createElement('div');
  cluster.className = 'pane-intro-cluster';
  cluster.append(...elements);
  return cluster;
}

function createPlaceholderPane(pane: PaneDefinition): HTMLElement {
  const section = createPaneSectionShell(pane);
  const card = section.querySelector('.preferences-card');
  if (!card) {
    return section;
  }

  const overview = document.createElement('div');
  overview.className = 'pane-placeholder-banner';

  const reason = document.createElement('div');
  reason.className = 'pane-placeholder-banner__reason';
  reason.textContent = pane.placeholderReason || 'Needs additional backend work';

  const detail = document.createElement('p');
  detail.className = 'pane-placeholder-banner__detail';
  detail.textContent = pane.placeholderDetail || pane.summary;

  overview.append(reason, detail);
  card.appendChild(createPaneIntroCluster(overview, createOverviewBanner(pane)));

  const grid = document.createElement('div');
  grid.className = 'cards-grid';
  grid.appendChild(createPlaceholderCard(
      'What this pane should eventually contain',
      'The sidebar follows the mockup now, and this placeholder keeps the information architecture visible until the corresponding service bridge is ready.',
      pane.placeholderHighlights || [],
      pane.navMeta.includes('service API') ? 'api' : 'bridge'));
  grid.appendChild(createPlaceholderCard(
      'What still blocks it',
      pane.placeholderDetail || pane.summary,
      [pane.placeholderReason || 'Additional backend work required'],
      pane.navMeta.includes('service API') ? 'api' : 'bridge'));
  card.appendChild(grid);

  return section;
}

function createBrowserPane(pane: PaneDefinition): HTMLElement {
  const section = createPaneSectionShell(pane);
  const card = section.querySelector('.preferences-card');
  if (!card) {
    return section;
  }

  card.classList.add('preferences-card--browser');

  const overview = createOverviewBanner(pane);
  overview.classList.add('overview-banner--embedded');
  const title = overview.querySelector('.overview-banner__title');
  if (title) {
    title.textContent = 'Embedded Chromium distinction';
  }
  const copy = overview.querySelector('.overview-banner__copy');
  if (copy) {
    copy.textContent =
        'The outer shell follows the mockup, while the inner embedded chrome://settings document remains Chromium-native.';
  }
  card.appendChild(overview);

  const browserShell = document.createElement('div');
  browserShell.className = 'browser-settings-shell browser-settings-shell--wide';

  const browserView = document.createElement('iframe');
  browserView.className = 'browser-settings-webview';
  browserView.setAttribute('role', 'document');
  browserView.setAttribute('src', BROWSER_SETTINGS_URL);
  browserView.addEventListener('load', () => {
    console.info('Embedded browser settings iframe loaded.');
  });
  browserView.addEventListener('error', event => {
    console.warn('Failed to load embedded browser settings iframe.', event);
  });

  browserShell.appendChild(browserView);
  const grid = document.createElement('div');
  grid.className = 'cards-grid';

  grid.appendChild(browserShell);

  card.appendChild(grid);
  return section;
}

function createPaneSectionShell(pane: PaneDefinition): HTMLElement {
  const section = document.createElement('section');
  section.className = 'preferences-section';
  section.id = getSectionId(pane.key);
  section.dataset['pane'] = pane.key;

  const card = document.createElement('div');
  card.className = 'preferences-card preferences-card--pane-shell';

  const header = document.createElement('header');
  header.className = 'preferences-section__header';

  const eyebrow = document.createElement('div');
  eyebrow.className = 'preferences-section__eyebrow';
  eyebrow.textContent = pane.eyebrow;

  const title = document.createElement('h2');
  title.className = 'preferences-section__title';
  title.textContent = pane.title;

  const summary = document.createElement('p');
  summary.className = 'preferences-section__summary';
  summary.textContent = pane.summary;

  const badges = document.createElement('div');
  badges.className = 'preferences-section__badges';
  const badgeDefinitions = pane.heroBadges ||
      [{label: getStatusBadgeLabel(pane.status), tone: getStatusBadgeTone(pane.status)}];
  for (const badgeDefinition of badgeDefinitions) {
    badges.appendChild(createStatusBadge(badgeDefinition.label, badgeDefinition.tone));
  }

  header.append(eyebrow, title, summary, badges);
  card.appendChild(header);
  section.appendChild(card);
  return section;
}

function createStatusBadge(label: string, tone: 'live'|'mixed'|'bridge'|'api'): HTMLElement {
  const badge = document.createElement('span');
  badge.className = `status-badge status-badge--${tone}`;
  badge.textContent = label;
  return badge;
}

function getStatusBadgeLabel(status: PaneStatus): string {
  switch (status) {
    case 'live-pref':
      return 'Live via Chromium prefs';
    case 'mixed':
      return 'Live + future-state';
    case 'live-embed':
      return 'Embedded browser page';
    case 'placeholder':
    default:
      return 'Planned future-state';
  }
}

function getStatusBadgeTone(status: PaneStatus): 'live'|'mixed'|'bridge'|'api' {
  switch (status) {
    case 'live-pref':
      return 'live';
    case 'mixed':
      return 'mixed';
    case 'live-embed':
      return 'api';
    case 'placeholder':
    default:
      return 'bridge';
  }
}

function createContentCard(
    title: string, description: string, settings: SettingValue[], badgeTone: 'live'|'mixed'|'bridge'|'api'): HTMLElement {
  const card = document.createElement('article');
  card.className = 'content-card';

  const header = document.createElement('div');
  header.className = 'content-card__header';

  const titleGroup = document.createElement('div');
  titleGroup.className = 'content-card__title-group';

  const heading = document.createElement('h3');
  heading.className = 'content-card__title';
  heading.textContent = title;

  const copy = document.createElement('p');
  copy.className = 'content-card__copy';
  copy.textContent = description;

  titleGroup.append(heading, copy);
  header.append(titleGroup, createStatusBadge(getCardBadgeLabel(badgeTone), badgeTone));

  const rows = document.createElement('div');
  rows.className = 'preferences-card__rows';
  settings.forEach((setting, index) => rows.appendChild(createSettingRow(setting, index === 0)));

  card.append(header, rows);
  return card;
}

function getCardBadgeLabel(tone: 'live'|'mixed'|'bridge'|'api'): string {
  switch (tone) {
    case 'live':
      return 'Live via Chromium prefs';
    case 'api':
      return 'Needs service API';
    case 'bridge':
      return 'Needs Maho bridge';
    case 'mixed':
    default:
      return 'Live + future-state';
  }
}

function createPlaceholderCard(
    title: string, description: string, highlights: string[], tone: 'bridge'|'api'): HTMLElement {
  const card = document.createElement('article');
  card.className = 'content-card content-card--placeholder';

  const header = document.createElement('div');
  header.className = 'content-card__header';

  const titleGroup = document.createElement('div');
  titleGroup.className = 'content-card__title-group';

  const heading = document.createElement('h3');
  heading.className = 'content-card__title';
  heading.textContent = title;

  const copy = document.createElement('p');
  copy.className = 'content-card__copy';
  copy.textContent = description;

  titleGroup.append(heading, copy);
  header.append(titleGroup, createStatusBadge(
      tone === 'api' ? 'Needs service API' : 'Needs Maho bridge', tone));

  const list = document.createElement('ul');
  list.className = 'placeholder-list';
  for (const highlight of highlights) {
    const item = document.createElement('li');
    item.className = 'placeholder-list__item';
    item.textContent = highlight;
    list.appendChild(item);
  }

  card.append(header, list);
  return card;
}

function createStructuredPlaceholderCard(
    title: string, description: string, rows: StructuredPlaceholderRow[], badgeTone: 'bridge'|'api'): HTMLElement {
  const card = document.createElement('article');
  card.className = 'content-card content-card--placeholder';

  const header = document.createElement('div');
  header.className = 'content-card__header';

  const titleGroup = document.createElement('div');
  titleGroup.className = 'content-card__title-group';

  const heading = document.createElement('h3');
  heading.className = 'content-card__title';
  heading.textContent = title;

  const copy = document.createElement('p');
  copy.className = 'content-card__copy';
  copy.textContent = description;

  titleGroup.append(heading, copy);
  header.append(titleGroup, createStatusBadge(getCardBadgeLabel(badgeTone), badgeTone));

  const rowsContainer = document.createElement('div');
  rowsContainer.className = 'preferences-card__rows';
  rows.forEach((row, index) => rowsContainer.appendChild(createStructuredPlaceholderRow(row, index === 0)));

  card.append(header, rowsContainer);
  return card;
}

function createStructuredPlaceholderRow(rowDefinition: StructuredPlaceholderRow, isFirstRow: boolean): HTMLElement {
  const row = document.createElement('section');
  row.className = 'setting-row setting-row--placeholder';
  if (isFirstRow) {
    row.classList.add('setting-row--first');
  }

  const copy = document.createElement('div');
  copy.className = 'setting-row__copy';

  const title = document.createElement('h4');
  title.className = 'setting-row__title';
  title.textContent = rowDefinition.label;

  const description = document.createElement('p');
  description.className = 'setting-row__description';
  description.append(rowDefinition.description, createStatusInlineBadge(rowDefinition.badgeLabel, rowDefinition.tone));

  copy.append(title, description);

  const controlArea = document.createElement('div');
  controlArea.className = 'setting-row__control';
  controlArea.appendChild(createDisabledPlaceholderControl(rowDefinition));

  row.append(copy, controlArea);
  return row;
}

function createStatusInlineBadge(label: string, tone: 'live'|'bridge'|'api'): HTMLElement {
  const badge = document.createElement('span');
  badge.className = `setting-inline-badge setting-inline-badge--${tone}`;
  badge.textContent = label;
  return badge;
}

function createDisabledPlaceholderControl(rowDefinition: StructuredPlaceholderRow): HTMLElement {
  switch (rowDefinition.control) {
    case 'toggle':
      return createDisabledToggleControl(rowDefinition);
    case 'select':
      return createDisabledSelectControl(rowDefinition);
    case 'slider':
      return createDisabledSliderControl(rowDefinition);
    case 'textarea':
      return createDisabledTextareaControl(rowDefinition);
    case 'number':
    case 'text':
    default:
      return createDisabledInputControl(rowDefinition);
  }
}

function createDisabledToggleControl(rowDefinition: StructuredPlaceholderRow): HTMLElement {
  const enabled = rowDefinition.value === 'true';
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'setting-switch is-disabled';
  button.disabled = true;
  if (enabled) {
    button.classList.add('is-on');
  }

  const track = document.createElement('span');
  track.className = 'setting-switch__track';
  const thumb = document.createElement('span');
  thumb.className = 'setting-switch__thumb';
  track.appendChild(thumb);

  const status = document.createElement('span');
  status.className = 'setting-switch__status';
  status.textContent = enabled ? 'On' : 'Off';

  button.append(track, status);
  return button;
}

function createDisabledInputControl(rowDefinition: StructuredPlaceholderRow): HTMLElement {
  const wrapper = document.createElement('label');
  wrapper.className = 'setting-input-shell is-disabled';

  const input = document.createElement('input');
  input.className = 'setting-input';
  input.type = rowDefinition.control === 'number' ? 'number' : 'text';
  input.value = rowDefinition.value || '';
  input.placeholder = rowDefinition.placeholder || '';
  input.disabled = true;

  wrapper.appendChild(input);

  if (rowDefinition.control === 'number') {
    const suffix = document.createElement('span');
    suffix.className = 'setting-input-shell__suffix';
    suffix.textContent = 'px';
    wrapper.appendChild(suffix);
  }

  return wrapper;
}

function createDisabledSelectControl(rowDefinition: StructuredPlaceholderRow): HTMLElement {
  const options = rowDefinition.options || [];

  if (rowDefinition.segmented) {
    const segmented = document.createElement('div');
    segmented.className = 'setting-segmented is-disabled';

    for (const option of options) {
      const button = document.createElement('button');
      button.type = 'button';
      button.className = 'setting-segment';
      button.textContent = option.displayLabel || option.label;
      button.disabled = true;
      if (option.value === rowDefinition.value) {
        button.classList.add('is-active');
      }
      segmented.appendChild(button);
    }

    return segmented;
  }

  const wrapper = document.createElement('label');
  wrapper.className = 'setting-input-shell setting-input-shell--select is-disabled';

  const select = document.createElement('select');
  select.className = 'setting-input setting-select';
  select.disabled = true;

  for (const option of options) {
    const optionElement = document.createElement('option');
    optionElement.value = option.value;
    optionElement.textContent = option.label;
    if (option.value === rowDefinition.value) {
      optionElement.selected = true;
    }
    select.appendChild(optionElement);
  }

  const suffix = document.createElement('span');
  suffix.className = 'setting-input-shell__suffix setting-input-shell__suffix--icon';
  suffix.textContent = '▾';
  suffix.setAttribute('aria-hidden', 'true');

  wrapper.append(select, suffix);
  return wrapper;
}

function createDisabledSliderControl(rowDefinition: StructuredPlaceholderRow): HTMLElement {
  const wrapper = document.createElement('div');
  wrapper.className = 'setting-slider-shell is-disabled';

  const slider = document.createElement('input');
  slider.type = 'range';
  slider.className = 'setting-slider';
  slider.disabled = true;
  slider.min = String(rowDefinition.min ?? 0);
  slider.max = String(rowDefinition.max ?? 100);
  slider.step = String(rowDefinition.step ?? 1);
  slider.value = rowDefinition.value || String(rowDefinition.min ?? 0);

  const value = document.createElement('span');
  value.className = 'setting-slider__value';
  value.textContent = `${slider.value}px`;

  wrapper.append(slider, value);
  return wrapper;
}

function createDisabledTextareaControl(rowDefinition: StructuredPlaceholderRow): HTMLElement {
  const wrapper = document.createElement('div');
  wrapper.className = 'setting-textarea-shell';

  const textarea = document.createElement('textarea');
  textarea.className = 'setting-textarea is-disabled';
  textarea.disabled = true;
  textarea.value = rowDefinition.value || '';
  textarea.rows = 4;

  wrapper.appendChild(textarea);
  return wrapper;
}

function createSettingRow(item: SettingValue, isFirstRow: boolean): HTMLElement {
  const meta = getSettingMetadata(item);
  const row = document.createElement('section');
  row.className = 'setting-row';
  if (isFirstRow) {
    row.classList.add('setting-row--first');
  }

  const copy = document.createElement('div');
  copy.className = 'setting-row__copy';

  const title = document.createElement('h4');
  title.className = 'setting-row__title';
  title.textContent = meta.label;

  const description = document.createElement('p');
  description.className = 'setting-row__description';
  description.append(meta.description, createInlineSettingBadge(item.key));

  copy.append(title, description);

  const controlArea = document.createElement('div');
  controlArea.className = 'setting-row__control';
  controlArea.appendChild(createControl(item, meta));

  row.append(copy, controlArea);
  return row;
}

function createControl(item: SettingValue, meta: SettingMetadata): HTMLElement {
  const creator = CONTROL_CREATORS[meta.control] || createInputControl;
  return creator(item, meta);
}

function createColorSwatchControl(item: SettingValue): HTMLElement {
  const container = document.createElement('div');
  container.className = 'setting-color-swatches';

  let currentColor: {r: number; g: number; b: number} | null = null;
  try {
    if (item.value) {
      currentColor = JSON.parse(item.value);
    }
  } catch {
    currentColor = null;
  }

  for (const color of ACCENT_COLORS) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'setting-color-swatch';
    button.setAttribute('aria-label', color.label);
    button.style.setProperty('--swatch-color', `rgb(${color.r}, ${color.g}, ${color.b})`);

    const isActive = !!currentColor && currentColor.r === color.r &&
        currentColor.g === color.g && currentColor.b === color.b;
    if (isActive) {
      button.classList.add('is-active');
    }

    button.addEventListener('click', async () => {
      const value = JSON.stringify({r: color.r, g: color.g, b: color.b, a: 1.0});
      await commitSettingValue(item.key, value, button);
    });

    container.appendChild(button);
  }

  return container;
}

function createSliderControl(item: SettingValue, meta: SettingMetadata): HTMLElement {
  const wrapper = document.createElement('div');
  wrapper.className = 'setting-slider-shell';

  const input = document.createElement('input');
  input.type = 'range';
  input.className = 'setting-slider';
  input.value = item.value || String(meta.min || 0);
  input.setAttribute('aria-label', meta.label);
  if (meta.min !== undefined) input.min = String(meta.min);
  if (meta.max !== undefined) input.max = String(meta.max);
  if (meta.step !== undefined) input.step = String(meta.step);

  const valueLabel = document.createElement('span');
  valueLabel.className = 'setting-slider__value';
  valueLabel.textContent = `${input.value}px`;

  let debounceTimer: ReturnType<typeof setTimeout> | null = null;
  input.addEventListener('input', () => {
    valueLabel.textContent = `${input.value}px`;
    if (debounceTimer) {
      clearTimeout(debounceTimer);
    }
    debounceTimer = setTimeout(() => {
      void commitSettingValue(item.key, input.value, input);
    }, 150);
  });

  wrapper.append(input, valueLabel);
  return wrapper;
}

function createTextareaControl(item: SettingValue, meta: SettingMetadata): HTMLElement {
  const wrapper = document.createElement('div');
  wrapper.className = 'setting-textarea-shell';

  const textarea = document.createElement('textarea');
  textarea.className = 'setting-textarea';
  textarea.value = item.value || '';
  textarea.placeholder = meta.placeholder || '';
  textarea.rows = 4;
  textarea.setAttribute('aria-label', meta.label);
  textarea.spellcheck = false;
  textarea.addEventListener('blur', () => {
    void commitSettingValue(item.key, textarea.value, textarea);
  });

  wrapper.appendChild(textarea);
  return wrapper;
}

const CONTROL_CREATORS: Record<ControlKind, (item: SettingValue, meta: SettingMetadata) => HTMLElement> = {
  text: createInputControl,
  number: createInputControl,
  toggle: createToggleControl,
  select: createSelectControl,
  'color-swatch': createColorSwatchControl,
  slider: createSliderControl,
  textarea: createTextareaControl,
};

async function commitSettingValue(
    key: string, value: string,
    control: HTMLButtonElement|HTMLInputElement|HTMLSelectElement|HTMLTextAreaElement) {
  control.disabled = true;
  const {success} = await pageHandler.setSetting(key, value);
  control.disabled = false;
  if (!success) {
    console.warn(`Unable to update setting: ${key}`);
  }
}

function createToggleControl(item: SettingValue, meta: SettingMetadata): HTMLElement {
  const enabled = item.value === 'true';
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'setting-switch';
  button.setAttribute('role', 'switch');
  button.setAttribute('aria-checked', String(enabled));
  button.setAttribute('aria-label', meta.label);

  if (enabled) {
    button.classList.add('is-on');
  }

  const track = document.createElement('span');
  track.className = 'setting-switch__track';

  const thumb = document.createElement('span');
  thumb.className = 'setting-switch__thumb';

  const status = document.createElement('span');
  status.className = 'setting-switch__status';
  status.textContent = enabled ? 'On' : 'Off';

  track.appendChild(thumb);
  button.append(track, status);
  button.addEventListener('click', async () => {
    await commitSettingValue(item.key, enabled ? 'false' : 'true', button);
  });

  return button;
}

function createInputControl(item: SettingValue, meta: SettingMetadata): HTMLElement {
  const wrapper = document.createElement('label');
  wrapper.className = 'setting-input-shell';

  const input = document.createElement('input');
  input.className = 'setting-input';
  input.value = item.value;
  input.placeholder = meta.placeholder || '';
  input.type = meta.control === 'number' ? 'number' : 'text';
  input.setAttribute('aria-label', meta.label);

  if (meta.control === 'number') {
    input.inputMode = 'numeric';
    if (meta.min !== undefined) input.min = String(meta.min);
    if (meta.max !== undefined) input.max = String(meta.max);
    if (meta.step !== undefined) input.step = String(meta.step);
  }

  const commitValue = async () => {
    const nextValue = normalizeInputValue(input.value, item.value, meta);
    input.value = nextValue;
    await commitSettingValue(item.key, nextValue, input);
  };

  input.addEventListener('change', () => {
    void commitValue();
  });
  input.addEventListener('keydown', (event: KeyboardEvent) => {
    if (event.key === 'Enter') {
      input.blur();
    }
  });

  wrapper.appendChild(input);

  if (meta.control === 'number') {
    const suffix = document.createElement('span');
    suffix.className = 'setting-input-shell__suffix';
    suffix.textContent = 'px';
    wrapper.appendChild(suffix);
  }

  return wrapper;
}

function createSelectControl(item: SettingValue, meta: SettingMetadata): HTMLElement {
  if (meta.segmented) {
    return createSegmentedControl(item, meta);
  }

  const wrapper = document.createElement('div');
  wrapper.className = 'setting-input-shell setting-input-shell--select';

  const select = document.createElement('select');
  select.className = 'setting-input setting-select';
  select.setAttribute('aria-label', meta.label);

  const options = meta.options || [];
  for (const option of options) {
    const optionElement = document.createElement('option');
    optionElement.value = option.value;
    optionElement.textContent = option.label;
    select.appendChild(optionElement);
  }

  const fallbackOption = options.some(option => option.value === item.value) ?
      null :
      createUnknownSelectOption(item.value);
  if (fallbackOption) {
    select.appendChild(fallbackOption);
  }
  select.value = item.value;
  select.addEventListener('change', () => {
    void commitSettingValue(item.key, select.value, select);
  });

  const indicator = document.createElement('span');
  indicator.className = 'setting-input-shell__suffix setting-input-shell__suffix--icon';
  indicator.textContent = '▾';
  indicator.setAttribute('aria-hidden', 'true');

  wrapper.append(select, indicator);
  return wrapper;
}

function createSegmentedControl(item: SettingValue, meta: SettingMetadata): HTMLElement {
  const segmented = document.createElement('div');
  segmented.className = 'setting-segmented';
  segmented.setAttribute('role', 'tablist');
  segmented.setAttribute('aria-label', meta.label);

  for (const option of meta.options || []) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'setting-segment';
    button.textContent = option.displayLabel || option.label;
    button.setAttribute('role', 'tab');
    button.setAttribute('aria-selected', String(option.value === item.value));
    if (option.value === item.value) {
      button.classList.add('is-active');
    }

    button.addEventListener('click', async () => {
      if (option.value === item.value) {
        return;
      }
      await commitSettingValue(item.key, option.value, button);
    });

    segmented.appendChild(button);
  }

  return segmented;
}

function createInlineSettingBadge(key: string): HTMLElement {
  const badge = document.createElement('span');
  badge.className = 'setting-inline-badge';
  badge.textContent = key;
  return badge;
}

function createUnknownSelectOption(value: string): HTMLOptionElement {
  const option = document.createElement('option');
  option.value = value;
  option.textContent = `Current value (${value})`;
  return option;
}

function normalizeInputValue(value: string, fallback: string, meta: SettingMetadata): string {
  if (meta.control !== 'number') {
    const trimmed = value.trim();
    return trimmed || fallback;
  }

  const parsed = Number.parseInt(value, 10);
  if (Number.isNaN(parsed)) {
    return fallback;
  }

  const min = meta.min ?? parsed;
  const max = meta.max ?? parsed;
  return String(Math.min(max, Math.max(min, parsed)));
}

function groupSettings(settings: SettingValue[]): Record<string, SettingValue[]> {
  const grouped: Record<string, SettingValue[]> = {};
  for (const setting of settings) {
    const category = setting.key.split('.')[0] || 'general';
    if (!grouped[category]) {
      grouped[category] = [];
    }
    grouped[category].push(setting);
  }
  return grouped;
}

function getSettingMetadata(item: SettingValue): SettingMetadata {
  const metadata = SETTING_METADATA[item.key];
  if (metadata) {
    return metadata;
  }

  const leafKey = item.key.split('.').slice(1).join(' ');
  return {
    label: humanizeToken(leafKey),
    description: 'Currently supported preference.',
    control: item.value === 'true' || item.value === 'false' ? 'toggle' : 'text',
  };
}

function humanizeToken(token: string): string {
  return token.replace(/_/g, ' ').replace(/\b\w/g, letter => letter.toUpperCase());
}

function getSectionId(key: string): string {
  return `section-${key}`;
}

document.addEventListener('DOMContentLoaded', initialize);
