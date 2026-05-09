import {
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
} from './maho_space_create.mojom-webui.js';
import type {ProfileInfo} from './maho_space_create.mojom-webui.js';
import {
  SPACE_THEME_COLOR_PRESETS,
  cssToHex,
  hsbToCss,
  hsbToCssAlpha,
} from '//resources/maho_common/maho_theme_utils.js';
import type {SpaceColor, SpaceThemeData} from
    '//resources/maho_common/maho_theme_utils.js';

type ThemeMode = 'solid'|'zen';

const COLOR_PRESETS = SPACE_THEME_COLOR_PRESETS;

const DEFAULT_COLOR_INDEX = 7;
const DEFAULT_MODE: ThemeMode = 'zen';
const DEFAULT_ICON = '⊞';

let handler: PageHandlerRemote;
let profiles: ProfileInfo[] = [];
let selectedColorIndex = DEFAULT_COLOR_INDEX;
let mode: ThemeMode = DEFAULT_MODE;
let selectedVisualMode = 'zen';
let grain = 0;
let brightness = 90;
let spaceName = '';
let themeEditorEngaged = false;

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
  themeDot: HTMLElement;
  themeDesc: HTMLElement;
  palette: HTMLElement;
  grainSlider: HTMLInputElement;
  brightnessSlider: HTMLInputElement;
  modeTabs: HTMLElement;
}

let dom: DomRefs;

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

function setThemeEditorEngaged(engaged = true): void {
  themeEditorEngaged = engaged;
  dom.root.classList.toggle('is-theme-editor-engaged', themeEditorEngaged);
}

function buildLeftPanel(): HTMLElement {
  const panel = el('div', 'sc-left');

  const illust = el('div', 'sc-illustration');
  const orb = el('div', 'sc-illustration-orb');
  orb.textContent = DEFAULT_ICON;
  illust.appendChild(orb);
  dom.illustration = illust;
  dom.illustrationOrb = orb;

  const title = el('div', 'sc-title', 'Create a Space');
  const subtitle = el('div', 'sc-subtitle',
      'Separate your tabs with calm, focused themes built for ' +
      'work, life, and everything in between.');

  const nameRow = el('div', 'sc-card-row');
  const nameLabel = el('div', 'sc-card-row-label', 'Space name');
  const nameInput = el('input', 'sc-name-input');
  nameInput.setAttribute('data-maho-identifier', 'space.create.name');
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
  const themeTitle = el('div', 'sc-icon-row-title', 'Choose a Theme');
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
      illust, title, subtitle,
      nameRow, profileRow, themeRow,
      spacer, createBtn, cancelBtn);

  return panel;
}

function buildRightPanel(): HTMLElement {
  const panel = el('div', 'sc-right');
  const shell = el('div', 'sc-right-shell');

  const topCluster = el('div', 'sc-right-top-cluster');
  const topHalo = el('div', 'sc-right-top-halo');
  const modeTabs = el('div', 'sc-mode-tabs sc-right-top-chips');
  dom.modeTabs = modeTabs;
  const modes: Array<{
    id: ThemeMode;
    visualId: string;
    icon: string;
    hookIndex: number;
  }> = [
    {id: 'solid', visualId: 'solid', icon: '✦', hookIndex: 0},
    {id: 'zen', visualId: 'zen', icon: '◌', hookIndex: 1},
    {id: 'solid', visualId: 'solid-accent', icon: '◒', hookIndex: 2},
  ];
  for (const m of modes) {
    const tab = el('button', 'sc-mode-tab');
    tab.type = 'button';
    tab.dataset['mode'] = m.id;
    tab.dataset['visualMode'] = m.visualId;
    tab.setAttribute('data-maho-identifier', `space.theme.mode.${m.hookIndex}`);
    tab.setAttribute('aria-label', m.id);
    const tabIcon = el('span', 'sc-mode-tab-icon', m.icon);
    tab.append(tabIcon);
    if (m.visualId === selectedVisualMode) {
      tab.classList.add('active');
    }
    tab.addEventListener('click', () => {
      mode = m.id;
      selectedVisualMode = m.visualId;
      setThemeEditorEngaged();
      updatePreview();
    });
    modeTabs.appendChild(tab);
  }
  topHalo.appendChild(modeTabs);
  topCluster.appendChild(topHalo);

  const stage = el('div', 'sc-preview-stage sc-right-canvas-shell');
  const frame = el('div', 'sc-preview-frame');
  const canvas = el('div', 'sc-preview-canvas');
  canvas.setAttribute('data-maho-identifier', 'space.theme.creatorPreview');
  const stageDots = el('div', 'sc-stage-dots');
  const stageGlow = el('div', 'sc-stage-glow');
  const noise = el('div', 'sc-preview-noise');
  const instruction = el(
      'div', 'sc-preview-instruction', 'Choose a theme for your Space');
  const label = el('div', 'sc-preview-label', 'Space');
  const badge = el('div', 'sc-preview-badge', 'New theme');
  dom.previewCanvas = canvas;
  dom.previewLabel = label;
  dom.previewNoise = noise;
  canvas.append(stageGlow, stageDots, noise, instruction, badge, label);
  frame.appendChild(canvas);
  stage.appendChild(frame);

  const controls = el('div', 'sc-preview-controls sc-right-layout');

  const swatchPanel = el('div', 'sc-swatch-panel sc-right-swatch-rail');
  const swatchRow = el('div', 'sc-swatch-row');
  const chevronLeft = el('button', 'sc-swatch-chevron', '‹');
  chevronLeft.type = 'button';
  chevronLeft.setAttribute('data-maho-identifier', 'space.theme.chevron.left');
  chevronLeft.addEventListener('click', () => {
    selectedColorIndex = (selectedColorIndex - 1 + COLOR_PRESETS.length) % COLOR_PRESETS.length;
    brightness = Math.round(COLOR_PRESETS[selectedColorIndex]!.brightness * 100);
    dom.brightnessSlider.value = String(brightness);
    setThemeEditorEngaged();
    updatePreview();
  });
  const palette = el('div', 'sc-color-palette');
  dom.palette = palette;
  const chevronRight = el('button', 'sc-swatch-chevron', '›');
  chevronRight.type = 'button';
  chevronRight.setAttribute('data-maho-identifier', 'space.theme.chevron.right');
  chevronRight.addEventListener('click', () => {
    selectedColorIndex = (selectedColorIndex + 1) % COLOR_PRESETS.length;
    brightness = Math.round(COLOR_PRESETS[selectedColorIndex]!.brightness * 100);
    dom.brightnessSlider.value = String(brightness);
    setThemeEditorEngaged();
    updatePreview();
  });
  swatchRow.append(chevronLeft, palette, chevronRight);
  swatchPanel.appendChild(swatchRow);

  const midControls = el('div', 'sc-right-mid-controls');
  const brightnessTrackShell = el('div', 'sc-luminosity-track-shell');
  const brightnessMinus = el('button', 'sc-luminosity-btn', '−');
  brightnessMinus.type = 'button';
  brightnessMinus.setAttribute('data-maho-identifier', 'space.theme.luminosity.minus');
  brightnessMinus.addEventListener('click', () => {
    brightness = Math.max(15, brightness - 5);
    brightnessSlider.value = String(brightness);
    setThemeEditorEngaged();
    updatePreview();
  });
  const brightnessSlider = el('input', 'sc-luminosity-range');
  brightnessSlider.type = 'range';
  brightnessSlider.min = '15';
  brightnessSlider.max = '100';
  brightnessSlider.value = String(brightness);
  brightnessSlider.addEventListener('input', () => {
    brightness = parseInt(brightnessSlider.value, 10);
    setThemeEditorEngaged();
    updatePreview();
  });
  dom.brightnessSlider = brightnessSlider;
  const brightnessPlus = el('button', 'sc-luminosity-btn', '+');
  brightnessPlus.type = 'button';
  brightnessPlus.setAttribute('data-maho-identifier', 'space.theme.luminosity.plus');
  brightnessPlus.addEventListener('click', () => {
    brightness = Math.min(100, brightness + 5);
    brightnessSlider.value = String(brightness);
    setThemeEditorEngaged();
    updatePreview();
  });
  brightnessTrackShell.appendChild(brightnessSlider);
  midControls.append(brightnessMinus, brightnessTrackShell, brightnessPlus);

  const bottomControls = el('div', 'sc-right-bottom-controls');

  const waveShell = el('div', 'sc-wave-shell');
  waveShell.setAttribute('aria-hidden', 'true');
  const waveSvg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  waveSvg.setAttribute('class', 'sc-wave-svg');
  waveSvg.setAttribute('viewBox', '0 0 260 92');
  waveSvg.setAttribute('preserveAspectRatio', 'none');
  const wavePathBack = document.createElementNS('http://www.w3.org/2000/svg', 'path');
  wavePathBack.setAttribute(
      'd',
      'M4 62 C22 30, 40 30, 58 62 S94 94, 122 52 S164 16, 194 48 S230 84, 256 40');
  wavePathBack.setAttribute('class', 'sc-wave-path sc-wave-path-back');
  const wavePathFront = document.createElementNS('http://www.w3.org/2000/svg', 'path');
  wavePathFront.setAttribute(
      'd',
      'M4 54 C20 82, 50 82, 70 48 S112 12, 140 46 S182 86, 214 56 S238 22, 256 34');
  wavePathFront.setAttribute('class', 'sc-wave-path sc-wave-path-front');
  waveSvg.append(wavePathBack, wavePathFront);
  waveShell.appendChild(waveSvg);

  const dialShell = el('div', 'sc-dial-shell');
  const dialRings = el('div', 'sc-dial-rings');
  dialRings.setAttribute('aria-hidden', 'true');
  dialRings.append(
      el('span', 'sc-dial-ring sc-dial-ring-outer'),
      el('span', 'sc-dial-ring sc-dial-ring-middle'),
      el('span', 'sc-dial-ring sc-dial-ring-inner'));
  const dialCore = el('div', 'sc-dial-core');
  dialCore.setAttribute('aria-hidden', 'true');
  const grainSlider = el('input', 'sc-grain-hook');
  grainSlider.type = 'range';
  grainSlider.min = '0';
  grainSlider.max = '100';
  grainSlider.value = String(grain);
  grainSlider.setAttribute('data-maho-identifier', 'space.theme.grainDial');
  grainSlider.setAttribute('aria-label', 'grain');
  grainSlider.addEventListener('input', () => {
    grain = parseInt(grainSlider.value, 10);
    setThemeEditorEngaged();
    updatePreview();
  });
  dom.grainSlider = grainSlider;
  dialShell.append(dialRings, dialCore, grainSlider);

  bottomControls.append(waveShell, dialShell);

  controls.append(topCluster, stage, midControls, swatchPanel, bottomControls);
  shell.appendChild(controls);
  panel.appendChild(shell);

  return panel;
}

function buildPalette(): void {
  dom.palette.textContent = '';
  COLOR_PRESETS.forEach((c, i) => {
    const swatch = el('button', 'sc-color-swatch');
    swatch.type = 'button';
    if (i === selectedColorIndex) {
      swatch.classList.add('active');
    }
    swatch.style.background = hsbToCss(c.hue, c.saturation, c.brightness);
    swatch.setAttribute('data-maho-identifier', `space.theme.swatch.${i}`);
    swatch.addEventListener('click', () => {
      selectedColorIndex = i;
      brightness = Math.round(c.brightness * 100);
      dom.brightnessSlider.value = String(brightness);
      setThemeEditorEngaged();
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
  const color = hsbToCss(c.hue, c.saturation, bAdj);
  const colorDark = hsbToCss(c.hue, c.saturation, bAdj * 0.6);

  if (mode === 'solid') {
    dom.previewCanvas.style.background = color;
  } else {
    dom.previewCanvas.style.background =
        `radial-gradient(ellipse at 30% 40%, ${color} 0%, ${colorDark} 100%)`;
  }

  dom.previewNoise.style.opacity = String(grain / 200);

  dom.themeDot.style.background = color;
  dom.illustration.style.background =
      hsbToCssAlpha(c.hue, c.saturation * 0.3, bAdj * 0.85, 0.16);
  dom.illustrationOrb.style.background =
      hsbToCssAlpha(c.hue, c.saturation * 0.4, bAdj * 0.85, 0.22);

  const modeLabel = mode === 'solid' ? 'Solid' : 'Zen';
  const hex = cssToHex(color);
  dom.themeDesc.textContent = `${modeLabel} · ${hex}`;

  dom.palette.querySelectorAll('.sc-color-swatch').forEach((s, i) => {
    s.classList.toggle('active', i === selectedColorIndex);
  });

  dom.modeTabs.querySelectorAll('.sc-mode-tab').forEach(
      (t: Element) => {
        const btn = t as HTMLButtonElement;
        btn.classList.toggle(
            'active', btn.dataset['visualMode'] === selectedVisualMode);
      });
}

function buildThemeJson(): string {
  const c = COLOR_PRESETS[selectedColorIndex];
  if (!c) {
    return '';
  }

  const bAdj = brightness / 100;
  const spaceColor: SpaceColor = {
    hue: c.hue,
    saturation: c.saturation,
    brightness: bAdj,
    grain: grain / 100,
  };

  const theme: SpaceThemeData = {type: 'solid', color: spaceColor};
  return JSON.stringify(theme);
}

function getColorHex(): string {
  const c = COLOR_PRESETS[selectedColorIndex];
  if (!c) {
    return '#5F8BFF';
  }
  return cssToHex(hsbToCss(c.hue, c.saturation, brightness / 100));
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
  setThemeEditorEngaged(false);
  updatePreview();

  void loadProfiles();

  requestAnimationFrame(() => {
    dom.nameInput.focus();
  });
}

document.addEventListener('DOMContentLoaded', initialize);
