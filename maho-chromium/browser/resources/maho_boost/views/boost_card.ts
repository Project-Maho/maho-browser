import {MahoBoostStore} from '../store.js';
import {BoostEditorTab, State} from '../types.js';
import {createButton, createEditorLabel, createElement, createMetaPill} from './elements.js';

type BoostCardEntry = State['boosts'][number];

const FONT_PRESETS: Array<{label: string; value: string}> = [
  {label: 'System default', value: ''},
  {label: 'System UI', value: '-apple-system, BlinkMacSystemFont, "Segoe UI", system-ui, sans-serif'},
  {label: 'Sans-serif', value: 'Arial, Helvetica, sans-serif'},
  {label: 'Serif', value: 'Georgia, "Times New Roman", serif'},
  {label: 'Monospace', value: '"SF Mono", Menlo, Monaco, "Courier New", monospace'},
  {label: 'Inter', value: 'Inter, sans-serif'},
  {label: 'Verdana', value: 'Verdana, Geneva, sans-serif'},
];

const BOOST_EDITOR_TABS: Array<{id: BoostEditorTab; label: string}> = [
  {id: 'css', label: 'CSS'},
  {id: 'js', label: 'JS'},
  {id: 'theme', label: 'Theme'},
  {id: 'font', label: 'Font'},
  {id: 'zap', label: 'Zap'},
];

export function buildBoostCard(
    state: State, store: MahoBoostStore, boost: BoostCardEntry): HTMLElement {
  const isSelected = boost.id === state.selection.selectedBoostId;
  const card = createElement('article', 'boost-card');
  card.dataset['selected'] = String(isSelected);
  card.append(buildBoostCardHeader(state, store, boost, isSelected));

  if (isSelected) {
    card.append(buildSelectedCardBody(state, store, boost));
  } else {
    card.append(buildCompactCardBody(state, store, boost));
  }

  return card;
}

function buildBoostCardHeader(
    state: State, store: MahoBoostStore, boost: BoostCardEntry,
    isSelected: boolean): HTMLElement {
  const header = createElement('div', 'boost-card-header');
  const titleWrap = createElement('div', 'boost-card-heading');
  titleWrap.append(
      createElement('h2', 'boost-card-title', boost.urlPattern),
      createElement('p', 'boost-card-meta', `ID: ${boost.id}`));

  const metaRow = createElement('div', 'boost-card-meta-row');
  metaRow.append(
      createTemplateChip(state, boost),
      createMetaPill(boost.enabled ? 'Enabled' : 'Disabled'),
      createMetaPill(`${getZapSelectors(boost).length} selectors`));

  const toggleRow = createElement('label', 'boost-toggle-row');
  const toggleLabel = createElement(
      'span', 'boost-toggle-label', boost.enabled ? 'Enabled' : 'Disabled');
  const toggle = document.createElement('input');
  toggle.type = 'checkbox';
  toggle.checked = boost.enabled;
  toggle.disabled = state.busy;
  toggle.addEventListener('change', () => {
    store.setSelectedBoost(boost.id);
    void store.toggleBoost(boost.id, toggle.checked);
  });
  toggleRow.append(toggleLabel, toggle);

  const actions = createElement('div', 'boost-card-header-actions');
  if (!isSelected) {
    actions.appendChild(createButton(
        'Edit boost',
        'boost-button boost-button--secondary boost-button--small',
        state.busy,
        () => {
          store.setSelectedBoost(boost.id);
        }));
  }
  actions.appendChild(toggleRow);

  const left = createElement('div', 'boost-card-header-copy');
  left.append(titleWrap, metaRow);
  header.append(left, actions);
  return header;
}

function buildCompactCardBody(
    state: State, store: MahoBoostStore, boost: BoostCardEntry): HTMLElement {
  const body = createElement('div', 'boost-card-compact-body');
  body.append(
      createElement('p', 'boost-card-summary', buildCompactSummary(boost)),
      createElement(
          'p',
          'boost-card-summary-muted',
          'Select this boost to switch between CSS, JS, theme, font, and zap editors.'));

  const actions = createElement('div', 'boost-card-actions');
  actions.appendChild(createButton(
      'Open editor',
      'boost-button boost-button--primary boost-button--small',
      state.busy,
      () => {
        store.setSelectedBoost(boost.id);
      }));
  body.appendChild(actions);
  return body;
}

function buildSelectedCardBody(
    state: State, store: MahoBoostStore, boost: BoostCardEntry): HTMLElement {
  const body = createElement('div', 'boost-card-expanded-body');
  body.append(
      buildPatternEditor(state, store, boost),
      buildEditorTabs(state, store, boost.id),
      buildActiveEditorPanel(state, store, boost),
      buildSelectedCardActions(state, store, boost));
  return body;
}

function buildPatternEditor(
    state: State, store: MahoBoostStore, boost: BoostCardEntry): HTMLElement {
  const fieldId = `boost-pattern-${boost.id}`;
  const wrapper = createElement('div', 'boost-pattern-editor');
  wrapper.appendChild(createEditorLabel(fieldId, 'URL pattern'));

  const row = createElement('div', 'boost-inline-row');
  const input = document.createElement('input');
  input.id = fieldId;
  input.className = 'boost-input';
  input.value = boost.urlPattern;
  input.disabled = state.busy;
  input.addEventListener('input', () => {
    store.updateBoostPatternDraft(boost.id, input.value);
  });

  row.append(
      input,
      createButton(
          'Save pattern',
          'boost-button boost-button--secondary',
          state.busy,
          () => {
            store.setSelectedBoost(boost.id);
            void store.saveBoostPattern(boost.id);
          }));
  wrapper.appendChild(row);
  return wrapper;
}

function buildEditorTabs(
    state: State, store: MahoBoostStore, boostId: string): HTMLElement {
  const tabBar = createElement('div', 'boost-card-tab-bar');
  for (const tab of BOOST_EDITOR_TABS) {
    const button = createButton(
        tab.label,
        'boost-card-tab',
        state.busy,
        () => {
          focusBoostEditor(store, boostId, tab.id);
        });
    if (state.selection.activeEditorTab === tab.id) {
      button.dataset['active'] = 'true';
    }
    tabBar.appendChild(button);
  }
  return tabBar;
}

function buildActiveEditorPanel(
    state: State, store: MahoBoostStore, boost: BoostCardEntry): HTMLElement {
  const panel = createElement('div', 'boost-card-panel');
  switch (state.selection.activeEditorTab) {
    case 'css':
      panel.appendChild(buildCssEditor(state, store, boost));
      break;
    case 'js':
      panel.appendChild(buildJsEditor(state, store, boost));
      break;
    case 'theme':
      panel.appendChild(buildColorThemeEditor(state, store, boost));
      break;
    case 'font':
      panel.appendChild(buildFontOverrideEditor(state, store, boost));
      break;
    case 'zap':
      panel.appendChild(buildZapEditor(state, store, boost));
      break;
  }
  return panel;
}

function buildCssEditor(
    state: State, store: MahoBoostStore, boost: BoostCardEntry): HTMLElement {
  const fieldId = `boost-css-${boost.id}`;
  const wrapper = createElement('div', 'boost-editor-group');
  const textarea = document.createElement('textarea');
  textarea.id = fieldId;
  textarea.className = 'boost-textarea';
  textarea.value = boost.customCss;
  textarea.disabled = state.busy;
  textarea.addEventListener('input', () => {
    store.updateBoostCssDraft(boost.id, textarea.value);
  });

  wrapper.append(
      createEditorLabel(fieldId, 'Custom CSS'),
      textarea,
      createButton(
          'Save CSS',
          'boost-button boost-button--primary',
          state.busy,
          () => {
            focusBoostEditor(store, boost.id, 'css');
            void store.saveBoostCss(boost.id);
          }));
  return wrapper;
}

function buildJsEditor(
    state: State, store: MahoBoostStore, boost: BoostCardEntry): HTMLElement {
  const fieldId = `boost-js-${boost.id}`;
  const wrapper = createElement('div', 'boost-editor-group');
  const textarea = document.createElement('textarea');
  textarea.id = fieldId;
  textarea.className = 'boost-textarea';
  textarea.value = boost.customJs;
  textarea.disabled = state.busy;
  textarea.addEventListener('input', () => {
    store.updateBoostJsDraft(boost.id, textarea.value);
  });

  wrapper.append(
      createEditorLabel(fieldId, 'Custom JS'),
      textarea,
      createButton(
          'Save JS',
          'boost-button boost-button--secondary',
          state.busy,
          () => {
            focusBoostEditor(store, boost.id, 'js');
            void store.saveBoostJs(boost.id);
          }));
  return wrapper;
}

function buildZapEditor(
    state: State, store: MahoBoostStore, boost: BoostCardEntry): HTMLElement {
  const wrapper = createElement('div', 'boost-editor-group boost-zap-editor');
  const header = createElement('div', 'boost-zap-header');
  const copy = createElement('div', 'boost-zap-copy');
  copy.append(
      createElement('span', 'boost-editor-label', 'Zap selectors'),
      createElement(
          'p',
          'boost-zap-description',
          state.context ?
              `Hide distracting elements on ${state.context.siteLabel} by picking them from the live page.` :
              'Open this manager from a page context to pick live elements and save selectors here.'));

  const buttonLabel = state.zapModeBoostId === boost.id ?
      'Waiting for picker…' :
      'Enter zap mode';
  header.append(
      copy,
      createButton(
          buttonLabel,
          'boost-button boost-button--secondary',
          state.busy || !state.context || state.zapModeBoostId === boost.id,
          () => {
            focusBoostEditor(store, boost.id, 'zap');
            void store.enterZapMode(boost.id);
          }));

  const selectors = getZapSelectors(boost);
  const chipList = createElement('div', 'boost-zap-chip-list');
  if (selectors.length === 0) {
    chipList.appendChild(createElement('p', 'boost-zap-empty', 'No saved selectors yet.'));
  } else {
    for (const selector of selectors) {
      chipList.appendChild(buildZapSelectorChip(state, store, boost.id, selector));
    }
  }

  wrapper.append(header, chipList);
  return wrapper;
}

function buildZapSelectorChip(
    state: State, store: MahoBoostStore, boostId: string,
    selector: string): HTMLElement {
  const chip = createElement('div', 'boost-zap-chip');
  chip.append(
      createElement('code', 'boost-zap-chip-text', selector),
      createButton(
          'Remove',
          'boost-button boost-button--secondary boost-button--small boost-zap-chip-remove',
          state.busy,
          () => {
            focusBoostEditor(store, boostId, 'zap');
            void store.removeZapSelector(boostId, selector);
          }));
  return chip;
}

function buildColorThemeEditor(
    state: State, store: MahoBoostStore, boost: BoostCardEntry): HTMLElement {
  const wrapper = createElement('div', 'boost-editor-group');
  wrapper.appendChild(createElement('span', 'boost-editor-label', 'Color theme'));

  const colors = parseColorTheme(boost.colorThemeJson);
  const bgHolder = {value: colors.background};
  const textHolder = {value: colors.text};
  const linksHolder = {value: colors.links};

  const syncJson = () => {
    store.updateBoostColorThemeDraft(
        boost.id,
        buildColorThemeJson(bgHolder.value, textHolder.value, linksHolder.value));
  };

  const grid = createElement('div', 'boost-color-grid');
  grid.append(
      buildColorRow(state, 'Background', colors.background, (hex) => {
        bgHolder.value = hex;
        syncJson();
      }),
      buildColorRow(state, 'Text', colors.text, (hex) => {
        textHolder.value = hex;
        syncJson();
      }),
      buildColorRow(state, 'Links', colors.links, (hex) => {
        linksHolder.value = hex;
        syncJson();
      }));

  wrapper.append(
      grid,
      createButton(
          'Save color theme',
          'boost-button boost-button--secondary',
          state.busy,
          () => {
            focusBoostEditor(store, boost.id, 'theme');
            void store.saveBoostColorTheme(boost.id);
          }));
  return wrapper;
}

function buildColorRow(
    state: State, label: string, initial: string,
    onChange: (hex: string) => void): HTMLElement {
  const row = createElement('div', 'boost-color-row');
  const picker = document.createElement('input');
  picker.type = 'color';
  picker.className = 'boost-color-picker';
  picker.disabled = state.busy;
  picker.value = initial || '#000000';
  picker.addEventListener('input', () => {
    onChange(picker.value);
  });

  row.append(
      createElement('span', 'boost-color-label', label),
      picker,
      createButton(
          'Clear',
          'boost-button boost-button--secondary boost-button--small boost-color-clear',
          state.busy,
          () => {
            picker.value = '#000000';
            onChange('');
          }));
  return row;
}

function buildFontOverrideEditor(
    state: State, store: MahoBoostStore, boost: BoostCardEntry): HTMLElement {
  const wrapper = createElement('div', 'boost-editor-group');
  wrapper.appendChild(createElement('span', 'boost-editor-label', 'Font override'));

  const font = parseFontOverride(boost.fontOverrideJson);
  const fontHolder = {
    family: font.fontFamily,
    size: font.fontSize,
    lineHeight: font.lineHeight,
  };

  const syncJson = () => {
    store.updateBoostFontOverrideDraft(
        boost.id,
        buildFontOverrideJson(
            fontHolder.family, fontHolder.size, fontHolder.lineHeight));
  };

  const grid = createElement('div', 'boost-font-grid');
  const familyId = `boost-font-family-${boost.id}`;
  const familyGroup = createElement('div', 'boost-font-field');
  const familyLabel = createElement('label', 'boost-editor-label', 'Font family');
  familyLabel.htmlFor = familyId;

  const presetSelect = document.createElement('select');
  presetSelect.className = 'boost-input boost-font-preset';
  presetSelect.disabled = state.busy;
  for (const preset of FONT_PRESETS) {
    const option = document.createElement('option');
    option.value = preset.value;
    option.textContent = preset.label;
    option.selected = preset.value === fontHolder.family;
    presetSelect.appendChild(option);
  }

  const familyInput = document.createElement('input');
  familyInput.id = familyId;
  familyInput.className = 'boost-input';
  familyInput.placeholder = 'Custom font stack…';
  familyInput.value = fontHolder.family;
  familyInput.disabled = state.busy;

  presetSelect.addEventListener('change', () => {
    fontHolder.family = presetSelect.value;
    familyInput.value = presetSelect.value;
    syncJson();
  });
  familyInput.addEventListener('input', () => {
    fontHolder.family = familyInput.value;
    syncJson();
  });
  familyGroup.append(familyLabel, presetSelect, familyInput);

  const sizeGroup = buildNumericField(
      state,
      `boost-font-size-${boost.id}`,
      'Font size (px)',
      'e.g. 16',
      fontHolder.size,
      {min: '6', max: '72'},
      (value) => {
        fontHolder.size = value;
        syncJson();
      });
  const lineHeightGroup = buildNumericField(
      state,
      `boost-line-height-${boost.id}`,
      'Line height',
      'e.g. 1.6',
      fontHolder.lineHeight,
      {min: '0.5', max: '4', step: '0.05'},
      (value) => {
        fontHolder.lineHeight = value;
        syncJson();
      });

  grid.append(familyGroup, sizeGroup, lineHeightGroup);
  wrapper.append(
      grid,
      createButton(
          'Save font override',
          'boost-button boost-button--secondary',
          state.busy,
          () => {
            focusBoostEditor(store, boost.id, 'font');
            void store.saveBoostFontOverride(boost.id);
          }));
  return wrapper;
}

function buildNumericField(
    state: State, inputId: string, labelText: string, placeholder: string,
    value: string, constraints: {min: string; max: string; step?: string},
    onChange: (value: string) => void): HTMLElement {
  const group = createElement('div', 'boost-font-field');
  const label = createElement('label', 'boost-editor-label', labelText);
  label.htmlFor = inputId;

  const input = document.createElement('input');
  input.id = inputId;
  input.type = 'number';
  input.className = 'boost-input boost-font-size-input';
  input.placeholder = placeholder;
  input.min = constraints.min;
  input.max = constraints.max;
  input.value = value;
  input.disabled = state.busy;
  if (constraints.step) {
    input.step = constraints.step;
  }
  input.addEventListener('input', () => {
    onChange(input.value);
  });

  group.append(label, input);
  return group;
}

function buildSelectedCardActions(
    state: State, store: MahoBoostStore, boost: BoostCardEntry): HTMLElement {
  const actions = createElement('div', 'boost-card-actions');
  actions.appendChild(createButton(
      'Delete boost',
      'boost-button boost-button--danger',
      state.busy,
      () => {
        store.setSelectedBoost(boost.id);
        void store.deleteBoost(boost.id);
      }));
  return actions;
}

function buildCompactSummary(boost: BoostCardEntry): string {
  const parts = [];
  if (boost.customCss.trim()) {
    parts.push('CSS ready');
  }
  if (boost.customJs.trim()) {
    parts.push('JS ready');
  }
  if (boost.colorThemeJson.trim()) {
    parts.push('Theme override');
  }
  if (boost.fontOverrideJson.trim()) {
    parts.push('Font override');
  }
  if (getZapSelectors(boost).length) {
    parts.push(`${getZapSelectors(boost).length} zaps`);
  }
  return parts.join(' · ') || 'Blank boost ready for customization';
}

function createTemplateChip(_state: State, _boost: BoostCardEntry): HTMLElement {
  return createElement('span', 'boost-template-chip', 'Custom');
}

function getZapSelectors(boost: BoostCardEntry): string[] {
  return normalizeSelectors(boost.zapSelectors ?? []);
}

function normalizeSelectors(selectors: string[]): string[] {
  const unique = new Set<string>();
  for (const selector of selectors) {
    const trimmed = selector.trim();
    if (trimmed) {
      unique.add(trimmed);
    }
  }
  return [...unique];
}

function focusBoostEditor(
    store: MahoBoostStore, boostId: string, tab: BoostEditorTab) {
  store.setSelectedBoost(boostId);
  store.setSelectedEditorTab(tab);
}

function parseColorTheme(json: string): {background: string; text: string; links: string} {
  try {
    const parsed = json ? JSON.parse(json) as Record<string, unknown> : {};
    const bg = parsed['background'] as Record<string, number>|undefined;
    const text = parsed['text'] as Record<string, number>|undefined;
    const links = parsed['links'] as Record<string, number>|undefined;
    return {
      background: bg ? rgbaToHex(bg['r'] ?? 0, bg['g'] ?? 0, bg['b'] ?? 0) : '',
      text: text ? rgbaToHex(text['r'] ?? 0, text['g'] ?? 0, text['b'] ?? 0) : '',
      links: links ? rgbaToHex(links['r'] ?? 0, links['g'] ?? 0, links['b'] ?? 0) : '',
    };
  } catch {
    return {background: '', text: '', links: ''};
  }
}

function rgbaToHex(r: number, g: number, b: number): string {
  return '#' + [r, g, b].map(value => {
    return Math.round(value).toString(16).padStart(2, '0');
  }).join('');
}

function hexToRgba(hex: string): {r: number; g: number; b: number; a: number}|null {
  const result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex.trim());
  if (!result) {
    return null;
  }
  return {
    r: parseInt(result[1]!, 16),
    g: parseInt(result[2]!, 16),
    b: parseInt(result[3]!, 16),
    a: 1.0,
  };
}

function buildColorThemeJson(
    bgHex: string, textHex: string, linksHex: string): string {
  const obj: Record<string, unknown> = {};
  const bg = hexToRgba(bgHex);
  if (bg) {
    obj['background'] = bg;
  }
  const text = hexToRgba(textHex);
  if (text) {
    obj['text'] = text;
  }
  const links = hexToRgba(linksHex);
  if (links) {
    obj['links'] = links;
  }
  return Object.keys(obj).length > 0 ? JSON.stringify(obj) : '';
}

function parseFontOverride(
    json: string): {fontFamily: string; fontSize: string; lineHeight: string} {
  try {
    const parsed = json ? JSON.parse(json) as Record<string, unknown> : {};
    return {
      fontFamily: (parsed['fontFamily'] as string|undefined) ?? '',
      fontSize: parsed['fontSize'] !== undefined ? String(parsed['fontSize']) : '',
      lineHeight: parsed['lineHeight'] !== undefined ? String(parsed['lineHeight']) : '',
    };
  } catch {
    return {fontFamily: '', fontSize: '', lineHeight: ''};
  }
}

function buildFontOverrideJson(
    fontFamily: string, fontSize: string, lineHeight: string): string {
  const obj: Record<string, unknown> = {};
  if (fontFamily.trim()) {
    obj['fontFamily'] = fontFamily.trim();
  }
  const sizeNum = parseFloat(fontSize);
  if (!isNaN(sizeNum) && sizeNum > 0) {
    obj['fontSize'] = sizeNum;
  }
  const lineHeightNum = parseFloat(lineHeight);
  if (!isNaN(lineHeightNum) && lineHeightNum > 0) {
    obj['lineHeight'] = lineHeightNum;
  }
  return Object.keys(obj).length > 0 ? JSON.stringify(obj) : '';
}
