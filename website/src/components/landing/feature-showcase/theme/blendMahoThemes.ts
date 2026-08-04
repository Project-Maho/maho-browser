import type { BlendTriple } from "../spaces-carousel";
import {
  formatHex,
  linearSrgbToOklab,
  linearToSrgbChannel,
  oklabToLinearSrgb,
  parseSeed,
  srgbToLinearChannel,
  type Vec3,
} from "./oklch";
import type { MahoTheme } from "./tokens";

/**
 * Blend a hex color using OKLab interpolation with given weights.
 */
function blendHexInOklab(hexColors: readonly string[], weights: BlendTriple): string {
  let L = 0;
  let a = 0;
  let b = 0;

  for (let i = 0; i < hexColors.length; i++) {
    const w = weights[i];
    if (w <= 0) continue;
    const srgb = parseSeed(hexColors[i]);
    const lin: Vec3 = [
      srgbToLinearChannel(srgb[0]),
      srgbToLinearChannel(srgb[1]),
      srgbToLinearChannel(srgb[2]),
    ];
    const lab = linearSrgbToOklab(lin);
    L += lab[0] * w;
    a += lab[1] * w;
    b += lab[2] * w;
  }

  const blendedLin = oklabToLinearSrgb([L, a, b]);
  const srgbBytes: Vec3 = [
    linearToSrgbChannel(blendedLin[0]),
    linearToSrgbChannel(blendedLin[1]),
    linearToSrgbChannel(blendedLin[2]),
  ];
  return formatHex(srgbBytes);
}

export function blendSeedInOklab(seeds: readonly string[], weights: BlendTriple): string {
  return blendHexInOklab(seeds, weights);
}

/**
 * Blend multiple MahoTheme objects by given weights.
 * Performs perceptual OKLab blending on tint tokens (`sidebarTop`, `sidebarBottom`, `contentTint`),
 * and picks non-tint tokens from the dominant theme.
 */
export function blendMahoThemes(
  themes: readonly MahoTheme[],
  weights: BlendTriple,
): MahoTheme {
  let maxW = -1;
  let dominantIdx = 0;
  for (let i = 0; i < weights.length; i++) {
    if (weights[i] > maxW) {
      maxW = weights[i];
      dominantIdx = i;
    }
  }

  const dominantTheme = themes[dominantIdx] || themes[0];

  const blendedSidebarTop = blendHexInOklab(
    themes.map((theme) => theme.sidebarTop),
    weights,
  );
  const blendedSidebarBottom = blendHexInOklab(
    themes.map((theme) => theme.sidebarBottom),
    weights,
  );
  const blendedContentTint = blendHexInOklab(
    themes.map((theme) => theme.contentTint),
    weights,
  );

  const sidebarGradient = `linear-gradient(180deg, ${blendedSidebarTop}, ${blendedSidebarBottom})`;

  return {
    ...dominantTheme,
    sidebarTop: blendedSidebarTop,
    sidebarBottom: blendedSidebarBottom,
    sidebarGradient,
    contentTint: blendedContentTint,
  };
}
