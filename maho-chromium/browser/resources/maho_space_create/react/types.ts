export type ThemeMode = 'sparkle' | 'sun' | 'moon';

export interface ThemeSelection {
  mode: ThemeMode;
  preset_index: number;
  brightness: number;
}

export interface ThemeSelectionWire {
  theme_json: string;
}

export interface ThemePickerState {
  selection: ThemeSelection;
  loading: boolean;
}

export const SWATCH_COUNT = 9;

export function createDefaultSelection(): ThemeSelection {
  return {
    mode: 'sparkle',
    preset_index: 0,
    brightness: 54,
  };
}

export function createInitialState(): ThemePickerState {
  return {
    selection: createDefaultSelection(),
    loading: true,
  };
}

export function cloneSelection(selection: ThemeSelection): ThemeSelection {
  return {
    ...selection,
  };
}

export function cloneState(state: ThemePickerState): ThemePickerState {
  return {
    loading: state.loading,
    selection: cloneSelection(state.selection),
  };
}

export function clampBrightness(value: number): number {
  return Math.max(0, Math.min(100, Math.round(value)));
}

export function normalizePresetIndex(index: number): number {
  const normalized = index % SWATCH_COUNT;
  return normalized < 0 ? normalized + SWATCH_COUNT : normalized;
}

export function normalizeSelection(selection: Partial<ThemeSelection>): ThemeSelection {
  const fallback = createDefaultSelection();
  return {
    mode: selection.mode ?? fallback.mode,
    preset_index: normalizePresetIndex(selection.preset_index ?? fallback.preset_index),
    brightness: clampBrightness(selection.brightness ?? fallback.brightness),
  };
}

export function parseThemeSelection(json: string): ThemeSelection {
  if (!json) {
    return createDefaultSelection();
  }

  try {
    const parsed = JSON.parse(json) as Partial<ThemeSelection>;
    return normalizeSelection(parsed);
  } catch {
    return createDefaultSelection();
  }
}

export function serializeThemeSelection(selection: ThemeSelection): string {
  return JSON.stringify(normalizeSelection(selection));
}
