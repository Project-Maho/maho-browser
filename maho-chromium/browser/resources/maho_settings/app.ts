import {PageHandlerFactory, PageHandlerRemote, SettingValue, ProfileInfo, ATCRule, AISettings, ShortcutBinding, SyncStatus, SyncDeviceInfo, SyncKeyInfo, ThemeStoreEntry, AutofillAddress, AutofillPayment, FilterListInfo, ContentBlockerStats, InstalledExtension, PasswordProviderStatus, PasswordProviderKind, SavedPassword} from './maho_settings.mojom-webui.js';
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
import {DOMAIN_DEFINITIONS} from './schema/domains.js';
import type {LivePaneSectionDefinition} from './schema/panes.js';
import type {
  ControlKind,
  DomainId,
  PaneDefinition,
  SettingMetadata,
} from './models.js';

let pageHandler: PageHandlerRemote;
let callbackRouter: PageCallbackRouter;
let currentPaneKey = 'general';
let currentSettings: SettingValue[] = [];
let currentPasswordProviderStatus: PasswordProviderStatus | null = null;
let currentSavedPasswords: SavedPassword[] = [];
let currentPasswordSearchQuery = '';
let passwordDataLoaded = false;
let passwordDataLoading = false;
const revealedPasswordIds = new Set<string>();

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

function getPasswordsEnabled(settings: SettingValue[]): boolean {
  const item = settings.find(setting => setting.key === 'autofill.passwords_enabled');
  return item?.value !== 'false';
}

function isMahoNativeProvider(provider: PasswordProviderStatus['provider']): boolean {
  return provider === PasswordProviderKind.kMahoNative;
}

function updateSettingValue(
    settings: SettingValue[], key: string, value: string): SettingValue[] {
  let found = false;
  const nextSettings = settings.map(setting => {
    if (setting.key !== key) {
      return setting;
    }

    found = true;
    return {
      ...setting,
      key: setting.key,
      value,
    };
  });

  if (found) {
    return nextSettings;
  }

  return [...nextSettings, {key, value} as SettingValue];
}

function loadPasswordProviderState() {
  if (passwordDataLoaded || passwordDataLoading) {
    return;
  }
  passwordDataLoading = true;
  Promise.all([
    pageHandler.getPasswordProviderStatus(),
    pageHandler.getSavedPasswords(),
  ]).then(([providerResult, passwordsResult]) => {
    currentPasswordProviderStatus = providerResult.status;
    currentPasswordSearchQuery = '';
    currentSavedPasswords = passwordsResult.passwords.map(password => ({
      ...password,
      password: password.password || null,
      lastUsed: password.lastUsed || null,
    }));
    passwordDataLoaded = true;
    passwordDataLoading = false;
    renderSettings(currentSettings);
  }).catch(() => {
    currentPasswordProviderStatus = null;
    currentSavedPasswords = [];
    passwordDataLoaded = false;
    passwordDataLoading = false;
    renderSettings(currentSettings);
  });
}

function normalizeSavedPasswords(passwords: SavedPassword[]): SavedPassword[] {
  return passwords.map(password => ({
    ...password,
    password: password.password || null,
    lastUsed: password.lastUsed || null,
  }));
}

async function refreshSavedPasswords() {
  if (currentPasswordSearchQuery) {
    const {passwords} = await pageHandler.searchPasswords(currentPasswordSearchQuery);
    currentSavedPasswords = normalizeSavedPasswords(passwords);
  } else {
    const {passwords} = await pageHandler.getSavedPasswords();
    currentSavedPasswords = normalizeSavedPasswords(passwords);
  }
  renderSettings(currentSettings);
}

function renderSettings(settings: SettingValue[]) {
  const container = document.querySelector<HTMLElement>('maho-settings-app');
  if (!container) {
    return;
  }

  currentPaneKey = getInitialPaneKey();
  container.replaceChildren(createPreferencesShell(settings));
  if (currentPaneKey === 'passwords' && !passwordDataLoaded && !passwordDataLoading) {
    loadPasswordProviderState();
  }
}

function getInitialPaneKey(): string {
  const visible = PANE_DEFINITIONS.find(p => p.key === currentPaneKey);
  return visible ? currentPaneKey : PANE_DEFINITIONS[0]!.key;
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

  const brandTitle = document.createElement('h1');
  brandTitle.className = 'sidebar-title';
  brandTitle.textContent = 'Settings';

  brand.appendChild(brandTitle);

  const nav = document.createElement('nav');
  nav.className = 'sidebar-navigation';
  nav.setAttribute('aria-label', 'Settings panes');

  const panesByDomain = new Map<DomainId, PaneDefinition[]>();
  for (const pane of PANE_DEFINITIONS) {
    const list = panesByDomain.get(pane.domain) || [];
    list.push(pane);
    panesByDomain.set(pane.domain, list);
  }

  for (const domain of DOMAIN_DEFINITIONS) {
    const domainPanes = panesByDomain.get(domain.id);
    if (!domainPanes || domainPanes.length === 0) continue;

    const label = document.createElement('div');
    label.className = 'sidebar-domain-label';
    label.textContent = domain.label;
    nav.appendChild(label);

    for (const pane of domainPanes) {
      nav.appendChild(createPaneButton(pane));
    }
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

  text.appendChild(label);
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

  const pane = PANE_DEFINITION_MAP[currentPaneKey];
  const resolvedPane = (pane && pane.kind !== 'hidden') ? pane : PANE_DEFINITIONS[0]!;

  if (resolvedPane.kind === 'embed') {
    scroll.appendChild(createEmbedPane(resolvedPane));
  } else if (resolvedPane.contentKind === 'profiles') {
    scroll.appendChild(createDomainPane(resolvedPane, createProfilesContent));
  } else if (resolvedPane.contentKind === 'atc') {
    scroll.appendChild(createDomainPane(resolvedPane, createATCContent));
  } else if (resolvedPane.contentKind === 'ai') {
    scroll.appendChild(createDomainPane(resolvedPane, createAIContent));
  } else if (resolvedPane.contentKind === 'shortcuts') {
    scroll.appendChild(createDomainPane(resolvedPane, createShortcutsContent));
  } else if (resolvedPane.contentKind === 'sync') {
    scroll.appendChild(createDomainPane(resolvedPane, createSyncContent));
  } else if (resolvedPane.contentKind === 'theme-store') {
    scroll.appendChild(createDomainPane(resolvedPane, createThemeStoreContent));
  } else if (resolvedPane.contentKind === 'autofill') {
    scroll.appendChild(createDomainPane(resolvedPane, createAutofillContent));
  } else if (resolvedPane.contentKind === 'passwords') {
    scroll.appendChild(createDomainPane(resolvedPane, createPasswordsContent));
  } else if (resolvedPane.contentKind === 'content-blocker') {
    scroll.appendChild(createDomainPane(resolvedPane, createContentBlockerContent));
  } else if (resolvedPane.contentKind === 'extensions') {
    scroll.appendChild(createDomainPane(resolvedPane, createExtensionsContent));
  } else {
    scroll.appendChild(createLivePane(resolvedPane, settings));
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
    return createSingleLiveCard(pane.title, '', settings);
  }

  const cards: HTMLElement[] = [];
  for (const section of sections) {
    cards.push(createLivePaneCardFromSection(section, settings));
  }
  return cards;
}

function createLivePaneCardFromSection(
    section: LivePaneSectionDefinition, settings: SettingValue[]): HTMLElement {
  const card = createContentCard(
      section.card.title,
      section.card.description,
      selectSettings(settings, section.card.settingKeys));
  if (section.card.wide) {
    card.classList.add('content-card--wide');
  }
  return card;
}

function createSingleLiveCard(
    title: string, description: string, settings: SettingValue[]): HTMLElement[] {
  return [createContentCard(title, description, settings)];
}

function selectSettings(settings: SettingValue[], keys: string[]): SettingValue[] {
  const byKey = new Map(settings.map(setting => [setting.key, setting]));
  return keys.map(key => byKey.get(key)).filter(Boolean) as SettingValue[];
}

function createEmbedPane(pane: PaneDefinition): HTMLElement {
  const section = createPaneSectionShell(pane);
  const card = section.querySelector('.preferences-card');
  if (!card) {
    return section;
  }

  card.classList.add('preferences-card--browser');

  const browserShell = document.createElement('div');
  browserShell.className = 'browser-settings-shell browser-settings-shell--wide';

  const embedUrl = pane.embedUrl || 'chrome://settings/';
  const browserView = document.createElement('iframe');
  browserView.className = 'browser-settings-webview';
  browserView.setAttribute('role', 'document');
  browserView.setAttribute('src', embedUrl);
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

function createDomainPane(
    pane: PaneDefinition,
    contentFactory: (container: HTMLElement) => void): HTMLElement {
  const section = createPaneSectionShell(pane);
  const card = section.querySelector('.preferences-card');
  if (!card) {
    return section;
  }
  const grid = document.createElement('div');
  grid.className = 'cards-grid';
  const inner = document.createElement('article');
  inner.className = 'content-card content-card--wide';
  grid.appendChild(inner);
  card.appendChild(grid);
  contentFactory(inner);
  return section;
}

function showDomainError(container: HTMLElement, message: string) {
  container.textContent = '';
  const msg = document.createElement('p');
  msg.className = 'setting-row__description';
  msg.textContent = message;
  container.appendChild(msg);
}

function showDomainRetryError(
    container: HTMLElement, title: string, message: string,
    retryLabel: string, onRetry: () => void) {
  container.textContent = '';

  const row = document.createElement('section');
  row.className = 'setting-row setting-row--first';

  const copy = document.createElement('div');
  copy.className = 'setting-row__copy';

  const heading = document.createElement('h4');
  heading.className = 'setting-row__title';
  heading.textContent = title;

  const description = document.createElement('p');
  description.className = 'setting-row__description';
  description.textContent = message;
  copy.append(heading, description);

  const control = document.createElement('div');
  control.className = 'setting-row__control';

  const retryButton = document.createElement('button');
  retryButton.type = 'button';
  retryButton.className = 'setting-switch';
  retryButton.textContent = retryLabel;
  retryButton.addEventListener('click', () => onRetry());
  control.appendChild(retryButton);

  row.append(copy, control);
  container.appendChild(row);
}

function createProfilesContent(container: HTMLElement) {
  container.textContent = 'Loading profiles\u2026';
  pageHandler.getProfiles().then(({profiles}: {profiles: ProfileInfo[]}) => {
    container.textContent = '';
    const header = document.createElement('div');
    header.className = 'content-card__header';
    const heading = document.createElement('h3');
    heading.className = 'content-card__title';
    heading.textContent = 'Browser Profiles';
    header.appendChild(heading);
    container.appendChild(header);

    const rows = document.createElement('div');
    rows.className = 'preferences-card__rows';

    if (profiles.length === 0) {
      const empty = document.createElement('p');
      empty.className = 'setting-row__description';
      empty.textContent = 'No profiles configured.';
      rows.appendChild(empty);
    }

    for (const profile of profiles) {
      rows.appendChild(createProfileRow(profile, rows));
    }

    const addRow = document.createElement('div');
    addRow.className = 'setting-row';
    const nameInput = document.createElement('input');
    nameInput.className = 'setting-input';
    nameInput.placeholder = 'New profile name';
    const addBtn = document.createElement('button');
    addBtn.type = 'button';
    addBtn.className = 'setting-switch';
    addBtn.textContent = 'Create';
    addBtn.addEventListener('click', () => {
      if (!nameInput.value.trim()) return;
      addBtn.disabled = true;
      pageHandler.createProfile(nameInput.value.trim()).then(
          ({profile}: {profile: ProfileInfo | null}) => {
            addBtn.disabled = false;
            if (profile) {
              rows.insertBefore(createProfileRow(profile, rows), addRow);
              nameInput.value = '';
            }
          });
    });
    addRow.append(nameInput, addBtn);
    rows.appendChild(addRow);

    container.appendChild(rows);
  }).catch(() => showDomainError(container, 'Failed to load profiles.'));
}

function createProfileRow(profile: ProfileInfo, rowsContainer: HTMLElement): HTMLElement {
  const row = document.createElement('section');
  row.className = 'setting-row';

  const copy = document.createElement('div');
  copy.className = 'setting-row__copy';
  const title = document.createElement('h4');
  title.className = 'setting-row__title';
  title.textContent = profile.name;
  const desc = document.createElement('p');
  desc.className = 'setting-row__description';
  const parts: string[] = [`ID: ${profile.id}`];
  if (profile.isDefault) parts.push('Default');
  if (profile.isActive) parts.push('Active');
  if (profile.spaceIds.length > 0) {
    parts.push(`Spaces: ${profile.spaceIds.join(', ')}`);
  }
  desc.textContent = parts.join(' \u00b7 ');
  copy.append(title, desc);

  const controls = document.createElement('div');
  controls.className = 'setting-row__control';

  if (!profile.isActive) {
    const switchBtn = document.createElement('button');
    switchBtn.type = 'button';
    switchBtn.className = 'setting-switch';
    switchBtn.textContent = 'Switch';
    switchBtn.addEventListener('click', async () => {
      switchBtn.disabled = true;
      await pageHandler.switchProfile(profile.id);
      switchBtn.disabled = false;
      const parentCard = rowsContainer.closest('.content-card');
      if (parentCard) createProfilesContent(parentCard as HTMLElement);
    });
    controls.appendChild(switchBtn);
  }

  if (!profile.isDefault && !profile.isActive) {
    const removeBtn = document.createElement('button');
    removeBtn.type = 'button';
    removeBtn.className = 'setting-switch';
    removeBtn.textContent = '\u00d7';
    removeBtn.setAttribute('aria-label', 'Delete profile');
    removeBtn.addEventListener('click', async () => {
      removeBtn.disabled = true;
      await pageHandler.deleteProfile(profile.id);
      removeBtn.disabled = false;
      row.remove();
    });
    controls.appendChild(removeBtn);
  }

  row.append(copy, controls);
  return row;
}

function createATCContent(container: HTMLElement) {
  container.textContent = 'Loading rules\u2026';
  pageHandler.getATCRules().then(({rules}: {rules: ATCRule[]}) => {
    container.textContent = '';
    const header = document.createElement('div');
    header.className = 'content-card__header';
    const heading = document.createElement('h3');
    heading.className = 'content-card__title';
    heading.textContent = 'URL Routing Rules';
    header.appendChild(heading);
    container.appendChild(header);

    const rows = document.createElement('div');
    rows.className = 'preferences-card__rows';

    for (const rule of rules) {
      rows.appendChild(createATCRuleRow(rule));
    }

    const addRow = document.createElement('div');
    addRow.className = 'setting-row';
    const patternInput = document.createElement('input');
    patternInput.className = 'setting-input';
    patternInput.placeholder = 'URL pattern (e.g. *.example.com)';
    const spaceInput = document.createElement('input');
    spaceInput.className = 'setting-input';
    spaceInput.placeholder = 'Target space ID';
    const addBtn = document.createElement('button');
    addBtn.type = 'button';
    addBtn.className = 'setting-switch';
    addBtn.textContent = 'Add';
    addBtn.addEventListener('click', async () => {
      if (!patternInput.value || !spaceInput.value) return;
      addBtn.disabled = true;
      const {rule} = await pageHandler.addATCRule(patternInput.value, spaceInput.value);
      addBtn.disabled = false;
      if (rule) {
        rows.appendChild(createATCRuleRow(rule));
        patternInput.value = '';
        spaceInput.value = '';
      }
    });
    addRow.append(patternInput, spaceInput, addBtn);
    rows.appendChild(addRow);

    container.appendChild(rows);
  }).catch(() => showDomainError(container, 'Failed to load rules.'));
}

function createATCRuleRow(rule: ATCRule): HTMLElement {
  const row = document.createElement('section');
  row.className = 'setting-row';

  const copy = document.createElement('div');
  copy.className = 'setting-row__copy';
  const title = document.createElement('h4');
  title.className = 'setting-row__title';
  title.textContent = rule.urlPattern;
  const desc = document.createElement('p');
  desc.className = 'setting-row__description';
  desc.textContent = `\u2192 ${rule.targetSpaceId}`;
  copy.append(title, desc);

  const controls = document.createElement('div');
  controls.className = 'setting-row__control';

  const toggle = document.createElement('button');
  toggle.type = 'button';
  toggle.className = 'setting-switch';
  toggle.setAttribute('role', 'switch');
  toggle.setAttribute('aria-checked', String(rule.enabled));
  if (rule.enabled) toggle.classList.add('is-on');

  const track = document.createElement('span');
  track.className = 'setting-switch__track';
  const thumb = document.createElement('span');
  thumb.className = 'setting-switch__thumb';
  track.appendChild(thumb);
  const status = document.createElement('span');
  status.className = 'setting-switch__status';
  status.textContent = rule.enabled ? 'On' : 'Off';
  toggle.append(track, status);

  toggle.addEventListener('click', () => {
    const newEnabled = toggle.getAttribute('aria-checked') !== 'true';
    toggle.setAttribute('aria-checked', String(newEnabled));
    toggle.classList.toggle('is-on', newEnabled);
    status.textContent = newEnabled ? 'On' : 'Off';
    pageHandler.toggleATCRule(rule.id, newEnabled);
  });

  const removeBtn = document.createElement('button');
  removeBtn.type = 'button';
  removeBtn.className = 'setting-switch';
  removeBtn.textContent = '\u00d7';
  removeBtn.setAttribute('aria-label', 'Remove rule');
  removeBtn.addEventListener('click', () => {
    pageHandler.removeATCRule(rule.id);
    row.remove();
  });

  controls.append(toggle, removeBtn);
  row.append(copy, controls);
  return row;
}

function createAIContent(container: HTMLElement) {
  container.textContent = 'Loading AI settings\u2026';
  pageHandler.getAISettings().then(({settings}: {settings: AISettings}) => {
    container.textContent = '';
    const header = document.createElement('div');
    header.className = 'content-card__header';
    const heading = document.createElement('h3');
    heading.className = 'content-card__title';
    heading.textContent = 'AI Configuration';
    header.appendChild(heading);
    container.appendChild(header);

    const rows = document.createElement('div');
    rows.className = 'preferences-card__rows';

    rows.appendChild(createAIRow(
        'API Key',
        settings.hasApiKey ? 'Configured' : 'Not set',
        'password',
        (value: string) => { pageHandler.setAIApiKey(value); }));

    rows.appendChild(createAIRow(
        'Endpoint',
        settings.endpoint,
        'text',
        (value: string) => { pageHandler.setAIEndpoint(value); }));

    rows.appendChild(createAIRow(
        'Model',
        settings.model,
        'text',
        (value: string) => { pageHandler.setAIModel(value); }));

    rows.appendChild(createAIRow(
        'Approval Policy',
        settings.approvalPolicy,
        'text',
        (value: string) => { pageHandler.setAIApprovalPolicy(value); }));

    rows.appendChild(createAIToggleRow(
        'Session Persistence',
        settings.sessionPersistenceEnabled,
        (enabled: boolean) => { pageHandler.setAISessionPersistence(enabled); }));

    rows.appendChild(createAIRow(
        'Runtime Endpoint',
        settings.runtimeEndpoint,
        'text',
        (value: string) => { pageHandler.setAIRuntimeEndpoint(value); }));

    rows.appendChild(createAIRow(
        'Runtime Model',
        settings.runtimeModel,
        'text',
        (value: string) => { pageHandler.setAIRuntimeModel(value); }));

    container.appendChild(rows);
  }).catch(() => showDomainError(container, 'Failed to load AI settings.'));
}

function createAIRow(
    label: string, currentValue: string, inputType: string,
    onCommit: (v: string) => void): HTMLElement {
  const row = document.createElement('section');
  row.className = 'setting-row';

  const copy = document.createElement('div');
  copy.className = 'setting-row__copy';
  const title = document.createElement('h4');
  title.className = 'setting-row__title';
  title.textContent = label;
  copy.appendChild(title);

  const controlArea = document.createElement('div');
  controlArea.className = 'setting-row__control';
  const wrapper = document.createElement('label');
  wrapper.className = 'setting-input-shell';
  const input = document.createElement('input');
  input.className = 'setting-input';
  input.type = inputType;
  input.value = currentValue;
  input.setAttribute('aria-label', label);
  input.addEventListener('change', () => {
    onCommit(input.value);
  });
  input.addEventListener('keydown', (e: KeyboardEvent) => {
    if (e.key === 'Enter') input.blur();
  });
  wrapper.appendChild(input);
  controlArea.appendChild(wrapper);

  row.append(copy, controlArea);
  return row;
}

function createAIToggleRow(
    label: string, enabled: boolean, onCommit: (enabled: boolean) => void): HTMLElement {
  const row = document.createElement('section');
  row.className = 'setting-row';

  const copy = document.createElement('div');
  copy.className = 'setting-row__copy';
  const title = document.createElement('h4');
  title.className = 'setting-row__title';
  title.textContent = label;
  copy.appendChild(title);

  const controlArea = document.createElement('div');
  controlArea.className = 'setting-row__control';

  const toggle = document.createElement('button');
  toggle.type = 'button';
  toggle.className = 'setting-switch';
  toggle.setAttribute('role', 'switch');
  toggle.setAttribute('aria-checked', String(enabled));
  toggle.setAttribute('aria-label', label);
  if (enabled) {
    toggle.classList.add('is-on');
  }

  const track = document.createElement('span');
  track.className = 'setting-switch__track';
  const thumb = document.createElement('span');
  thumb.className = 'setting-switch__thumb';
  track.appendChild(thumb);
  const status = document.createElement('span');
  status.className = 'setting-switch__status';
  status.textContent = enabled ? 'On' : 'Off';
  toggle.append(track, status);

  toggle.addEventListener('click', () => {
    const nextEnabled = toggle.getAttribute('aria-checked') !== 'true';
    toggle.setAttribute('aria-checked', String(nextEnabled));
    toggle.classList.toggle('is-on', nextEnabled);
    status.textContent = nextEnabled ? 'On' : 'Off';
    onCommit(nextEnabled);
  });

  controlArea.appendChild(toggle);
  row.append(copy, controlArea);
  return row;
}

function createShortcutsContent(container: HTMLElement) {
  container.textContent = 'Loading shortcuts\u2026';
  pageHandler.getShortcuts().then(({shortcuts}: {shortcuts: ShortcutBinding[]}) => {
    container.textContent = '';
    const header = document.createElement('div');
    header.className = 'content-card__header';
    const heading = document.createElement('h3');
    heading.className = 'content-card__title';
    heading.textContent = 'Keyboard Shortcuts';

    const resetAllBtn = document.createElement('button');
    resetAllBtn.type = 'button';
    resetAllBtn.className = 'setting-switch';
    resetAllBtn.textContent = 'Reset All';
    resetAllBtn.addEventListener('click', async () => {
      resetAllBtn.disabled = true;
      await pageHandler.resetAllShortcuts();
      resetAllBtn.disabled = false;
      createShortcutsContent(container);
    });

    header.append(heading, resetAllBtn);
    container.appendChild(header);

    const rows = document.createElement('div');
    rows.className = 'preferences-card__rows';

    for (const binding of shortcuts) {
      rows.appendChild(createShortcutRow(binding, container));
    }

    container.appendChild(rows);
  }).catch(() => showDomainError(container, 'Failed to load shortcuts.'));
}

function createShortcutRow(binding: ShortcutBinding, parentContainer: HTMLElement): HTMLElement {
  const row = document.createElement('section');
  row.className = 'setting-row';

  const copy = document.createElement('div');
  copy.className = 'setting-row__copy';
  const title = document.createElement('h4');
  title.className = 'setting-row__title';
  title.textContent = binding.action.replace(/_/g, ' ').replace(/\b\w/g, l => l.toUpperCase());
  const desc = document.createElement('p');
  desc.className = 'setting-row__description';
  desc.textContent = !binding.isDefault ? 'Custom binding' : 'Default';
  copy.append(title, desc);

  const controlArea = document.createElement('div');
  controlArea.className = 'setting-row__control';

  const enabledToggle = document.createElement('button');
  enabledToggle.type = 'button';
  enabledToggle.className = 'setting-switch';
  enabledToggle.setAttribute('role', 'switch');
  enabledToggle.setAttribute('aria-checked', String(binding.enabled));
  enabledToggle.setAttribute('aria-label', `Enable ${binding.action}`);
  if (binding.enabled) {
    enabledToggle.classList.add('is-on');
  }

  const enabledTrack = document.createElement('span');
  enabledTrack.className = 'setting-switch__track';
  const enabledThumb = document.createElement('span');
  enabledThumb.className = 'setting-switch__thumb';
  enabledTrack.appendChild(enabledThumb);
  const enabledStatus = document.createElement('span');
  enabledStatus.className = 'setting-switch__status';
  enabledStatus.textContent = binding.enabled ? 'On' : 'Off';
  enabledToggle.append(enabledTrack, enabledStatus);

  enabledToggle.addEventListener('click', () => {
    const nextEnabled = enabledToggle.getAttribute('aria-checked') !== 'true';
    enabledToggle.disabled = true;
    pageHandler.toggleShortcut(binding.action, nextEnabled);
    enabledToggle.setAttribute('aria-checked', String(nextEnabled));
    enabledToggle.classList.toggle('is-on', nextEnabled);
    enabledStatus.textContent = nextEnabled ? 'On' : 'Off';
    enabledToggle.disabled = false;
    createShortcutsContent(parentContainer);
  });

  const input = document.createElement('input');
  input.className = 'setting-input';
  input.value = binding.keyCombo;
  input.setAttribute('aria-label', `Shortcut for ${binding.action}`);
  input.addEventListener('change', async () => {
    input.disabled = true;
    await pageHandler.setShortcut(binding.action, input.value);
    input.disabled = false;
  });
  input.addEventListener('keydown', (e: KeyboardEvent) => {
    if (e.key === 'Enter') input.blur();
  });

  const resetBtn = document.createElement('button');
  resetBtn.type = 'button';
  resetBtn.className = 'setting-switch';
  resetBtn.textContent = 'Reset';
  resetBtn.style.marginLeft = '8px';
  resetBtn.style.minWidth = '60px';
  if (binding.isDefault) resetBtn.disabled = true;
  resetBtn.addEventListener('click', async () => {
    resetBtn.disabled = true;
    await pageHandler.resetShortcut(binding.action);
    resetBtn.disabled = false;
    createShortcutsContent(parentContainer);
  });

  controlArea.append(enabledToggle, input, resetBtn);
  row.append(copy, controlArea);
  return row;
}

function createSyncContent(container: HTMLElement) {
  container.textContent = 'Loading sync status\u2026';
  pageHandler.getSyncStatus().then(({status}: {status: SyncStatus}) => {
    container.textContent = '';
    const header = document.createElement('div');
    header.className = 'content-card__header';
    const heading = document.createElement('h3');
    heading.className = 'content-card__title';
    heading.textContent = 'Account & Sync';
    header.appendChild(heading);
    container.appendChild(header);

    const rows = document.createElement('div');
    rows.className = 'preferences-card__rows';

    const statusRow = document.createElement('section');
    statusRow.className = 'setting-row setting-row--first';
    const statusCopy = document.createElement('div');
    statusCopy.className = 'setting-row__copy';
    const statusTitle = document.createElement('h4');
    statusTitle.className = 'setting-row__title';
    statusTitle.textContent = 'Status';
    const statusDesc = document.createElement('p');
    statusDesc.className = 'setting-row__description';
    statusDesc.textContent = status.isSyncing ?
        `Syncing \u00b7 ${status.statusLabel}` :
        `Not connected \u00b7 ${status.statusLabel || 'idle'}`;
    statusCopy.append(statusTitle, statusDesc);
    statusRow.appendChild(statusCopy);
    rows.appendChild(statusRow);

    if (status.errorMessage) {
      const errorRow = document.createElement('section');
      errorRow.className = 'setting-row';
      const errorCopy = document.createElement('div');
      errorCopy.className = 'setting-row__copy';
      const errorTitle = document.createElement('h4');
      errorTitle.className = 'setting-row__title';
      errorTitle.textContent = 'Error';
      const errorDesc = document.createElement('p');
      errorDesc.className = 'setting-row__description';
      errorDesc.textContent = status.errorMessage;
      errorCopy.append(errorTitle, errorDesc);
      errorRow.appendChild(errorCopy);
      rows.appendChild(errorRow);
    }

    if (status.isSyncing) {
      renderSyncActiveControls(rows, container);
    } else {
      renderSyncInactiveControls(rows, container);
    }

    container.appendChild(rows);
  }).catch(() => showDomainError(container, 'Failed to load sync status.'));
}

function renderSyncActiveControls(rows: HTMLElement, parentContainer: HTMLElement) {
  pageHandler.getSyncDevices().then(({devices}: {devices: SyncDeviceInfo[]}) => {
    if (devices.length > 0) {
      const devHeader = document.createElement('section');
      devHeader.className = 'setting-row';
      const devCopy = document.createElement('div');
      devCopy.className = 'setting-row__copy';
      const devTitle = document.createElement('h4');
      devTitle.className = 'setting-row__title';
      devTitle.textContent = 'Connected Devices';
      devCopy.appendChild(devTitle);
      devHeader.appendChild(devCopy);
      rows.appendChild(devHeader);

      for (const device of devices) {
        const dRow = document.createElement('section');
        dRow.className = 'setting-row';
        const dCopy = document.createElement('div');
        dCopy.className = 'setting-row__copy';
        const dTitle = document.createElement('h4');
        dTitle.className = 'setting-row__title';
        dTitle.textContent = device.name;
        const dDesc = document.createElement('p');
        dDesc.className = 'setting-row__description';
        dDesc.textContent = `${device.deviceType} \u00b7 ${device.isOnline ? 'Online' : 'Offline'}`;
        dCopy.append(dTitle, dDesc);
        dRow.appendChild(dCopy);
        rows.appendChild(dRow);
      }
    }
  }).catch(() => {
    const errRow = document.createElement('section');
    errRow.className = 'setting-row';
    const errMsg = document.createElement('p');
    errMsg.className = 'setting-row__description';
    errMsg.textContent = 'Failed to load connected devices.';
    errRow.appendChild(errMsg);
    rows.appendChild(errRow);
  });

  const stopRow = document.createElement('section');
  stopRow.className = 'setting-row';
  const stopBtn = document.createElement('button');
  stopBtn.type = 'button';
  stopBtn.className = 'setting-switch';
  stopBtn.textContent = 'Stop Sync';
  stopBtn.addEventListener('click', async () => {
    stopBtn.disabled = true;
    await pageHandler.stopSync();
    stopBtn.disabled = false;
    createSyncContent(parentContainer);
  });
  stopRow.appendChild(stopBtn);
  rows.appendChild(stopRow);
}

function renderSyncInactiveControls(rows: HTMLElement, parentContainer: HTMLElement) {
  let pendingKeyInfo: SyncKeyInfo|null = null;
  let recoveryPhraseRow: HTMLElement|null = null;

  const startRow = document.createElement('section');
  startRow.className = 'setting-row';
  const startCopy = document.createElement('div');
  startCopy.className = 'setting-row__copy';
  const startTitle = document.createElement('h4');
  startTitle.className = 'setting-row__title';
  startTitle.textContent = 'Start New Sync';
  startCopy.appendChild(startTitle);

  const startControl = document.createElement('div');
  startControl.className = 'setting-row__control';
  const genBtn = document.createElement('button');
  genBtn.type = 'button';
  genBtn.className = 'setting-switch';
  genBtn.textContent = 'Generate Recovery Phrase';

  const joinRow = document.createElement('section');
  joinRow.className = 'setting-row';

  const renderRecoveryPhraseRow = (keyInfo: SyncKeyInfo) => {
    recoveryPhraseRow?.remove();
    recoveryPhraseRow = createSyncRecoveryPhraseRow(keyInfo, parentContainer);
    rows.insertBefore(recoveryPhraseRow, joinRow);
    genBtn.textContent = 'Generate New Phrase';
  };

  genBtn.addEventListener('click', async () => {
    genBtn.disabled = true;
    const {keyInfo} = await pageHandler.generateSyncKey();
    if (keyInfo && keyInfo.recoveryPhrase) {
      pendingKeyInfo = keyInfo;
      renderRecoveryPhraseRow(pendingKeyInfo);
    }
    genBtn.disabled = false;
  });
  startControl.appendChild(genBtn);
  startRow.append(startCopy, startControl);
  rows.appendChild(startRow);

  const joinCopy = document.createElement('div');
  joinCopy.className = 'setting-row__copy';
  const joinTitle = document.createElement('h4');
  joinTitle.className = 'setting-row__title';
  joinTitle.textContent = 'Join Existing Sync';
  joinCopy.appendChild(joinTitle);

  const joinControl = document.createElement('div');
  joinControl.className = 'setting-row__control';
  const phraseInput = document.createElement('input');
  phraseInput.className = 'setting-input';
  phraseInput.placeholder = 'Recovery phrase';
  const joinBtn = document.createElement('button');
  joinBtn.type = 'button';
  joinBtn.className = 'setting-switch';
  joinBtn.textContent = 'Join';
  joinBtn.style.marginLeft = '8px';
  joinBtn.style.minWidth = '60px';
  joinBtn.addEventListener('click', async () => {
    if (!phraseInput.value.trim()) return;
    joinBtn.disabled = true;
    await pageHandler.joinSync(phraseInput.value.trim());
    joinBtn.disabled = false;
    createSyncContent(parentContainer);
  });
  joinControl.append(phraseInput, joinBtn);
  joinRow.append(joinCopy, joinControl);
  rows.appendChild(joinRow);
}

function createSyncRecoveryPhraseRow(
    keyInfo: SyncKeyInfo, parentContainer: HTMLElement): HTMLElement {
  const row = document.createElement('section');
  row.className = 'setting-row';

  const copy = document.createElement('div');
  copy.className = 'setting-row__copy';
  const title = document.createElement('h4');
  title.className = 'setting-row__title';
  title.textContent = 'Recovery Phrase';
  const description = document.createElement('p');
  description.className = 'setting-row__description';
  description.textContent = keyInfo.recoveryPhrase;
  copy.append(title, description);

  const control = document.createElement('div');
  control.className = 'setting-row__control';

  if (navigator.clipboard?.writeText) {
    const copyBtn = document.createElement('button');
    copyBtn.type = 'button';
    copyBtn.className = 'setting-switch';
    copyBtn.textContent = 'Copy';
    copyBtn.addEventListener('click', async () => {
      copyBtn.disabled = true;
      try {
        await navigator.clipboard.writeText(keyInfo.recoveryPhrase);
        copyBtn.textContent = 'Copied';
      } finally {
        window.setTimeout(() => {
          copyBtn.textContent = 'Copy';
          copyBtn.disabled = false;
        }, 1200);
      }
    });
    control.appendChild(copyBtn);
  }

  const continueBtn = document.createElement('button');
  continueBtn.type = 'button';
  continueBtn.className = 'setting-switch';
  continueBtn.textContent = 'Continue';
  continueBtn.addEventListener('click', async () => {
    continueBtn.disabled = true;
    const buttons = control.querySelectorAll<HTMLButtonElement>('button');
    buttons.forEach(button => button.disabled = true);
    await pageHandler.startSync(keyInfo.syncKey);
    createSyncContent(parentContainer);
  });
  control.appendChild(continueBtn);

  row.append(copy, control);
  return row;
}

function createThemeStoreContent(container: HTMLElement) {
  container.textContent = 'Loading themes\u2026';
  container.setAttribute('data-maho-identifier', 'settings.themeStore.loading');

  Promise.allSettled([
    pageHandler.fetchThemeStore(),
    pageHandler.getSpaces(),
  ]).then(([themeStoreResult, spacesResult]) => {
    const themeStore =
        themeStoreResult.status === 'fulfilled' ? themeStoreResult.value : null;
    const themes = themeStore?.themes || [];
    const success = themeStore?.success || false;
    const spaces = spacesResult.status === 'fulfilled' ? spacesResult.value.spaces : [];

    container.textContent = '';
    const header = document.createElement('div');
    header.className = 'content-card__header';
    const heading = document.createElement('h3');
    heading.className = 'content-card__title';
    heading.textContent = 'Community Themes';
    const refreshButton = document.createElement('button');
    refreshButton.type = 'button';
    refreshButton.className = 'setting-switch';
    refreshButton.textContent = 'Refresh';
    refreshButton.setAttribute('data-maho-identifier', 'settings.themeStore.refresh');
    refreshButton.addEventListener('click', () => createThemeStoreContent(container));
    header.append(heading, refreshButton);
    container.appendChild(header);

    if (themes.length === 0) {
      if (!success) {
        showDomainRetryError(
            container, 'Theme Store unavailable',
            'We could not load the theme catalog right now. Check your connection and try again.',
            'Retry', () => createThemeStoreContent(container));
        return;
      }

      const empty = document.createElement('p');
      empty.className = 'setting-row__description';
      empty.textContent = 'No themes available.';
      empty.setAttribute('data-maho-identifier', 'settings.themeStore.emptyState');
      container.appendChild(empty);
      return;
    }

    let selectedSpaceId = spaces.length > 0 ? spaces[0]!.id : '';

    if (!success) {
      const notice = document.createElement('p');
      notice.className = 'setting-row__description';
      notice.textContent = 'Showing cached themes while the remote catalog is unavailable.';
      container.appendChild(notice);
    }

    if (spaces.length > 0) {
      const spaceRow = document.createElement('section');
      spaceRow.className = 'setting-row setting-row--first';
      const spaceCopy = document.createElement('div');
      spaceCopy.className = 'setting-row__copy';
      const spaceTitle = document.createElement('h4');
      spaceTitle.className = 'setting-row__title';
      spaceTitle.textContent = 'Apply to space';
      spaceCopy.appendChild(spaceTitle);

      const spaceControl = document.createElement('div');
      spaceControl.className = 'setting-row__control';
      const spaceWrapper = document.createElement('div');
      spaceWrapper.className = 'setting-input-shell setting-input-shell--select';
      const spaceSelect = document.createElement('select');
      spaceSelect.className = 'setting-input setting-select';
      spaceSelect.setAttribute('aria-label', 'Target space');
      for (const space of spaces) {
        const opt = document.createElement('option');
        opt.value = space.id;
        opt.textContent = space.name;
        spaceSelect.appendChild(opt);
      }
      spaceSelect.addEventListener('change', () => {
        selectedSpaceId = spaceSelect.value;
      });
      const indicator = document.createElement('span');
      indicator.className = 'setting-input-shell__suffix setting-input-shell__suffix--icon';
      indicator.textContent = '\u25be';
      indicator.setAttribute('aria-hidden', 'true');
      spaceWrapper.append(spaceSelect, indicator);
      spaceControl.appendChild(spaceWrapper);
      spaceRow.append(spaceCopy, spaceControl);
      container.appendChild(spaceRow);
    }

    const rows = document.createElement('div');
    rows.className = 'preferences-card__rows';

    for (const theme of themes) {
      rows.appendChild(createThemeRow(theme, () => selectedSpaceId));
    }

    container.appendChild(rows);
  });
}

function createThemeRow(
    theme: ThemeStoreEntry,
    getSpaceId: () => string): HTMLElement {
  const row = document.createElement('section');
  row.className = 'setting-row';

  const copy = document.createElement('div');
  copy.className = 'setting-row__copy';
  const title = document.createElement('h4');
  title.className = 'setting-row__title';
  title.textContent = theme.name;
  const desc = document.createElement('p');
  desc.className = 'setting-row__description';
  const parts: string[] = [];
  if (theme.author) parts.push(`by ${theme.author}`);
  if (theme.description) parts.push(theme.description);
  desc.textContent = parts.join(' \u00b7 ') || 'Community theme';
  copy.append(title, desc);

  const controls = document.createElement('div');
  controls.className = 'setting-row__control';
  const applyBtn = document.createElement('button');
  applyBtn.type = 'button';
  applyBtn.className = 'setting-switch';
  applyBtn.textContent = 'Apply';
  applyBtn.setAttribute('data-maho-identifier', `settings.themeStore.install-${theme.id}`);
  applyBtn.addEventListener('click', async () => {
    const spaceId = getSpaceId();
    if (!spaceId || !theme.themeJson) return;
    applyBtn.disabled = true;
    applyBtn.textContent = 'Applying\u2026';
    const {success} = await pageHandler.applyThemeToSpace(spaceId, theme.themeJson);
    applyBtn.disabled = false;
    applyBtn.textContent = success ? 'Applied' : 'Failed';
  });
  controls.appendChild(applyBtn);

  row.append(copy, controls);
  return row;
}

function createSectionHeader(title: string, spaced = false): HTMLElement {
  const header = document.createElement('div');
  header.className = spaced ?
      'content-card__header content-card__header--spaced' :
      'content-card__header';

  const heading = document.createElement('h3');
  heading.className = 'content-card__title';
  heading.textContent = title;
  header.appendChild(heading);
  return header;
}

function createManagedSettingRow(
    titleText: string, descriptionText: string,
    isFirstRow = false): {row: HTMLElement; controlArea: HTMLDivElement} {
  const row = document.createElement('section');
  row.className = 'setting-row';
  if (isFirstRow) {
    row.classList.add('setting-row--first');
  }

  const copy = document.createElement('div');
  copy.className = 'setting-row__copy';

  const title = document.createElement('h4');
  title.className = 'setting-row__title';
  title.textContent = titleText;

  const description = document.createElement('p');
  description.className = 'setting-row__description';
  description.textContent = descriptionText;
  copy.append(title, description);

  const controlArea = document.createElement('div');
  controlArea.className = 'setting-row__control';

  row.append(copy, controlArea);
  return {row, controlArea};
}

function createTextInputShell(
    placeholder: string, ariaLabel: string,
    value = ''): {wrapper: HTMLLabelElement; input: HTMLInputElement} {
  const wrapper = document.createElement('label');
  wrapper.className = 'setting-input-shell';

  const input = document.createElement('input');
  input.className = 'setting-input';
  input.type = 'text';
  input.value = value;
  input.placeholder = placeholder;
  input.setAttribute('aria-label', ariaLabel);

  wrapper.appendChild(input);
  return {wrapper, input};
}

function createActionButton(label: string): HTMLButtonElement {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'setting-switch';
  button.textContent = label;
  return button;
}

function createAsyncToggleButton(
    label: string, enabled: boolean,
    onToggle: (nextEnabled: boolean) => Promise<void>|void): HTMLButtonElement {
  const toggle = document.createElement('button');
  toggle.type = 'button';
  toggle.className = 'setting-switch';
  toggle.setAttribute('role', 'switch');
  toggle.setAttribute('aria-checked', String(enabled));
  toggle.setAttribute('aria-label', label);
  if (enabled) {
    toggle.classList.add('is-on');
  }

  const track = document.createElement('span');
  track.className = 'setting-switch__track';

  const thumb = document.createElement('span');
  thumb.className = 'setting-switch__thumb';
  track.appendChild(thumb);

  const status = document.createElement('span');
  status.className = 'setting-switch__status';
  status.textContent = enabled ? 'On' : 'Off';

  toggle.append(track, status);
  toggle.addEventListener('click', async () => {
    if (toggle.disabled) {
      return;
    }

    toggle.disabled = true;
    try {
      await onToggle(toggle.getAttribute('aria-checked') !== 'true');
    } finally {
      toggle.disabled = false;
    }
  });

  return toggle;
}

function createTransientId(prefix: string): string {
  const randomUUID = globalThis.crypto &&
          typeof globalThis.crypto.randomUUID === 'function' ?
      globalThis.crypto.randomUUID.bind(globalThis.crypto) :
      null;
  if (randomUUID) {
    return `${prefix}-${randomUUID()}`;
  }

  return `${prefix}-${Date.now()}-${Math.random().toString(16).slice(2)}`;
}

function createAutofillContent(container: HTMLElement) {
  container.textContent = 'Loading autofill data\u2026';

  Promise.all([
    pageHandler.getAutofillAddresses(),
    pageHandler.getAutofillPayments(),
  ]).then(([{addresses}, {payments}]: [{addresses: AutofillAddress[]}, {payments: AutofillPayment[]}]) => {
    container.textContent = '';

    container.appendChild(createSectionHeader('Saved Addresses'));

    const addrRows = document.createElement('div');
    addrRows.className = 'preferences-card__rows';
    if (addresses.length === 0) {
      const empty = document.createElement('p');
      empty.className = 'setting-row__description';
      empty.textContent = 'No saved addresses.';
      addrRows.appendChild(empty);
    }

    addresses.forEach((address, index) => {
      const locationParts = [
        address.addressLine1,
        address.addressLine2,
        [address.city, address.state, address.postalCode].filter(Boolean).join(' '),
        address.country,
      ].filter(Boolean);
      const contactParts = [address.phone, address.email].filter(Boolean);
      const descriptionParts: string[] = [];
      if (locationParts.length > 0) {
        descriptionParts.push(locationParts.join(', '));
      }
      if (contactParts.length > 0) {
        descriptionParts.push(contactParts.join(' · '));
      }

      const {row, controlArea} = createManagedSettingRow(
          address.name || address.email || address.addressLine1 || 'Unnamed address',
          descriptionParts.join(' · ') || 'No address details', index === 0);
      controlArea.classList.add('setting-actions');

      const deleteButton = createActionButton('Delete');
      deleteButton.setAttribute(
          'aria-label',
          `Delete address ${address.name || address.addressLine1 || address.id}`);
      deleteButton.addEventListener('click', async () => {
        deleteButton.disabled = true;
        try {
          await pageHandler.deleteAutofillAddress(address.id);
        } finally {
          createAutofillContent(container);
        }
      });

      controlArea.appendChild(deleteButton);
      addrRows.appendChild(row);
    });

    const addAddressRow = createManagedSettingRow(
        'Add address',
        'Create a new saved address for form autofill. Fields left blank stay empty.',
        addresses.length === 0);
    addAddressRow.controlArea.classList.add('setting-row__control--stacked');

    const addressFields = document.createElement('div');
    addressFields.className = 'setting-control-stack';

    const {wrapper: addressNameWrapper, input: addressNameInput} = createTextInputShell(
        'Full name', 'Address full name');
    const {wrapper: addressLine1Wrapper, input: addressLine1Input} = createTextInputShell(
        'Address line 1', 'Address line 1');
    const {wrapper: addressLine2Wrapper, input: addressLine2Input} = createTextInputShell(
        'Address line 2', 'Address line 2');
    const {wrapper: cityWrapper, input: cityInput} = createTextInputShell(
        'City', 'Address city');
    const {wrapper: stateWrapper, input: stateInput} = createTextInputShell(
        'State or region', 'Address state or region');
    const {wrapper: postalWrapper, input: postalInput} = createTextInputShell(
        'Postal code', 'Address postal code');
    const {wrapper: countryWrapper, input: countryInput} = createTextInputShell(
        'Country', 'Address country');
    const {wrapper: phoneWrapper, input: phoneInput} = createTextInputShell(
        'Phone', 'Address phone number');
    const {wrapper: emailWrapper, input: emailInput} = createTextInputShell(
        'Email', 'Address email');

    addressFields.append(
        addressNameWrapper, addressLine1Wrapper, addressLine2Wrapper, cityWrapper,
        stateWrapper, postalWrapper, countryWrapper, phoneWrapper, emailWrapper);

    const addAddressActions = document.createElement('div');
    addAddressActions.className = 'setting-actions';
    const addAddressButton = createActionButton('Add Address');
    addAddressButton.addEventListener('click', async () => {
      const values = [
        addressNameInput,
        addressLine1Input,
        addressLine2Input,
        cityInput,
        stateInput,
        postalInput,
        countryInput,
        phoneInput,
        emailInput,
      ].map(input => input.value.trim());
      if (!values.some(Boolean)) {
        return;
      }

      addAddressButton.disabled = true;
      try {
        await pageHandler.addAutofillAddress(
            createTransientId('autofill-address'), values[0]!, values[1]!,
            values[2]!, values[3]!, values[4]!, values[5]!, values[6]!,
            values[7]!, values[8]!);
      } finally {
        createAutofillContent(container);
      }
    });
    addAddressActions.appendChild(addAddressButton);

    addAddressRow.controlArea.append(addressFields, addAddressActions);
    addrRows.appendChild(addAddressRow.row);
    container.appendChild(addrRows);

    container.appendChild(createSectionHeader('Payment Methods', true));

    const payRows = document.createElement('div');
    payRows.className = 'preferences-card__rows';
    if (payments.length === 0) {
      const empty = document.createElement('p');
      empty.className = 'setting-row__description';
      empty.textContent = 'No saved payment methods.';
      payRows.appendChild(empty);
    }

    payments.forEach((payment, index) => {
      const descriptionParts = [
        payment.cardholderName,
        payment.expiration ? `Exp ${payment.expiration}` : '',
      ].filter(Boolean);
      const {row, controlArea} = createManagedSettingRow(
          `${payment.cardNetwork || 'Card'} •••• ${payment.lastFour || '????'}`,
          descriptionParts.join(' · ') || 'No details', index === 0);
      controlArea.classList.add('setting-actions');

      const deleteButton = createActionButton('Delete');
      deleteButton.setAttribute(
          'aria-label',
          `Delete payment method ${payment.cardholderName || payment.lastFour || payment.id}`);
      deleteButton.addEventListener('click', async () => {
        deleteButton.disabled = true;
        try {
          await pageHandler.deleteAutofillPayment(payment.id);
        } finally {
          createAutofillContent(container);
        }
      });

      controlArea.appendChild(deleteButton);
      payRows.appendChild(row);
    });

    const addPaymentRow = createManagedSettingRow(
        'Add payment method',
        'Store a payment method for faster checkout autofill.',
        payments.length === 0);
    addPaymentRow.controlArea.classList.add('setting-row__control--stacked');

    const paymentFields = document.createElement('div');
    paymentFields.className = 'setting-control-stack';

    const {wrapper: networkWrapper, input: networkInput} = createTextInputShell(
        'Card network', 'Payment card network');
    const {wrapper: lastFourWrapper, input: lastFourInput} = createTextInputShell(
        'Last four digits', 'Payment card last four digits');
    const {wrapper: expirationWrapper, input: expirationInput} = createTextInputShell(
        'Expiration (MM/YY)', 'Payment card expiration');
    const {wrapper: cardholderWrapper, input: cardholderInput} = createTextInputShell(
        'Cardholder name', 'Payment cardholder name');

    paymentFields.append(
        networkWrapper, lastFourWrapper, expirationWrapper, cardholderWrapper);

    const addPaymentActions = document.createElement('div');
    addPaymentActions.className = 'setting-actions';
    const addPaymentButton = createActionButton('Add Payment');
    addPaymentButton.addEventListener('click', async () => {
      const values = [networkInput, lastFourInput, expirationInput, cardholderInput]
                         .map(input => input.value.trim());
      if (!values.some(Boolean)) {
        return;
      }

      addPaymentButton.disabled = true;
      try {
        await pageHandler.addAutofillPayment(
            createTransientId('autofill-payment'), values[0]!, values[1]!,
            values[2]!, values[3]!);
      } finally {
        createAutofillContent(container);
      }
    });
    addPaymentActions.appendChild(addPaymentButton);

    addPaymentRow.controlArea.append(paymentFields, addPaymentActions);
    payRows.appendChild(addPaymentRow.row);
    container.appendChild(payRows);
  }).catch(() => showDomainError(container, 'Failed to load autofill data.'));
}

function createPasswordsContent(container: HTMLElement) {
  const passwordsEnabled = getPasswordsEnabled(currentSettings);
  const providerStatus = currentPasswordProviderStatus;
  const header = document.createElement('div');
  header.className = 'content-card__header';

  const titleGroup = document.createElement('div');
  titleGroup.className = 'content-card__title-group';

  const heading = document.createElement('h3');
  heading.className = 'content-card__title';
  heading.textContent = 'Saved Passwords';

  const copy = document.createElement('p');
  copy.className = 'content-card__copy';
  copy.textContent = providerStatus ?
      `Passwords provider: ${providerStatus.displayName}.` :
      'Loading password provider…';

  titleGroup.append(heading, copy);
  header.appendChild(titleGroup);

  const providerCard = document.createElement('div');
  providerCard.className = 'content-card__copy passwords-provider-card';

  const enabledRow = document.createElement('section');
  enabledRow.className = 'setting-row setting-row--first';

  const enabledCopy = document.createElement('div');
  enabledCopy.className = 'setting-row__copy';

  const enabledTitle = document.createElement('h4');
  enabledTitle.className = 'setting-row__title';
  enabledTitle.textContent = 'Passwords';

  const enabledDesc = document.createElement('p');
  enabledDesc.className = 'setting-row__description';
  enabledDesc.textContent = 'Enable password management to show saved passwords.';
  enabledCopy.append(enabledTitle, enabledDesc);

  const enabledControl = document.createElement('div');
  enabledControl.className = 'setting-row__control';
  const enabledToggle = createAsyncToggleButton(
      'Passwords', passwordsEnabled, async nextEnabled => {
        const success = await commitSettingValue(
            'autofill.passwords_enabled', nextEnabled ? 'true' : 'false',
            enabledToggle);
        if (!success) {
          return;
        }
        currentSettings = updateSettingValue(
            currentSettings, 'autofill.passwords_enabled',
            nextEnabled ? 'true' : 'false');
        renderSettings(currentSettings);
      });
  enabledControl.appendChild(enabledToggle);
  enabledRow.append(enabledCopy, enabledControl);
  providerCard.appendChild(enabledRow);

  const providerRow = document.createElement('section');
  providerRow.className = 'setting-row';
  const providerCopy = document.createElement('div');
  providerCopy.className = 'setting-row__copy';
  const providerTitle = document.createElement('h4');
  providerTitle.className = 'setting-row__title';
  providerTitle.textContent = providerStatus ?
      `Password provider · ${providerStatus.displayName}` :
      'Password provider';
  const providerDesc = document.createElement('p');
  providerDesc.className = 'setting-row__description';
  providerDesc.id = 'password-provider-description';
  providerDesc.textContent = providerStatus ?
      providerStatus.description :
      'Loading provider details…';
  providerCopy.append(providerTitle, providerDesc);

  const providerControl = document.createElement('div');
  providerControl.className = 'setting-row__control';
  const providerBadge = document.createElement('span');
  providerBadge.className = 'setting-switch setting-switch--readonly';
  providerBadge.textContent = providerStatus ?
      (providerStatus.provider === PasswordProviderKind.kMahoNative ?
           'Maho Native' :
           providerStatus.provider === PasswordProviderKind.kBitwarden ?
               'Bitwarden' :
               '1Password') :
      'Loading…';
  providerControl.appendChild(providerBadge);
  providerRow.append(providerCopy, providerControl);
  providerCard.appendChild(providerRow);

  const providerHint = document.createElement('p');
  providerHint.className = 'content-card__copy';
  providerHint.textContent = providerStatus ?
      `Capabilities: ${providerStatus.capabilities.canListSavedPasswords ? 'list' : 'no list'}, ${providerStatus.capabilities.canSearchSavedPasswords ? 'search' : 'no search'}, ${providerStatus.capabilities.canAddSavedPasswords ? 'add' : 'no add'}, ${providerStatus.capabilities.canEditSavedPasswords ? 'edit' : 'no edit'}, ${providerStatus.capabilities.canRevealPasswords ? 'reveal' : 'no reveal'}, ${providerStatus.capabilities.canDeleteSavedPasswords ? 'delete' : 'no delete'}.` :
      'Capabilities loading…';
  providerCard.appendChild(providerHint);

  header.appendChild(providerCard);
  container.appendChild(header);

  const shell = document.createElement('div');
  shell.className = 'browser-settings-shell browser-settings-shell--wide';

  if (!providerStatus) {
    const notice = document.createElement('p');
    notice.className = 'setting-row__description';
    notice.textContent = 'Loading password provider status…';
    shell.appendChild(notice);
  } else if (!isMahoNativeProvider(providerStatus.provider)) {
    const providerNotice = document.createElement('section');
    providerNotice.className = 'setting-row';

    const providerCopy = document.createElement('div');
    providerCopy.className = 'setting-row__copy';

    const providerTitle = document.createElement('h4');
    providerTitle.className = 'setting-row__title';
    providerTitle.textContent = `${providerStatus.displayName} is managing passwords`;

    const providerDescription = document.createElement('p');
    providerDescription.className = 'setting-row__description';
    providerDescription.textContent =
        `${providerStatus.displayName} handles save, fill, and autosave through its extension. Chromium's built-in password manager is suppressed in this mode, and Maho Native passwords are not manageable here.`;

    providerCopy.append(providerTitle, providerDescription);

    const providerControl = document.createElement('div');
    providerControl.className = 'setting-row__control';
    const providerBadge = document.createElement('span');
    providerBadge.className = 'setting-switch setting-switch--readonly';
    providerBadge.textContent = 'Extension-managed';
    providerControl.appendChild(providerBadge);

    providerNotice.append(providerCopy, providerControl);
    shell.appendChild(providerNotice);

    if (currentSavedPasswords.length > 0) {
      const localNotice = document.createElement('p');
      localNotice.className = 'setting-row__description';
      localNotice.textContent = `${currentSavedPasswords.length} Maho Native password${currentSavedPasswords.length === 1 ? '' : 's'} remain stored locally and can only be managed after switching back to Maho Native.`;
      shell.appendChild(localNotice);
    }
  } else if (!passwordsEnabled) {
    const notice = document.createElement('p');
    notice.className = 'setting-row__description';
    notice.textContent = 'Passwords is turned off. Turn it on to show saved passwords.';
    shell.appendChild(notice);
  } else {
    const nativeSection = document.createElement('div');
    nativeSection.className = 'cards-grid';
    nativeSection.appendChild(createNativePasswordsContent());
    shell.appendChild(nativeSection);
  }

  container.appendChild(shell);
}

function createNativePasswordsContent(): HTMLElement {
  const content = document.createElement('div');
  const providerStatus = currentPasswordProviderStatus;
  const passwords = currentSavedPasswords;
  const searchRow = document.createElement('section');
  searchRow.className = 'setting-row setting-row--first';

  const searchCopy = document.createElement('div');
  searchCopy.className = 'setting-row__copy';
  const searchTitle = document.createElement('h4');
  searchTitle.className = 'setting-row__title';
  searchTitle.textContent = 'Search saved passwords';
  const searchDesc = document.createElement('p');
  searchDesc.className = 'setting-row__description';
  searchDesc.textContent = 'Filter the native password list below.';
  searchCopy.append(searchTitle, searchDesc);

  const searchControl = document.createElement('div');
  searchControl.className = 'setting-row__control';
  const searchInput = document.createElement('input');
  searchInput.className = 'setting-input';
  searchInput.type = 'search';
  searchInput.placeholder = 'Search passwords';
  searchInput.value = currentPasswordSearchQuery;
  searchInput.addEventListener('input', () => {
    currentPasswordSearchQuery = searchInput.value.trim();
    refreshSavedPasswords();
  });
  searchControl.appendChild(searchInput);
  searchRow.append(searchCopy, searchControl);
  content.appendChild(searchRow);

  if (providerStatus?.capabilities.canAddSavedPasswords) {
    const addRow = createManagedSettingRow(
        'Add password', 'Store a new native password entry.', passwords.length === 0);
    addRow.controlArea.classList.add('setting-row__control--stacked');

    const addFields = document.createElement('div');
    addFields.className = 'setting-control-stack';

    const {wrapper: domainWrapper, input: domainInput} = createTextInputShell(
        'Domain', 'Password domain');
    const {wrapper: usernameWrapper, input: usernameInput} = createTextInputShell(
        'Username', 'Password username');
    const {wrapper: passwordWrapper, input: passwordInput} = createTextInputShell(
        'Password', 'Password value');
    passwordInput.type = 'password';
    addFields.append(domainWrapper, usernameWrapper, passwordWrapper);

    const addActions = document.createElement('div');
    addActions.className = 'setting-actions';
    const addButton = createActionButton('Add Password');
    addButton.addEventListener('click', async () => {
      const domain = domainInput.value.trim();
      const username = usernameInput.value.trim();
      const password = passwordInput.value;
      if (!domain) {
        return;
      }

      addButton.disabled = true;
      try {
        const success = await pageHandler.addPassword(domain, username, password);
        if (!success) {
          return;
        }
        domainInput.value = '';
        usernameInput.value = '';
        passwordInput.value = '';
        await refreshSavedPasswords();
      } finally {
        addButton.disabled = false;
      }
    });
    addActions.appendChild(addButton);
    addRow.controlArea.append(addFields, addActions);
    content.appendChild(addRow.row);
  }

  const list = document.createElement('div');
  list.className = 'preferences-card__rows';

  if (passwords.length === 0) {
    const empty = document.createElement('p');
    empty.className = 'setting-row__description';
    empty.textContent = 'No saved passwords.';
    list.appendChild(empty);
  }

  for (const savedPassword of passwords) {
    const row = document.createElement('section');
    row.className = 'setting-row';
    const copy = document.createElement('div');
    copy.className = 'setting-row__copy';
    const title = document.createElement('h4');
    title.className = 'setting-row__title';
    title.textContent = `${savedPassword.username || 'Unknown user'} @ ${savedPassword.domain || 'Unknown domain'}`;
    const desc = document.createElement('p');
    desc.className = 'setting-row__description';
    desc.textContent = [
      savedPassword.createdAt ? `Created ${savedPassword.createdAt}` : '',
      savedPassword.lastUsed ? `Last used ${savedPassword.lastUsed}` : '',
    ].filter(Boolean).join(' · ');
    copy.append(title, desc);

    const passwordLine = document.createElement('p');
    passwordLine.className = 'setting-row__description';
    if (savedPassword.password) {
      passwordLine.textContent = revealedPasswordIds.has(savedPassword.id) ?
          `Password: ${savedPassword.password}` :
          'Password hidden';
    } else {
      passwordLine.textContent = 'No password stored.';
    }
    copy.appendChild(passwordLine);

    const control = document.createElement('div');
    control.className = 'setting-row__control';
    if (providerStatus?.capabilities.canEditSavedPasswords) {
      const usernameInput = document.createElement('input');
      usernameInput.className = 'setting-input';
      usernameInput.type = 'text';
      usernameInput.value = savedPassword.username;
      usernameInput.placeholder = 'Username';
      usernameInput.setAttribute('aria-label', `Username for ${savedPassword.domain}`);

      const saveButton = createActionButton('Save Username');
      saveButton.addEventListener('click', async () => {
        const username = usernameInput.value.trim();
        saveButton.disabled = true;
        try {
          const success = await pageHandler.updatePasswordUsername(
              savedPassword.id, username);
          if (!success) {
            return;
          }
          await refreshSavedPasswords();
        } finally {
          saveButton.disabled = false;
        }
      });

      control.append(usernameInput, saveButton);
    }

    if (providerStatus?.capabilities.canRevealPasswords && savedPassword.password) {
      const revealButton = createActionButton(
          revealedPasswordIds.has(savedPassword.id) ? 'Hide' : 'Reveal');
      revealButton.addEventListener('click', () => {
        if (revealedPasswordIds.has(savedPassword.id)) {
          revealedPasswordIds.delete(savedPassword.id);
        } else {
          revealedPasswordIds.add(savedPassword.id);
        }
        renderSettings(currentSettings);
      });
      control.appendChild(revealButton);
    }

    const deleteButton = createActionButton('Delete');
    deleteButton.addEventListener('click', async () => {
      deleteButton.disabled = true;
      try {
        const success = await pageHandler.deletePassword(savedPassword.id);
        if (!success) {
          return;
        }
        revealedPasswordIds.delete(savedPassword.id);
        await refreshSavedPasswords();
      } finally {
        deleteButton.disabled = false;
      }
    });
    control.appendChild(deleteButton);

    row.append(copy, control);
    list.appendChild(row);
  }

  content.appendChild(list);
  return content;
}

function createContentBlockerContent(container: HTMLElement) {
  container.textContent = 'Loading content blocker\u2026';

  Promise.all([
    pageHandler.getFilterLists(),
    pageHandler.getContentBlockerStats(),
  ]).then(([{lists}, {stats}]: [{lists: FilterListInfo[]}, {stats: ContentBlockerStats}]) => {
    container.textContent = '';

    container.appendChild(createSectionHeader('Content Blocker'));

    const statsRow = document.createElement('section');
    statsRow.className = 'setting-row setting-row--first';
    const statsCopy = document.createElement('div');
    statsCopy.className = 'setting-row__copy';
    const statsTitle = document.createElement('h4');
    statsTitle.className = 'setting-row__title';
    statsTitle.textContent = 'Engine Status';
    const statsDesc = document.createElement('p');
    statsDesc.className = 'setting-row__description';
    statsDesc.textContent = `${stats.totalRuleCount.toLocaleString()} rules active \u00b7 ${stats.filterListCount} filter list(s)`;
    statsCopy.append(statsTitle, statsDesc);
    const statsControl = document.createElement('div');
    statsControl.className = 'setting-row__control setting-actions';
    const rebuildBtn = document.createElement('button');
    rebuildBtn.type = 'button';
    rebuildBtn.className = 'setting-switch';
    rebuildBtn.textContent = 'Rebuild';
    rebuildBtn.addEventListener('click', async () => {
      rebuildBtn.disabled = true;
      try {
        await pageHandler.rebuildContentRules();
      } finally {
        createContentBlockerContent(container);
      }
    });
    statsControl.appendChild(rebuildBtn);
    statsRow.append(statsCopy, statsControl);
    container.appendChild(statsRow);

    container.appendChild(createSectionHeader('Filter Lists', true));

    const rows = document.createElement('div');
    rows.className = 'preferences-card__rows';

    if (lists.length === 0) {
      const empty = document.createElement('p');
      empty.className = 'setting-row__description';
      empty.textContent = 'No filter lists configured.';
      rows.appendChild(empty);
    }

    lists.forEach((filterList, index) => {
      const descriptionParts = [
        filterList.url,
        `${filterList.ruleCount.toLocaleString()} rules`,
        filterList.enabled ? 'Enabled' : 'Disabled',
      ].filter(Boolean);
      const {row, controlArea} = createManagedSettingRow(
          filterList.name || filterList.id,
          descriptionParts.join(' · '), index === 0);
      controlArea.classList.add('setting-actions');

      const toggle = createAsyncToggleButton(
          `Enable ${filterList.name || filterList.id}`, filterList.enabled,
          async (nextEnabled: boolean) => {
            await pageHandler.toggleFilterList(filterList.id, nextEnabled);
            createContentBlockerContent(container);
          });

      const removeButton = createActionButton('Remove');
      removeButton.setAttribute(
          'aria-label', `Remove filter list ${filterList.name || filterList.id}`);
      removeButton.addEventListener('click', async () => {
        removeButton.disabled = true;
        try {
          await pageHandler.removeFilterList(filterList.id);
        } finally {
          createContentBlockerContent(container);
        }
      });

      controlArea.append(toggle, removeButton);
      rows.appendChild(row);
    });

    const addListRow = createManagedSettingRow(
        'Add filter list', 'Add a custom filter list by name and source URL.',
        lists.length === 0);
    addListRow.controlArea.classList.add('setting-row__control--stacked');

    const listFields = document.createElement('div');
    listFields.className = 'setting-control-stack';

    const {wrapper: listNameWrapper, input: listNameInput} = createTextInputShell(
        'Filter list name', 'Filter list name');
    const {wrapper: listUrlWrapper, input: listUrlInput} = createTextInputShell(
        'https://example.com/filters.txt', 'Filter list URL');
    listFields.append(listNameWrapper, listUrlWrapper);

    const addListActions = document.createElement('div');
    addListActions.className = 'setting-actions';
    const addListButton = createActionButton('Add Filter List');
    addListButton.addEventListener('click', async () => {
      const name = listNameInput.value.trim();
      const url = listUrlInput.value.trim();
      if (!name || !url) {
        return;
      }

      addListButton.disabled = true;
      try {
        await pageHandler.addFilterList(createTransientId('filter-list'), name, url);
      } finally {
        createContentBlockerContent(container);
      }
    });
    addListActions.appendChild(addListButton);

    addListRow.controlArea.append(listFields, addListActions);
    rows.appendChild(addListRow.row);
    container.appendChild(rows);
  }).catch(() => showDomainError(container, 'Failed to load content blocker.'));
}

function createExtensionsContent(container: HTMLElement) {
  container.textContent = 'Loading extensions\u2026';

  pageHandler.getInstalledExtensions().then(({extensions}: {extensions: InstalledExtension[]}) => {
    container.textContent = '';

    container.appendChild(createSectionHeader('Installed Extensions'));

    const rows = document.createElement('div');
    rows.className = 'preferences-card__rows';

    if (extensions.length === 0) {
      const empty = document.createElement('p');
      empty.className = 'setting-row__description';
      empty.textContent = 'No extensions installed.';
      rows.appendChild(empty);
    }

    extensions.forEach((extension, index) => {
      const descriptionParts = [
        extension.version ? `v${extension.version}` : '',
        extension.enabled ? 'Enabled' : 'Disabled',
      ].filter(Boolean);
      if (extension.description) {
        descriptionParts.push(extension.description);
      }

      const {row, controlArea} = createManagedSettingRow(
          extension.name || extension.id,
          descriptionParts.join(' · '), index === 0);
      controlArea.classList.add('setting-actions');

      const toggle = createAsyncToggleButton(
          `Enable ${extension.name || extension.id}`, extension.enabled,
          async () => {
            await pageHandler.toggleExtension(extension.id);
            createExtensionsContent(container);
          });

      const removeButton = createActionButton('Remove');
      removeButton.setAttribute(
          'aria-label', `Remove extension ${extension.name || extension.id}`);
      removeButton.addEventListener('click', async () => {
        removeButton.disabled = true;
        try {
          await pageHandler.removeExtension(extension.id);
        } finally {
          createExtensionsContent(container);
        }
      });

      controlArea.append(toggle, removeButton);
      rows.appendChild(row);
    });
    container.appendChild(rows);
  }).catch(() => showDomainError(container, 'Failed to load extensions.'));
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

  const title = document.createElement('h2');
  title.className = 'preferences-section__title';
  title.textContent = pane.title;

  header.appendChild(title);
  card.appendChild(header);
  section.appendChild(card);
  return section;
}

function createContentCard(
    title: string, description: string, settings: SettingValue[]): HTMLElement {
  const card = document.createElement('article');
  card.className = 'content-card';

  const header = document.createElement('div');
  header.className = 'content-card__header';

  const titleGroup = document.createElement('div');
  titleGroup.className = 'content-card__title-group';

  const heading = document.createElement('h3');
  heading.className = 'content-card__title';
  heading.textContent = title;

  titleGroup.appendChild(heading);
  if (description) {
    const copy = document.createElement('p');
    copy.className = 'content-card__copy';
    copy.textContent = description;
    titleGroup.appendChild(copy);
  }
  header.appendChild(titleGroup);

  const rows = document.createElement('div');
  rows.className = 'preferences-card__rows';
  settings.forEach((setting, index) => rows.appendChild(createSettingRow(setting, index === 0)));

  card.append(header, rows);
  return card;
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
  title.textContent = meta ? meta.label : item.key;

  const description = document.createElement('p');
  description.className = 'setting-row__description';
  description.textContent = meta ? meta.description : `Metadata missing for ${item.key}.`;

  copy.append(title, description);

  const controlArea = document.createElement('div');
  controlArea.className = 'setting-row__control';
  if (meta) {
    controlArea.appendChild(createControl(item, meta));
  } else {
    const notice = document.createElement('p');
    notice.className = 'setting-row__description';
    notice.textContent = `Unsupported setting metadata for ${item.key}. Current value: ${item.value || '(empty)'}`;
    controlArea.appendChild(notice);
  }

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
  return success;
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

  const hasKnownValue = options.some(option => option.value === item.value);
  if (!hasKnownValue) {
    select.setAttribute('aria-invalid', 'true');
  }
  select.value = hasKnownValue ? item.value : (options[0]?.value || '');
  select.addEventListener('change', () => {
    void commitSettingValue(item.key, select.value, select);
  });

  if (!hasKnownValue) {
    const notice = document.createElement('p');
    notice.className = 'setting-row__description';
    notice.textContent = `Current value ${item.value || '(empty)'} is not recognized by the metadata registry.`;
    wrapper.appendChild(notice);
  }

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

function getSettingMetadata(item: SettingValue): SettingMetadata|null {
  const metadata = SETTING_METADATA[item.key];
  if (metadata) {
    return metadata;
  }

  return null;
}

function getSectionId(key: string): string {
  return `section-${key}`;
}

document.addEventListener('DOMContentLoaded', initialize);
