export class InvalidSeedError extends Error {
  constructor(seed: string) {
    super(`Invalid color seed: "${seed}". Expected 3 or 6 hex digits with optional leading #.`);
    this.name = "InvalidSeedError";
  }
}

/**
 * Parse a hex seed string into sRGB bytes [0..255, 0..255, 0..255].
 * Accepts 3-digit or 6-digit hex, with or without leading '#'.
 * Case insensitive. Throws InvalidSeedError for any invalid input.
 */
export function parseSeed(input: string): [number, number, number] {
  if (typeof input !== "string") {
    throw new InvalidSeedError(String(input));
  }
  let clean = input.trim();
  if (clean.startsWith("#")) {
    clean = clean.slice(1);
  }
  if (!/^[0-9a-fA-F]{3}$|^[0-9a-fA-F]{6}$/.test(clean)) {
    throw new InvalidSeedError(input);
  }
  if (clean.length === 3) {
    clean = clean
      .split("")
      .map((c) => c + c)
      .join("");
  }
  const num = parseInt(clean, 16);
  const r = (num >> 16) & 0xff;
  const g = (num >> 8) & 0xff;
  const b = num & 0xff;
  return [r, g, b];
}

/**
 * Convert sRGB channel [0..255] to linear sRGB [0..1].
 */
export function srgbToLinearChannel(c: number): number {
  const norm = c / 255;
  return norm <= 0.04045 ? norm / 12.92 : Math.pow((norm + 0.055) / 1.055, 2.4);
}

/**
 * Convert linear sRGB channel [0..1] to sRGB channel [0..255].
 */
export function linearToSrgbChannel(c: number): number {
  const clamped = Math.max(0, Math.min(1, c));
  const srgb = clamped <= 0.0031308 ? clamped * 12.92 : 1.055 * Math.pow(clamped, 1 / 2.4) - 0.055;
  return Math.round(srgb * 255);
}

export type Vec3 = [number, number, number];

// Bjorn Ottosson OKLab matrices
// M1: linear sRGB -> LMS
const M1 = [
  [0.4122214708, 0.5363325363, 0.0514459929],
  [0.2119034982, 0.6806995451, 0.1073969566],
  [0.0883024619, 0.2817188376, 0.6299787005],
];

// M1_INV: LMS -> linear sRGB
const M1_INV = [
  [4.0767416621, -3.3077115913, 0.2309699292],
  [-1.2684380046, 2.6097574011, -0.3413193965],
  [-0.0041960863, -0.7034186147, 1.707614701],
];

// M2: LMS^1/3 -> OKLab (L, a, b)
const M2 = [
  [0.2104542553, 0.793617785, -0.0040720403],
  [1.9779984951, -2.428592205, 0.4505937099],
  [0.0259040371, 0.7827717662, -0.8086757986],
];

// M2_INV: OKLab -> LMS^1/3
const M2_INV = [
  [1.0, 0.3963377774, 0.2158037573],
  [1.0, -0.1055613458, -0.0638541728],
  [1.0, -0.0894841775, -1.291485548],
];

/**
 * Linear sRGB [0..1] -> OKLab [L, a, b]
 */
export function linearSrgbToOklab(rgb: Vec3): Vec3 {
  const l_ = M1[0][0] * rgb[0] + M1[0][1] * rgb[1] + M1[0][2] * rgb[2];
  const m_ = M1[1][0] * rgb[0] + M1[1][1] * rgb[1] + M1[1][2] * rgb[2];
  const s_ = M1[2][0] * rgb[0] + M1[2][1] * rgb[1] + M1[2][2] * rgb[2];

  const l = Math.cbrt(l_);
  const m = Math.cbrt(m_);
  const s = Math.cbrt(s_);

  return [
    M2[0][0] * l + M2[0][1] * m + M2[0][2] * s,
    M2[1][0] * l + M2[1][1] * m + M2[1][2] * s,
    M2[2][0] * l + M2[2][1] * m + M2[2][2] * s,
  ];
}

/**
 * OKLab [L, a, b] -> Linear sRGB [0..1]
 */
export function oklabToLinearSrgb(lab: Vec3): Vec3 {
  const l_ = M2_INV[0][0] * lab[0] + M2_INV[0][1] * lab[1] + M2_INV[0][2] * lab[2];
  const m_ = M2_INV[1][0] * lab[0] + M2_INV[1][1] * lab[1] + M2_INV[1][2] * lab[2];
  const s_ = M2_INV[2][0] * lab[0] + M2_INV[2][1] * lab[1] + M2_INV[2][2] * lab[2];

  const l = l_ * l_ * l_;
  const m = m_ * m_ * m_;
  const s = s_ * s_ * s_;

  return [
    M1_INV[0][0] * l + M1_INV[0][1] * m + M1_INV[0][2] * s,
    M1_INV[1][0] * l + M1_INV[1][1] * m + M1_INV[1][2] * s,
    M1_INV[2][0] * l + M1_INV[2][1] * m + M1_INV[2][2] * s,
  ];
}

/**
 * OKLab [L, a, b] -> OKLCH [L, C, h] (h in degrees 0..360)
 */
export function oklabToOklch(lab: Vec3): Vec3 {
  const [L, a, b] = lab;
  const C = Math.hypot(a, b);
  let h = (Math.atan2(b, a) * 180) / Math.PI;
  if (h < 0) {
    h += 360;
  }
  return [L, C, h];
}

/**
 * OKLCH [L, C, h] -> OKLab [L, a, b]
 */
export function oklchToOklab(lch: Vec3): Vec3 {
  const [L, C, h] = lch;
  const rad = (h * Math.PI) / 180;
  const a = C * Math.cos(rad);
  const b = C * Math.sin(rad);
  return [L, a, b];
}

/**
 * Check if linear sRGB [r, g, b] is within sRGB gamut [0..1]
 */
export function isInGamut(rgb: Vec3): boolean {
  const eps = 0.0001;
  return (
    rgb[0] >= -eps &&
    rgb[0] <= 1 + eps &&
    rgb[1] >= -eps &&
    rgb[1] <= 1 + eps &&
    rgb[2] >= -eps &&
    rgb[2] <= 1 + eps
  );
}

/**
 * Clamp OKLCH color to sRGB gamut by binary searching chroma (C) down at fixed L and h.
 */
export function gamutClampToSrgb(lch: Vec3): Vec3 {
  const [L, C, h] = lch;
  let linearRgb = oklabToLinearSrgb(oklchToOklab([L, C, h]));
  if (isInGamut(linearRgb)) {
    return [
      linearToSrgbChannel(linearRgb[0]),
      linearToSrgbChannel(linearRgb[1]),
      linearToSrgbChannel(linearRgb[2]),
    ];
  }

  let minC = 0;
  let maxC = C;
  let currentC = C;

  for (let i = 0; i < 16; i++) {
    currentC = (minC + maxC) / 2;
    linearRgb = oklabToLinearSrgb(oklchToOklab([L, currentC, h]));
    if (isInGamut(linearRgb)) {
      minC = currentC;
    } else {
      maxC = currentC;
    }
  }

  linearRgb = oklabToLinearSrgb(oklchToOklab([L, minC, h]));
  return [
    linearToSrgbChannel(linearRgb[0]),
    linearToSrgbChannel(linearRgb[1]),
    linearToSrgbChannel(linearRgb[2]),
  ];
}

/**
 * Format sRGB bytes [0..255] as hex string `#rrggbb` (lowercase).
 */
export function formatHex(rgb: Vec3): string {
  const r = Math.max(0, Math.min(255, Math.round(rgb[0]))).toString(16).padStart(2, "0");
  const g = Math.max(0, Math.min(255, Math.round(rgb[1]))).toString(16).padStart(2, "0");
  const b = Math.max(0, Math.min(255, Math.round(rgb[2]))).toString(16).padStart(2, "0");
  return `#${r}${g}${b}`;
}

/**
 * Format sRGB bytes [0..255] + alpha [0..1] as `rgba(r, g, b, a)`.
 */
export function formatRgba(rgb: Vec3, alpha: number): string {
  const r = Math.max(0, Math.min(255, Math.round(rgb[0])));
  const g = Math.max(0, Math.min(255, Math.round(rgb[1])));
  const b = Math.max(0, Math.min(255, Math.round(rgb[2])));
  const cleanAlpha = Math.round(alpha * 1000) / 1000;
  return `rgba(${r}, ${g}, ${b}, ${cleanAlpha})`;
}

/**
 * Calculate sRGB relative luminance per WCAG 2.1.
 */
export function relativeLuminance(rgb: Vec3): number {
  const r = srgbToLinearChannel(rgb[0]);
  const g = srgbToLinearChannel(rgb[1]);
  const b = srgbToLinearChannel(rgb[2]);
  return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

/**
 * Calculate WCAG 2.1 contrast ratio between two colors (hex or sRGB bytes).
 */
export function contrastRatio(colorA: string | Vec3, colorB: string | Vec3): number {
  const rgbA = typeof colorA === "string" ? parseSeed(colorA) : colorA;
  const rgbB = typeof colorB === "string" ? parseSeed(colorB) : colorB;
  const lumA = relativeLuminance(rgbA);
  const lumB = relativeLuminance(rgbB);
  const l1 = Math.max(lumA, lumB);
  const l2 = Math.min(lumA, lumB);
  return (l1 + 0.05) / (l2 + 0.05);
}

/**
 * Calculate Delta E in OKLab space (Euclidean distance between two OKLab colors).
 */
export function deltaEOk(colorA: string | Vec3, colorB: string | Vec3): number {
  const rgbA = typeof colorA === "string" ? parseSeed(colorA) : colorA;
  const rgbB = typeof colorB === "string" ? parseSeed(colorB) : colorB;

  const linA: Vec3 = [
    srgbToLinearChannel(rgbA[0]),
    srgbToLinearChannel(rgbA[1]),
    srgbToLinearChannel(rgbA[2]),
  ];
  const linB: Vec3 = [
    srgbToLinearChannel(rgbB[0]),
    srgbToLinearChannel(rgbB[1]),
    srgbToLinearChannel(rgbB[2]),
  ];

  const labA = linearSrgbToOklab(linA);
  const labB = linearSrgbToOklab(linB);

  return Math.hypot(labA[0] - labB[0], labA[1] - labB[1], labA[2] - labB[2]);
}

/**
 * Calculate absolute minimum difference between two hue angles in degrees (0..180).
 */
export function hueDelta(h1: number, h2: number): number {
  const diff = Math.abs(h1 - h2) % 360;
  return diff > 180 ? 360 - diff : diff;
}
