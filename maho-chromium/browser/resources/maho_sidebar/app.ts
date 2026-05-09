import {
  PageHandlerFactory,
  PageHandlerRemote,
  SpaceInfo,
  TabInfo,
} from './maho_sidebar.mojom-webui.js';
import {PageCallbackRouter} from './maho_sidebar.mojom-webui.js';
import type {ATCRule, ProfileInfo} from './maho_sidebar.mojom-webui.js';
import {
  SIDEBAR_THEME_COLOR_PRESETS as PRESET_COLORS,
  computeHarmonyColors,
  hexToHsb,
  hsbToHex,
  hsbToRgb,
  normalizeHue,
} from '//resources/maho_common/maho_theme_utils.js';
import type {
  ColorHarmony,
  SpaceColor,
  SpaceThemeData,
} from '//resources/maho_common/maho_theme_utils.js';

type WizardMode =
    'create'|'change-icon'|'edit-theme'|'theme-store'|'manage-spaces';
type WizardStep = 'icon'|'details';

interface SidebarTypeEnvelope {
  profiles?: ProfileInfo[];
  atcRules?: ATCRule[];
}

interface Elements {
  app: HTMLElement;
  body: HTMLElement;
  strip: HTMLElement;
  stripRail: HTMLElement;
  tabPanel: HTMLElement;
  tabList: HTMLElement;
  managePanel: HTMLElement;
  sidebarTitle: HTMLElement;
  contextMenuLayer: HTMLElement;
}

interface EmojiOption {
  emoji: string;
  label: string;
  keywords: string[];
}


interface ThemeStoreEntry {
  id: string;
  name: string;
  description: string;
  author: string;
  version: string;
  theme: unknown;
  preview_url: string|null;
  download_count: number;
  created_at: string;
  updated_at: string;
  tags: string[];
}

interface WizardState {
  mode: WizardMode;
  step: WizardStep;
  targetSpaceId: string|null;
  selectedIcon: string;
  selectedColor: string;
  name: string;
  search: string;
  insertAfterIndex: number;
}

interface ContextMenuState {
  spaceId: string;
  x: number;
  y: number;
  confirmDelete: boolean;
}

interface DragState {
  spaceId: string;
  sourceIndex: number;
  indicatorIndex: number;
}

type SpaceInfoCompat = SpaceInfo & {
  tab_count?: number;
  profileId?: string;
  profile_id?: string;
  position?: number;
};

const sidebarTypeEnvelope: SidebarTypeEnvelope = {};
void sidebarTypeEnvelope;

const EMOJI_OPTIONS: EmojiOption[] = [
  {emoji: '✨', label: 'Sparkles', keywords: ['sparkles', 'magic', 'star', 'glow']},
  {emoji: '📚', label: 'Books', keywords: ['books', 'reading', 'study', 'library']},
  {emoji: '🧠', label: 'Brain', keywords: ['brain', 'focus', 'think', 'deep work']},
  {emoji: '🎯', label: 'Target', keywords: ['target', 'goal', 'work', 'focus']},
  {emoji: '💼', label: 'Briefcase', keywords: ['briefcase', 'office', 'business', 'client']},
  {emoji: '🛠️', label: 'Tools', keywords: ['tools', 'build', 'maker', 'dev']},
  {emoji: '🌊', label: 'Wave', keywords: ['wave', 'water', 'calm', 'blue']},
  {emoji: '🌙', label: 'Moon', keywords: ['moon', 'night', 'dark', 'evening']},
  {emoji: '🌅', label: 'Sunrise', keywords: ['sunrise', 'morning', 'dawn', 'fresh']},
  {emoji: '🔥', label: 'Fire', keywords: ['fire', 'energy', 'hot', 'urgent']},
  {emoji: '💡', label: 'Light Bulb', keywords: ['idea', 'light bulb', 'thinking', 'brainstorm']},
  {emoji: '🎨', label: 'Palette', keywords: ['art', 'paint', 'design', 'creative']},
  {emoji: '🎧', label: 'Headphones', keywords: ['music', 'audio', 'listen', 'sound']},
  {emoji: '📸', label: 'Camera', keywords: ['camera', 'photo', 'image', 'shot']},
  {emoji: '📝', label: 'Memo', keywords: ['notes', 'write', 'draft', 'text']},
  {emoji: '📈', label: 'Chart', keywords: ['chart', 'growth', 'metrics', 'analytics']},
  {emoji: '🧭', label: 'Compass', keywords: ['compass', 'plan', 'explore', 'direction']},
  {emoji: '🚀', label: 'Rocket', keywords: ['rocket', 'launch', 'ship', 'fast']},
  {emoji: '🏡', label: 'House', keywords: ['home', 'house', 'personal', 'cozy']},
  {emoji: '🌿', label: 'Leaf', keywords: ['leaf', 'nature', 'green', 'calm']},
  {emoji: '🍎', label: 'Apple', keywords: ['apple', 'food', 'red', 'fresh']},
  {emoji: '🍋', label: 'Lemon', keywords: ['lemon', 'yellow', 'bright', 'fresh']},
  {emoji: '🥑', label: 'Avocado', keywords: ['avocado', 'green', 'food', 'healthy']},
  {emoji: '☕', label: 'Coffee', keywords: ['coffee', 'cafe', 'drink', 'morning']},
  {emoji: '🎮', label: 'Controller', keywords: ['game', 'controller', 'play', 'gaming']},
  {emoji: '🎬', label: 'Clapper', keywords: ['movie', 'film', 'watch', 'video']},
  {emoji: '🛍️', label: 'Shopping Bag', keywords: ['shopping', 'bag', 'buy', 'store']},
  {emoji: '💬', label: 'Speech Bubble', keywords: ['chat', 'message', 'talk', 'conversation']},
  {emoji: '🔒', label: 'Lock', keywords: ['lock', 'private', 'secure', 'safe']},
  {emoji: '🧪', label: 'Test Tube', keywords: ['science', 'test', 'lab', 'experiment']},
  {emoji: '📰', label: 'Newspaper', keywords: ['news', 'paper', 'read', 'media']},
  {emoji: '🗂️', label: 'Dividers', keywords: ['files', 'organize', 'folder', 'archive']},
  {emoji: '🧳', label: 'Suitcase', keywords: ['travel', 'trip', 'bag', 'vacation']},
  {emoji: '🏃', label: 'Runner', keywords: ['run', 'fitness', 'move', 'energy']},
  {emoji: '🪐', label: 'Planet', keywords: ['planet', 'space', 'orbit', 'cosmic']},
  {emoji: '🌈', label: 'Rainbow', keywords: ['rainbow', 'color', 'bright', 'playful']},
  {emoji: '💎', label: 'Gem', keywords: ['gem', 'premium', 'shine', 'luxury']},
  {emoji: '🧵', label: 'Thread', keywords: ['thread', 'craft', 'weave', 'maker']},
  {emoji: '📍', label: 'Pin', keywords: ['pin', 'location', 'place', 'map']},
  {emoji: '🧸', label: 'Bear', keywords: ['bear', 'soft', 'cozy', 'play']},
];

const COLOR_OPTIONS = [
  '#F16063',
  '#FF9166',
  '#F7C548',
  '#7BC47F',
  '#56C3D9',
  '#5F8BFF',
  '#8B6CF5',
  '#E66BCB',
];


const CREATE_QUERY_PARAM = 'create';
const SWIPE_DEBOUNCE_MS = 300;
const SPACE_ANIMATION_MS = 200;

let pageHandler: PageHandlerRemote;
let callbackRouter: PageCallbackRouter;
let elements: Elements;
let collapsed = false;
let createSpaceOnLoad =
    new URLSearchParams(window.location.search).get(CREATE_QUERY_PARAM) === '1';
let spaces: SpaceInfo[] = [];
let visibleSpaces: SpaceInfo[] = [];
let tabs: TabInfo[] = [];
let profiles: ProfileInfo[] = [];
let previousActiveSpaceId: string|null = null;
let wizardState: WizardState|null = null;
let contextMenuState: ContextMenuState|null = null;
let renameSpaceId: string|null = null;
let renameValue = '';
let renameFocusToken = 0;
let dragState: DragState|null = null;
let lastSwipeAt = 0;
let enteringSpaceIds = new Set<string>();
let themeEditorState: {
  currentColor: SpaceColor;
  previewMode: 'solid'|'zen'|'gradient';
  harmony: ColorHarmony;
  opacity: number;
  selectedPresetIndex: number;
  instructionVisible: boolean;
  canvas: HTMLCanvasElement|null;
  cursorPoint: {x: number; y: number}|null;
}|null = null;
let cachedNoiseData: {width: number; height: number; canvas: HTMLCanvasElement}|null = null;
let exitingSpaceIds = new Set<string>();
let exitingSpaceMap = new Map<string, SpaceInfo>();
let exitingTimers = new Map<string, number>();

function initialize() {
  callbackRouter = new PageCallbackRouter();
  pageHandler = new PageHandlerRemote();

  const factory = PageHandlerFactory.getRemote();
  factory.createPageHandler(
      callbackRouter.$.bindNewPipeAndPassRemote(),
      pageHandler.$.bindNewPipeAndPassReceiver());

  const app = document.querySelector<HTMLElement>('maho-sidebar-app');
  if (!app) {
    return;
  }

  elements = createShell(app);
  bindGlobalEvents();

  callbackRouter.tabsChanged.addListener((nextTabs: TabInfo[]) => {
    tabs = nextTabs;
    renderTabs();
  });

  callbackRouter.spacesChanged.addListener((nextSpaces: SpaceInfo[]) => {
    updateSpaces(nextSpaces);
  });

  callbackRouter.profilesChanged.addListener((nextProfiles: ProfileInfo[]) => {
    profiles = nextProfiles;
  });

  callbackRouter.spaceThemeChanged.addListener((spaceId: string, color: string) => {
    const nextSpaces = spaces.map((space) => {
      if (space.id !== spaceId) {
        return space;
      }
      return {...space, color};
    });
    updateSpaces(nextSpaces, false);
  });

  void pageHandler.getSpaces().then(({spaces: nextSpaces}: {spaces: SpaceInfo[]}) => {
    updateSpaces(nextSpaces);
    if (createSpaceOnLoad) {
      createSpaceOnLoad = false;
      triggerCreateSpace();
    }
  });

  void pageHandler.getTabs().then(({tabs: nextTabs}: {tabs: TabInfo[]}) => {
    tabs = nextTabs;
    renderTabs();
  });

  void pageHandler.getProfiles().then(({profiles: nextProfiles}: {profiles: ProfileInfo[]}) => {
    profiles = nextProfiles;
  });
}

function createShell(app: HTMLElement): Elements {
  app.replaceChildren();

  const header = createHeader();
  const body = document.createElement('div');
  body.className = 'sidebar-body';

  const strip = document.createElement('aside');
  strip.className = 'space-strip';
  strip.setAttribute('aria-label', 'Spaces');
  strip.setAttribute('data-maho-identifier', 'sidebarSpacesStrip');

  const stripRail = document.createElement('div');
  stripRail.className = 'space-strip-rail';

  const tabPanel = document.createElement('section');
  tabPanel.className = 'tab-panel';

  const sidebarTitle = document.createElement('div');
  sidebarTitle.className = 'sidebar-title';

  const tabList = document.createElement('div');
  tabList.className = 'tab-list';

  const managePanel = document.createElement('section');
  managePanel.className = 'manage-spaces-panel';
  managePanel.setAttribute('data-maho-identifier', 'sidebar.manageSpaces.panel');
  managePanel.setAttribute('aria-label', 'Manage Spaces');

  const contextMenuLayer = document.createElement('div');
  contextMenuLayer.className = 'space-context-menu-layer';

  strip.appendChild(stripRail);
  tabPanel.append(sidebarTitle, tabList, managePanel);
  body.append(strip, tabPanel);
  app.append(header, body, contextMenuLayer);

  return {app, body, strip, stripRail, tabPanel, tabList, managePanel, sidebarTitle, contextMenuLayer};
}

function createHeader(): HTMLElement {
  const header = document.createElement('div');
  header.className = 'sidebar-header';

  const heading = document.createElement('div');
  heading.className = 'sidebar-heading';

  const eyebrow = document.createElement('div');
  eyebrow.className = 'sidebar-eyebrow';
  eyebrow.textContent = 'Workspace';

  const title = document.createElement('h2');
  title.textContent = 'Tabs';
  title.setAttribute('data-maho-identifier', 'sidebarTitle');

  heading.append(eyebrow, title);

  const collapseBtn = document.createElement('button');
  collapseBtn.type = 'button';
  collapseBtn.className = 'sidebar-collapse-btn';
  collapseBtn.textContent = '‹';
  collapseBtn.title = 'Collapse sidebar';
  collapseBtn.setAttribute('aria-label', 'Collapse sidebar');
  collapseBtn.setAttribute('data-maho-identifier', 'sidebarCollapseButton');
  collapseBtn.addEventListener('click', toggleCollapse);

  header.append(heading, collapseBtn);
  return header;
}

function bindGlobalEvents() {
  document.addEventListener('keydown', handleGlobalKeyDown);
  document.addEventListener('pointerdown', handleDocumentPointerDown, true);
  window.addEventListener('blur', () => {
    closeContextMenu();
  });
  elements.strip.addEventListener('wheel', handleStripWheel, {passive: true});
  elements.strip.addEventListener('dragover', handleStripDragOver);
  elements.strip.addEventListener('drop', handleStripDrop);
}

function toggleCollapse() {
  collapsed = !collapsed;
  elements.app.classList.toggle('collapsed', collapsed);
  const collapseBtn =
      elements.app.querySelector<HTMLButtonElement>('.sidebar-collapse-btn');
  if (!collapseBtn) {
    return;
  }
  collapseBtn.textContent = collapsed ? '›' : '‹';
  collapseBtn.title = collapsed ? 'Expand sidebar' : 'Collapse sidebar';
  collapseBtn.setAttribute(
      'aria-label', collapsed ? 'Expand sidebar' : 'Collapse sidebar');
}

function updateSpaces(nextSpaces: SpaceInfo[], animateTabs = true) {
  const previousSpaces = spaces.slice();
  const previousIds = new Set(previousSpaces.map((space) => space.id));
  const nextIds = new Set(nextSpaces.map((space) => space.id));
  const nextActiveSpace =
      nextSpaces.find((space) => space.active) || nextSpaces[0] || null;
  const nextActiveSpaceId = nextActiveSpace?.id || null;

  nextSpaces.forEach((space) => {
    if (previousIds.has(space.id)) {
      return;
    }
    enteringSpaceIds.add(space.id);
    window.setTimeout(() => {
      if (enteringSpaceIds.delete(space.id)) {
        renderSpaceStrip();
      }
    }, SPACE_ANIMATION_MS + 20);
  });

  previousSpaces.forEach((space) => {
    if (nextIds.has(space.id) || exitingSpaceIds.has(space.id)) {
      return;
    }
    exitingSpaceIds.add(space.id);
    exitingSpaceMap.set(space.id, space);
    const timer = window.setTimeout(() => {
      exitingSpaceIds.delete(space.id);
      exitingSpaceMap.delete(space.id);
      exitingTimers.delete(space.id);
      renderSpaceStrip();
    }, SPACE_ANIMATION_MS);
    exitingTimers.set(space.id, timer);
  });

  previousSpaces.forEach((space) => {
    if (!nextIds.has(space.id)) {
      return;
    }
    const timer = exitingTimers.get(space.id);
    if (timer) {
      window.clearTimeout(timer);
      exitingTimers.delete(space.id);
    }
    exitingSpaceIds.delete(space.id);
    exitingSpaceMap.delete(space.id);
  });

  spaces = nextSpaces.slice();
  visibleSpaces = buildVisibleSpaces(previousSpaces, nextSpaces);
  applyThemeTint(nextActiveSpace);

  if (animateTabs && previousActiveSpaceId && previousActiveSpaceId !== nextActiveSpaceId) {
    animateTabSwitch();
  }
  previousActiveSpaceId = nextActiveSpaceId;

  if (renameSpaceId && !spaces.some((space) => space.id === renameSpaceId)) {
    renameSpaceId = null;
    renameValue = '';
  }
  const currentContextMenuState = contextMenuState;
  if (currentContextMenuState &&
      !spaces.some((space) => space.id === currentContextMenuState.spaceId)) {
    contextMenuState = null;
  }
  const currentWizardState = wizardState;
  if (currentWizardState?.targetSpaceId &&
      !spaces.some((space) => space.id === currentWizardState.targetSpaceId)) {
    wizardState = null;
  }
  if (wizardState?.mode === 'manage-spaces' && spaces.length === 0) {
    wizardState = null;
  }

  renderSpaceStrip();
}

function buildVisibleSpaces(
    previousSpaces: SpaceInfo[], nextSpaces: SpaceInfo[]): SpaceInfo[] {
  const merged = nextSpaces.slice();
  let offset = 0;

  previousSpaces.forEach((space, index) => {
    if (!exitingSpaceIds.has(space.id)) {
      return;
    }
    const insertionIndex = Math.min(index + offset, merged.length);
    merged.splice(insertionIndex, 0, exitingSpaceMap.get(space.id) || space);
    offset += 1;
  });

  return merged;
}

function renderSpaceStrip() {
  elements.stripRail.replaceChildren();
  renderSidebarBody();

  if (dragState && dragState.indicatorIndex <= 0) {
    elements.stripRail.appendChild(createDropIndicator());
  }

  visibleSpaces.forEach((space, index) => {
    const wrapper = document.createElement('div');
    wrapper.className = 'space-slot';
    wrapper.dataset['spaceId'] = space.id;
    wrapper.dataset['index'] = String(index);

    if (space.active) {
      wrapper.classList.add('is-active');
    }
    if (enteringSpaceIds.has(space.id)) {
      wrapper.classList.add('is-entering');
    }
    if (exitingSpaceIds.has(space.id)) {
      wrapper.classList.add('is-deleting');
    }
    if (renameSpaceId === space.id) {
      wrapper.classList.add('is-renaming');
    }

    wrapper.addEventListener('dragover', (event) => handleSpaceDragOver(event, index));
    wrapper.addEventListener('drop', (event) => handleSpaceDrop(event, index));

    if (renameSpaceId === space.id) {
      wrapper.appendChild(createRenameField(space));
    } else {
      wrapper.appendChild(createSpaceButton(space, index));
    }

    elements.stripRail.appendChild(wrapper);
    if (dragState && dragState.indicatorIndex === index + 1) {
      elements.stripRail.appendChild(createDropIndicator());
    }
  });

  elements.stripRail.appendChild(createAddButton());

  const wizard = createWizardPanel();
  if (wizard) {
    elements.stripRail.appendChild(wizard);
  }

  renderContextMenu();
}


function createSpaceButton(space: SpaceInfo, index: number): HTMLElement {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'space-icon';
  button.draggable = !exitingSpaceIds.has(space.id);
  button.dataset['spaceId'] = space.id;
  button.dataset['spaceIndex'] = String(index);
  button.title = `${space.name} (${getSpaceTabCount(space)})`;
  button.textContent = getSpaceDisplayIcon(space);
  button.style.setProperty('--space-accent', normalizeSpaceColor(space.color));
  button.style.setProperty(
      '--space-accent-muted', withAlpha(normalizeSpaceColor(space.color), '22'));
  button.setAttribute('aria-label', `${space.name}, ${getSpaceTabCount(space)} tabs`);
  button.setAttribute('data-maho-identifier', `sidebarSpaceButton-${space.id}`);
  button.setAttribute('data-maho-space-id', space.id);
  button.setAttribute('aria-current', space.active ? 'true' : 'false');

  button.addEventListener('click', () => {
    closeContextMenu();
    closeWizard();
    renameSpaceId = null;
    renameValue = '';
    void pageHandler.activateSpace(space.id);
  });
  button.addEventListener('contextmenu', (event) => {
    event.preventDefault();
    openContextMenu(space.id, event.clientX, event.clientY);
  });
  button.addEventListener('dragstart', (event) => handleSpaceDragStart(event, space.id, index));
  button.addEventListener('dragend', clearDragState);

  return button;
}

function createRenameField(space: SpaceInfo): HTMLElement {
  const shell = document.createElement('div');
  shell.className = 'space-rename-shell';

  const input = document.createElement('input');
  input.className = 'space-rename-input';
  input.value = renameValue;
  input.setAttribute('aria-label', `Rename ${space.name}`);
  input.setAttribute('data-maho-identifier', `sidebarSpaceRenameField-${space.id}`);
  input.setAttribute('data-maho-space-id', space.id);

  const focusToken = ++renameFocusToken;
  window.setTimeout(() => {
    if (renameSpaceId !== space.id || focusToken !== renameFocusToken) {
      return;
    }
    input.focus();
    input.select();
  }, 0);

  let settled = false;
  const finalize = (commit: boolean) => {
    if (settled) {
      return;
    }
    settled = true;

    const nextValue = input.value.trim();
    renameSpaceId = null;
    renameValue = '';
    renderSpaceStrip();

    if (commit && nextValue && nextValue !== space.name) {
      void pageHandler.renameSpace(space.id, nextValue);
    }
  };

  input.addEventListener('input', () => {
    renameValue = input.value;
  });
  input.addEventListener('keydown', (event: KeyboardEvent) => {
    if (event.key === 'Enter') {
      event.preventDefault();
      finalize(true);
      return;
    }
    if (event.key === 'Escape') {
      event.preventDefault();
      finalize(false);
    }
  });
  input.addEventListener('blur', () => finalize(true));

  shell.appendChild(input);
  return shell;
}

function createAddButton(): HTMLElement {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'space-add-btn';
  button.textContent = '+';
  button.title = 'Create space';
  button.setAttribute('aria-label', 'Create space');
  button.setAttribute('data-maho-identifier', 'sidebarCreateSpaceButton');
  button.addEventListener('click', triggerCreateSpace);
  button.addEventListener('dragover', (event) => {
    if (!dragState) {
      return;
    }
    event.preventDefault();
    dragState.indicatorIndex = spaces.length;
    renderSpaceStrip();
  });
  button.addEventListener('drop', (event) => handleSpaceDrop(event, spaces.length));
  return button;
}

function renderSidebarBody() {
  const showingManageSpaces = wizardState?.mode === 'manage-spaces';
  elements.sidebarTitle.classList.toggle('is-hidden', showingManageSpaces);
  elements.tabList.classList.toggle('is-hidden', showingManageSpaces);
  elements.managePanel.classList.toggle('is-hidden', !showingManageSpaces);
  if (showingManageSpaces) {
    renderManageSpacesPanel();
    return;
  }
  elements.managePanel.replaceChildren();
  renderTabs();
}

function createDropIndicator(): HTMLElement {
  const indicator = document.createElement('div');
  indicator.className = 'space-drop-indicator';
  return indicator;
}

function createManageSpacesPanelShell(): HTMLElement {
  const panel = document.createElement('div');
  panel.className = 'manage-spaces-panel__shell';
  panel.setAttribute('data-maho-identifier', 'sidebar.manageSpaces.shell');
  return panel;
}

function renderManageSpacesPanel() {
  elements.managePanel.replaceChildren();

  const shell = createManageSpacesPanelShell();

  const header = document.createElement('div');
  header.className = 'manage-spaces-panel__header';

  const title = document.createElement('div');
  title.className = 'manage-spaces-panel__title';
  title.textContent = 'Manage Spaces';

  const actions = document.createElement('div');
  actions.className = 'manage-spaces-panel__header-actions';

  const backButton = document.createElement('button');
  backButton.type = 'button';
  backButton.className = 'manage-spaces-panel__back';
  backButton.textContent = 'Back';
  backButton.setAttribute('aria-label', 'Return to sidebar tabs');
  backButton.setAttribute('data-maho-identifier', 'sidebar.manageSpaces.back');
  backButton.addEventListener('click', () => {
    closeWizard();
  });

  const createButton = document.createElement('button');
  createButton.type = 'button';
  createButton.className = 'manage-spaces-panel__create';
  createButton.textContent = 'Create';
  createButton.setAttribute('aria-label', 'Create space from manage panel');
  createButton.setAttribute('data-maho-identifier', 'sidebar.manageSpaces.create');
  createButton.addEventListener('click', () => {
    triggerCreateSpace();
  });

  actions.append(backButton, createButton);
  header.append(title, actions);
  shell.appendChild(header);

  if (spaces.length === 0) {
    const empty = document.createElement('div');
    empty.className = 'manage-spaces-panel__empty';
    empty.textContent = 'No spaces available.';
    empty.setAttribute('data-maho-identifier', 'sidebar.manageSpaces.emptyState');
    shell.appendChild(empty);
  } else {
    const list = document.createElement('div');
    list.className = 'manage-spaces-panel__list';
    list.setAttribute('data-maho-identifier', 'sidebar.manageSpaces.list');

    spaces.forEach((space) => {
      list.appendChild(createManageSpacesRow(space));
    });
    shell.appendChild(list);
  }

  elements.managePanel.appendChild(shell);
}

function createManageSpacesRow(space: SpaceInfo): HTMLElement {
  const row = document.createElement('div');
  row.className = 'manage-spaces-row';
  row.setAttribute('data-maho-identifier', `sidebar.manageSpaces.row.${space.id}`);
  row.setAttribute('data-maho-space-id', space.id);

  const color = normalizeSpaceColor(space.color);
  row.style.setProperty('--space-accent', color);
  row.style.setProperty('--space-accent-muted', withAlpha(color, '22'));

  const icon = document.createElement('div');
  icon.className = 'manage-spaces-row__icon';
  icon.textContent = getSpaceDisplayIcon(space);

  const body = document.createElement('div');
  body.className = 'manage-spaces-row__body';

  const name = document.createElement('div');
  name.className = 'manage-spaces-row__name';
  name.textContent = space.name;

  const meta = document.createElement('div');
  meta.className = 'manage-spaces-row__meta';
  const profileName = getProfileName(getCurrentProfileId(space));
  meta.textContent = `${profileName} · ${getSpaceTabCount(space)} tabs`;

  body.append(name, meta);

  const actions = document.createElement('div');
  actions.className = 'manage-spaces-row__actions';

  const editButton = document.createElement('button');
  editButton.type = 'button';
  editButton.className = 'manage-spaces-row__action';
  editButton.textContent = 'Edit';
  editButton.setAttribute('aria-label', `Edit ${space.name}`);
  editButton.setAttribute('data-maho-identifier', `sidebar.manageSpaces.edit.${space.id}`);
  editButton.addEventListener('click', () => {
    closeWizard();
    closeContextMenu();
    openInlineRename(space.id);
  });

  const deleteButton = document.createElement('button');
  deleteButton.type = 'button';
  deleteButton.className = 'manage-spaces-row__action is-danger';
  deleteButton.textContent = 'Delete';
  deleteButton.setAttribute('aria-label', `Delete ${space.name}`);
  deleteButton.setAttribute('data-maho-identifier', `sidebar.manageSpaces.delete.${space.id}`);
  deleteButton.addEventListener('click', () => {
    openManageDeleteConfirm(space.id);
  });

  actions.append(editButton, deleteButton);
  row.append(icon, body, actions);
  return row;
}

function openManageDeleteConfirm(spaceId: string) {
  const space = spaces.find((item) => item.id === spaceId);
  if (!space) {
    return;
  }
  contextMenuState = {
    spaceId,
    x: 24,
    y: 24,
    confirmDelete: true,
  };
    renderManageSpacesPanel();
    renderContextMenu();
}

function getProfileName(profileId: string): string {
  const profile = profiles.find((item) => item.id === profileId);
  return profile?.name || (profileId === 'default' ? 'Default profile' : profileId);
}

function getCurrentProfileId(space: SpaceInfo): string {
  const compat = space as SpaceInfoCompat;
  return compat.profileId || compat.profile_id || 'default';
}

function saturationToCanvasY(saturation: number): number {
  return 1 - Math.min(1, Math.max(0, saturation));
}

function canvasYToSaturation(normalizedY: number): number {
  return 1 - Math.min(1, Math.max(0, normalizedY));
}

function createWizardPanel(): HTMLElement|null {
  if (!wizardState) {
    return null;
  }

  const panel = document.createElement('section');
  panel.className = 'space-wizard';
  panel.setAttribute('aria-label', getWizardTitle());
  panel.setAttribute('data-maho-identifier', 'sidebarWizardPanel');

  const header = document.createElement('div');
  header.className = 'space-wizard__header';

  const title = document.createElement('div');
  title.className = 'space-wizard__title';
  title.textContent = getWizardTitle();

  const headerActions = document.createElement('div');
  headerActions.className = 'space-wizard__header-actions';

  const closeButton = document.createElement('button');
  closeButton.type = 'button';
  closeButton.className = 'space-wizard__close';
  closeButton.textContent = '×';
  closeButton.setAttribute('aria-label', 'Close panel');
  closeButton.addEventListener('click', closeWizard);

  headerActions.appendChild(closeButton);
  header.append(title, headerActions);
  panel.appendChild(header);

  if (wizardState.mode === 'create') {
    if (wizardState.step === 'icon') {
      panel.appendChild(createEmojiPicker());
    } else {
      panel.appendChild(createCreateDetailsPanel());
    }
    return panel;
  }

  if (wizardState.mode === 'change-icon') {
    panel.appendChild(createEmojiPicker());
    return panel;
  }

  if (wizardState.mode === 'theme-store') {
    panel.appendChild(createThemeStorePanel());
    return panel;
  }

  panel.appendChild(createThemeEditorPanel());
  return panel;
}

function createEmojiPicker(): HTMLElement {
  const section = document.createElement('div');
  section.className = 'space-wizard__content';

  const searchField = document.createElement('input');
  searchField.className = 'space-wizard__search';
  searchField.type = 'search';
  searchField.placeholder = 'Search emoji';
  searchField.value = wizardState?.search || '';
  searchField.setAttribute('aria-label', 'Search emoji');
  searchField.setAttribute('data-maho-identifier', 'sidebarEmojiSearchField');
  searchField.addEventListener('input', () => {
    if (!wizardState) {
      return;
    }
    wizardState.search = searchField.value;
    renderSpaceStrip();
  });

  const grid = document.createElement('div');
  grid.className = 'emoji-grid';

  const filteredOptions = filterEmojiOptions(wizardState?.search || '');
  filteredOptions.forEach((option) => {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'emoji-grid__item';
    button.textContent = option.emoji;
    button.setAttribute('aria-label', `Select ${option.label}`);
  button.setAttribute('data-maho-identifier', `sidebarEmojiOptionButton-${option.emoji}`);
  button.setAttribute('data-maho-emoji', option.emoji);
    if (wizardState?.selectedIcon === option.emoji) {
      button.classList.add('is-selected');
    }
    button.addEventListener('click', () => {
      if (!wizardState) {
        return;
      }
      wizardState.selectedIcon = option.emoji;
      if (wizardState.mode === 'create') {
        wizardState.step = 'details';
        renderSpaceStrip();
        focusWizardNameInput();
        return;
      }
      if (wizardState.targetSpaceId) {
        void pageHandler.changeSpaceIcon(wizardState.targetSpaceId, option.emoji);
      }
      closeWizard();
    });
    grid.appendChild(button);
  });

  section.append(searchField, grid);
  return section;
}

function createCreateDetailsPanel(): HTMLElement {
  const section = document.createElement('div');
  section.className = 'space-wizard__content';

  const preview = document.createElement('div');
  preview.className = 'space-wizard__preview';

  const previewIcon = document.createElement('div');
  previewIcon.className = 'space-wizard__preview-icon';
  previewIcon.textContent = wizardState?.selectedIcon || '✨';
  previewIcon.style.setProperty(
      '--space-accent', wizardState?.selectedColor || COLOR_OPTIONS[0] || '#F16063');

  const previewCopy = document.createElement('div');
  previewCopy.className = 'space-wizard__preview-copy';

  const previewTitle = document.createElement('div');
  previewTitle.className = 'space-wizard__preview-title';
  previewTitle.textContent = wizardState?.name.trim() || 'New Space';

  const previewDetail = document.createElement('div');
  previewDetail.className = 'space-wizard__preview-detail';
  previewDetail.textContent = 'Choose a name and theme tint';

  previewCopy.append(previewTitle, previewDetail);
  preview.append(previewIcon, previewCopy);

  const nameInput = document.createElement('input');
  nameInput.className = 'space-wizard__name';
  nameInput.type = 'text';
  nameInput.placeholder = 'Space name';
  nameInput.value = wizardState?.name || '';
  nameInput.setAttribute('aria-label', 'Space name');
  nameInput.setAttribute('data-maho-identifier', 'sidebarCreateSpaceNameField');
  nameInput.addEventListener('input', () => {
    if (!wizardState) {
      return;
    }
    wizardState.name = nameInput.value;
    renderSpaceStrip();
  });
  nameInput.addEventListener('keydown', (event: KeyboardEvent) => {
    if (event.key === 'Enter') {
      event.preventDefault();
      submitCreateSpace();
    }
  });

  const palette = createColorPalette(
      wizardState?.selectedColor || COLOR_OPTIONS[0] || '#F16063',
      (color: string) => {
        if (!wizardState) {
          return;
        }
        wizardState.selectedColor = color;
        renderSpaceStrip();
      });

  const actions = document.createElement('div');
  actions.className = 'space-wizard__actions';

  const backButton = document.createElement('button');
  backButton.type = 'button';
  backButton.className = 'space-wizard__secondary';
  backButton.textContent = 'Back';
  backButton.addEventListener('click', () => {
    if (!wizardState) {
      return;
    }
    wizardState.step = 'icon';
    renderSpaceStrip();
  });

  const createButton = document.createElement('button');
  createButton.type = 'button';
  createButton.className = 'space-wizard__primary';
  createButton.textContent = 'Create Space';
  createButton.addEventListener('click', submitCreateSpace);

  actions.append(backButton, createButton);
  section.append(preview, nameInput, palette, actions);

  window.setTimeout(() => {
    if (wizardState?.mode === 'create' && wizardState.step === 'details') {
      nameInput.focus();
      nameInput.select();
    }
  }, 0);

  return section;
}

function createThemeEditorPanel(): HTMLElement {
  const section = document.createElement('div');
  section.className = 'space-wizard__content theme-editor';
  const initialPreset = PRESET_COLORS[0]!;
  const initialColor: SpaceColor = hexToHsb(wizardState?.selectedColor || '') || {
    hue: initialPreset.hue,
    saturation: initialPreset.saturation,
    brightness: initialPreset.brightness,
    grain: 0,
  };
  const initialPresetIndex = PRESET_COLORS.findIndex((preset) => {
    return Math.abs(preset.hue - initialColor.hue) < 0.01 &&
        Math.abs(preset.saturation - initialColor.saturation) < 0.03 &&
        Math.abs(preset.brightness - initialColor.brightness) < 0.03;
  });

  themeEditorState = {
    currentColor: {
      hue: initialColor.hue,
      saturation: Math.min(1, Math.max(0.2, initialColor.saturation)),
      brightness: Math.min(1, Math.max(0, initialColor.brightness)),
      grain: initialColor.grain,
    },
    previewMode: 'zen',
    harmony: 'single_analogous',
    opacity: 1,
    selectedPresetIndex: initialPresetIndex,
    instructionVisible: true,
    canvas: null,
    cursorPoint: {
      x: Math.min(1, Math.max(0, initialColor.hue)),
      y: saturationToCanvasY(Math.min(1, Math.max(0, initialColor.saturation))),
    },
  };

  const modeMeta: Array<{id: 'solid'|'zen'|'gradient'; label: string; icon: string; identifier: string}> = [
    {id: 'solid', label: 'Solid', icon: '●', identifier: 'space.theme.mode.0'},
    {id: 'zen', label: 'Zen', icon: '◌', identifier: 'space.theme.mode.1'},
    {id: 'gradient', label: 'Gradient', icon: '✦', identifier: 'space.theme.mode.2'},
  ];

  const modeSection = document.createElement('div');
  modeSection.className = 'theme-mode-section';
  const modeHeader = document.createElement('div');
  modeHeader.className = 'theme-section-header';
  const modeHeaderCopy = document.createElement('div');
  modeHeaderCopy.className = 'theme-section-copy';
  const modeHeaderTitle = document.createElement('div');
  modeHeaderTitle.className = 'theme-section-title';
  modeHeaderTitle.textContent = 'Theme mode';
  const modeHeaderCaption = document.createElement('div');
  modeHeaderCaption.className = 'theme-section-caption';
  modeHeaderCaption.textContent = 'Choose a focused field, a soft zen field, or a richer gradient stage.';
  modeHeaderCopy.append(modeHeaderTitle, modeHeaderCaption);
  const modeBadge = document.createElement('div');
  modeBadge.className = 'theme-mode-badge';
  modeBadge.textContent = 'Preview';
  modeHeader.append(modeHeaderCopy, modeBadge);

  const modeRow = document.createElement('div');
  modeRow.className = 'theme-mode-row';

  const canvasWrapper = document.createElement('div');
  canvasWrapper.className = 'theme-canvas-wrapper';

  const canvasFrame = document.createElement('div');
  canvasFrame.className = 'theme-canvas-frame';
  const canvasFrameTop = document.createElement('div');
  canvasFrameTop.className = 'theme-canvas-frame-top';
  const canvasFrameMeta = document.createElement('div');
  canvasFrameMeta.className = 'theme-canvas-frame-meta';
  const canvasFrameLabel = document.createElement('span');
  canvasFrameLabel.className = 'theme-canvas-frame-label';
  canvasFrameLabel.textContent = 'Live preview';
  const canvasFrameCaption = document.createElement('span');
  canvasFrameCaption.className = 'theme-canvas-frame-caption';
  canvasFrameCaption.textContent = 'Sidebar stage';
  canvasFrameMeta.append(canvasFrameLabel, canvasFrameCaption);
  canvasFrameTop.append(canvasFrameMeta, modeRow);

  const canvasStage = document.createElement('div');
  canvasStage.className = 'theme-canvas-stage';

  const canvas = document.createElement('canvas');
  canvas.width = 280;
  canvas.height = 200;
  canvas.setAttribute('aria-label', 'Theme color canvas');
  canvas.setAttribute('data-maho-identifier', 'space.theme.creatorPreview');
  themeEditorState.canvas = canvas;
  const resizeObserver = new ResizeObserver((entries) => {
    for (const entry of entries) {
      const dpr = window.devicePixelRatio || 1;
      const w = Math.round(entry.contentRect.width * dpr);
      const h = Math.round(entry.contentRect.height * dpr);
      if (w > 0 && h > 0 && (canvas.width !== w || canvas.height !== h)) {
        canvas.width = w;
        canvas.height = h;
        cachedNoiseData = null;
        redrawThemeCanvas();
      }
    }
  });
  resizeObserver.observe(canvas);

  const overlay = document.createElement('div');
  overlay.className = 'theme-canvas-overlay';

  const iconCircle = document.createElement('div');
  iconCircle.className = 'theme-icon-circle';
  iconCircle.textContent = wizardState?.selectedIcon || '🪐';

  const name = document.createElement('div');
  name.className = 'theme-space-name';
  name.textContent = wizardState?.name || 'Space';

  const tabsCount = document.createElement('div');
  tabsCount.className = 'theme-tabs-count';
  const targetSpace = wizardState?.targetSpaceId ?
      spaces.find((space) => space.id === wizardState?.targetSpaceId) || null :
      null;
  const tabCount = targetSpace ? getSpaceTabCount(targetSpace) : 0;
  tabsCount.textContent = `${tabCount} tabs`;

  const instruction = document.createElement('div');
  instruction.className = 'theme-instruction';
  instruction.textContent = 'Tap the field, then refine mode, grain, and harmony.';

  const brightnessRow = document.createElement('div');
  brightnessRow.className = 'theme-brightness-row';

  const brightnessDown = document.createElement('button');
  brightnessDown.type = 'button';
  brightnessDown.className = 'theme-brightness-btn';
  brightnessDown.textContent = '−';
  brightnessDown.setAttribute('aria-label', 'Decrease brightness');
  brightnessDown.setAttribute('data-maho-identifier', 'space.theme.luminosity.minus');

  const brightnessUp = document.createElement('button');
  brightnessUp.type = 'button';
  brightnessUp.className = 'theme-brightness-btn';
  brightnessUp.textContent = '+';
  brightnessUp.setAttribute('aria-label', 'Increase brightness');
  brightnessUp.setAttribute('data-maho-identifier', 'space.theme.luminosity.plus');

  brightnessRow.append(brightnessDown, brightnessUp);

  const hexInput = document.createElement('input');
  hexInput.className = 'theme-hex-input';
  hexInput.type = 'text';
  hexInput.spellcheck = false;
  hexInput.maxLength = 7;
  hexInput.setAttribute('aria-label', 'Theme hex color');
  hexInput.setAttribute('data-maho-identifier', 'space.theme.hexInput');

  const swatchRail = document.createElement('div');
  swatchRail.className = 'theme-swatch-rail';
  const swatchTitle = document.createElement('span');
  swatchTitle.className = 'theme-swatch-rail-title';
  swatchTitle.textContent = 'Swatches';
  const swatchCaption = document.createElement('span');
  swatchCaption.className = 'theme-swatch-rail-caption';
  swatchCaption.textContent = 'Cycle or tap';
  const swatchHeader = document.createElement('div');
  swatchHeader.className = 'theme-swatch-rail-header';
  swatchHeader.append(swatchTitle, swatchCaption);

  const swatchRow = document.createElement('div');
  swatchRow.className = 'theme-swatch-row';
  const chevronLeft = document.createElement('button');
  chevronLeft.type = 'button';
  chevronLeft.className = 'theme-swatch-chevron';
  chevronLeft.textContent = '‹';
  chevronLeft.setAttribute('aria-label', 'Previous swatch');
  chevronLeft.setAttribute('data-maho-identifier', 'space.theme.chevron.left');
  const swatchTrack = document.createElement('div');
  swatchTrack.className = 'theme-swatch-track';
  const chevronRight = document.createElement('button');
  chevronRight.type = 'button';
  chevronRight.className = 'theme-swatch-chevron';
  chevronRight.textContent = '›';
  chevronRight.setAttribute('aria-label', 'Next swatch');
  chevronRight.setAttribute('data-maho-identifier', 'space.theme.chevron.right');
  swatchRow.append(chevronLeft, swatchTrack, chevronRight);
  swatchRail.append(swatchHeader, swatchRow);

  overlay.append(iconCircle, name, tabsCount, instruction, brightnessRow, hexInput, swatchRail);
  canvasStage.append(canvas, overlay);
  canvasFrame.append(canvasFrameTop, canvasStage);
  canvasWrapper.appendChild(canvasFrame);

  const controls = document.createElement('div');
  controls.className = 'theme-controls';

  const primaryControls = document.createElement('div');
  primaryControls.className = 'theme-controls-primary';

  const grainCard = document.createElement('div');
  grainCard.className = 'theme-control-card';
  const grainHeader = document.createElement('div');
  grainHeader.className = 'theme-control-card-header';
  const grainTitle = document.createElement('div');
  grainTitle.className = 'theme-control-card-title';
  grainTitle.textContent = 'Texture';
  const grainCaption = document.createElement('div');
  grainCaption.className = 'theme-control-card-caption';
  grainCaption.textContent = 'Dial in grain for a more tactile stage.';
  grainHeader.append(grainTitle, grainCaption);
  const grainRow = document.createElement('div');
  grainRow.className = 'theme-control-row';
  const grainLabel = document.createElement('div');
  grainLabel.className = 'theme-control-label';
  grainLabel.textContent = 'Grain';
  const grainSlider = document.createElement('input');
  grainSlider.className = 'theme-control-slider';
  grainSlider.type = 'range';
  grainSlider.min = '0';
  grainSlider.max = '1';
  grainSlider.step = '0.01';
  grainSlider.value = String(themeEditorState.currentColor.grain);
  grainSlider.setAttribute('data-maho-identifier', 'space.theme.grainDial');
  grainRow.append(grainLabel, grainSlider);
  grainCard.append(grainHeader, grainRow);

  const densityCard = document.createElement('div');
  densityCard.className = 'theme-control-card';
  const densityHeader = document.createElement('div');
  densityHeader.className = 'theme-control-card-header';
  const densityTitle = document.createElement('div');
  densityTitle.className = 'theme-control-card-title';
  densityTitle.textContent = 'Blend';
  const densityCaption = document.createElement('div');
  densityCaption.className = 'theme-control-card-caption';
  densityCaption.textContent = 'Control opacity and hue relationships.';
  densityHeader.append(densityTitle, densityCaption);

  const gradientGroup = document.createElement('div');
  gradientGroup.className = 'theme-gradient-only is-hidden';

  const harmonyControl = document.createElement('div');
  harmonyControl.className = 'theme-control-row theme-control-row--stacked';
  const harmonyLabel = document.createElement('div');
  harmonyLabel.className = 'theme-control-label';
  harmonyLabel.textContent = 'Harmony';
  const harmonyRow = document.createElement('div');
  harmonyRow.className = 'theme-harmony-row';
  harmonyControl.append(harmonyLabel, harmonyRow);

  const opacityRow = document.createElement('div');
  opacityRow.className = 'theme-control-row';
  const opacityLabel = document.createElement('div');
  opacityLabel.className = 'theme-control-label';
  opacityLabel.textContent = 'Opacity';
  const opacitySlider = document.createElement('input');
  opacitySlider.className = 'theme-control-slider';
  opacitySlider.type = 'range';
  opacitySlider.min = '0';
  opacitySlider.max = '1';
  opacitySlider.step = '0.01';
  opacitySlider.value = String(themeEditorState.opacity);
  opacitySlider.setAttribute('data-maho-identifier', 'space.theme.opacitySlider');
  opacityRow.append(opacityLabel, opacitySlider);
  gradientGroup.append(harmonyControl, opacityRow);
  densityCard.append(densityHeader, gradientGroup);

  primaryControls.append(grainCard, densityCard);
  controls.appendChild(primaryControls);

  const browseThemesButton = document.createElement('button');
  browseThemesButton.type = 'button';
  browseThemesButton.className = 'space-wizard__secondary theme-store-browse';
  browseThemesButton.textContent = 'Browse Themes';
  browseThemesButton.addEventListener('click', showThemeStorePanel);

  const actions = document.createElement('div');
  actions.className = 'space-wizard__actions';

  const cancelButton = document.createElement('button');
  cancelButton.type = 'button';
  cancelButton.className = 'space-wizard__secondary';
  cancelButton.textContent = 'Cancel';
  cancelButton.addEventListener('click', () => {
    themeEditorState = null;
    cachedNoiseData = null;
    closeWizard();
  });

  const applyButton = document.createElement('button');
  applyButton.type = 'button';
  applyButton.className = 'space-wizard__primary';
  applyButton.textContent = 'Apply Theme';
  applyButton.addEventListener('click', () => {
    if (wizardState?.targetSpaceId && themeEditorState) {
      const theme: SpaceThemeData = themeEditorState.previewMode === 'gradient' ? {
        type: 'gradient',
        gradientColors: computeHarmonyColors(
            themeEditorState.currentColor, themeEditorState.harmony),
        harmony: themeEditorState.harmony,
        opacity: themeEditorState.opacity,
        texture: themeEditorState.currentColor.grain,
      } : {
        type: 'solid',
        color: themeEditorState.currentColor,
      };
      void pageHandler.updateSpaceTheme(wizardState.targetSpaceId, JSON.stringify(theme));
      themeEditorState = null;
      cachedNoiseData = null;
    }
    closeWizard();
  });

  actions.append(cancelButton, applyButton);
  section.append(modeSection, canvasWrapper, controls, browseThemesButton, actions);
  modeSection.append(modeHeader, modeRow);

  const updateInstruction = () => {
    if (!themeEditorState) {
      return;
    }
    instruction.classList.toggle('is-hidden', !themeEditorState.instructionVisible);
  };

  const updateModeButtons = () => {
    if (!themeEditorState) {
      return;
    }
    modeRow.querySelectorAll<HTMLButtonElement>('.theme-mode-chip').forEach((button) => {
      button.classList.toggle('is-selected', button.dataset['mode'] === themeEditorState?.previewMode);
    });
  };

  const updateThemePreview = () => {
    if (!themeEditorState) {
      return;
    }
    const hex = hsbToHex(
        themeEditorState.currentColor.hue,
        themeEditorState.currentColor.saturation,
        themeEditorState.currentColor.brightness);
    hexInput.value = hex.toUpperCase();
    iconCircle.style.backgroundColor = hex;
    section.style.setProperty('--space-theme-color', hex);
    if (wizardState) {
      wizardState.selectedColor = hex;
    }
    swatchTrack.querySelectorAll('.theme-swatch').forEach((node, index) => {
      node.classList.toggle('is-selected', index === themeEditorState?.selectedPresetIndex);
    });
    section.classList.toggle('is-gradient-mode', themeEditorState.previewMode === 'gradient');
    gradientGroup.classList.toggle('is-hidden', themeEditorState.previewMode !== 'gradient');
    grainCard.classList.toggle('is-hidden', false);
    updateModeButtons();
    updateInstruction();
    redrawThemeCanvas();
  };

  const applyColorSelection = (color: SpaceColor, presetIndex = -1) => {
    if (!themeEditorState) {
      return;
    }
    themeEditorState.currentColor = {
      hue: normalizeHue(color.hue),
      saturation: Math.min(1, Math.max(0.2, color.saturation)),
      brightness: Math.min(1, Math.max(0, color.brightness)),
      grain: Math.min(1, Math.max(0, color.grain)),
    };
    themeEditorState.selectedPresetIndex = presetIndex;
    themeEditorState.cursorPoint = {
      x: themeEditorState.currentColor.hue,
      y: saturationToCanvasY(themeEditorState.currentColor.saturation),
    };
    updateThemePreview();
  };

  const setInteractionState = () => {
    if (!themeEditorState) {
      return;
    }
    if (themeEditorState.instructionVisible) {
      themeEditorState.instructionVisible = false;
      updateInstruction();
    }
  };

  const updateColorFromPointer = (event: MouseEvent) => {
    if (!themeEditorState || !themeEditorState.canvas) {
      return;
    }
    const bounds = themeEditorState.canvas.getBoundingClientRect();
    const x = Math.min(bounds.width, Math.max(0, event.clientX - bounds.left));
    const y = Math.min(bounds.height, Math.max(0, event.clientY - bounds.top));
    const normalizedX = bounds.width > 0 ? x / bounds.width : 0;
    const normalizedY = bounds.height > 0 ? y / bounds.height : 0;
    themeEditorState.currentColor.hue = normalizeHue(normalizedX);
    themeEditorState.currentColor.saturation = canvasYToSaturation(normalizedY);
    themeEditorState.cursorPoint = {x: normalizedX, y: normalizedY};
    themeEditorState.selectedPresetIndex = -1;
    setInteractionState();
    updateThemePreview();
  };

  let pointerActive = false;
  let rafPending = false;
  canvas.addEventListener('mousedown', (event) => {
    pointerActive = true;
    updateColorFromPointer(event);
  });
  canvas.addEventListener('mousemove', (event) => {
    if (!pointerActive && event.buttons !== 1) {
      return;
    }
    if (rafPending) {
      return;
    }
    rafPending = true;
    requestAnimationFrame(() => {
      rafPending = false;
      updateColorFromPointer(event);
    });
  });
  canvas.addEventListener('mouseup', () => {
    pointerActive = false;
  });
  canvas.addEventListener('mouseleave', () => {
    pointerActive = false;
  });

  brightnessDown.addEventListener('click', () => {
    if (!themeEditorState) {
      return;
    }
    setInteractionState();
    themeEditorState.currentColor.brightness = Math.max(
        0.3, themeEditorState.currentColor.brightness - 0.05);
    updateThemePreview();
  });

  brightnessUp.addEventListener('click', () => {
    if (!themeEditorState) {
      return;
    }
    setInteractionState();
    themeEditorState.currentColor.brightness = Math.min(
        1, themeEditorState.currentColor.brightness + 0.05);
    updateThemePreview();
  });

  hexInput.addEventListener('change', () => {
    const color = hexToHsb(hexInput.value);
    if (!color || !themeEditorState) {
      hexInput.value = hsbToHex(
          themeEditorState?.currentColor.hue || 0,
          themeEditorState?.currentColor.saturation || 0,
          themeEditorState?.currentColor.brightness || 0).toUpperCase();
      return;
    }
    setInteractionState();
    applyColorSelection({...color, grain: themeEditorState.currentColor.grain}, -1);
  });

  const cyclePreset = (direction: number) => {
    if (!themeEditorState) {
      return;
    }
    const currentIndex = themeEditorState.selectedPresetIndex >= 0 ?
        themeEditorState.selectedPresetIndex : initialPresetIndex >= 0 ? initialPresetIndex : 0;
    const nextIndex = (currentIndex + direction + PRESET_COLORS.length) % PRESET_COLORS.length;
    const preset = PRESET_COLORS[nextIndex];
    if (!preset) {
      return;
    }
    setInteractionState();
    applyColorSelection({
      hue: preset.hue,
      saturation: preset.saturation,
      brightness: preset.brightness,
      grain: themeEditorState.currentColor.grain,
    }, nextIndex);
  };

  chevronLeft.addEventListener('click', () => cyclePreset(-1));
  chevronRight.addEventListener('click', () => cyclePreset(1));

  PRESET_COLORS.forEach((preset, index) => {
    const swatch = document.createElement('button');
    swatch.type = 'button';
    swatch.className = 'theme-swatch';
    swatch.title = preset.title;
    swatch.setAttribute('aria-label', preset.title);
    swatch.setAttribute('data-maho-identifier', `space.theme.swatch.${index}`);
    swatch.setAttribute('data-maho-preset-title', preset.title);
    swatch.style.backgroundColor = hsbToHex(preset.hue, preset.saturation, preset.brightness);
    swatch.addEventListener('click', () => {
      setInteractionState();
      applyColorSelection({
        hue: preset.hue,
        saturation: preset.saturation,
        brightness: preset.brightness,
        grain: themeEditorState?.currentColor.grain || 0,
      }, index);
    });
    swatchTrack.appendChild(swatch);
  });

  modeMeta.forEach(({id, label, icon, identifier}) => {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'theme-mode-chip';
    button.dataset['mode'] = id;
    button.setAttribute('data-maho-identifier', identifier);
    const iconNode = document.createElement('span');
    iconNode.className = 'theme-mode-chip__icon';
    iconNode.textContent = icon;
    const labelNode = document.createElement('span');
    labelNode.className = 'theme-mode-chip__label';
    labelNode.textContent = label;
    button.append(iconNode, labelNode);
    button.addEventListener('click', () => {
      if (!themeEditorState) {
        return;
      }
      setInteractionState();
      themeEditorState.previewMode = id;
      updateThemePreview();
    });
    modeRow.appendChild(button);
  });

  const harmonyOptions: Array<{label: string; value: ColorHarmony}> = [
    {label: 'Comp', value: 'complementary'},
    {label: 'Analog', value: 'single_analogous'},
    {label: 'Split', value: 'split_complementary'},
    {label: 'Triad', value: 'triadic'},
    {label: 'Custom', value: 'custom'},
  ];

  harmonyOptions.forEach(({label, value}) => {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'theme-harmony-btn';
    button.textContent = label;
    button.addEventListener('click', () => {
      if (!themeEditorState) {
        return;
      }
      setInteractionState();
      themeEditorState.harmony = value;
      harmonyRow.querySelectorAll('.theme-harmony-btn').forEach((node) => {
        node.classList.toggle('is-selected', node === button);
      });
      redrawThemeCanvas();
    });
    if (themeEditorState?.harmony === value) {
      button.classList.add('is-selected');
    }
    harmonyRow.appendChild(button);
  });

  opacitySlider.addEventListener('input', () => {
    if (!themeEditorState) {
      return;
    }
    setInteractionState();
    themeEditorState.opacity = Number(opacitySlider.value);
    redrawThemeCanvas();
  });

  grainSlider.addEventListener('input', () => {
    if (!themeEditorState) {
      return;
    }
    setInteractionState();
    themeEditorState.currentColor.grain = Number(grainSlider.value);
    redrawThemeCanvas();
  });

  applyColorSelection(initialColor, initialPresetIndex);
  return section;
}

function createThemeStorePanel(): HTMLElement {
  const section = document.createElement('div');
  section.className = 'space-wizard__content theme-store-panel';
  section.setAttribute('data-maho-identifier', 'sidebar.themeStore.panel');

  const header = document.createElement('div');
  header.className = 'theme-store-header';

  const backButton = document.createElement('button');
  backButton.type = 'button';
  backButton.className = 'theme-store-back';
  backButton.textContent = '←';
  backButton.setAttribute('aria-label', 'Back to theme editor');
  backButton.setAttribute('data-maho-identifier', 'sidebarThemeStoreBackButton');
  backButton.addEventListener('click', showThemeEditorPanel);

  const title = document.createElement('h3');
  title.textContent = 'Theme Store';

  const refreshButton = document.createElement('button');
  refreshButton.type = 'button';
  refreshButton.className = 'theme-store-refresh';
  refreshButton.textContent = 'Refresh';
  refreshButton.setAttribute('aria-label', 'Refresh themes');
  refreshButton.setAttribute('data-maho-identifier', 'sidebar.themeStore.refresh');

  header.append(backButton, title, refreshButton);

  const content = document.createElement('div');
  content.className = 'theme-store-list';
  content.setAttribute('data-maho-identifier', 'sidebar.themeStore.table');

  section.append(header, content);

  const renderStatus = (message: string, canRetry = false) => {
    content.replaceChildren();
    const status = document.createElement('div');
    status.className = 'theme-store-status';
    if (message === 'Loading themes…') {
      status.setAttribute('data-maho-identifier', 'sidebar.themeStore.loading');
    } else if (message === 'No themes available') {
      status.setAttribute('data-maho-identifier', 'sidebar.themeStore.emptyState');
    }
    status.textContent = message;
    if (canRetry) {
      const retryButton = document.createElement('button');
      retryButton.type = 'button';
      retryButton.className = 'theme-store-retry';
      retryButton.textContent = 'Retry';
      retryButton.addEventListener('click', () => {
        void loadThemes();
      });
      status.appendChild(document.createElement('br'));
      status.appendChild(retryButton);
    }
    content.appendChild(status);
  };

  const formatInstallCount = (count: number) => {
    return `${count} ${count === 1 ? 'install' : 'installs'}`;
  };

  const createThemeCard = (entry: ThemeStoreEntry) => {
    const card = document.createElement('article');
    card.className = 'theme-store-card';

    const cardHeader = document.createElement('div');
    cardHeader.className = 'theme-store-card-header';

    const name = document.createElement('div');
    name.className = 'theme-store-card-name';
    name.textContent = entry.name;

    const installButton = document.createElement('button');
    installButton.type = 'button';
    installButton.className = 'theme-store-card-install';
    installButton.textContent = 'Install';
    installButton.setAttribute('data-maho-identifier', `sidebar.themeStore.install-${entry.id}`);

    cardHeader.append(name, installButton);

    const author = document.createElement('div');
    author.className = 'theme-store-card-author';
    author.textContent = entry.author;

    const description = document.createElement('div');
    description.className = 'theme-store-card-desc';
    description.textContent = entry.description;

    const meta = document.createElement('div');
    meta.className = 'theme-store-card-meta';

    entry.tags.forEach((tag) => {
      const pill = document.createElement('span');
      pill.className = 'theme-store-tag';
      pill.textContent = tag;
      meta.appendChild(pill);
    });

    const downloads = document.createElement('span');
    downloads.className = 'theme-store-downloads';
    downloads.textContent = formatInstallCount(entry.download_count);
    meta.appendChild(downloads);

    installButton.addEventListener('click', () => {
      const spaceId = wizardState?.targetSpaceId;
      if (!spaceId) {
        return;
      }
      installButton.disabled = true;
      installButton.textContent = 'Installing…';
      pageHandler.updateSpaceTheme(spaceId, JSON.stringify(entry.theme));
      if (wizardState?.mode !== 'theme-store') {
        return;
      }
      installButton.textContent = 'Installed';
      installButton.classList.add('is-installed');
    });

    card.append(cardHeader, author, description, meta);
    return card;
  };

  const loadThemes = async () => {
    renderStatus('Loading themes…');
    refreshButton.disabled = true;
    try {
      const {manifestJson}: {manifestJson: string} = await pageHandler.fetchThemeStore();
      const manifest = JSON.parse(manifestJson) as {
        version: number;
        themes: ThemeStoreEntry[];
      };
      content.replaceChildren();
      const entries = Array.isArray(manifest.themes) ? manifest.themes : [];
      if (!entries.length) {
        renderStatus('No themes available');
        return;
      }
      entries.forEach((entry) => {
        content.appendChild(createThemeCard(entry));
      });
    } catch {
      renderStatus('Failed to load themes', true);
    } finally {
      refreshButton.disabled = false;
    }
  };

  refreshButton.addEventListener('click', () => {
    void loadThemes();
  });

  void loadThemes();
  return section;
}

function showThemeStorePanel() {
  if (!wizardState || wizardState.mode !== 'edit-theme') {
    return;
  }
  const content = document.querySelector<HTMLElement>('.space-wizard__content');
  const panel = content?.parentElement;
  if (!content || !panel) {
    wizardState.mode = 'theme-store';
    themeEditorState = null;
    cachedNoiseData = null;
    renderSpaceStrip();
    return;
  }
  wizardState.mode = 'theme-store';
  themeEditorState = null;
  cachedNoiseData = null;
  const title = panel.querySelector<HTMLElement>('.space-wizard__title');
  if (title) {
    title.textContent = getWizardTitle();
  }
  content.replaceWith(createThemeStorePanel());
}

function showThemeEditorPanel() {
  if (!wizardState || wizardState.mode !== 'theme-store') {
    return;
  }
  const content = document.querySelector<HTMLElement>('.space-wizard__content');
  const panel = content?.parentElement;
  wizardState.mode = 'edit-theme';
  if (!content || !panel) {
    renderSpaceStrip();
    return;
  }
  const title = panel.querySelector<HTMLElement>('.space-wizard__title');
  if (title) {
    title.textContent = getWizardTitle();
  }
  content.replaceWith(createThemeEditorPanel());
}


function redrawThemeCanvas() {
  if (!themeEditorState?.canvas) {
    return;
  }
  const canvas = themeEditorState.canvas;
  const ctx = canvas.getContext('2d');
  if (!ctx) {
    return;
  }

  const width = canvas.width;
  const height = canvas.height;
  const image = ctx.createImageData(width, height);
  const pixels = image.data;

  for (let y = 0; y < height; y += 1) {
    const saturation = canvasYToSaturation(y / Math.max(1, height - 1));
    for (let x = 0; x < width; x += 1) {
      const hue = x / Math.max(1, width - 1);
      const [r, g, b] = hsbToRgb(hue, saturation, themeEditorState.currentColor.brightness);
      const index = (y * width + x) * 4;
      pixels[index] = Math.round(r);
      pixels[index + 1] = Math.round(g);
      pixels[index + 2] = Math.round(b);
      pixels[index + 3] = 255;
    }
  }
  ctx.putImageData(image, 0, 0);

  if (themeEditorState.previewMode === 'zen') {
    const zenGlow = ctx.createRadialGradient(
        width * 0.28, height * 0.24, width * 0.06,
        width * 0.52, height * 0.56, width * 0.78);
    zenGlow.addColorStop(0, 'rgba(255, 255, 255, 0.22)');
    zenGlow.addColorStop(0.28, 'rgba(255, 255, 255, 0.08)');
    zenGlow.addColorStop(1, 'rgba(0, 0, 0, 0.22)');
    ctx.save();
    ctx.fillStyle = zenGlow;
    ctx.fillRect(0, 0, width, height);
    ctx.restore();
  }

  if (themeEditorState.previewMode === 'gradient') {
    const gradientColors = computeHarmonyColors(
        themeEditorState.currentColor, themeEditorState.harmony);
    const gradient = ctx.createLinearGradient(0, 0, width, height);
    const stopDivisor = Math.max(1, gradientColors.length - 1);
    gradientColors.forEach((color, index) => {
      gradient.addColorStop(
          index / stopDivisor, hsbToHex(color.hue, color.saturation, color.brightness));
    });
    ctx.save();
    ctx.globalAlpha = themeEditorState.opacity;
    ctx.fillStyle = gradient;
    ctx.fillRect(0, 0, width, height);
    ctx.restore();
  }

  ctx.save();
  ctx.fillStyle = 'rgba(255, 255, 255, 0.08)';
  for (let x = 10; x < width; x += 20) {
    for (let y = 10; y < height; y += 20) {
      ctx.beginPath();
      ctx.arc(x, y, 0.9, 0, Math.PI * 2);
      ctx.fill();
    }
  }
  ctx.restore();

  ctx.save();
  ctx.strokeStyle = 'rgba(255, 255, 255, 0.08)';
  ctx.lineWidth = 1;
  ctx.setLineDash([2, 10]);
  ctx.strokeRect(14, 14, Math.max(0, width - 28), Math.max(0, height - 28));
  ctx.restore();

  if (themeEditorState.currentColor.grain > 0) {
    if (!cachedNoiseData || cachedNoiseData.width !== width || cachedNoiseData.height !== height) {
      const noiseCanvas = document.createElement('canvas');
      noiseCanvas.width = width;
      noiseCanvas.height = height;
      const noiseCtx = noiseCanvas.getContext('2d');
      if (noiseCtx) {
        const noise = noiseCtx.createImageData(width, height);
        for (let index = 0; index < noise.data.length; index += 4) {
          const value = Math.floor(Math.random() * 255);
          noise.data[index] = value;
          noise.data[index + 1] = value;
          noise.data[index + 2] = value;
          noise.data[index + 3] = 255;
        }
        noiseCtx.putImageData(noise, 0, 0);
        cachedNoiseData = {width, height, canvas: noiseCanvas};
      }
    }
    if (cachedNoiseData) {
      ctx.save();
      ctx.globalAlpha = themeEditorState.currentColor.grain * 0.25;
      ctx.drawImage(cachedNoiseData.canvas, 0, 0);
      ctx.restore();
    }
  }

  if (themeEditorState.cursorPoint) {
    const cursorX = themeEditorState.cursorPoint.x * width;
    const cursorY = themeEditorState.cursorPoint.y * height;
    ctx.save();
    ctx.strokeStyle = 'rgba(255, 255, 255, 1)';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.arc(cursorX, cursorY, 6, 0, Math.PI * 2);
    ctx.stroke();
    ctx.restore();
  }
}


function createColorPalette(
    selectedColor: string, onSelect: (color: string) => void): HTMLElement {
  const palette = document.createElement('div');
  palette.className = 'space-color-palette';

  COLOR_OPTIONS.forEach((color) => {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'space-color-swatch';
    button.style.backgroundColor = color;
    button.setAttribute('aria-label', `Select ${color}`);
    if (selectedColor.toLowerCase() === color.toLowerCase()) {
      button.classList.add('is-selected');
    }
    button.addEventListener('click', () => onSelect(color));
    palette.appendChild(button);
  });

  return palette;
}

function renderContextMenu() {
  elements.contextMenuLayer.replaceChildren();

  if (!contextMenuState) {
    return;
  }

  const space = spaces.find((item) => item.id === contextMenuState?.spaceId);
  if (!space) {
    contextMenuState = null;
    return;
  }

  const menu = document.createElement('div');
  menu.className = 'space-context-menu';
  menu.style.left = `${contextMenuState.x}px`;
  menu.style.top = `${contextMenuState.y}px`;
  menu.setAttribute('role', 'menu');
  menu.setAttribute('aria-label', `${space.name} options`);
  menu.setAttribute('data-maho-identifier', 'sidebar.spaceContextMenu');
  menu.setAttribute('data-maho-space-id', space.id);

  menu.append(
      createContextMenuButton('Manage Spaces', () => {
        closeContextMenu();
        openManageSpacesPanel();
      }),
      createContextMenuButton('Rename Space', () => {
        closeContextMenu();
        openInlineRename(space.id);
      }),
      createContextMenuButton('Change Icon', () => {
        closeContextMenu();
        openIconEditor(space);
      }),
      createContextMenuButton('Edit Theme', () => {
        closeContextMenu();
        openThemeEditor(space);
      }),
      createContextMenuButton('Export Theme', () => {
        closeContextMenu();
        void pageHandler.exportTheme(space.id);
      }),
      createContextMenuButton('Import Theme', () => {
        closeContextMenu();
        void pageHandler.importTheme(space.id);
      }),
      createContextSeparator(),
  );

  if (contextMenuState.confirmDelete) {
    const confirmation = document.createElement('div');
    confirmation.className = 'space-context-menu__confirm';
    confirmation.setAttribute('data-maho-identifier', 'sidebar.spaceDeleteConfirmation');
    confirmation.setAttribute('data-maho-space-id', space.id);

    const copy = document.createElement('div');
    copy.className = 'space-context-menu__confirm-copy';
    copy.textContent = `Delete ${space.name}?`;

    const actions = document.createElement('div');
    actions.className = 'space-context-menu__confirm-actions';

    const cancelButton = document.createElement('button');
    cancelButton.type = 'button';
    cancelButton.className = 'space-context-menu__ghost';
    cancelButton.textContent = 'Cancel';
    cancelButton.setAttribute('data-maho-identifier', 'sidebar.spaceDelete.cancel');
    cancelButton.addEventListener('click', () => {
      if (!contextMenuState) {
        return;
      }
      contextMenuState.confirmDelete = false;
      renderContextMenu();
    });

    const confirmButton = document.createElement('button');
    confirmButton.type = 'button';
    confirmButton.className = 'space-context-menu__danger';
    confirmButton.textContent = 'Delete';
    confirmButton.setAttribute('data-maho-identifier', 'sidebar.spaceDelete.confirm');
    confirmButton.addEventListener('click', () => {
      closeContextMenu();
      closeWizard();
      void pageHandler.deleteSpace(space.id);
    });

    actions.append(cancelButton, confirmButton);
    confirmation.append(copy, actions);
    menu.appendChild(confirmation);
  } else {
    menu.appendChild(createContextMenuButton('Delete Space', () => {
      if (!contextMenuState) {
        return;
      }
      contextMenuState.confirmDelete = true;
      renderContextMenu();
    }, true));
  }

  elements.contextMenuLayer.appendChild(menu);
  requestAnimationFrame(() => clampContextMenuPosition(menu));
}

function createContextMenuButton(
    label: string, onClick: () => void, isDanger = false): HTMLElement {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'space-context-menu__item';
  if (isDanger) {
    button.classList.add('is-danger');
  }
  button.textContent = label;
  button.setAttribute('role', 'menuitem');
  if (label === 'Manage Spaces') {
    button.setAttribute('data-maho-identifier', 'sidebar.spaceContextMenu.manageSpaces');
  } else if (label === 'Rename Space') {
    button.setAttribute('data-maho-identifier', 'sidebar.spaceContextMenu.rename');
  } else if (label === 'Change Icon') {
    button.setAttribute('data-maho-identifier', 'sidebar.spaceContextMenu.changeIcon');
  } else if (label === 'Edit Theme') {
    button.setAttribute('data-maho-identifier', 'sidebar.spaceContextMenu.editTheme');
  } else if (label === 'Export Theme') {
    button.setAttribute('data-maho-identifier', 'space.contextMenu.exportTheme');
  } else if (label === 'Import Theme') {
    button.setAttribute('data-maho-identifier', 'space.contextMenu.importTheme');
  } else if (isDanger) {
    button.setAttribute('data-maho-identifier', 'sidebar.spaceContextMenu.delete');
  } else {
    button.setAttribute('data-maho-identifier', `sidebar.spaceContextMenu.${label.replace(/\s+/g, '')}`);
  }
  button.addEventListener('click', onClick);
  return button;
}

function createContextSeparator(): HTMLElement {
  const separator = document.createElement('div');
  separator.className = 'space-context-menu__separator';
  separator.setAttribute('role', 'separator');
  separator.setAttribute('data-maho-identifier', 'sidebar.spaceContextMenu.separator');
  return separator;
}

function clampContextMenuPosition(menu: HTMLElement) {
  if (!contextMenuState) {
    return;
  }
  const bounds = elements.app.getBoundingClientRect();
  const maxLeft = bounds.width - menu.offsetWidth - 12;
  const maxTop = bounds.height - menu.offsetHeight - 12;
  const left = Math.max(12, Math.min(contextMenuState.x, maxLeft));
  const top = Math.max(12, Math.min(contextMenuState.y, maxTop));
  menu.style.left = `${left}px`;
  menu.style.top = `${top}px`;
}

function renderTabs() {
  elements.tabList.replaceChildren();

  const pinnedTabs = tabs.filter((tab) => tab.section === 'pinned');
  const todayTabs = tabs.filter((tab) => tab.section !== 'pinned');

  if (pinnedTabs.length > 0) {
    elements.tabList.appendChild(createSectionHeader('Pinned'));
    pinnedTabs.forEach((tab) => {
      elements.tabList.appendChild(createTabItem(tab));
    });
  }

  elements.tabList.appendChild(createSectionHeader('Today'));
  todayTabs.forEach((tab) => {
    elements.tabList.appendChild(createTabItem(tab));
  });
}

function createSectionHeader(title: string): HTMLElement {
  const header = document.createElement('div');
  header.className = 'tab-section-header';
  header.textContent = title;
  return header;
}

function createTabItem(tab: TabInfo): HTMLElement {
  const item = document.createElement('div');
  item.className = 'tab-item' + (tab.active ? ' active' : '');

  const closeBtn = document.createElement('button');
  closeBtn.type = 'button';
  closeBtn.className = 'close-btn';
  closeBtn.textContent = '×';
  closeBtn.setAttribute('aria-label', `Close ${tab.title}`);
  closeBtn.setAttribute('data-maho-identifier', 'sidebarTabCloseButton');
  closeBtn.setAttribute('data-maho-tab-id', String(tab.tabId));
  closeBtn.addEventListener('click', (event) => {
    event.stopPropagation();
    void pageHandler.closeTab(tab.tabId);
  });

  const details = document.createElement('div');
  details.className = 'tab-details';

  const title = document.createElement('div');
  title.className = 'tab-title';
  title.textContent = tab.title;

  const url = document.createElement('div');
  url.className = 'tab-url';
  url.textContent = tab.url;

  details.append(title, url);
  item.append(closeBtn, details);
  item.addEventListener('click', () => {
    void pageHandler.activateTab(tab.tabId);
  });
  return item;
}

function handleGlobalKeyDown(event: KeyboardEvent) {
  if (event.key === 'Escape') {
    if (renameSpaceId) {
      renameSpaceId = null;
      renameValue = '';
      renderSpaceStrip();
      return;
    }
    if (contextMenuState) {
      closeContextMenu();
      return;
    }
    if (wizardState?.mode === 'manage-spaces') {
      closeWizard();
      return;
    }
    if (wizardState) {
      closeWizard();
    }
    return;
  }

  if (shouldIgnoreGlobalShortcut(event)) {
    return;
  }

  const hasPrimaryModifier = event.metaKey || event.ctrlKey;
  if (hasPrimaryModifier && !event.shiftKey && /^[1-9]$/.test(event.key)) {
    const targetIndex = Number.parseInt(event.key, 10) - 1;
    const targetSpace = spaces[targetIndex];
    if (!targetSpace) {
      return;
    }
    event.preventDefault();
    void pageHandler.activateSpace(targetSpace.id);
    return;
  }

  if (hasPrimaryModifier && event.altKey && !event.shiftKey &&
      (event.key === 'ArrowLeft' || event.key === 'ArrowRight')) {
    event.preventDefault();
    switchRelativeSpace(event.key === 'ArrowRight' ? 1 : -1);
  }
}

function handleDocumentPointerDown(event: PointerEvent) {
  const target = event.target as HTMLElement | null;
  if (!target) {
    closeContextMenu();
    closeWizard();
    return;
  }

  const insideMenu = Boolean(target.closest('.space-context-menu'));
  const insideWizard = Boolean(target.closest('.space-wizard'));
  const insideManagePanel = Boolean(target.closest('.manage-spaces-panel'));
  const onSpaceTrigger = Boolean(
      target.closest('.space-icon') || target.closest('.space-add-btn'));
  const insideRename = Boolean(target.closest('.space-rename-shell'));

  if (!insideMenu && contextMenuState) {
    closeContextMenu();
  }

  if (!insideWizard && !insideManagePanel && !onSpaceTrigger && !insideMenu && wizardState) {
    closeWizard();
  }

  if (!insideManagePanel && wizardState?.mode === 'manage-spaces' && !insideMenu && !insideWizard && !onSpaceTrigger) {
    closeWizard();
  }

  if (!insideRename && !insideMenu && renameSpaceId) {
    renameSpaceId = null;
    renameValue = '';
    renderSpaceStrip();
  }
}

function handleStripWheel(event: WheelEvent) {
  if (Math.abs(event.deltaX) <= Math.abs(event.deltaY) ||
      Math.abs(event.deltaX) <= 30) {
    return;
  }

  const now = Date.now();
  if (now - lastSwipeAt < SWIPE_DEBOUNCE_MS) {
    return;
  }
  lastSwipeAt = now;
  switchRelativeSpace(event.deltaX > 0 ? 1 : -1);
}

function handleStripDragOver(event: DragEvent) {
  if (!dragState) {
    return;
  }
  event.preventDefault();
  dragState.indicatorIndex = spaces.length;
  renderSpaceStrip();
}

function handleStripDrop(event: DragEvent) {
  if (!dragState) {
    return;
  }
  event.preventDefault();
  commitSpaceReorder(dragState.indicatorIndex);
}

function handleSpaceDragStart(event: DragEvent, spaceId: string, index: number) {
  dragState = {spaceId, sourceIndex: index, indicatorIndex: index};
  closeContextMenu();
  closeWizard();
  if (event.dataTransfer) {
    event.dataTransfer.effectAllowed = 'move';
    event.dataTransfer.setData('text/plain', spaceId);
  }
}

function handleSpaceDragOver(event: DragEvent, index: number) {
  if (!dragState) {
    return;
  }
  event.preventDefault();
  const target = event.currentTarget as HTMLElement;
  const bounds = target.getBoundingClientRect();
  const midpoint = bounds.top + bounds.height / 2;
  dragState.indicatorIndex = event.clientY < midpoint ? index : index + 1;
  renderSpaceStrip();
}

function handleSpaceDrop(event: DragEvent, index: number) {
  if (!dragState) {
    return;
  }
  event.preventDefault();
  const target = event.currentTarget as HTMLElement;
  const bounds = target.getBoundingClientRect();
  const midpoint = bounds.top + bounds.height / 2;
  const targetIndex = event.clientY < midpoint ? index : index + 1;
  commitSpaceReorder(targetIndex);
}

function commitSpaceReorder(targetIndex: number) {
  if (!dragState) {
    return;
  }

  const sourceId = dragState.spaceId;
  const sourceIndex = spaces.findIndex((space) => space.id === sourceId);
  clearDragState();
  if (sourceIndex < 0) {
    return;
  }

  let nextIndex = targetIndex;
  if (sourceIndex < nextIndex) {
    nextIndex -= 1;
  }
  nextIndex = Math.max(0, Math.min(nextIndex, Math.max(spaces.length - 1, 0)));
  if (nextIndex === sourceIndex) {
    return;
  }
  void pageHandler.reorderSpace(sourceId, nextIndex);
}

function clearDragState() {
  dragState = null;
  renderSpaceStrip();
}

function triggerCreateSpace() {
  const activeSpace = getActiveSpace();
  wizardState = {
    mode: 'create',
    step: 'icon',
    targetSpaceId: null,
    selectedIcon: activeSpace?.icon || EMOJI_OPTIONS[0]?.emoji || '✨',
    selectedColor: normalizeSpaceColor(activeSpace?.color || COLOR_OPTIONS[0] || '#F16063'),
    name: '',
    search: '',
    insertAfterIndex: getActiveSpaceIndex(),
  };
  renameSpaceId = null;
  renameValue = '';
  closeContextMenu();
  renderSpaceStrip();
}

function openManageSpacesPanel() {
  themeEditorState = null;
  cachedNoiseData = null;
  wizardState = {
    mode: 'manage-spaces',
    step: 'details',
    targetSpaceId: null,
    selectedIcon: getActiveSpace()?.icon || EMOJI_OPTIONS[0]?.emoji || '✨',
    selectedColor: normalizeSpaceColor(getActiveSpace()?.color || COLOR_OPTIONS[0] || '#F16063'),
    name: '',
    search: '',
    insertAfterIndex: getActiveSpaceIndex(),
  };
  renameSpaceId = null;
  renameValue = '';
  renderSpaceStrip();
}

function submitCreateSpace() {
  if (!wizardState || wizardState.mode !== 'create') {
    return;
  }
  const name = wizardState.name.trim();
  if (!name) {
    focusWizardNameInput();
    return;
  }
  void pageHandler.createSpace(
      name,
      wizardState.selectedColor,
      wizardState.selectedIcon,
      null,
      wizardState.insertAfterIndex);
  closeWizard();
}

function openContextMenu(spaceId: string, clientX: number, clientY: number) {
  const bounds = elements.app.getBoundingClientRect();
  contextMenuState = {
    spaceId,
    x: clientX - bounds.left,
    y: clientY - bounds.top,
    confirmDelete: false,
  };
  renameSpaceId = null;
  renameValue = '';
  renderSpaceStrip();
}

function openInlineRename(spaceId: string) {
  const space = spaces.find((item) => item.id === spaceId);
  if (!space) {
    return;
  }
  renameSpaceId = spaceId;
  renameValue = space.name;
  closeWizard();
  renderSpaceStrip();
}

function openIconEditor(space: SpaceInfo) {
  wizardState = {
    mode: 'change-icon',
    step: 'icon',
    targetSpaceId: space.id,
    selectedIcon: space.icon || getSpaceDisplayIcon(space),
    selectedColor: normalizeSpaceColor(space.color),
    name: space.name,
    search: '',
    insertAfterIndex: getActiveSpaceIndex(),
  };
  renameSpaceId = null;
  renameValue = '';
  renderSpaceStrip();
}

function openThemeEditor(space: SpaceInfo) {
  wizardState = {
    mode: 'edit-theme',
    step: 'details',
    targetSpaceId: space.id,
    selectedIcon: space.icon || getSpaceDisplayIcon(space),
    selectedColor: normalizeSpaceColor(space.color),
    name: space.name,
    search: '',
    insertAfterIndex: getActiveSpaceIndex(),
  };
  renameSpaceId = null;
  renameValue = '';
  themeEditorState = null;
  cachedNoiseData = null;
  renderSpaceStrip();
}

function closeContextMenu() {
  if (!contextMenuState) {
    return;
  }
  contextMenuState = null;
  renderContextMenu();
}

function closeWizard() {
  if (!wizardState) {
    return;
  }
  themeEditorState = null;
  cachedNoiseData = null;
  wizardState = null;
  renderSpaceStrip();
}

function animateTabSwitch() {
  elements.tabList.classList.add('is-switching');
  window.setTimeout(() => {
    elements.tabList.classList.remove('is-switching');
  }, 140);
}

function applyThemeTint(activeSpace: SpaceInfo|null) {
  const color = normalizeSpaceColor(activeSpace?.color || COLOR_OPTIONS[0] || '#F16063');
  elements.app.style.setProperty('--space-theme-color', color);
  elements.app.style.setProperty('--space-theme-color-muted', withAlpha(color, '15'));
  elements.app.style.setProperty('--space-theme-bg', withAlpha(color, '0A'));
}

function switchRelativeSpace(offset: number) {
  if (!spaces.length) {
    return;
  }
  const currentIndex = getActiveSpaceIndex();
  const nextIndex = (currentIndex + offset + spaces.length) % spaces.length;
  const nextSpace = spaces[nextIndex];
  if (!nextSpace) {
    return;
  }
  void pageHandler.activateSpace(nextSpace.id);
}

function getActiveSpace(): SpaceInfo|null {
  return spaces.find((space) => space.active) || spaces[0] || null;
}

function getActiveSpaceIndex(): number {
  const index = spaces.findIndex((space) => space.active);
  return index >= 0 ? index : 0;
}

function getSpaceDisplayIcon(space: SpaceInfo): string {
  const icon = space.icon?.trim();
  if (icon) {
    return icon;
  }
  const initial = space.name.trim().charAt(0).toUpperCase();
  return initial || '•';
}

function getSpaceTabCount(space: SpaceInfo): number {
  const compat = space as SpaceInfoCompat;
  return compat.tabCount ?? compat.tab_count ?? 0;
}

function normalizeSpaceColor(color: string): string {
  return /^#[0-9a-fA-F]{6}$/.test(color) ? color : '#F16063';
}

function withAlpha(color: string, alpha: string): string {
  if (!/^#[0-9a-fA-F]{6}$/.test(color)) {
    return color;
  }
  return `${color}${alpha}`;
}

function shouldIgnoreGlobalShortcut(event: KeyboardEvent): boolean {
  const target = event.target as HTMLElement | null;
  if (!target) {
    return false;
  }
  const tagName = target.tagName.toLowerCase();
  return target.isContentEditable || tagName === 'input' || tagName === 'textarea' ||
      tagName === 'select';
}

function filterEmojiOptions(search: string): EmojiOption[] {
  const query = search.trim().toLowerCase();
  if (!query) {
    return EMOJI_OPTIONS;
  }
  return EMOJI_OPTIONS.filter((option) => {
    if (option.emoji.includes(query)) {
      return true;
    }
    if (option.label.toLowerCase().includes(query)) {
      return true;
    }
    return option.keywords.some((keyword) => keyword.toLowerCase().includes(query));
  });
}

function getWizardTitle(): string {
  if (!wizardState) {
    return '';
  }
  if (wizardState.mode === 'create') {
    return wizardState.step === 'icon' ? 'Choose an icon' : 'Name your space';
  }
  if (wizardState.mode === 'change-icon') {
    return 'Change icon';
  }
  if (wizardState.mode === 'theme-store') {
    return 'Theme store';
  }
  return 'Edit theme';
}

function focusWizardNameInput() {
  window.setTimeout(() => {
    const input = document.querySelector<HTMLInputElement>('.space-wizard__name');
    input?.focus();
    input?.select();
  }, 0);
}

function scheduleInitialize() {
  if (document.readyState === 'complete') {
    window.setTimeout(initialize, 0);
    return;
  }
  window.addEventListener('load', () => {
    window.setTimeout(initialize, 0);
  }, {once: true});
}

scheduleInitialize();
