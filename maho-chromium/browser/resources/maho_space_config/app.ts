import {
  InitialFocus,
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
} from './maho_space_config.mojom-webui.js';
import type {ProfileInfo, SpaceInfo} from './maho_space_config.mojom-webui.js';

interface ColorPreset {
  title: string;
  h: number;
  s: number;
  b: number;
}

const COLOR_PRESETS: ColorPreset[] = [
  {title: 'Cream', h: 0.10, s: 0.15, b: 0.95},
  {title: 'Pink', h: 0.92, s: 0.80, b: 0.90},
  {title: 'Purple', h: 0.75, s: 0.80, b: 0.90},
  {title: 'Red', h: 0.00, s: 0.80, b: 0.90},
  {title: 'Orange', h: 0.08, s: 0.80, b: 0.90},
  {title: 'Yellow', h: 0.15, s: 0.80, b: 0.90},
  {title: 'Green', h: 0.35, s: 0.80, b: 0.90},
  {title: 'Blue', h: 0.58, s: 0.80, b: 0.90},
  {title: 'Indigo', h: 0.62, s: 0.30, b: 0.55},
];

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

function hsbToCss(h: number, s: number, b: number): string {
  const l = b * (1 - s / 2);
  const sl = l === 0 || l === 1 ? 0 : (b - l) / Math.min(l, 1 - l);
  const hDeg = Math.round(h * 360);
  const sPct = Math.round(sl * 100);
  const lPct = Math.round(l * 100);
  return `hsl(${hDeg}, ${sPct}%, ${lPct}%)`;
}

function cssToHex(cssColor: string): string {
  const d = document.createElement('div');
  d.style.color = cssColor;
  document.body.appendChild(d);
  const rgb = getComputedStyle(d).color;
  document.body.removeChild(d);
  const m = rgb.match(/(\d+)/g);
  if (!m) {
    return '#000000';
  }
  return '#' +
      m.slice(0, 3)
          .map((n: string) => parseInt(n, 10).toString(16).padStart(2, '0'))
          .join('')
          .toUpperCase();
}

function hexToClosestPresetIndex(hex: string): number {
  const d = document.createElement('div');
  d.style.color = hex;
  document.body.appendChild(d);
  const rgb = getComputedStyle(d).color;
  document.body.removeChild(d);
  const m = rgb.match(/(\d+)/g);
  if (!m) {
    return DEFAULT_COLOR_INDEX;
  }
  const r = parseInt(m[0], 10) / 255;
  const g = parseInt(m[1], 10) / 255;
  const bv = parseInt(m[2], 10) / 255;

  let bestIndex = 0;
  let bestDist = Infinity;
  for (let i = 0; i < COLOR_PRESETS.length; i++) {
    const preset = COLOR_PRESETS[i];
    const css = hsbToCss(preset.h, preset.s, preset.b);
    const d2 = document.createElement('div');
    d2.style.color = css;
    document.body.appendChild(d2);
    const rgb2 = getComputedStyle(d2).color;
    document.body.removeChild(d2);
    const m2 = rgb2.match(/(\d+)/g);
    if (!m2) {
      continue;
    }
    const pr = parseInt(m2[0], 10) / 255;
    const pg = parseInt(m2[1], 10) / 255;
    const pb = parseInt(m2[2], 10) / 255;
    const dist = (r - pr) ** 2 + (g - pg) ** 2 + (bv - pb) ** 2;
    if (dist < bestDist) {
      bestDist = dist;
      bestIndex = i;
    }
  }
  return bestIndex;
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

function getColorHex(): string {
  const c = COLOR_PRESETS[selectedColorIndex];
  if (!c) {
    return '#5F8BFF';
  }
  return cssToHex(hsbToCss(c.h, c.s, c.b));
}

function buildThemeJson(): string {
  const c = COLOR_PRESETS[selectedColorIndex];
  if (!c) {
    return '';
  }
  return JSON.stringify({
    type: 'solid',
    color: {
      hue: c.h,
      saturation: c.s,
      brightness: c.b,
      grain: 0,
    },
  });
}

function updateColorUI(): void {
  const c = COLOR_PRESETS[selectedColorIndex];
  if (!c) {
    return;
  }
  const color = hsbToCss(c.h, c.s, c.b);
  dom.colorDot.style.background = color;
  dom.headerIcon.style.background = hsbToCss(c.h, c.s * 0.4, c.b * 0.85) + '38';

  dom.palette.querySelectorAll('.scfg-color-swatch').forEach((s, i) => {
    s.classList.toggle('active', i === selectedColorIndex);
  });
}

function buildPalette(): void {
  dom.palette.textContent = '';
  COLOR_PRESETS.forEach((c, i) => {
    const swatch = el('div', 'scfg-color-swatch');
    if (i === selectedColorIndex) {
      swatch.classList.add('active');
    }
    swatch.style.background = hsbToCss(c.h, c.s, c.b);
    swatch.addEventListener('click', () => {
      selectedColorIndex = i;
      updateColorUI();
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
    promises.push(handler.updateColor(getColorHex(), buildThemeJson()));
  }
  if (profileChanged) {
    promises.push(
        handler.updateProfile(dom.profileSelect.value || null));
  }

  if (promises.length > 0) {
    await Promise.all(promises);
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

  if (info.color) {
    selectedColorIndex = hexToClosestPresetIndex(info.color);
  }

  buildUI();
  buildPalette();
  updateColorUI();

  void loadProfiles();
  applyInitialFocus(focus);
}

document.addEventListener('DOMContentLoaded', () => {
  void initialize();
});
