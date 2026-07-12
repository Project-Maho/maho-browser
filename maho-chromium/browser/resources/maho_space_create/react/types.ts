export type ThemeMode = 'sparkle' | 'sun' | 'moon';

export type ColorHarmony =
    | 'complementary'
    | 'singleAnalogous'
    | 'splitComplementary'
    | 'analogous'
    | 'triadic'
    | 'floating';

export const HARMONIES: Record<ColorHarmony, number[]> = {
  complementary: [180],
  singleAnalogous: [310],
  splitComplementary: [150, 210],
  analogous: [50, 310],
  triadic: [120, 240],
  floating: [],
};

export const HARMONY_MAX_DOTS: Record<ColorHarmony, number> = {
  complementary: 2,
  singleAnalogous: 2,
  splitComplementary: 3,
  analogous: 3,
  triadic: 3,
  floating: 8,
};

export interface CanvasGeometry {
  width: number;
  height: number;
  padding: number;
}

export const DEFAULT_CANVAS_GEOMETRY: CanvasGeometry = {
  width: 380,
  height: 380,
  padding: 20,
};

export const DEFAULT_DISTANCE = 0.3;

export function distanceFromLightness(lightness: number | undefined): number {
  if (typeof lightness === 'number' && Number.isFinite(lightness) &&
      lightness > 0 && lightness <= 100) {
    return clampUnit(1 - lightness / 100);
  }
  return DEFAULT_DISTANCE;
}

// ---------------------------------------------------------------------------
// Polar color mapping (Zen parity)
// ---------------------------------------------------------------------------

export function getColorFromPosition(
    x: number, y: number,
    geom: CanvasGeometry): {hue: number; saturation: number; lightness: number} {
  const cx = geom.width / 2;
  const cy = geom.height / 2;
  const dx = x - cx;
  const dy = y - cy;
  const angle = Math.atan2(dy, dx);
  let angleDeg = angle * 180 / Math.PI;
  if (angleDeg < 0) angleDeg += 360;
  const distance = Math.sqrt(dx * dx + dy * dy);
  const radius = Math.min(cx, cy) - geom.padding;
  const normalized = Math.max(0, Math.min(1, distance / radius));
  const hue = normalizeHue(angleDeg / 360);
  const saturation = (90 + (1 - normalized) * 10) / 100;
  const lightness = (1 - normalized) * 100;
  return {hue, saturation, lightness};
}

export function clampToCircle(
    x: number, y: number,
    geom: CanvasGeometry): {x: number; y: number} {
  const cx = geom.width / 2;
  const cy = geom.height / 2;
  const dx = x - cx;
  const dy = y - cy;
  const distance = Math.sqrt(dx * dx + dy * dy);
  const radius = Math.min(cx, cy) - geom.padding;
  if (distance <= radius) {
    return {x, y};
  }
  const angle = Math.atan2(dy, dx);
  return {
    x: cx + Math.cos(angle) * radius,
    y: cy + Math.sin(angle) * radius,
  };
}

export function positionForHueAndDistance(
    hue: number, distance: number,
    geom: CanvasGeometry): {x: number; y: number} {
  const cx = geom.width / 2;
  const cy = geom.height / 2;
  const radius = Math.min(cx, cy) - geom.padding;
  const angleDeg = hue * 360;
  const angleRad = angleDeg * Math.PI / 180;
  return {
    x: cx + Math.cos(angleRad) * distance * radius,
    y: cy + Math.sin(angleRad) * distance * radius,
  };
}

// ---------------------------------------------------------------------------
// Opacity helpers
// ---------------------------------------------------------------------------

export const OPACITY_MIN = 0.0;
export const OPACITY_MAX = 0.9;

export function clampOpacity(v: number): number {
  return Math.max(OPACITY_MIN, Math.min(OPACITY_MAX, v));
}

// ---------------------------------------------------------------------------
// Texture helpers
// ---------------------------------------------------------------------------

export function snapTexture(t: number): number {
  let snapped = Math.round(t * 16) / 16;
  if (snapped === 1) snapped = 0;
  return snapped;
}

// ---------------------------------------------------------------------------
// HSB ↔ RGB (local, no cross-module import)
// ---------------------------------------------------------------------------

/**
 * Converts HSB (all 0-1) to RGB (each 0-255 integer).
 */
export function hsbToRgb(
    h: number, s: number, b: number): [number, number, number] {
  const i = Math.floor(h * 6);
  const f = h * 6 - i;
  const p = b * (1 - s);
  const q = b * (1 - f * s);
  const t = b * (1 - (1 - f) * s);
  let r: number, g: number, bl: number;
  switch (i % 6) {
    case 0: r = b; g = t; bl = p; break;
    case 1: r = q; g = b; bl = p; break;
    case 2: r = p; g = b; bl = t; break;
    case 3: r = p; g = q; bl = b; break;
    case 4: r = t; g = p; bl = b; break;
    default: r = b; g = p; bl = q; break;
  }
  return [
    Math.round(r * 255),
    Math.round(g * 255),
    Math.round(bl * 255),
  ];
}

/**
 * Converts RGB (each 0-255) to HSB (each 0-1).
 */
export function rgbToHsb(
    r: number, g: number, b: number): {h: number; s: number; b: number} {
  const rn = r / 255;
  const gn = g / 255;
  const bn = b / 255;
  const max = Math.max(rn, gn, bn);
  const min = Math.min(rn, gn, bn);
  const d = max - min;
  let hue = 0;
  if (d !== 0) {
    if (max === rn) {
      hue = ((gn - bn) / d + 6) % 6;
    } else if (max === gn) {
      hue = (bn - rn) / d + 2;
    } else {
      hue = (rn - gn) / d + 4;
    }
    hue /= 6;
  }
  const sat = max === 0 ? 0 : d / max;
  return {h: hue, s: sat, b: max};
}

// ---------------------------------------------------------------------------
// Core data model
// ---------------------------------------------------------------------------

export interface ZenStop {
  hue: number;
  saturation: number;
  isPrimary: boolean;
  position: {x: number; y: number};
  lightness: number;
}

export interface ThemeSelection {
  mode: ThemeMode;
  preset_index: number;
  grain: number;
  opacity: number;
  harmony: ColorHarmony;
  stops: ZenStop[];
}

export interface ThemeSelectionWire {
  themeJson: string;
}

export interface ThemePickerState {
  selection: ThemeSelection;
  loading: boolean;
}

// ---------------------------------------------------------------------------
// Serialization types
// ---------------------------------------------------------------------------

export interface SerializedSpaceColor {
  hue: number;
  saturation: number;
  brightness: number;
  grain: number;
}

export interface SerializedThemeColor {
  hue: number;
  saturation: number;
  brightness: number;
  isCustom: boolean;
  isPrimary: boolean;
  c?: [number, number, number];
  position?: {x: number; y: number};
  algorithm?: string;
  lightness?: number;
}

export interface SerializedSolidTheme {
  type: 'solid';
  color: SerializedSpaceColor;
  opacity?: number;
}

export interface SerializedZenTheme {
  type: 'gradient';
  gradientColors: SerializedThemeColor[];
  harmony: string;
  opacity: number;
  texture: number;
  scheme?: 'auto' | 'light' | 'dark';
}

export type SerializedTheme = SerializedSolidTheme | SerializedZenTheme;

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------

export const SWATCH_COUNT = 9;

interface PresetTriple {
  hue: number;
  saturation: number;
  brightness: number;
}

export const PRESETS: ReadonlyArray<PresetTriple> = Object.freeze([
  {hue: 0.10, saturation: 0.15, brightness: 0.95},
  {hue: 0.92, saturation: 0.80, brightness: 0.90},
  {hue: 0.75, saturation: 0.80, brightness: 0.90},
  {hue: 0.00, saturation: 0.80, brightness: 0.90},
  {hue: 0.08, saturation: 0.80, brightness: 0.90},
  {hue: 0.15, saturation: 0.80, brightness: 0.90},
  {hue: 0.35, saturation: 0.80, brightness: 0.90},
  {hue: 0.58, saturation: 0.80, brightness: 0.90},
  {hue: 0.62, saturation: 0.30, brightness: 0.55},
]);

export interface ZenPreset {
  id: string;
  lightness: number;
  algorithm: ColorHarmony;
  numDots: number;
  primaryPosition: {x: number; y: number};
  colors: Array<{r: number; g: number; b: number}>;
}

const PRESET_IDS = [
  'Cream', 'Pink', 'Purple', 'Red', 'Orange',
  'Yellow', 'Green', 'Blue', 'Indigo',
];

function buildZenPresets(): ZenPreset[] {
  const geom = DEFAULT_CANVAS_GEOMETRY;
  const lightness = (1 - DEFAULT_DISTANCE) * 100;
  const distance = distanceFromLightness(lightness);
  return PRESETS.map((p, i) => {
    const pos = positionForHueAndDistance(p.hue, distance, geom);
    const [r, g, b] = hsbToRgb(p.hue, p.saturation, p.brightness);
    return {
      id: PRESET_IDS[i],
      lightness,
      algorithm: 'analogous' as ColorHarmony,
      numDots: 3,
      primaryPosition: pos,
      colors: [{r, g, b}],
    };
  });
}

export const ZEN_PRESETS: ReadonlyArray<ZenPreset> = Object.freeze(
    buildZenPresets());

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

export function clampUnit(value: number): number {
  return Math.max(0, Math.min(1, value));
}

export function clampBrightness(value: number): number {
  return Math.max(0, Math.min(100, Math.round(value)));
}

export function normalizePresetIndex(index: number): number {
  if (!Number.isFinite(index)) {
    return 0;
  }
  const normalized = Math.trunc(index) % SWATCH_COUNT;
  return normalized < 0 ? normalized + SWATCH_COUNT : normalized;
}

export function normalizeHue(hue: number): number {
  const wrapped = hue - Math.floor(hue);
  return wrapped < 0 ? wrapped + 1 : wrapped;
}

export function stopsForPreset(presetIndex: number): ZenStop[] {
  const preset = PRESETS[normalizePresetIndex(presetIndex)];
  const geom = DEFAULT_CANVAS_GEOMETRY;
  const lightness = (1 - DEFAULT_DISTANCE) * 100;
  const distance = distanceFromLightness(lightness);
  const primaryPos = positionForHueAndDistance(preset.hue, distance, geom);
  const offsets = HARMONIES['analogous'];
  const secondaries = offsets.map(offset => {
    const newHue = normalizeHue(preset.hue + offset / 360);
    const pos = positionForHueAndDistance(newHue, distance, geom);
    return {
      hue: newHue,
      saturation: preset.saturation,
      isPrimary: false,
      position: pos,
      lightness,
    };
  });
  return [
    {
      hue: preset.hue,
      saturation: preset.saturation,
      isPrimary: true,
      position: primaryPos,
      lightness,
    },
    ...secondaries,
  ];
}

export function createDefaultSelection(): ThemeSelection {
  return {
    mode: 'sparkle',
    preset_index: 0,
    grain: 0,
    opacity: 0.5,
    harmony: 'analogous',
    stops: stopsForPreset(0),
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
    mode: selection.mode,
    preset_index: selection.preset_index,
    grain: selection.grain,
    opacity: selection.opacity,
    harmony: selection.harmony,
    stops: selection.stops.map(stop => ({
      ...stop,
      position: {...stop.position},
    })),
  };
}

export function cloneState(state: ThemePickerState): ThemePickerState {
  return {
    loading: state.loading,
    selection: cloneSelection(state.selection),
  };
}

export function normalizeSelection(
    selection: Partial<ThemeSelection>): ThemeSelection {
  const fallback = createDefaultSelection();
  const presetIndex = normalizePresetIndex(
      selection.preset_index ?? fallback.preset_index);
  const harmony: ColorHarmony = isValidHarmony(selection.harmony)
      ? selection.harmony
      : fallback.harmony;

  let stops: ZenStop[];
  if (Array.isArray(selection.stops) &&
      selection.stops.length <= HARMONY_MAX_DOTS[harmony]) {
    stops = selection.stops.map(stop => {
      const hue = typeof stop.hue === 'number' ? normalizeHue(stop.hue) : 0;
      const lightness = typeof stop.lightness === 'number' &&
              Number.isFinite(stop.lightness) && stop.lightness > 0 &&
              stop.lightness <= 100
          ? stop.lightness
          : (1 - DEFAULT_DISTANCE) * 100;
      return {
        hue,
        saturation: typeof stop.saturation === 'number'
            ? clampUnit(stop.saturation) : 0,
        isPrimary: Boolean(stop.isPrimary),
        position: positionForHueAndDistance(
            hue, distanceFromLightness(lightness), DEFAULT_CANVAS_GEOMETRY),
        lightness,
      };
    });
  } else {
    stops = stopsForPreset(presetIndex);
  }

  if (stops.length > 0 && !stops.some(s => s.isPrimary)) {
    stops[0]!.isPrimary = true;
  }

  return {
    mode: selection.mode ?? fallback.mode,
    preset_index: presetIndex,
    grain: clampBrightness(selection.grain ?? fallback.grain),
    opacity: clampOpacity(selection.opacity ?? fallback.opacity),
    harmony,
    stops,
  };
}

function isValidHarmony(h: unknown): h is ColorHarmony {
  return typeof h === 'string' && h in HARMONIES;
}

// ---------------------------------------------------------------------------
// Harmony recomputation
// ---------------------------------------------------------------------------

export function recomputeSecondaryPositions(
    primary: ZenStop, harmony: ColorHarmony,
    geom: CanvasGeometry): Array<{
      position: {x: number; y: number};
      hue: number;
      saturation: number;
      lightness: number;
    }> {
  const cx = geom.width / 2;
  const cy = geom.height / 2;
  const dx = primary.position.x - cx;
  const dy = primary.position.y - cy;
  const distance = Math.sqrt(dx * dx + dy * dy);
  const radius = Math.min(cx, cy) - geom.padding;
  const normalizedDistance = radius > 0 ? distance / radius : 0;
  const baseDeg = Math.atan2(dy, dx) * 180 / Math.PI;

  return HARMONIES[harmony].map(offsetDeg => {
    const newDeg = baseDeg + offsetDeg;
    const newRad = newDeg * Math.PI / 180;
    const pos = {
      x: cx + Math.cos(newRad) * distance,
      y: cy + Math.sin(newRad) * distance,
    };
    const newHue = normalizeHue(((newDeg % 360) + 360) % 360 / 360);
    return {
      position: pos,
      hue: newHue,
      saturation: primary.saturation,
      lightness: primary.lightness,
    };
  });
}

// ---------------------------------------------------------------------------
// Wire serialization
// ---------------------------------------------------------------------------

const ANALOGOUS_OFFSETS = [-50 / 360, 50 / 360];

function buildAutoZenStops(
    primaryHue: number, primarySaturation: number): ZenStop[] {
  const geom = DEFAULT_CANVAS_GEOMETRY;
  const lightness = (1 - DEFAULT_DISTANCE) * 100;
  const distance = distanceFromLightness(lightness);
  const primaryPos = positionForHueAndDistance(primaryHue, distance, geom);
  return [
    {
      hue: normalizeHue(primaryHue),
      saturation: clampUnit(primarySaturation),
      isPrimary: true,
      position: primaryPos,
      lightness,
    },
    ...ANALOGOUS_OFFSETS.map(offset => ({
      hue: normalizeHue(primaryHue + offset),
      saturation: clampUnit(primarySaturation),
      isPrimary: false,
      position: positionForHueAndDistance(
          normalizeHue(primaryHue + offset), distance, geom),
      lightness,
    })),
  ];
}

function modeToScheme(mode: ThemeMode): 'auto' | 'light' | 'dark' {
  switch (mode) {
    case 'sun':
      return 'light';
    case 'moon':
      return 'dark';
    default:
      return 'auto';
  }
}

function schemeToMode(scheme: unknown): ThemeMode {
  if (scheme === 'light') return 'sun';
  if (scheme === 'dark') return 'moon';
  return 'sparkle';
}

function mapHarmonyToWire(harmony: ColorHarmony): string {
  if (harmony === 'singleAnalogous') return 'single_analogous';
  if (harmony === 'splitComplementary') return 'split_complementary';
  return harmony;
}

function mapWireToHarmony(wire: unknown): ColorHarmony {
  if (wire === 'single_analogous') return 'singleAnalogous';
  if (wire === 'split_complementary') return 'splitComplementary';
  if (isValidHarmony(wire)) return wire;
  return 'analogous';
}

export function selectionToWireTheme(
    selection: ThemeSelection): SerializedTheme {
  const normalized = normalizeSelection(selection);
  const grainUnit = clampUnit(normalized.grain / 100);
  return {
    type: 'gradient',
    gradientColors: normalized.stops.map(stop => {
      const brightnessUnit = clampUnit(stop.lightness / 100);
      const [r, g, b] = hsbToRgb(stop.hue, stop.saturation, brightnessUnit);
      const color: SerializedThemeColor = {
        hue: stop.hue,
        saturation: stop.saturation,
        brightness: brightnessUnit,
        isCustom: false,
        isPrimary: stop.isPrimary,
        c: [r, g, b],
        position: {x: stop.position.x, y: stop.position.y},
      };
      if (stop.isPrimary) {
        color.algorithm = mapHarmonyToWire(normalized.harmony);
        color.lightness = stop.lightness;
      }
      return color;
    }),
    harmony: mapHarmonyToWire(normalized.harmony),
    opacity: normalized.opacity,
    texture: grainUnit,
    scheme: modeToScheme(normalized.mode),
  };
}

function nearestPresetIndex(hue: number): number {
  const target = normalizeHue(hue);
  let bestIndex = 0;
  let bestDistance = Number.POSITIVE_INFINITY;
  for (let i = 0; i < PRESETS.length; i++) {
    const candidate = PRESETS[i].hue;
    const direct = Math.abs(candidate - target);
    const wrap = 1 - direct;
    const distance = Math.min(direct, wrap);
    if (distance < bestDistance) {
      bestDistance = distance;
      bestIndex = i;
    }
  }
  return bestIndex;
}

function decodeHueValue(value: number): number {
  return value <= 1 ? value : value / 360;
}

export function wireThemeToSelection(
    json: string | undefined | null): ThemePickerState {
  const fallback: ThemePickerState = {
    selection: createDefaultSelection(),
    loading: false,
  };
  if (!json) {
    return fallback;
  }
  try {
    const parsed = JSON.parse(json) as Record<string, unknown>;
    if (!parsed || typeof parsed !== 'object' || !parsed.type) {
      return fallback;
    }

    if (parsed.type === 'solid') {
      const solid = parsed as unknown as SerializedSolidTheme;
      const color = solid.color;
      if (!color || typeof color.hue !== 'number') {
        return fallback;
      }
      const huePart = decodeHueValue(color.hue);
      const presetIndex = nearestPresetIndex(huePart);
      const grain = typeof color.grain === 'number'
          ? clampBrightness(Math.round(color.grain * 100))
          : 0;
      const saturation = typeof color.saturation === 'number'
          ? clampUnit(color.saturation)
          : PRESETS[presetIndex].saturation;
      return {
        selection: {
          mode: 'sparkle',
          preset_index: presetIndex,
          grain,
          opacity: typeof solid.opacity === 'number'
              ? clampOpacity(solid.opacity)
              : OPACITY_MAX,
          harmony: 'analogous',
          stops: buildAutoZenStops(huePart, saturation),
        },
        loading: false,
      };
    }

    if (parsed.type === 'zen' || parsed.type === 'gradient') {
      const zen = parsed as unknown as SerializedZenTheme;
      const stops = zen.gradientColors;
      if (!Array.isArray(stops)) {
        return fallback;
      }

      // Parse harmony
      const rawHarmony = (parsed as Record<string, unknown>).harmony;
      const harmony: ColorHarmony = mapWireToHarmony(rawHarmony);
      const maxStops = HARMONY_MAX_DOTS[harmony];

      // Parse opacity
      const rawOpacity = typeof zen.opacity === 'number' ? zen.opacity : OPACITY_MAX;
      const opacity = clampOpacity(rawOpacity);

      // Parse stops
      const validStops: ZenStop[] = stops.map(stop => {
        let hue: number;
        let saturation: number;
        let lightness: number;

        // Try c array first (Zen primary), fall back to HSB
        const cArray = (stop as Record<string, unknown>).c;
        if (Array.isArray(cArray) && cArray.length === 3 &&
            cArray.every(v => typeof v === 'number')) {
          const hsb = rgbToHsb(
              cArray[0] as number, cArray[1] as number, cArray[2] as number);
          hue = hsb.h;
          saturation = hsb.s;
          // Use per-stop lightness if present; else derive from brightness
          const rawLightness =
              (stop as Record<string, unknown>).lightness;
          lightness = typeof rawLightness === 'number'
              ? rawLightness
              : hsb.b * 100;
        } else {
          hue = typeof stop.hue === 'number'
              ? normalizeHue(decodeHueValue(stop.hue)) : 0;
          saturation = typeof stop.saturation === 'number'
              ? clampUnit(stop.saturation) : 0;
          const rawBrightness = typeof stop.brightness === 'number'
              ? stop.brightness : 0;
          const rawLightness =
              (stop as Record<string, unknown>).lightness;
          lightness = typeof rawLightness === 'number'
              ? rawLightness
              : rawBrightness * 100;
        }

        if (!(typeof lightness === 'number' && Number.isFinite(lightness) &&
              lightness > 0 && lightness <= 100)) {
          const distance = DEFAULT_DISTANCE;
          lightness = (1 - distance) * 100;
          saturation = (90 + (1 - distance) * 10) / 100;
        }

        const position = positionForHueAndDistance(
            hue, distanceFromLightness(lightness), DEFAULT_CANVAS_GEOMETRY);

        return {
          hue,
          saturation,
          isPrimary: Boolean(stop.isPrimary),
          position,
          lightness,
        };
      });

      // Pad to at least 3 stops for analogous/splitComplementary/triadic
      if (validStops.length > 0) {
        while (validStops.length < Math.min(3, maxStops)) {
          const last = validStops[validStops.length - 1]!;
          validStops.push({
            ...last,
            position: {...last.position},
            isPrimary: false,
          });
        }
      }

      if (validStops.length > 0 && !validStops.some(s => s.isPrimary)) {
        validStops[0]!.isPrimary = true;
      }

      const primary = validStops.find(s => s.isPrimary);
      const texture = zen.texture;
      const grain = typeof texture === 'number'
          ? clampBrightness(Math.round(texture * 100))
          : 0;

      return {
        selection: {
          mode: schemeToMode(zen.scheme),
          preset_index: primary ? nearestPresetIndex(primary.hue) : 0,
          grain,
          opacity,
          harmony,
          stops: validStops,
        },
        loading: false,
      };
    }

    return fallback;
  } catch {
    return fallback;
  }
}

export function parseThemeSelection(json: string): ThemeSelection {
  return wireThemeToSelection(json).selection;
}

export function serializeThemeSelection(
    selection: ThemeSelection): string {
  return JSON.stringify(selectionToWireTheme(selection));
}

export function harmonyDotsForSelection(
    selection: ThemeSelection): SerializedThemeColor[] {
  return selection.stops.map(stop => ({
    hue: stop.hue,
    saturation: stop.saturation,
    brightness: clampUnit(stop.lightness / 100),
    isCustom: false,
    isPrimary: stop.isPrimary,
  }));
}
