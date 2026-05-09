import {
  InitialFocus,
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
} from './maho_space_config.mojom-webui.js';
import type {SpaceInfo} from './maho_space_config.mojom-webui.js';
import {
  SPACE_THEME_COLOR_PRESETS,
  cssToHex,
  computeHarmonyColors,
  hsbToCss,
  hsbToCssAlpha,
} from '//resources/maho_common/maho_theme_utils.js';
import type {ColorHarmony, SpaceColor, SpaceThemeData} from '//resources/maho_common/maho_theme_utils.js';

const COLOR_PRESETS = SPACE_THEME_COLOR_PRESETS;

type ThemeMode = 'solid'|'zen'|'gradient';

const DEFAULT_COLOR_INDEX = 7;

let handler: PageHandlerRemote;
let currentSpace: SpaceInfo | null = null;
let selectedColorIndex = DEFAULT_COLOR_INDEX;
let mode: ThemeMode = 'solid';
let grain = 0;
let brightnessVal = 90;
let harmony: ColorHarmony = 'single_analogous';
let opacity = 1;
let initialThemeJson = '';
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
  modeTabs: HTMLElement;
  grainSlider: HTMLInputElement;
  brightnessSlider: HTMLInputElement;
  harmonyRow: HTMLElement;
  opacitySlider: HTMLInputElement;
  gradientGroup: HTMLElement;
}

let dom: DomRefs;

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
  const [rStr, gStr, bStr] = m;
  if (!rStr || !gStr || !bStr) {
    return DEFAULT_COLOR_INDEX;
  }
  const r = parseInt(rStr, 10) / 255;
  const g = parseInt(gStr, 10) / 255;
  const bv = parseInt(bStr, 10) / 255;

  let bestIndex = 0;
  let bestDist = Infinity;
  for (let i = 0; i < COLOR_PRESETS.length; i++) {
    const preset = COLOR_PRESETS[i]!;
    const css = hsbToCss(preset.hue, preset.saturation, preset.brightness);
    const d2 = document.createElement('div');
    d2.style.color = css;
    document.body.appendChild(d2);
    const rgb2 = getComputedStyle(d2).color;
    document.body.removeChild(d2);
    const m2 = rgb2.match(/(\d+)/g);
    if (!m2) {
      continue;
    }
    const [prStr, pgStr, pbStr] = m2;
    if (!prStr || !pgStr || !pbStr) {
      continue;
    }
    const pr = parseInt(prStr, 10) / 255;
    const pg = parseInt(pgStr, 10) / 255;
    const pb = parseInt(pbStr, 10) / 255;
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
  return cssToHex(hsbToCss(c.hue, c.saturation, brightnessVal / 100));
}

function buildThemeJson(): string {
  const c = COLOR_PRESETS[selectedColorIndex];
  if (!c) {
    return '';
  }
  const bAdj = brightnessVal / 100;
  const spaceColor: SpaceColor = {
    hue: c.hue,
    saturation: c.saturation,
    brightness: bAdj,
    grain: grain / 100,
  };
  let theme: SpaceThemeData;
  if (mode === 'gradient') {
    theme = {
      type: 'gradient',
      gradientColors: computeHarmonyColors(spaceColor, harmony),
      harmony,
      opacity,
      texture: grain / 100,
    };
  } else {
    theme = {type: 'solid', color: spaceColor};
  }
  return JSON.stringify(theme);
}

function updateColorUI(): void {
  const c = COLOR_PRESETS[selectedColorIndex];
  if (!c) {
    return;
  }
  const color = hsbToCss(c.hue, c.saturation, brightnessVal / 100);
  dom.colorDot.style.background = color;
  dom.headerIcon.style.background =
      hsbToCssAlpha(c.hue, c.saturation * 0.4, (brightnessVal / 100) * 0.85, 0.22);

  dom.palette.querySelectorAll('.scfg-color-swatch').forEach((s, i) => {
    s.classList.toggle('active', i === selectedColorIndex);
  });
  dom.modeTabs.querySelectorAll('.scfg-mode-tab').forEach((t) => {
    t.classList.toggle('active', t.getAttribute('data-mode') === mode);
  });
  dom.gradientGroup.classList.toggle('is-hidden', mode !== 'gradient');
  dom.harmonyRow.querySelectorAll('.scfg-harmony-btn').forEach((t) => {
    t.classList.toggle('active', t.getAttribute('data-harmony') === harmony);
  });
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

  const modeRow = el('div', 'scfg-card-row');
  const modeInner = el('div', 'scfg-icon-row');
  const modeIcon = el('div', 'scfg-icon-row-icon', '◌');
  const modeText = el('div', 'scfg-icon-row-text');
  const modeTitle = el('div', 'scfg-icon-row-title', 'Mode');
  const modeDesc = el('div', 'scfg-icon-row-desc', 'Solid, Zen, or gradient');
  modeText.append(modeTitle, modeDesc);
  const modeTabs = el('div', 'scfg-mode-tabs');
  dom.modeTabs = modeTabs;
  const modes: {id: ThemeMode; label: string}[] = [
    {id: 'solid', label: 'Solid'},
    {id: 'zen', label: 'Zen'},
    {id: 'gradient', label: 'Gradient'},
  ];
  for (const m of modes) {
    const tab = el('button', 'scfg-mode-tab', m.label);
    tab.type = 'button';
    tab.dataset['mode'] = m.id;
    if (m.id === mode) {
      tab.classList.add('active');
    }
    tab.addEventListener('click', () => {
      mode = m.id;
      updateColorUI();
    });
    modeTabs.appendChild(tab);
  }
  modeInner.append(modeIcon, modeText, modeTabs);
  modeRow.appendChild(modeInner);

  const grainRow = el('div', 'scfg-card-row');
  const grainInner = el('div', 'scfg-icon-row');
  const grainIcon = el('div', 'scfg-icon-row-icon', '◌');
  const grainText = el('div', 'scfg-icon-row-text');
  const grainTitle = el('div', 'scfg-icon-row-title', 'Grain');
  const grainDesc = el('div', 'scfg-icon-row-desc', 'Texture for solid and gradient themes');
  grainText.append(grainTitle, grainDesc);
  const grainSlider = el('input', 'scfg-slider-input');
  grainSlider.type = 'range';
  grainSlider.min = '0';
  grainSlider.max = '100';
  grainSlider.value = String(grain);
  grainSlider.addEventListener('input', () => {
    grain = parseInt(grainSlider.value, 10);
  });
  dom.grainSlider = grainSlider;
  grainInner.append(grainIcon, grainText, grainSlider);
  grainRow.appendChild(grainInner);

  const brightnessRow = el('div', 'scfg-card-row');
  const brightnessInner = el('div', 'scfg-icon-row');
  const brightnessIcon = el('div', 'scfg-icon-row-icon', '☼');
  const brightnessText = el('div', 'scfg-icon-row-text');
  const brightnessTitle = el('div', 'scfg-icon-row-title', 'Brightness');
  const brightnessDesc = el('div', 'scfg-icon-row-desc', 'Adjust the selected color tone');
  brightnessText.append(brightnessTitle, brightnessDesc);
  const brightnessSlider = el('input', 'scfg-slider-input');
  brightnessSlider.type = 'range';
  brightnessSlider.min = '15';
  brightnessSlider.max = '100';
  brightnessSlider.value = String(brightnessVal);
  brightnessSlider.addEventListener('input', () => {
    brightnessVal = parseInt(brightnessSlider.value, 10);
    updateColorUI();
  });
  dom.brightnessSlider = brightnessSlider;
  brightnessInner.append(brightnessIcon, brightnessText, brightnessSlider);
  brightnessRow.appendChild(brightnessInner);

  const gradientGroup = el('div', 'scfg-gradient-only is-hidden');
  dom.gradientGroup = gradientGroup;
  const harmonyRow = el('div', 'scfg-harmony-row');
  dom.harmonyRow = harmonyRow;
  const harmonyButtons: {id: ColorHarmony; label: string}[] = [
    {id: 'complementary', label: 'Comp'},
    {id: 'single_analogous', label: 'Analog'},
    {id: 'split_complementary', label: 'Split'},
    {id: 'triadic', label: 'Triad'},
    {id: 'custom', label: 'Custom'},
  ];
  for (const h of harmonyButtons) {
    const button = el('button', 'scfg-harmony-btn', h.label);
    button.type = 'button';
    button.dataset['harmony'] = h.id;
    if (h.id === harmony) {
      button.classList.add('active');
    }
    button.addEventListener('click', () => {
      harmony = h.id;
      updateColorUI();
    });
    harmonyRow.appendChild(button);
  }

  const opacityRow = el('div', 'scfg-card-row');
  const opacityInner = el('div', 'scfg-icon-row');
  const opacityIcon = el('div', 'scfg-icon-row-icon', '◍');
  const opacityText = el('div', 'scfg-icon-row-text');
  const opacityTitle = el('div', 'scfg-icon-row-title', 'Opacity');
  const opacityDesc = el('div', 'scfg-icon-row-desc', 'Blend strength for gradient themes');
  opacityText.append(opacityTitle, opacityDesc);
  const opacitySlider = el('input', 'scfg-slider-input');
  opacitySlider.type = 'range';
  opacitySlider.min = '0';
  opacitySlider.max = '1';
  opacitySlider.step = '0.01';
  opacitySlider.value = String(opacity);
  opacitySlider.addEventListener('input', () => {
    opacity = parseFloat(opacitySlider.value);
  });
  dom.opacitySlider = opacitySlider;
  opacityInner.append(opacityIcon, opacityText, opacitySlider);
  opacityRow.appendChild(opacityInner);

  gradientGroup.append(harmonyRow, opacityRow);

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

  root.append(
      header, nameRow, iconRow, colorRow, modeRow, grainRow, brightnessRow,
      gradientGroup, profileRow, spacer, actions);
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
  const currentThemeJson = buildThemeJson();
  const colorChanged = getColorHex() !== currentSpace.color ||
      currentThemeJson !== initialThemeJson;
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

  const themeSource =
      (info as SpaceInfo & {themeJson?: string; theme?: string}).themeJson ||
      (info as SpaceInfo & {themeJson?: string; theme?: string}).theme || '';
  if (themeSource) {
    try {
      const theme = JSON.parse(themeSource) as SpaceThemeData;
      if (theme.type === 'gradient') {
        mode = 'gradient';
        harmony = theme.harmony;
        opacity = theme.opacity;
        grain = Math.round(theme.texture * 100);
        const primary =
            theme.gradientColors.find((color: {isPrimary: boolean}) => color.isPrimary) ||
            theme.gradientColors[0];
        if (primary) {
          brightnessVal = Math.round(primary.brightness * 100);
          selectedColorIndex = hexToClosestPresetIndex(
              cssToHex(hsbToCss(primary.hue, primary.saturation, primary.brightness)));
        }
        initialThemeJson = themeSource;
      } else {
        mode = 'solid';
        brightnessVal = Math.round(theme.color.brightness * 100);
        grain = Math.round(theme.color.grain * 100);
        selectedColorIndex = hexToClosestPresetIndex(
            cssToHex(hsbToCss(
                theme.color.hue, theme.color.saturation, theme.color.brightness)));
        initialThemeJson = themeSource;
      }
    } catch {
      if (info.color) {
        selectedColorIndex = hexToClosestPresetIndex(info.color);
        brightnessVal = Math.round(COLOR_PRESETS[selectedColorIndex]!.brightness * 100);
        initialThemeJson = JSON.stringify({
          type: 'solid',
          color: {
            hue: COLOR_PRESETS[selectedColorIndex]!.hue,
            saturation: COLOR_PRESETS[selectedColorIndex]!.saturation,
            brightness: brightnessVal / 100,
            grain: 0,
          },
        });
      }
    }
  } else if (info.color) {
    selectedColorIndex = hexToClosestPresetIndex(info.color);
    brightnessVal = Math.round(COLOR_PRESETS[selectedColorIndex]!.brightness * 100);
    initialThemeJson = JSON.stringify({
      type: 'solid',
      color: {
        hue: COLOR_PRESETS[selectedColorIndex]!.hue,
        saturation: COLOR_PRESETS[selectedColorIndex]!.saturation,
        brightness: brightnessVal / 100,
        grain: 0,
      },
    });
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
