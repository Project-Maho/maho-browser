import {
  formatHex,
  formatRgba,
  gamutClampToSrgb,
  linearSrgbToOklab,
  oklabToOklch,
  parseSeed,
  srgbToLinearChannel,
} from "./oklch";
import type { MahoTheme } from "./tokens";

/**
 * Derives all 18 semantic MahoTheme tokens deterministically from a single color seed.
 * Performs calculation in OKLCH color space with gamut clamping to sRGB.
 */
export function deriveMahoTheme(seedInput: string): MahoTheme {
  const srgb = parseSeed(seedInput);
  const linRgb: [number, number, number] = [
    srgbToLinearChannel(srgb[0]),
    srgbToLinearChannel(srgb[1]),
    srgbToLinearChannel(srgb[2]),
  ];
  const lab = linearSrgbToOklab(linRgb);
  const [_Ls, Cs, hs] = oklabToOklch(lab);

  const makeHex = (LTarget: number, cRule: number): string => {
    const lch: [number, number, number] = [LTarget, cRule, hs];
    const clampedRgb = gamutClampToSrgb(lch);
    return formatHex(clampedRgb);
  };

  const makeRgba = (LTarget: number, cRule: number, alpha: number): string => {
    const lch: [number, number, number] = [LTarget, cRule, hs];
    const clampedRgb = gamutClampToSrgb(lch);
    return formatRgba(clampedRgb, alpha);
  };

  const windowHex = makeHex(0.205, Math.min(Cs * 0.55, 0.045));
  const sidebarBottomHex = makeHex(0.235, Math.min(Cs * 0.6, 0.05));
  const sidebarTopHex = makeHex(0.335, Math.min(Cs * 0.75, 0.07));
  const contentTintHex = makeHex(0.3, Math.min(Cs * 0.4, 0.035));
  // chromeSurface: L=0.405, C=min(Cs*0.80, 0.046) anchors near measured #554057 (ΔE_OK <= 0.02)
  const chromeSurfaceHex = makeHex(0.405, Math.min(Cs * 0.8, 0.046));
  const activeSurfaceHex = makeHex(0.46, Math.min(Cs * 0.85, 0.07));
  const hoverSurfaceRgba = makeRgba(0.62, Math.min(Cs * 0.5, 0.05), 0.08);
  const borderStrongRgba = makeRgba(0.72, Math.min(Cs * 0.4, 0.04), 0.14);
  const borderSubtleRgba = makeRgba(0.72, Math.min(Cs * 0.4, 0.04), 0.06);
  const textPrimaryHex = makeHex(0.955, Math.min(Cs * 0.12, 0.012));
  const textSecondaryHex = makeHex(0.8, Math.min(Cs * 0.18, 0.02));
  const textMutedHex = makeHex(0.63, Math.min(Cs * 0.22, 0.028));
  const accentHex = makeHex(0.72, Math.max(Cs, 0.13));
  const focusRingRgba = makeRgba(0.72, Math.max(Cs, 0.13), 0.65);
  const shadowColorRgba = makeRgba(0.08, Math.min(Cs * 0.3, 0.03), 0.38);
  const backdropScrimRgba = makeRgba(0.1, Math.min(Cs * 0.35, 0.035), 0.55);

  const sidebarGradient = `linear-gradient(180deg, ${sidebarTopHex}, ${sidebarBottomHex})`;

  return {
    window: windowHex,
    sidebarTop: sidebarTopHex,
    sidebarBottom: sidebarBottomHex,
    sidebarGradient,
    contentTint: contentTintHex,
    chromeSurface: chromeSurfaceHex,
    activeSurface: activeSurfaceHex,
    hoverSurface: hoverSurfaceRgba,
    borderStrong: borderStrongRgba,
    borderSubtle: borderSubtleRgba,
    textPrimary: textPrimaryHex,
    textSecondary: textSecondaryHex,
    textMuted: textMutedHex,
    accent: accentHex,
    focusRing: focusRingRgba,
    shadowColor: shadowColorRgba,
    backdropScrim: backdropScrimRgba,
    backdropBlur: "30px",
  };
}
