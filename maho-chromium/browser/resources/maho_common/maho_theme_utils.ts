export interface SpaceColor {
  hue: number;
  saturation: number;
  brightness: number;
  grain: number;
}

export interface ThemeColor {
  hue: number;
  saturation: number;
  brightness: number;
  isCustom: boolean;
  isPrimary: boolean;
}

export type ColorHarmony =
    'complementary'|'single_analogous'|'split_complementary'|'analogous'|
    'triadic'|'custom';

export interface SolidTheme {
  type: 'solid';
  color: SpaceColor;
}

export interface GradientTheme {
  type: 'gradient';
  gradientColors: ThemeColor[];
  harmony: ColorHarmony;
  opacity: number;
  texture: number;
}

export interface ZenTheme {
  type: 'zen';
  gradientColors: ThemeColor[];
  harmony: ColorHarmony;
  opacity: number;
  texture: number;
}

export type SpaceThemeData = SolidTheme|GradientTheme|ZenTheme;

export const ZEN_THEME_HARMONY: ColorHarmony = 'analogous';

export interface ColorPreset {
  title: string;
  hue: number;
  saturation: number;
  brightness: number;
}

// The standalone create/config WebUI surfaces mirror the broader Swift sheet
// palette, while the sidebar uses a tighter, higher-contrast preset set that
// better fits the compact strip/editor footprint. Keep both arrays explicit so
// each surface can preserve its intended visual tuning without drifting ad hoc.
export const SPACE_THEME_COLOR_PRESETS: ColorPreset[] = [
  {title: 'Cream', hue: 0.10, saturation: 0.15, brightness: 0.95},
  {title: 'Pink', hue: 0.92, saturation: 0.80, brightness: 0.90},
  {title: 'Purple', hue: 0.75, saturation: 0.80, brightness: 0.90},
  {title: 'Red', hue: 0.00, saturation: 0.80, brightness: 0.90},
  {title: 'Orange', hue: 0.08, saturation: 0.80, brightness: 0.90},
  {title: 'Yellow', hue: 0.15, saturation: 0.80, brightness: 0.90},
  {title: 'Green', hue: 0.35, saturation: 0.80, brightness: 0.90},
  {title: 'Blue', hue: 0.58, saturation: 0.80, brightness: 0.90},
  {title: 'Indigo', hue: 0.62, saturation: 0.30, brightness: 0.55},
];

export const SIDEBAR_THEME_COLOR_PRESETS: ColorPreset[] = [
  {title: 'Coral', hue: 0.00, saturation: 0.62, brightness: 0.95},
  {title: 'Orange', hue: 0.07, saturation: 0.60, brightness: 0.95},
  {title: 'Gold', hue: 0.12, saturation: 0.68, brightness: 0.97},
  {title: 'Green', hue: 0.38, saturation: 0.52, brightness: 0.77},
  {title: 'Cyan', hue: 0.52, saturation: 0.60, brightness: 0.85},
  {title: 'Blue', hue: 0.62, saturation: 0.63, brightness: 1.0},
  {title: 'Purple', hue: 0.72, saturation: 0.52, brightness: 0.96},
  {title: 'Pink', hue: 0.90, saturation: 0.58, brightness: 0.90},
];

export function normalizeHue(hue: number): number {
  return hue - Math.floor(hue);
}

export function computeHarmonyColors(
    color: SpaceColor, harmony: ColorHarmony): ThemeColor[] {
  const primary: ThemeColor = {
    hue: normalizeHue(color.hue),
    saturation: color.saturation,
    brightness: color.brightness,
    isCustom: harmony === 'custom',
    isPrimary: true,
  };
  const buildColor = (hueOffset: number): ThemeColor => ({
    hue: normalizeHue(color.hue + hueOffset),
    saturation: color.saturation,
    brightness: color.brightness,
    isCustom: false,
    isPrimary: false,
  });
  switch (harmony) {
    case 'complementary':
      return [primary, buildColor(0.5)];
    case 'single_analogous':
      return [primary, buildColor(310 / 360)];
    case 'split_complementary':
      return [primary, buildColor(0.417), buildColor(0.583)];
    case 'analogous':
      return [primary, buildColor(-50 / 360), buildColor(50 / 360)];
    case 'triadic':
      return [primary, buildColor(0.333), buildColor(0.667)];
    case 'custom':
      return [primary];
  }
}

export function buildZenTheme(spaceColor: SpaceColor, texture: number): ZenTheme {
  return {
    type: 'zen',
    gradientColors: computeHarmonyColors(spaceColor, ZEN_THEME_HARMONY),
    harmony: ZEN_THEME_HARMONY,
    opacity: 1,
    texture,
  };
}

export function hsbToRgb(h: number, s: number, b: number): [number, number, number] {
  if (s <= 0) {
    return [b * 255, b * 255, b * 255];
  }
  const hWrapped = h - Math.floor(h);
  const h6 = hWrapped * 6;
  const sector = Math.floor(h6) % 6;
  const f = h6 - Math.floor(h6);
  const p = b * (1 - s);
  const q = b * (1 - s * f);
  const t = b * (1 - s * (1 - f));
  const pairs: [number, number, number][] = [
    [b, t, p], [q, b, p], [p, b, t], [p, q, b], [t, p, b], [b, p, q],
  ];
  const [r, g, bb] = pairs[sector]!;
  return [r * 255, g * 255, bb * 255];
}

export function hsbToHsl(
    h: number, s: number, b: number): {h: number; s: number; l: number} {
  const l = b * (1 - s / 2);
  const sl = l === 0 || l === 1 ? 0 : (b - l) / Math.min(l, 1 - l);
  return {
    h: Math.round(h * 360),
    s: Math.round(sl * 100),
    l: Math.round(l * 100),
  };
}

export function hsbToCss(h: number, s: number, b: number): string {
  const hsl = hsbToHsl(h, s, b);
  return `hsl(${hsl.h}, ${hsl.s}%, ${hsl.l}%)`;
}

export function hsbToCssAlpha(
    h: number, s: number, b: number, alpha: number): string {
  const hsl = hsbToHsl(h, s, b);
  return `hsla(${hsl.h}, ${hsl.s}%, ${hsl.l}%, ${alpha})`;
}

export function hsbToHex(h: number, s: number, b: number): string {
  const [r, g, bb] = hsbToRgb(h, s, b);
  const toHex = (value: number) => {
    return Math.round(Math.min(255, Math.max(0, value))).toString(16).padStart(2, '0');
  };
  return `#${toHex(r)}${toHex(g)}${toHex(bb)}`;
}

export function cssToHex(cssColor: string): string {
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

export function hexToHsb(hex: string): SpaceColor|null {
  const match = hex.match(/^#?([0-9a-fA-F]{6})$/);
  if (!match) {
    return null;
  }
  const r = parseInt(match[1]!.substring(0, 2), 16) / 255;
  const g = parseInt(match[1]!.substring(2, 4), 16) / 255;
  const b = parseInt(match[1]!.substring(4, 6), 16) / 255;
  const max = Math.max(r, g, b);
  const min = Math.min(r, g, b);
  const delta = max - min;
  let h = 0;
  if (delta > 0) {
    if (max === r) {
      h = ((g - b) / delta + 6) % 6 / 6;
    } else if (max === g) {
      h = ((b - r) / delta + 2) / 6;
    } else {
      h = ((r - g) / delta + 4) / 6;
    }
  }
  const s = max > 0 ? delta / max : 0;
  return {hue: h, saturation: s, brightness: max, grain: 0};
}
