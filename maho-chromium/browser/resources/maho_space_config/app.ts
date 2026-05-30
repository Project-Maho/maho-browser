import {
  InitialFocus,
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
} from './maho_space_config.mojom-webui.js';
import type {SpaceInfo} from './maho_space_config.mojom-webui.js';
import {
  SPACE_THEME_COLOR_PRESETS,
  hsbToCss,
  cssToHex,
} from '//resources/maho_common/maho_theme_utils.js';

const COLOR_PRESETS = SPACE_THEME_COLOR_PRESETS;

const DEFAULT_COLOR_INDEX = 7;

let handler: PageHandlerRemote;
let currentSpace: SpaceInfo | null = null;
let selectedColorIndex = DEFAULT_COLOR_INDEX;
let spaceName = '';
let spaceIcon = '⊞';
let saving = false;

interface DomRefs {
  root: HTMLElement;
  nameInput: HTMLInputElement;
  iconInput: HTMLInputElement;
  profileSelect: HTMLSelectElement;
  saveBtn: HTMLButtonElement;
  headerIcon: HTMLElement;
  colorDot: HTMLElement;
  palette: HTMLElement;
}

let dom: DomRefs;

function getColorHex(): string {
  const c = COLOR_PRESETS[selectedColorIndex];
  if (!c) {
    return '#5F8BFF';
  }
  return cssToHex(hsbToCss(c.hue, c.saturation, c.brightness));
}

function el<K extends keyof HTMLElementTagNameMap>(
    tag: K, className?: string,
    text?: string): HTMLElementTagNameMap[K] {
  const e = document.createElement(tag);
  if (className) {
    e.className = className;
  }
  if (text !== undefined) {
    e.textContent = text;
  }
  return e;
}

function updateSwatchSelection(): void {
  dom.palette.querySelectorAll('.scfg-color-swatch').forEach((s, i) => {
    s.classList.toggle('active', i === selectedColorIndex);
  });
  const c = COLOR_PRESETS[selectedColorIndex];
  if (c) {
    dom.colorDot.style.background = hsbToCss(c.hue, c.saturation, c.brightness);
  }
}

function buildPalette(): void {
  dom.palette.textContent = '';
  COLOR_PRESETS.forEach((c, i) => {
    const swatch = el('div', 'scfg-color-swatch');
    if (i === selectedColorIndex) {
      swatch.classList.add('active');
    }
    swatch.style.background = hsbToCss(c.hue, c.saturation, c.brightness);
    swatch.addEventListener('click', () => {
      selectedColorIndex = i;
      updateSwatchSelection();
    });
    dom.palette.appendChild(swatch);
  });
}

function buildUI(): void {
  const root = dom.root;

  const header = el('div', 'scfg-header');
  const headerIcon = el('div', 'scfg-header-icon');
  headerIcon.textContent = spaceIcon;
  dom.headerIcon = headerIcon;
  const headerTitle = el('div', 'scfg-header-title', 'Space Settings');
  header.append(headerIcon, headerTitle);

  const nameRow = el('div', 'scfg-card-row');
  const nameLabel = el('div', 'scfg-card-row-label', 'Space name');
  const nameInput = el('input', 'scfg-name-input');
  nameInput.type = 'text';
  nameInput.placeholder = 'Enter a name';
  nameInput.value = spaceName;
  nameInput.addEventListener('input', () => {
    spaceName = nameInput.value.trim();
  });
  dom.nameInput = nameInput;
  nameRow.append(nameLabel, nameInput);

  const iconRow = el('div', 'scfg-card-row');
  const iconInner = el('div', 'scfg-icon-row');
  const iconIcon = el('div', 'scfg-icon-row-icon', '🎨');
  const iconText = el('div', 'scfg-icon-row-text');
  const iconTitle = el('div', 'scfg-icon-row-title', 'Icon');
  const iconDesc = el('div', 'scfg-icon-row-desc', 'Emoji or symbol for this space');
  iconText.append(iconTitle, iconDesc);
  const iconInput = el('input', 'scfg-icon-input');
  iconInput.type = 'text';
  iconInput.maxLength = 4;
  iconInput.value = spaceIcon;
  iconInput.addEventListener('input', () => {
    spaceIcon = iconInput.value;
    dom.headerIcon.textContent = spaceIcon || '⊞';
  });
  dom.iconInput = iconInput;
  iconInner.append(iconIcon, iconText, iconInput);
  iconRow.appendChild(iconInner);

  const colorRow = el('div', 'scfg-card-row');
  const colorInner = el('div', 'scfg-icon-row');
  const colorIcon = el('div', 'scfg-icon-row-icon', '✦');
  const colorText = el('div', 'scfg-icon-row-text');
  const colorTitle = el('div', 'scfg-icon-row-title', 'Color');
  const colorDesc = el('div', 'scfg-icon-row-desc', 'Theme color for tabs and sidebar');
  colorText.append(colorTitle, colorDesc);
  const colorDot = el('div', 'scfg-color-dot');
  dom.colorDot = colorDot;
  colorInner.append(colorIcon, colorText, colorDot);

  const palette = el('div', 'scfg-color-palette');
  dom.palette = palette;

  colorRow.append(colorInner, palette);

  const profileRow = el('div', 'scfg-card-row');
  const profileInner = el('div', 'scfg-icon-row');
  const profileIcon = el('div', 'scfg-icon-row-icon', '👤');
  const profileText = el('div', 'scfg-icon-row-text');
  const profileTitle = el('div', 'scfg-icon-row-title', 'Profile');
  const profileDesc = el('div', 'scfg-icon-row-desc', 'Browser identity for this space');
  profileText.append(profileTitle, profileDesc);
  const pillSelect = el('div', 'scfg-pill-select');
  const profileSelect = el('select');
  const defaultOpt = el('option', undefined, 'Default');
  defaultOpt.value = '';
  profileSelect.appendChild(defaultOpt);
  dom.profileSelect = profileSelect;
  const chevron = el('span', 'scfg-pill-chevron', '▾');
  pillSelect.append(profileSelect, chevron);
  profileInner.append(profileIcon, profileText, pillSelect);
  profileRow.appendChild(profileInner);

  const spacer = el('div', 'scfg-spacer');

  const actions = el('div', 'scfg-actions');
  const cancelBtn = el('button', 'scfg-cancel-btn', 'Cancel');
  cancelBtn.addEventListener('click', handleCancel);
  const saveBtn = el('button', 'scfg-save-btn', 'Save');
  saveBtn.addEventListener('click', handleSave);
  dom.saveBtn = saveBtn;
  actions.append(cancelBtn, saveBtn);

  root.append(header, nameRow, iconRow, colorRow, profileRow, spacer, actions);
}

async function handleSave(): Promise<void> {
  if (saving || !currentSpace) {
    return;
  }
  saving = true;
  dom.saveBtn.disabled = true;
  dom.saveBtn.textContent = 'Saving…';

  const nameChanged = spaceName !== currentSpace.name;
  const iconChanged = spaceIcon !== currentSpace.icon;
  const colorChanged = getColorHex() !== currentSpace.color;
  const profileChanged =
      (dom.profileSelect.value || null) !== (currentSpace.profileId || null);

  const promises: Promise<{success: boolean}>[] = [];

  if (nameChanged && spaceName) {
    promises.push(handler.updateName(spaceName));
  }
  if (iconChanged) {
    promises.push(handler.updateIcon(spaceIcon));
  }
  if (colorChanged) {
    promises.push(handler.updateColor(getColorHex(), ''));
  }
  if (profileChanged) {
    promises.push(handler.updateProfile(dom.profileSelect.value || null));
  }

  if (promises.length > 0) {
    const results = await Promise.all(promises);
    const allSucceeded = results.every((r) => r.success);
    if (!allSucceeded) {
      saving = false;
      dom.saveBtn.disabled = false;
      dom.saveBtn.textContent = 'Save';
      return;
    }
  }

  handler.closeDialog();
}

function handleCancel(): void {
  handler.closeDialog();
}

async function loadProfiles(): Promise<void> {
  const {profiles} = await handler.getProfiles();

  dom.profileSelect.textContent = '';
  const defaultOpt = el('option', undefined, 'Default');
  defaultOpt.value = '';
  dom.profileSelect.appendChild(defaultOpt);

  for (const p of profiles) {
    const opt = el('option', undefined, p.name);
    opt.value = p.id;
    if (currentSpace && currentSpace.profileId === p.id) {
      opt.selected = true;
    } else if (p.isDefault && (!currentSpace || !currentSpace.profileId)) {
      opt.selected = true;
    }
    dom.profileSelect.appendChild(opt);
  }
}

function applyInitialFocus(focus: InitialFocus): void {
  requestAnimationFrame(() => {
    switch (focus) {
      case InitialFocus.kName:
        dom.nameInput.focus();
        dom.nameInput.select();
        break;
      case InitialFocus.kIcon:
        dom.iconInput.focus();
        dom.iconInput.select();
        break;
      case InitialFocus.kColor:
        dom.palette.querySelector('.scfg-color-swatch.active')
            ?.scrollIntoView({block: 'center'});
        break;
      case InitialFocus.kProfile:
        dom.profileSelect.focus();
        break;
    }
  });
}

async function initialize(): Promise<void> {
  const callbackRouter = new PageCallbackRouter();
  handler = new PageHandlerRemote();

  const factory = PageHandlerFactory.getRemote();
  factory.createPageHandler(
      callbackRouter.$.bindNewPipeAndPassRemote(),
      handler.$.bindNewPipeAndPassReceiver());

  const root = document.querySelector<HTMLElement>('maho-space-config');
  if (!root) {
    return;
  }

  dom = {} as DomRefs;
  dom.root = root;

  const {info, focus} = await handler.getSpaceInfo();
  if (!info) {
    return;
  }

  currentSpace = info;
  spaceName = info.name;
  spaceIcon = info.icon || '⊞';

  buildUI();
  buildPalette();
  updateSwatchSelection();

  void loadProfiles();
  applyInitialFocus(focus);
}

document.addEventListener('DOMContentLoaded', () => {
  void initialize();
});
