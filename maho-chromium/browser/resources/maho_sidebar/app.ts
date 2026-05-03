import {
  PageHandlerFactory,
  PageHandlerRemote,
  SpaceInfo,
  TabInfo,
} from './maho_sidebar.mojom-webui.js';
import {PageCallbackRouter} from './maho_sidebar.mojom-webui.js';
import type {ATCRule, ProfileInfo} from './maho_sidebar.mojom-webui.js';

type WizardMode = 'create'|'change-icon'|'edit-theme'|'theme-store';
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
  tabList: HTMLElement;
  contextMenuLayer: HTMLElement;
}

interface EmojiOption {
  emoji: string;
  label: string;
  keywords: string[];
}

interface SpaceColor {
  hue: number;
  saturation: number;
  brightness: number;
  grain: number;
}

interface ThemeColor {
  hue: number;
  saturation: number;
  brightness: number;
  isCustom: boolean;
  isPrimary: boolean;
}

type ColorHarmony = 'complementary'|'single_analogous'|'split_complementary'|'analogous'|'triadic'|'custom';

interface SolidTheme {
  type: 'solid';
  color: SpaceColor;
}

interface GradientTheme {
  type: 'gradient';
  gradientColors: ThemeColor[];
  harmony: ColorHarmony;
  opacity: number;
  texture: number;
}

type SpaceThemeData = SolidTheme|GradientTheme;

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

const PRESET_COLORS: {title: string; color: SpaceColor}[] = [
  {title: 'Coral', color: {hue: 0.0, saturation: 0.62, brightness: 0.95, grain: 0}},
  {title: 'Orange', color: {hue: 0.07, saturation: 0.60, brightness: 0.95, grain: 0}},
  {title: 'Gold', color: {hue: 0.12, saturation: 0.68, brightness: 0.97, grain: 0}},
  {title: 'Green', color: {hue: 0.38, saturation: 0.52, brightness: 0.77, grain: 0}},
  {title: 'Cyan', color: {hue: 0.52, saturation: 0.60, brightness: 0.85, grain: 0}},
  {title: 'Blue', color: {hue: 0.62, saturation: 0.63, brightness: 1.0, grain: 0}},
  {title: 'Purple', color: {hue: 0.72, saturation: 0.52, brightness: 0.96, grain: 0}},
  {title: 'Pink', color: {hue: 0.90, saturation: 0.58, brightness: 0.90, grain: 0}},
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
  isGradientMode: boolean;
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
}

function createShell(app: HTMLElement): Elements {
  app.replaceChildren();

  const header = createHeader();
  const body = document.createElement('div');
  body.className = 'sidebar-body';

  const strip = document.createElement('aside');
  strip.className = 'space-strip';
  strip.setAttribute('aria-label', 'Spaces');

  const stripRail = document.createElement('div');
  stripRail.className = 'space-strip-rail';

  const tabPanel = document.createElement('section');
  tabPanel.className = 'tab-panel';

  const tabList = document.createElement('div');
  tabList.className = 'tab-list';

  const contextMenuLayer = document.createElement('div');
  contextMenuLayer.className = 'space-context-menu-layer';

  strip.appendChild(stripRail);
  tabPanel.appendChild(tabList);
  body.append(strip, tabPanel);
  app.append(header, body, contextMenuLayer);

  return {app, body, strip, stripRail, tabList, contextMenuLayer};
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

  heading.append(eyebrow, title);

  const collapseBtn = document.createElement('button');
  collapseBtn.type = 'button';
  collapseBtn.className = 'sidebar-collapse-btn';
  collapseBtn.textContent = '‹';
  collapseBtn.title = 'Collapse sidebar';
  collapseBtn.setAttribute('aria-label', 'Collapse sidebar');
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

function createDropIndicator(): HTMLElement {
  const indicator = document.createElement('div');
  indicator.className = 'space-drop-indicator';
  return indicator;
}

function createWizardPanel(): HTMLElement|null {
  if (!wizardState) {
    return null;
  }

  const panel = document.createElement('section');
  panel.className = 'space-wizard';
  panel.setAttribute('aria-label', getWizardTitle());

  const header = document.createElement('div');
  header.className = 'space-wizard__header';

  const title = document.createElement('div');
  title.className = 'space-wizard__title';
  title.textContent = getWizardTitle();

  const closeButton = document.createElement('button');
  closeButton.type = 'button';
  closeButton.className = 'space-wizard__close';
  closeButton.textContent = '×';
  closeButton.setAttribute('aria-label', 'Close panel');
  closeButton.addEventListener('click', closeWizard);

  header.append(title, closeButton);
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

  const initialColor = hexToHsb(wizardState?.selectedColor || '') ||
      {...PRESET_COLORS[0]!.color};
  const initialPresetIndex = PRESET_COLORS.findIndex(({color}) => {
    return Math.abs(color.hue - initialColor.hue) < 0.01 &&
        Math.abs(color.saturation - initialColor.saturation) < 0.03 &&
        Math.abs(color.brightness - initialColor.brightness) < 0.03;
  });

  themeEditorState = {
    currentColor: {
      hue: initialColor.hue,
      saturation: Math.min(1, Math.max(0.2, initialColor.saturation)),
      brightness: Math.min(1, Math.max(0, initialColor.brightness)),
      grain: initialColor.grain,
    },
    isGradientMode: false,
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

  const canvasWrapper = document.createElement('div');
  canvasWrapper.className = 'theme-canvas-wrapper';

  const canvas = document.createElement('canvas');
  canvas.width = 280;
  canvas.height = 200;
  canvas.setAttribute('aria-label', 'Theme color canvas');
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
  instruction.textContent = 'Tap to pick a color for this space';

  const brightnessRow = document.createElement('div');
  brightnessRow.className = 'theme-brightness-row';

  const brightnessDown = document.createElement('button');
  brightnessDown.type = 'button';
  brightnessDown.className = 'theme-brightness-btn';
  brightnessDown.textContent = '−';
  brightnessDown.setAttribute('aria-label', 'Decrease brightness');

  const brightnessUp = document.createElement('button');
  brightnessUp.type = 'button';
  brightnessUp.className = 'theme-brightness-btn';
  brightnessUp.textContent = '+';
  brightnessUp.setAttribute('aria-label', 'Increase brightness');

  brightnessRow.append(brightnessDown, brightnessUp);

  const hexInput = document.createElement('input');
  hexInput.className = 'theme-hex-input';
  hexInput.type = 'text';
  hexInput.spellcheck = false;
  hexInput.maxLength = 7;
  hexInput.setAttribute('aria-label', 'Theme hex color');

  const swatchRow = document.createElement('div');
  swatchRow.className = 'theme-swatch-row';

  const gradientToggle = document.createElement('button');
  gradientToggle.type = 'button';
  gradientToggle.className = 'theme-gradient-toggle';
  gradientToggle.textContent = 'Gradient';
  gradientToggle.setAttribute('aria-pressed', 'false');

  overlay.append(iconCircle, name, tabsCount, instruction, brightnessRow, hexInput, swatchRow,
      gradientToggle);
  canvasWrapper.append(canvas, overlay);

  const controls = document.createElement('div');
  controls.className = 'theme-controls';

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
  grainRow.append(grainLabel, grainSlider);

  const gradientGroup = document.createElement('div');
  gradientGroup.className = 'theme-gradient-only is-hidden';

  const harmonyControl = document.createElement('div');
  harmonyControl.className = 'theme-control-row';
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
  opacityRow.append(opacityLabel, opacitySlider);
  gradientGroup.append(harmonyControl, opacityRow);

  controls.append(grainRow, gradientGroup);

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
      const theme: SpaceThemeData = themeEditorState.isGradientMode ? {
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
  section.append(canvasWrapper, controls, browseThemesButton, actions);

  const updateInstruction = () => {
    if (!themeEditorState) {
      return;
    }
    instruction.classList.toggle('is-hidden', !themeEditorState.instructionVisible);
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
    swatchRow.querySelectorAll('.theme-swatch').forEach((node, index) => {
      node.classList.toggle('is-selected', index === themeEditorState?.selectedPresetIndex);
    });
    gradientToggle.classList.toggle('is-active', themeEditorState.isGradientMode);
    gradientToggle.setAttribute('aria-pressed', String(themeEditorState.isGradientMode));
    gradientGroup.classList.toggle('is-hidden', !themeEditorState.isGradientMode);
    grainRow.classList.toggle('is-hidden', themeEditorState.isGradientMode);
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

  PRESET_COLORS.forEach(({title, color}, index) => {
    const swatch = document.createElement('button');
    swatch.type = 'button';
    swatch.className = 'theme-swatch';
    swatch.title = title;
    swatch.setAttribute('aria-label', title);
    swatch.style.backgroundColor = hsbToHex(color.hue, color.saturation, color.brightness);
    swatch.addEventListener('click', () => {
      setInteractionState();
      applyColorSelection({...color, grain: themeEditorState?.currentColor.grain || 0}, index);
    });
    swatchRow.appendChild(swatch);
  });

  gradientToggle.addEventListener('click', () => {
    if (!themeEditorState) {
      return;
    }
    setInteractionState();
    themeEditorState.isGradientMode = !themeEditorState.isGradientMode;
    updateThemePreview();
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

  const header = document.createElement('div');
  header.className = 'theme-store-header';

  const backButton = document.createElement('button');
  backButton.type = 'button';
  backButton.className = 'theme-store-back';
  backButton.textContent = '←';
  backButton.setAttribute('aria-label', 'Back to theme editor');
  backButton.addEventListener('click', showThemeEditorPanel);

  const title = document.createElement('h3');
  title.textContent = 'Theme Store';

  const refreshButton = document.createElement('button');
  refreshButton.type = 'button';
  refreshButton.className = 'theme-store-refresh';
  refreshButton.textContent = 'Refresh';
  refreshButton.setAttribute('aria-label', 'Refresh themes');

  header.append(backButton, title, refreshButton);

  const content = document.createElement('div');
  content.className = 'theme-store-list';

  section.append(header, content);

  const renderStatus = (message: string, canRetry = false) => {
    content.replaceChildren();
    const status = document.createElement('div');
    status.className = 'theme-store-status';
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

  if (themeEditorState.isGradientMode) {
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

function computeHarmonyColors(color: SpaceColor, harmony: ColorHarmony): ThemeColor[] {
  const primary: ThemeColor = {
    hue: normalizeHue(color.hue),
    saturation: color.saturation,
    brightness: color.brightness,
    isCustom: harmony === 'custom',
    isPrimary: true,
  };
  const buildColor = (hueOffset: number): ThemeColor => ({
    hue: normalizeHue(color.hue + hueOffset),
    saturation: color.saturation,
    brightness: color.brightness,
    isCustom: false,
    isPrimary: false,
  });

  // Offsets match Swift SpaceColorPreviewView harmony calculations.
  switch (harmony) {
    case 'complementary':
      return [primary, buildColor(0.5)];
    case 'single_analogous':
      // Swift: 310° = -50° on the color wheel.
      return [primary, buildColor(310 / 360)];
    case 'split_complementary':
      return [primary, buildColor(0.417), buildColor(0.583)];
    case 'analogous':
      // Swift: ±50° offsets. Sidebar maps "Analog" button here for
      // 3-color gradient parity with the native shell.
      return [primary, buildColor(-50 / 360), buildColor(50 / 360)];
    case 'triadic':
      return [primary, buildColor(0.333), buildColor(0.667)];
    case 'custom':
      return [primary];
  }
}

function normalizeHue(hue: number): number {
  return hue - Math.floor(hue);
}

function canvasYToSaturation(normalizedY: number): number {
  const clampedY = Math.min(1, Math.max(0, normalizedY));
  return 0.2 + (1 - clampedY) * 0.8;
}

function saturationToCanvasY(saturation: number): number {
  const clampedSaturation = Math.min(1, Math.max(0.2, saturation));
  return 1 - ((clampedSaturation - 0.2) / 0.8);
}

function hsbToRgb(h: number, s: number, b: number): [number, number, number] {
  if (s <= 0) {
    return [b * 255, b * 255, b * 255];
  }
  const hWrapped = h - Math.floor(h);
  const h6 = hWrapped * 6;
  const sector = Math.floor(h6) % 6;
  const f = h6 - Math.floor(h6);
  const p = b * (1 - s);
  const q = b * (1 - s * f);
  const t = b * (1 - s * (1 - f));
  const pairs: [number, number, number][] = [
    [b, t, p], [q, b, p], [p, b, t], [p, q, b], [t, p, b], [b, p, q],
  ];
  const [r, g, bb] = pairs[sector]!;
  return [r * 255, g * 255, bb * 255];
}

function hexToHsb(hex: string): SpaceColor|null {
  const match = hex.match(/^#?([0-9a-fA-F]{6})$/);
  if (!match) {
    return null;
  }
  const r = parseInt(match[1]!.substring(0, 2), 16) / 255;
  const g = parseInt(match[1]!.substring(2, 4), 16) / 255;
  const b = parseInt(match[1]!.substring(4, 6), 16) / 255;
  const max = Math.max(r, g, b);
  const min = Math.min(r, g, b);
  const delta = max - min;
  let h = 0;
  if (delta > 0) {
    if (max === r) {
      h = ((g - b) / delta + 6) % 6 / 6;
    } else if (max === g) {
      h = ((b - r) / delta + 2) / 6;
    } else {
      h = ((r - g) / delta + 4) / 6;
    }
  }
  const s = max > 0 ? delta / max : 0;
  return {hue: h, saturation: s, brightness: max, grain: 0};
}

function hsbToHex(h: number, s: number, b: number): string {
  const [r, g, bb] = hsbToRgb(h, s, b);
  const toHex = (value: number) => {
    return Math.round(Math.min(255, Math.max(0, value))).toString(16).padStart(2, '0');
  };
  return `#${toHex(r)}${toHex(g)}${toHex(bb)}`;
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

  menu.append(
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
      createContextSeparator(),
  );

  if (contextMenuState.confirmDelete) {
    const confirmation = document.createElement('div');
    confirmation.className = 'space-context-menu__confirm';

    const copy = document.createElement('div');
    copy.className = 'space-context-menu__confirm-copy';
    copy.textContent = `Delete ${space.name}?`;

    const actions = document.createElement('div');
    actions.className = 'space-context-menu__confirm-actions';

    const cancelButton = document.createElement('button');
    cancelButton.type = 'button';
    cancelButton.className = 'space-context-menu__ghost';
    cancelButton.textContent = 'Cancel';
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
  button.addEventListener('click', onClick);
  return button;
}

function createContextSeparator(): HTMLElement {
  const separator = document.createElement('div');
  separator.className = 'space-context-menu__separator';
  separator.setAttribute('role', 'separator');
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
  const onSpaceTrigger = Boolean(
      target.closest('.space-icon') || target.closest('.space-add-btn'));
  const insideRename = Boolean(target.closest('.space-rename-shell'));

  if (!insideMenu && contextMenuState) {
    closeContextMenu();
  }

  if (!insideWizard && !onSpaceTrigger && !insideMenu && wizardState) {
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
