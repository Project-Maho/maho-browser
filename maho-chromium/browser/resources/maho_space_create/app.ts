import {
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
} from './maho_space_create.mojom-webui.js';
import type {ProfileInfo} from './maho_space_create.mojom-webui.js';

type ThemeMode = 'solid'|'zen'|'gradient';
type ColorHarmony = 'complementary';

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
const DEFAULT_MODE: ThemeMode = 'zen';
const DEFAULT_ICON = '⊞';

let handler: PageHandlerRemote;
let profiles: ProfileInfo[] = [];
let selectedColorIndex = DEFAULT_COLOR_INDEX;
let mode: ThemeMode = DEFAULT_MODE;
let grain = 0;
let brightness = 90;
let spaceName = '';

interface DomRefs {
  root: HTMLElement;
  nameInput: HTMLInputElement;
  profileSelect: HTMLSelectElement;
  createBtn: HTMLButtonElement;
  illustration: HTMLElement;
  illustrationOrb: HTMLElement;
  previewCanvas: HTMLElement;
  previewLabel: HTMLElement;
  previewNoise: HTMLElement;
  gradientOverlay: HTMLElement;
  themeDot: HTMLElement;
  themeDesc: HTMLElement;
  palette: HTMLElement;
  grainSlider: HTMLInputElement;
  brightnessSlider: HTMLInputElement;
  modeTabs: HTMLElement;
}

let dom: DomRefs;

function hsbToHsl(
    h: number, s: number, b: number):
    {h: number; s: number; l: number} {
  const l = b * (1 - s / 2);
  const sl = l === 0 || l === 1 ? 0 : (b - l) / Math.min(l, 1 - l);
  return {
    h: Math.round(h * 360),
    s: Math.round(sl * 100),
    l: Math.round(l * 100),
  };
}

function hsbToCss(h: number, s: number, b: number): string {
  const hsl = hsbToHsl(h, s, b);
  return `hsl(${hsl.h}, ${hsl.s}%, ${hsl.l}%)`;
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

function normalizeHue(hue: number): number {
  return hue - Math.floor(hue);
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

function buildLeftPanel(): HTMLElement {
  const panel = el('div', 'sc-left');

  const illust = el('div', 'sc-illustration');
  const orb = el('div', 'sc-illustration-orb');
  orb.textContent = DEFAULT_ICON;
  illust.appendChild(orb);
  dom.illustration = illust;
  dom.illustrationOrb = orb;

  const eyebrow = el('div', 'sc-eyebrow', 'New Space');
  const title = el('div', 'sc-title', 'Create a Space');
  const subtitle = el('div', 'sc-subtitle',
      'Separate your tabs with calm, focused themes built for ' +
      'work, life, and everything in between.');

  const nameRow = el('div', 'sc-card-row');
  const nameLabel = el('div', 'sc-card-row-label', 'Space name');
  const nameInput = el('input', 'sc-name-input');
  nameInput.type = 'text';
  nameInput.placeholder = 'Enter a name';
  nameInput.autofocus = true;
  nameInput.addEventListener('input', () => {
    spaceName = nameInput.value.trim();
    dom.createBtn.disabled = !spaceName;
    dom.previewLabel.textContent = spaceName || 'Space';
  });
  dom.nameInput = nameInput;
  nameRow.append(nameLabel, nameInput);

  const profileRow = el('div', 'sc-card-row');
  const profileIconRow = el('div', 'sc-icon-row');
  const profileIcon = el('div', 'sc-icon-row-icon', '👤');
  const profileText = el('div', 'sc-icon-row-text');
  const profileTitle = el('div', 'sc-icon-row-title', 'Profile');
  const profileDesc = el('div', 'sc-icon-row-desc',
      'Choose the browser identity for this space');
  profileText.append(profileTitle, profileDesc);
  const pillSelect = el('div', 'sc-pill-select');
  const profileSelect = el('select');
  const defaultOpt = el('option', undefined, 'Default');
  defaultOpt.value = '';
  profileSelect.appendChild(defaultOpt);
  dom.profileSelect = profileSelect;
  const chevron = el('span', 'sc-pill-chevron', '▾');
  pillSelect.append(profileSelect, chevron);
  profileIconRow.append(profileIcon, profileText, pillSelect);
  profileRow.appendChild(profileIconRow);

  const themeRow = el('div', 'sc-card-row');
  const themeIconRow = el('div', 'sc-icon-row');
  const themeIcon = el('div', 'sc-icon-row-icon', '✦');
  const themeText = el('div', 'sc-icon-row-text');
  const themeTitle = el('div', 'sc-icon-row-title', 'Theme studio');
  const themeDesc = el('div', 'sc-icon-row-desc', 'Solid · ready to customize');
  dom.themeDesc = themeDesc;
  themeText.append(themeTitle, themeDesc);
  const themeDot = el('div', 'sc-theme-dot');
  dom.themeDot = themeDot;
  themeIconRow.append(themeIcon, themeText, themeDot);
  themeRow.appendChild(themeIconRow);

  const spacer = el('div', 'sc-spacer');

  const createBtn = el('button', 'sc-create-btn', 'Create Space');
  createBtn.disabled = true;
  createBtn.addEventListener('click', handleCreate);
  dom.createBtn = createBtn;

  const cancelBtn = el('button', 'sc-cancel-btn', 'Cancel');
  cancelBtn.addEventListener('click', handleCancel);

  panel.append(
      illust, eyebrow, title, subtitle,
      nameRow, profileRow, themeRow,
      spacer, createBtn, cancelBtn);

  return panel;
}

function buildRightPanel(): HTMLElement {
  const panel = el('div', 'sc-right');

  const canvas = el('div', 'sc-preview-canvas');
  const gradientOverlay = el('div', 'sc-gradient-overlay');
  const noise = el('div', 'sc-preview-noise');
  const label = el('div', 'sc-preview-label', 'Space');
  dom.previewCanvas = canvas;
  dom.previewLabel = label;
  dom.previewNoise = noise;
  dom.gradientOverlay = gradientOverlay;
  canvas.append(gradientOverlay, noise, label);

  const controls = el('div', 'sc-preview-controls');

  const palette = el('div', 'sc-color-palette');
  dom.palette = palette;

  const modeTabs = el('div', 'sc-mode-tabs');
  dom.modeTabs = modeTabs;
  const modes: {id: ThemeMode; label: string}[] = [
    {id: 'solid', label: 'Solid'},
    {id: 'zen', label: 'Zen'},
    {id: 'gradient', label: 'Gradient'},
  ];
  for (const m of modes) {
    const tab = el('button', 'sc-mode-tab', m.label);
    tab.dataset['mode'] = m.id;
    if (m.id === mode) {
      tab.classList.add('active');
    }
    tab.addEventListener('click', () => {
      mode = m.id;
      updatePreview();
    });
    modeTabs.appendChild(tab);
  }

  const grainRow = el('div', 'sc-slider-row');
  const grainLabel = el('span', 'sc-slider-label', 'Grain');
  const grainSlider = el('input');
  grainSlider.type = 'range';
  grainSlider.min = '0';
  grainSlider.max = '100';
  grainSlider.value = String(grain);
  grainSlider.addEventListener('input', () => {
    grain = parseInt(grainSlider.value, 10);
    updatePreview();
  });
  dom.grainSlider = grainSlider;
  grainRow.append(grainLabel, grainSlider);

  const brightnessRow = el('div', 'sc-slider-row');
  const brightnessLabel = el('span', 'sc-slider-label', 'Brightness');
  const brightnessSlider = el('input');
  brightnessSlider.type = 'range';
  brightnessSlider.min = '15';
  brightnessSlider.max = '100';
  brightnessSlider.value = String(brightness);
  brightnessSlider.addEventListener('input', () => {
    brightness = parseInt(brightnessSlider.value, 10);
    updatePreview();
  });
  dom.brightnessSlider = brightnessSlider;
  brightnessRow.append(brightnessLabel, brightnessSlider);

  controls.append(palette, modeTabs, grainRow, brightnessRow);
  panel.append(canvas, controls);

  return panel;
}

function buildPalette(): void {
  dom.palette.textContent = '';
  COLOR_PRESETS.forEach((c, i) => {
    const swatch = el('div', 'sc-color-swatch');
    if (i === selectedColorIndex) {
      swatch.classList.add('active');
    }
    swatch.style.background = hsbToCss(c.h, c.s, c.b);
    swatch.addEventListener('click', () => {
      selectedColorIndex = i;
      brightness = Math.round(c.b * 100);
      dom.brightnessSlider.value = String(brightness);
      updatePreview();
    });
    dom.palette.appendChild(swatch);
  });
}

function updatePreview(): void {
  const c = COLOR_PRESETS[selectedColorIndex];
  if (!c) {
    return;
  }

  const bAdj = brightness / 100;
  const color = hsbToCss(c.h, c.s, bAdj);
  const colorDark = hsbToCss(c.h, c.s, bAdj * 0.6);
  const colorMid = hsbToCss(c.h, c.s * 0.7, bAdj * 0.8);

  if (mode === 'solid') {
    dom.previewCanvas.style.background = color;
    dom.gradientOverlay.style.opacity = '0';
  } else if (mode === 'zen') {
    dom.previewCanvas.style.background =
        `radial-gradient(ellipse at 30% 40%, ${color} 0%, ${colorDark} 100%)`;
    dom.gradientOverlay.style.opacity = '0';
  } else {
    dom.previewCanvas.style.background = color;
    const comp = hsbToCss(
        normalizeHue(c.h + 0.5), c.s * 0.8, bAdj * 0.85);
    dom.gradientOverlay.style.background =
        `linear-gradient(135deg, ${color} 0%, ${comp} 50%, ${colorMid} 100%)`;
    dom.gradientOverlay.style.opacity = '1';
  }

  dom.previewNoise.style.opacity = String(grain / 200);

  dom.themeDot.style.background = color;
  dom.illustration.style.background =
      hsbToCss(c.h, c.s * 0.3, bAdj * 0.85) + '28';
  dom.illustrationOrb.style.background =
      hsbToCss(c.h, c.s * 0.4, bAdj * 0.85) + '38';

  const modeLabel =
      mode === 'solid' ? 'Solid' : mode === 'zen' ? 'Zen' : 'Gradient';
  if (mode === 'gradient') {
    dom.themeDesc.textContent = 'Gradient · 2 tones · Complementary';
  } else {
    const hex = cssToHex(color);
    dom.themeDesc.textContent = `${modeLabel} · ${hex}`;
  }

  dom.palette.querySelectorAll('.sc-color-swatch').forEach((s, i) => {
    s.classList.toggle('active', i === selectedColorIndex);
  });

  dom.modeTabs.querySelectorAll('.sc-mode-tab').forEach(
      (t: Element) => {
        const btn = t as HTMLButtonElement;
        btn.classList.toggle('active', btn.dataset['mode'] === mode);
      });
}

function buildThemeJson(): string {
  const c = COLOR_PRESETS[selectedColorIndex];
  if (!c) {
    return '';
  }

  const bAdj = brightness / 100;
  const spaceColor: SpaceColor = {
    hue: c.h,
    saturation: c.s,
    brightness: bAdj,
    grain: grain / 100,
  };

  let theme: SpaceThemeData;
  if (mode === 'gradient') {
    const primary: ThemeColor = {
      hue: normalizeHue(c.h),
      saturation: c.s,
      brightness: bAdj,
      isCustom: false,
      isPrimary: true,
    };
    const secondary: ThemeColor = {
      hue: normalizeHue(c.h + 0.5),
      saturation: c.s,
      brightness: bAdj,
      isCustom: false,
      isPrimary: false,
    };
    theme = {
      type: 'gradient',
      gradientColors: [primary, secondary],
      harmony: 'complementary',
      opacity: 0.8,
      texture: grain / 100,
    };
  } else {
    theme = {type: 'solid', color: spaceColor};
  }
  return JSON.stringify(theme);
}

function getColorHex(): string {
  const c = COLOR_PRESETS[selectedColorIndex];
  if (!c) {
    return '#5F8BFF';
  }
  return cssToHex(hsbToCss(c.h, c.s, brightness / 100));
}

async function handleCreate(): Promise<void> {
  if (!spaceName) {
    dom.nameInput.focus();
    return;
  }

  dom.createBtn.disabled = true;
  dom.createBtn.textContent = 'Creating…';

  const profileId = dom.profileSelect.value || null;
  const themeJson = buildThemeJson();

  const {success} = await handler.createSpace(
      spaceName, getColorHex(), DEFAULT_ICON, profileId, themeJson);

  if (success) {
    handler.closeDialog();
  } else {
    dom.createBtn.disabled = false;
    dom.createBtn.textContent = 'Create Space';
  }
}

function handleCancel(): void {
  handler.closeDialog();
}

async function loadProfiles(): Promise<void> {
  const {profiles: loaded} = await handler.getProfiles();
  profiles = loaded;

  dom.profileSelect.textContent = '';
  const defaultOpt = el('option', undefined, 'Default');
  defaultOpt.value = '';
  dom.profileSelect.appendChild(defaultOpt);

  for (const p of profiles) {
    const opt = el('option', undefined, p.name);
    opt.value = p.id;
    if (p.isDefault) {
      opt.selected = true;
    }
    dom.profileSelect.appendChild(opt);
  }
}

function initialize(): void {
  const callbackRouter = new PageCallbackRouter();
  handler = new PageHandlerRemote();

  const factory = PageHandlerFactory.getRemote();
  factory.createPageHandler(
      callbackRouter.$.bindNewPipeAndPassRemote(),
      handler.$.bindNewPipeAndPassReceiver());

  const root = document.querySelector<HTMLElement>('maho-space-create');
  if (!root) {
    return;
  }

  dom = {} as DomRefs;
  dom.root = root;

  root.appendChild(buildLeftPanel());
  root.appendChild(buildRightPanel());

  buildPalette();
  updatePreview();

  void loadProfiles();

  requestAnimationFrame(() => {
    dom.nameInput.focus();
  });
}

document.addEventListener('DOMContentLoaded', initialize);
