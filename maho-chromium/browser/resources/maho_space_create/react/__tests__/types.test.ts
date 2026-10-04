import {describe, expect, it} from 'vitest';
import {
  DEFAULT_CANVAS_GEOMETRY,
  HARMONIES,
  HARMONY_MAX_DOTS,
  OPACITY_MAX,
  OPACITY_MIN,
  PRESETS,
  SWATCH_COUNT,
  ZEN_PRESETS,
  clampBrightness,
  clampOpacity,
  clampToCircle,
  clampUnit,
  createDefaultSelection,
  createInitialState,
  getColorFromPosition,
  harmonyDotsForSelection,
  hsbToRgb,
  normalizeHue,
  normalizePresetIndex,
  parseThemeSelection,
  positionForHueAndDistance,
  recomputeSecondaryPositions,
  rgbToHsb,
  selectionToWireTheme,
  serializeThemeSelection,
  snapTexture,
  stopsForPreset,
  wireThemeToSelection,
} from '../types.js';
import type {SerializedSolidTheme, SerializedZenTheme, ZenStop} from '../types.js';

describe('PRESETS table', () => {
  it('has exactly 9 entries', () => {
    expect(PRESETS).toHaveLength(SWATCH_COUNT);
  });

  it('each entry has hue/saturation/brightness in [0,1]', () => {
    for (const preset of PRESETS) {
      expect(preset.hue).toBeGreaterThanOrEqual(0);
      expect(preset.hue).toBeLessThanOrEqual(1);
      expect(preset.saturation).toBeGreaterThanOrEqual(0);
      expect(preset.saturation).toBeLessThanOrEqual(1);
      expect(preset.brightness).toBeGreaterThanOrEqual(0);
      expect(preset.brightness).toBeLessThanOrEqual(1);
    }
  });

  it('matches the native ColorPreset table from maho_sidebar_create_space_view.cc', () => {
    expect(PRESETS).toEqual([
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
  });
});

describe('clampBrightness', () => {
  it('clamps below 0 to 0', () => expect(clampBrightness(-50)).toBe(0));
  it('clamps above 100 to 100', () => expect(clampBrightness(250)).toBe(100));
  it('rounds fractional values', () => {
    expect(clampBrightness(50.6)).toBe(51);
    expect(clampBrightness(50.4)).toBe(50);
  });
});

describe('clampUnit', () => {
  it('clamps to [0,1]', () => {
    expect(clampUnit(-1)).toBe(0);
    expect(clampUnit(2)).toBe(1);
    expect(clampUnit(0.5)).toBe(0.5);
  });
});

describe('normalizeHue', () => {
  it('wraps 1.5 to 0.5', () => expect(normalizeHue(1.5)).toBeCloseTo(0.5));
  it('wraps -0.25 to 0.75', () => expect(normalizeHue(-0.25)).toBeCloseTo(0.75));
  it('keeps values in [0,1) unchanged', () => expect(normalizeHue(0.7)).toBeCloseTo(0.7));
});

describe('normalizePresetIndex', () => {
  it('wraps positive overflow modulo 9', () => expect(normalizePresetIndex(11)).toBe(2));
  it('wraps negative values', () => {
    expect(normalizePresetIndex(-1)).toBe(8);
    expect(normalizePresetIndex(-10)).toBe(8);
  });
  it('returns 0 for NaN', () => expect(normalizePresetIndex(NaN)).toBe(0));
  it('truncates fractional input', () => expect(normalizePresetIndex(2.7)).toBe(2));
});

describe('HARMONIES constants', () => {
  it('complementary === [180]', () => {
    expect(HARMONIES.complementary).toEqual([180]);
  });

  it('analogous === [50, 310]', () => {
    expect(HARMONIES.analogous).toEqual([50, 310]);
  });

  it('singleAnalogous === [310]', () => {
    expect(HARMONIES.singleAnalogous).toEqual([310]);
  });

  it('splitComplementary === [150, 210]', () => {
    expect(HARMONIES.splitComplementary).toEqual([150, 210]);
  });

  it('triadic === [120, 240]', () => {
    expect(HARMONIES.triadic).toEqual([120, 240]);
  });

  it('floating === []', () => {
    expect(HARMONIES.floating).toEqual([]);
  });
});

describe('HARMONY_MAX_DOTS', () => {
  it('has correct max values for each harmony', () => {
    expect(HARMONY_MAX_DOTS.complementary).toBe(2);
    expect(HARMONY_MAX_DOTS.singleAnalogous).toBe(2);
    expect(HARMONY_MAX_DOTS.splitComplementary).toBe(3);
    expect(HARMONY_MAX_DOTS.analogous).toBe(3);
    expect(HARMONY_MAX_DOTS.triadic).toBe(3);
    expect(HARMONY_MAX_DOTS.floating).toBe(8);
  });
});

describe('getColorFromPosition — polar mapping', () => {
  const geom = {width: 380, height: 380, padding: 20};
  const cx = 190;
  const cy = 190;
  const radius = 170;

  it('edge → lightness 0 (right edge)', () => {
    const result = getColorFromPosition(cx + radius, cy, geom);
    expect(result.lightness).toBeCloseTo(0, 1);
    expect(result.hue).toBeCloseTo(0, 2);
  });

  it('center → lightness 100', () => {
    const result = getColorFromPosition(cx, cy, geom);
    expect(result.lightness).toBeCloseTo(100, 1);
  });

  it('90° angle (bottom of canvas) → hue 0.25', () => {
    const result = getColorFromPosition(cx, cy + radius, geom);
    expect(result.hue).toBeCloseTo(0.25, 2);
  });

  it('saturation stays in [0.9, 1.0]', () => {
    const edge = getColorFromPosition(cx + radius, cy, geom);
    const center = getColorFromPosition(cx, cy, geom);
    expect(edge.saturation).toBeCloseTo(0.9, 2);
    expect(center.saturation).toBeCloseTo(1.0, 2);
  });
});

describe('clampToCircle', () => {
  const geom = {width: 380, height: 380, padding: 20};
  const cx = 190;
  const cy = 190;
  const radius = 170;

  it('inside unchanged', () => {
    const result = clampToCircle(200, 200, geom);
    expect(result.x).toBe(200);
    expect(result.y).toBe(200);
  });

  it('outside snaps to perimeter', () => {
    const result = clampToCircle(cx + 300, cy, geom);
    expect(result.x).toBeCloseTo(cx + radius, 1);
    expect(result.y).toBeCloseTo(cy, 1);
  });
});

describe('positionForHueAndDistance', () => {
  const geom = {width: 380, height: 380, padding: 20};
  const cx = 190;
  const radius = 170;

  it('hue 0 distance 1 → right edge', () => {
    const result = positionForHueAndDistance(0, 1, geom);
    expect(result.x).toBeCloseTo(cx + radius, 1);
    expect(result.y).toBeCloseTo(190, 1);
  });

  it('hue 0.5 distance 1 → left edge', () => {
    const result = positionForHueAndDistance(0.5, 1, geom);
    expect(result.x).toBeCloseTo(cx - radius, 1);
    expect(result.y).toBeCloseTo(190, 1);
  });
});

describe('clampOpacity', () => {
  it('clamps below min to 0.25', () => {
    expect(clampOpacity(0.1)).toBe(OPACITY_MIN);
  });

  it('clamps above max to 0.9', () => {
    expect(clampOpacity(0.95)).toBe(OPACITY_MAX);
  });

  it('passes through valid values', () => {
    expect(clampOpacity(0.5)).toBe(0.5);
  });
});

describe('snapTexture', () => {
  it('rounds to nearest 1/16', () => {
    expect(snapTexture(0.03125)).toBe(0.0625);
  });

  it('wraps 1.0 to 0', () => {
    expect(snapTexture(1.0)).toBe(0);
  });

  it('keeps 0 as 0', () => {
    expect(snapTexture(0)).toBe(0);
  });

  it('snaps 0.5 to 0.5', () => {
    expect(snapTexture(0.5)).toBe(0.5);
  });
});

describe('hsbToRgb', () => {
  it('pure red', () => {
    const [r, g, b] = hsbToRgb(0, 1, 1);
    expect(r).toBe(255);
    expect(g).toBe(0);
    expect(b).toBe(0);
  });

  it('black', () => {
    const [r, g, b] = hsbToRgb(0, 0, 0);
    expect(r).toBe(0);
    expect(g).toBe(0);
    expect(b).toBe(0);
  });

  it('white', () => {
    const [r, g, b] = hsbToRgb(0, 0, 1);
    expect(r).toBe(255);
    expect(g).toBe(255);
    expect(b).toBe(255);
  });
});

describe('rgbToHsb', () => {
  it('pure red', () => {
    const hsb = rgbToHsb(255, 0, 0);
    expect(hsb.h).toBeCloseTo(0, 2);
    expect(hsb.s).toBeCloseTo(1, 2);
    expect(hsb.b).toBeCloseTo(1, 2);
  });

  it('black', () => {
    const hsb = rgbToHsb(0, 0, 0);
    expect(hsb.b).toBe(0);
  });

  it('round-trips through hsbToRgb', () => {
    const [r, g, b] = hsbToRgb(0.3, 0.7, 0.8);
    const hsb = rgbToHsb(r, g, b);
    expect(hsb.h).toBeCloseTo(0.3, 1);
    expect(hsb.s).toBeCloseTo(0.7, 1);
    expect(hsb.b).toBeCloseTo(0.8, 1);
  });
});

describe('selectionToWireTheme — gradient mode', () => {
  it('returns gradient theme with 3 gradient colors for sparkle mode', () => {
    const wire = selectionToWireTheme(createDefaultSelection());
    expect(wire.type).toBe('gradient');
    const zen = wire as SerializedZenTheme;
    expect(zen.gradientColors).toHaveLength(3);
    expect(zen.harmony).toBe('analogous');
  });

  it('first gradient color is marked isPrimary', () => {
    const wire = selectionToWireTheme(createDefaultSelection()) as SerializedZenTheme;
    expect(wire.gradientColors[0].isPrimary).toBe(true);
    expect(wire.gradientColors[1].isPrimary).toBe(false);
    expect(wire.gradientColors[2].isPrimary).toBe(false);
  });

  it('wire type is gradient', () => {
    const wire = selectionToWireTheme(createDefaultSelection());
    expect(wire.type).toBe('gradient');
  });

  it('wire stops have c: [r,g,b] integers', () => {
    const wire = selectionToWireTheme(createDefaultSelection()) as SerializedZenTheme;
    for (const stop of wire.gradientColors) {
      expect(stop.c).toBeDefined();
      expect(stop.c).toHaveLength(3);
      for (const v of stop.c!) {
        expect(Number.isInteger(v)).toBe(true);
        expect(v).toBeGreaterThanOrEqual(0);
        expect(v).toBeLessThanOrEqual(255);
      }
    }
  });

  it('wire stops have position {x,y}', () => {
    const wire = selectionToWireTheme(createDefaultSelection()) as SerializedZenTheme;
    for (const stop of wire.gradientColors) {
      expect(stop.position).toBeDefined();
      expect(typeof stop.position!.x).toBe('number');
      expect(typeof stop.position!.y).toBe('number');
    }
  });

  it('wire opacity in [0.25, 0.9]', () => {
    const wire = selectionToWireTheme(createDefaultSelection()) as SerializedZenTheme;
    expect(wire.opacity).toBeGreaterThanOrEqual(OPACITY_MIN);
    expect(wire.opacity).toBeLessThanOrEqual(OPACITY_MAX);
  });

  it('wire backward compat: hue/saturation/brightness still present per stop', () => {
    const wire = selectionToWireTheme(createDefaultSelection()) as SerializedZenTheme;
    for (const stop of wire.gradientColors) {
      expect(typeof stop.hue).toBe('number');
      expect(typeof stop.saturation).toBe('number');
      expect(typeof stop.brightness).toBe('number');
    }
  });

  it('primary stop has algorithm and lightness', () => {
    const wire = selectionToWireTheme(createDefaultSelection()) as SerializedZenTheme;
    const primary = wire.gradientColors.find(s => s.isPrimary)!;
    expect(primary.algorithm).toBe('analogous');
    expect(typeof primary.lightness).toBe('number');
  });

  it('serializes provided stop hues unchanged', () => {
    const stops = stopsForPreset(7);
    stops[0].hue = 0.58;
    stops[1].hue = 0.441;
    stops[2].hue = 0.719;
    const sel = {
      mode: 'sparkle' as const,
      preset_index: 7,
      grain: 0,
      opacity: 0.5,
      harmony: 'analogous' as const,
      stops,
    };
    const wire = selectionToWireTheme(sel) as SerializedZenTheme;
    const primary = wire.gradientColors[0];
    expect(primary.hue).toBeCloseTo(0.58);
    const secondaryHues = [wire.gradientColors[1].hue, wire.gradientColors[2].hue].sort();
    expect(secondaryHues[0]).toBeCloseTo(0.441);
    expect(secondaryHues[1]).toBeCloseTo(0.719);
  });
});

describe('selectionToWireTheme — sun mode', () => {
  it('returns gradient theme with scheme=light', () => {
    const stops = stopsForPreset(3);
    const wire = selectionToWireTheme({
      mode: 'sun',
      preset_index: 3,
      grain: 0,
      opacity: 0.5,
      harmony: 'analogous',
      stops,
    });
    expect(wire.type).toBe('gradient');
    expect((wire as SerializedZenTheme).scheme).toBe('light');
  });
});

describe('selectionToWireTheme — moon mode', () => {
  it('returns gradient theme with scheme=dark', () => {
    const stops = stopsForPreset(3);
    const wire = selectionToWireTheme({
      mode: 'moon',
      preset_index: 3,
      grain: 0,
      opacity: 0.5,
      harmony: 'analogous',
      stops,
    });
    expect(wire.type).toBe('gradient');
    expect((wire as SerializedZenTheme).scheme).toBe('dark');
  });
});

describe('selectionToWireTheme — sparkle mode', () => {
  it('returns gradient theme with scheme=auto', () => {
    const wire = selectionToWireTheme(createDefaultSelection());
    expect(wire.type).toBe('gradient');
    expect((wire as SerializedZenTheme).scheme).toBe('auto');
  });
});

describe('wireThemeToSelection — zen/gradient', () => {
  it('round-trips a valid zen theme to sparkle mode with primary stop', () => {
    const wire = JSON.stringify({
      type: 'zen',
      gradientColors: [
        {hue: 0.58, saturation: 0.80, brightness: 0.50, isCustom: false, isPrimary: true},
        {hue: 0.441, saturation: 0.80, brightness: 0.50, isCustom: false, isPrimary: false},
        {hue: 0.719, saturation: 0.80, brightness: 0.50, isCustom: false, isPrimary: false},
      ],
      harmony: 'analogous',
      opacity: 0.5,
      texture: 0,
    });
    const result = wireThemeToSelection(wire);
    expect(result.loading).toBe(false);
    expect(result.selection.mode).toBe('sparkle');
    expect(result.selection.stops).toHaveLength(3);
    const primary = result.selection.stops.find(s => s.isPrimary);
    expect(primary?.hue).toBeCloseTo(0.58);
    expect(primary?.saturation).toBeCloseTo(0.80);
    expect(result.selection.preset_index).toBe(7);
    expect(primary?.lightness).toBe(50);
  });

  it('falls back to first stop if no isPrimary marker', () => {
    const wire = JSON.stringify({
      type: 'zen',
      gradientColors: [
        {hue: 0.35, saturation: 0.80, brightness: 0.90},
      ],
      harmony: 'analogous',
      opacity: 0.5,
      texture: 0,
    });
    const result = wireThemeToSelection(wire);
    expect(result.selection.mode).toBe('sparkle');
    const primary = result.selection.stops.find(s => s.isPrimary);
    expect(primary?.hue).toBeCloseTo(0.35);
  });

  it('accepts type gradient', () => {
    const wire = JSON.stringify({
      type: 'gradient',
      gradientColors: [
        {hue: 0.35, saturation: 0.80, brightness: 0.90, isPrimary: true},
      ],
      harmony: 'triadic',
      opacity: 0.7,
      texture: 0.25,
    });
    const result = wireThemeToSelection(wire);
    expect(result.selection.harmony).toBe('triadic');
    expect(result.selection.opacity).toBe(0.7);
  });

  it('parses c array when present', () => {
    const wire = JSON.stringify({
      type: 'gradient',
      gradientColors: [
        {c: [255, 0, 0], isPrimary: true, position: {x: 200, y: 190}},
        {c: [0, 255, 0], isPrimary: false, position: {x: 100, y: 100}},
        {c: [0, 0, 255], isPrimary: false, position: {x: 300, y: 300}},
      ],
      harmony: 'analogous',
      opacity: 0.5,
      texture: 0,
    });
    const result = wireThemeToSelection(wire);
    const primary = result.selection.stops.find(s => s.isPrimary)!;
    expect(primary.hue).toBeCloseTo(0, 1);
    expect(primary.saturation).toBeCloseTo(1, 1);
  });
});

describe('wireThemeToSelection — solid', () => {
  it('decodes solid to sparkle mode (no scheme info)', () => {
    const wire = JSON.stringify({
      type: 'solid',
      color: {hue: 0.58, saturation: 0.80, brightness: 0.95, grain: 0},
    });
    const result = wireThemeToSelection(wire);
    expect(result.selection.mode).toBe('sparkle');
  });

  it('snaps to nearest preset by hue', () => {
    const wire = JSON.stringify({
      type: 'solid',
      color: {hue: 0.345, saturation: 0.80, brightness: 0.90, grain: 0},
    });
    expect(wireThemeToSelection(wire).selection.preset_index).toBe(6);
  });

  it('handles hue in degrees (>1) by dividing by 360', () => {
    const wire = JSON.stringify({
      type: 'solid',
      color: {hue: 209.0, saturation: 0.80, brightness: 0.90, grain: 0},
    });
    expect(wireThemeToSelection(wire).selection.preset_index).toBe(7);
  });
});

describe('wireThemeToSelection — zen with scheme', () => {
  it('infers sun mode from scheme=light', () => {
    const wire = JSON.stringify({
      type: 'zen',
      gradientColors: [
        {hue: 0.58, saturation: 0.80, brightness: 0.50, isPrimary: true},
      ],
      harmony: 'analogous',
      opacity: 0.5,
      texture: 0,
      scheme: 'light',
    });
    expect(wireThemeToSelection(wire).selection.mode).toBe('sun');
  });

  it('infers moon mode from scheme=dark', () => {
    const wire = JSON.stringify({
      type: 'zen',
      gradientColors: [
        {hue: 0.58, saturation: 0.80, brightness: 0.30, isPrimary: true},
      ],
      harmony: 'analogous',
      opacity: 0.5,
      texture: 0,
      scheme: 'dark',
    });
    expect(wireThemeToSelection(wire).selection.mode).toBe('moon');
  });

  it('defaults to sparkle when scheme missing or auto', () => {
    const wire = JSON.stringify({
      type: 'zen',
      gradientColors: [
        {hue: 0.58, saturation: 0.80, brightness: 0.50, isPrimary: true},
      ],
      harmony: 'analogous',
      opacity: 0.5,
      texture: 0,
    });
    expect(wireThemeToSelection(wire).selection.mode).toBe('sparkle');
  });
});

describe('wireThemeToSelection — fallbacks', () => {
  it('returns default state on garbage JSON', () => {
    expect(wireThemeToSelection('this-is-not-json').selection).toEqual(createDefaultSelection());
  });
  it('returns default state when type is missing', () => {
    expect(wireThemeToSelection(JSON.stringify({color: {}})).selection).toEqual(createDefaultSelection());
  });
  it('returns default state when undefined', () => {
    expect(wireThemeToSelection(undefined).selection).toEqual(createDefaultSelection());
  });
  it('returns default state when null', () => {
    expect(wireThemeToSelection(null).selection).toEqual(createDefaultSelection());
  });
  it('returns default state on empty string', () => {
    expect(wireThemeToSelection('').selection).toEqual(createDefaultSelection());
  });
});

describe('serializeThemeSelection', () => {
  it('produces parseable JSON for gradient mode', () => {
    const json = serializeThemeSelection(createDefaultSelection());
    const parsed = JSON.parse(json);
    expect(parsed.type).toBe('gradient');
    expect(Array.isArray(parsed.gradientColors)).toBe(true);
  });

  it('produces parseable JSON for sun mode (gradient, scheme=light)', () => {
    const stops = stopsForPreset(3);
    const sel = {
      mode: 'sun' as const,
      preset_index: 3,
      grain: 0,
      opacity: 0.5,
      harmony: 'analogous' as const,
      stops,
    };
    const json = serializeThemeSelection(sel);
    const parsed = JSON.parse(json);
    expect(parsed.type).toBe('gradient');
    expect(parsed.scheme).toBe('light');
  });

  it('round-trips a sparkle selection back to sparkle mode', () => {
    const stops = stopsForPreset(5);
    const original = {
      mode: 'sparkle' as const,
      preset_index: 5,
      grain: 0,
      opacity: 0.6,
      harmony: 'analogous' as const,
      stops,
    };
    const json = serializeThemeSelection(original);
    const recovered = parseThemeSelection(json);
    expect(recovered.mode).toBe('sparkle');
    expect(recovered.preset_index).toBe(5);
    const primary = recovered.stops.find(s => s.isPrimary);
    expect(primary?.hue).toBeCloseTo(0.15);
  });
});

describe('wire round-trip', () => {
  it('positions preserved through serialize → parse', () => {
    const sel = createDefaultSelection();
    sel.stops[0].position = {x: 250, y: 150};
    sel.stops[1].position = {x: 100, y: 200};
    sel.stops[2].position = {x: 300, y: 300};
    const json = serializeThemeSelection(sel);
    const recovered = parseThemeSelection(json);
    expect(recovered.stops[0].position.x).toBeCloseTo(250, 0);
    expect(recovered.stops[0].position.y).toBeCloseTo(150, 0);
    expect(recovered.stops[1].position.x).toBeCloseTo(100, 0);
    expect(recovered.stops[1].position.y).toBeCloseTo(200, 0);
  });

  it('harmony preserved through round-trip', () => {
    const sel = createDefaultSelection();
    sel.harmony = 'triadic';
    const json = serializeThemeSelection(sel);
    const recovered = parseThemeSelection(json);
    expect(recovered.harmony).toBe('triadic');
  });

  it('opacity preserved through round-trip', () => {
    const sel = createDefaultSelection();
    sel.opacity = 0.75;
    const json = serializeThemeSelection(sel);
    const recovered = parseThemeSelection(json);
    expect(recovered.opacity).toBeCloseTo(0.75);
  });
});

describe('harmonyDotsForSelection', () => {
  it('produces 3 dots with one primary', () => {
    const dots = harmonyDotsForSelection(createDefaultSelection());
    expect(dots).toHaveLength(3);
    expect(dots.filter(d => d.isPrimary)).toHaveLength(1);
  });

  it('reflects each stop hue independently', () => {
    const stops = stopsForPreset(7);
    stops[0].hue = 0.58;
    stops[1].hue = 0.10;
    stops[1].saturation = 0.50;
    stops[2].hue = 0.30;
    stops[2].saturation = 0.30;
    const dots = harmonyDotsForSelection({
      mode: 'sparkle',
      preset_index: 7,
      grain: 0,
      opacity: 0.5,
      harmony: 'analogous',
      stops,
    });
    const primary = dots.find(d => d.isPrimary);
    expect(primary?.hue).toBeCloseTo(0.58);
    expect(dots[1].hue).toBeCloseTo(0.10);
    expect(dots[2].hue).toBeCloseTo(0.30);
  });
});

describe('createInitialState', () => {
  it('starts in loading state with default selection', () => {
    const state = createInitialState();
    expect(state.loading).toBe(true);
    expect(state.selection).toEqual(createDefaultSelection());
  });
});

describe('createDefaultSelection', () => {
  it('has opacity 0.5 and harmony analogous', () => {
    const sel = createDefaultSelection();
    expect(sel.opacity).toBe(0.5);
    expect(sel.harmony).toBe('analogous');
  });

  it('stops have position and lightness', () => {
    const sel = createDefaultSelection();
    for (const stop of sel.stops) {
      expect(typeof stop.position.x).toBe('number');
      expect(typeof stop.position.y).toBe('number');
      expect(typeof stop.lightness).toBe('number');
    }
  });
});

describe('recomputeSecondaryPositions', () => {
  const geom = DEFAULT_CANVAS_GEOMETRY;

  it('analogous → 2 secondaries at +50 and +310 deg from primary', () => {
    const primary: ZenStop = {
      hue: 0,
      saturation: 0.9,
      isPrimary: true,
      position: positionForHueAndDistance(0, 0.7, geom),
      lightness: 85,
    };
    const secondaries = recomputeSecondaryPositions(primary, 'analogous', geom);
    expect(secondaries).toHaveLength(2);
    expect(secondaries[0].hue).toBeCloseTo(normalizeHue(50 / 360), 2);
    expect(secondaries[1].hue).toBeCloseTo(normalizeHue(310 / 360), 2);
  });

  it('complementary → 1 secondary at +180 deg', () => {
    const primary: ZenStop = {
      hue: 0.25,
      saturation: 0.9,
      isPrimary: true,
      position: positionForHueAndDistance(0.25, 0.7, geom),
      lightness: 85,
    };
    const secondaries = recomputeSecondaryPositions(primary, 'complementary', geom);
    expect(secondaries).toHaveLength(1);
  });

  it('floating → 0 secondaries', () => {
    const primary: ZenStop = {
      hue: 0,
      saturation: 0.9,
      isPrimary: true,
      position: positionForHueAndDistance(0, 0.7, geom),
      lightness: 85,
    };
    const secondaries = recomputeSecondaryPositions(primary, 'floating', geom);
    expect(secondaries).toHaveLength(0);
  });

  it('preserves saturation and lightness from primary', () => {
    const primary: ZenStop = {
      hue: 0.5,
      saturation: 0.85,
      isPrimary: true,
      position: positionForHueAndDistance(0.5, 0.6, geom),
      lightness: 70,
    };
    const secondaries = recomputeSecondaryPositions(primary, 'triadic', geom);
    for (const s of secondaries) {
      expect(s.saturation).toBe(0.85);
      expect(s.lightness).toBe(70);
    }
  });
});

describe('ZEN_PRESETS', () => {
  it('has 9 entries', () => {
    expect(ZEN_PRESETS).toHaveLength(9);
  });

  it('each preset has required fields', () => {
    for (const preset of ZEN_PRESETS) {
      expect(typeof preset.id).toBe('string');
      expect(preset.algorithm).toBe('analogous');
      expect(preset.numDots).toBe(3);
      expect(typeof preset.primaryPosition.x).toBe('number');
      expect(typeof preset.primaryPosition.y).toBe('number');
      expect(preset.colors).toHaveLength(1);
      expect(preset.colors[0].r).toBeGreaterThanOrEqual(0);
      expect(preset.colors[0].r).toBeLessThanOrEqual(255);
    }
  });
});

describe('wireThemeToSelection — opacity clamping', () => {
  it('clamps low opacity to 0.25', () => {
    const wire = JSON.stringify({
      type: 'gradient',
      gradientColors: [
        {hue: 0.5, saturation: 0.8, brightness: 0.9, isPrimary: true},
      ],
      harmony: 'analogous',
      opacity: 0.1,
      texture: 0,
    });
    const result = wireThemeToSelection(wire);
    expect(result.selection.opacity).toBe(0.25);
  });

  it('clamps high opacity to 0.9', () => {
    const wire = JSON.stringify({
      type: 'gradient',
      gradientColors: [
        {hue: 0.5, saturation: 0.8, brightness: 0.9, isPrimary: true},
      ],
      harmony: 'analogous',
      opacity: 1.0,
      texture: 0,
    });
    const result = wireThemeToSelection(wire);
    expect(result.selection.opacity).toBe(0.9);
  });
});
