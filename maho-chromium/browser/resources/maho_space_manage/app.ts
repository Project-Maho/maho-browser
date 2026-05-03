import {
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
} from './maho_space_manage.mojom-webui.js';
import type {SpaceInfo} from './maho_space_manage.mojom-webui.js';

let pageHandler: PageHandlerRemote;
let callbackRouter: PageCallbackRouter;
let spaces: SpaceInfo[] = [];
let selectedSpaceId = '';

function el<K extends keyof HTMLElementTagNameMap>(
    tag: K, className?: string, text?: string): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  if (className) {
    node.className = className;
  }
  if (text !== undefined) {
    node.textContent = text;
  }
  return node;
}

function bindMojo(): void {
  callbackRouter = new PageCallbackRouter();
  pageHandler = new PageHandlerRemote();
  const factory = PageHandlerFactory.getRemote();
  factory.createPageHandler(
      callbackRouter.$.bindNewPipeAndPassRemote(),
      pageHandler.$.bindNewPipeAndPassReceiver());
}

async function loadSpaces(): Promise<void> {
  const {spaces: result} = await pageHandler.getSpaces();
  spaces = result;
  const active = spaces.find((space) => space.active);
  selectedSpaceId = active?.id || spaces[0]?.id || '';
  render();
}

function switchToSpace(spaceId: string): void {
  void pageHandler.switchToSpace(spaceId).then(({success}) => {
    if (success) {
      loadSpaces();
    }
  });
}

function openCreateSpace(): void {
  void pageHandler.openCreateSpace();
}

function openConfigForSpace(spaceId: string): void {
  void pageHandler.openSpaceConfig(spaceId);
}

function closeDialog(): void {
  void pageHandler.closeDialog();
}

function renderCard(space: SpaceInfo): HTMLElement {
  const card = el('div', 'msm-card');
  if (space.active) {
    card.classList.add('active');
  }

  const main = el('div', 'msm-card-main');
  const icon = el('div', 'msm-icon', space.icon || '⊞');
  icon.style.background = `${space.color}26`;
  icon.style.color = space.color;
  const meta = el('div', 'msm-meta');
  const nameRow = el('div', 'msm-name-row');
  const name = el('div', 'msm-name', space.name);
  nameRow.appendChild(name);
  if (space.active) {
    nameRow.appendChild(el('div', 'msm-active-pill', 'Active'));
  }
  const color = el('div', 'msm-color');
  const dot = el('div', 'msm-color-dot');
  dot.style.background = space.color;
  color.append(dot, el('span', undefined, space.color));
  meta.append(nameRow, color);
  main.append(icon, meta);

  const actions = el('div', 'msm-card-actions');
  const switchBtn = el('button', 'msm-chip', space.active ? 'Current' : 'Switch');
  switchBtn.disabled = space.active;
  switchBtn.addEventListener('click', (event) => {
    event.stopPropagation();
    switchToSpace(space.id);
  });
  const configBtn = el('button', 'msm-chip', 'Configure');
  configBtn.addEventListener('click', (event) => {
    event.stopPropagation();
    openConfigForSpace(space.id);
  });
  actions.append(switchBtn, configBtn);

  card.append(main, actions);
  card.addEventListener('click', () => {
    selectedSpaceId = space.id;
    render();
  });

  return card;
}

function render(): void {
  const root = document.querySelector('maho-space-manage');
  if (!root) {
    return;
  }

  root.textContent = '';
  const header = el('div', 'msm-header');
  const titleBlock = el('div', 'msm-title-block');
  titleBlock.append(
      el('div', 'msm-eyebrow', 'Spaces'),
      el('div', 'msm-title', 'Manage Spaces'),
      el('div', 'msm-subtitle',
          'Switch between spaces, open the create flow, or edit the current ' +
              'space without leaving this surface.'));

  const actions = el('div', 'msm-actions');
  const createBtn = el('button', 'msm-btn primary', 'Create Space');
  createBtn.addEventListener('click', openCreateSpace);
  const configBtn = el('button', 'msm-btn', 'Configure Selected');
  configBtn.disabled = !selectedSpaceId;
  configBtn.addEventListener('click', () => {
    if (selectedSpaceId) {
      openConfigForSpace(selectedSpaceId);
    }
  });
  const closeBtn = el('button', 'msm-btn', 'Close');
  closeBtn.addEventListener('click', closeDialog);
  actions.append(createBtn, configBtn, closeBtn);
  header.append(titleBlock, actions);

  const list = el('div', 'msm-list');
  if (!spaces.length) {
    list.append(el('div', 'msm-empty', 'No spaces are available yet.'));
  } else {
    for (const space of spaces) {
      list.appendChild(renderCard(space));
    }
  }

  root.append(header, list);
}

function main(): void {
  bindMojo();
  loadSpaces();

  // Re-fetch spaces when the manage surface regains visibility, such as after
  // the create dialog closes or the user switches back to this window.
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible') {
      loadSpaces();
    }
  });
  window.addEventListener('focus', () => {
    loadSpaces();
  });
}

void main();
