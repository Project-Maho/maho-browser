import { readFileSync } from "fs";
import { join } from "path";
import { deriveMahoTheme } from "../deriveMahoTheme";
import {
  InvalidSeedError,
  contrastRatio,
  deltaEOk,
  hueDelta,
  linearSrgbToOklab,
  oklabToOklch,
  parseSeed,
  srgbToLinearChannel,
  type Vec3,
} from "../oklch";

type Matcher = {
  readonly toBe: (expected: unknown) => void;
  readonly toEqual: (expected: unknown) => void;
  readonly toBeGreaterThan: (expected: number) => void;
  readonly toBeLessThan: (expected: number) => void;
  readonly toBeInstanceOf: (expected: { readonly prototype: Error }) => void;
  readonly not: { readonly toContain: (expected: string) => void };
};

declare function describe(name: string, callback: () => void): void;
declare function test(name: string, callback: () => void): void;
declare function expect(value: unknown): Matcher;

function getLightness(hex: string): number {
  const srgb = parseSeed(hex);
  const lin: Vec3 = [
    srgbToLinearChannel(srgb[0]),
    srgbToLinearChannel(srgb[1]),
    srgbToLinearChannel(srgb[2]),
  ];
  return linearSrgbToOklab(lin)[0];
}

function getHue(hex: string): number {
  const srgb = parseSeed(hex);
  const lin: Vec3 = [
    srgbToLinearChannel(srgb[0]),
    srgbToLinearChannel(srgb[1]),
    srgbToLinearChannel(srgb[2]),
  ];
  return oklabToOklch(linearSrgbToOklab(lin))[2];
}

describe("deriveMahoTheme & Invariants", () => {
  test("throws InvalidSeedError instead of substituting a fallback seed", () => {
    let thrown: unknown;
    try {
      deriveMahoTheme("not-a-seed");
    } catch (error) {
      if (error instanceof InvalidSeedError) {
        thrown = error;
      } else {
        throw error;
      }
    }

    expect(thrown).toBeInstanceOf(InvalidSeedError);
  });

  test("INV-DET: determinism across multiple calls & stable snapshot", () => {
    const t1 = deriveMahoTheme("#b58ad8");
    const t2 = deriveMahoTheme("#b58ad8");
    expect(t1).toEqual(t2);

    expect(t1.window).toBe("#1d1127");
    expect(t1.sidebarTop).toBe("#412c52");
    expect(t1.sidebarBottom).toBe("#251730");
    expect(t1.chromeSurface).toBe("#50425c");
    expect(t1.textPrimary).toBe("#f2eef6");
  });

  test("INV-NODEP: deriveMahoTheme source has no forbidden color dependencies", () => {
    const code = readFileSync(join(__dirname, "../deriveMahoTheme.ts"), "utf-8");
    expect(code).not.toContain("culori");
    expect(code).not.toContain("chroma");
    expect(code).not.toContain("require('color')");
  });

  test("INV-ANCHOR: chromeSurface is within ΔE_OK <= 0.02 of measured anchor #554057", () => {
    const theme = deriveMahoTheme("#b58ad8");
    const dist = deltaEOk(theme.chromeSurface, "#554057");
    expect(dist).toBeLessThan(0.02);
  });

  test("INV-CONTRAST: readability contrast ratios are satisfied", () => {
    const theme = deriveMahoTheme("#b58ad8");
    expect(contrastRatio(theme.textPrimary, theme.window)).toBeGreaterThan(4.5);
    expect(contrastRatio(theme.textPrimary, theme.chromeSurface)).toBeGreaterThan(4.5);
    expect(contrastRatio(theme.textSecondary, theme.chromeSurface)).toBeGreaterThan(3.0);
    expect(contrastRatio(theme.textMuted, theme.window)).toBeGreaterThan(2.5);
  });

  test("INV-ORDER: monotonic lightness ordering of surfaces", () => {
    const theme = deriveMahoTheme("#b58ad8");
    const Lwin = getLightness(theme.window);
    const LsbBot = getLightness(theme.sidebarBottom);
    const LsbTop = getLightness(theme.sidebarTop);
    const Lchrome = getLightness(theme.chromeSurface);
    const Lactive = getLightness(theme.activeSurface);

    expect(Lwin < LsbBot).toBe(true);
    expect(LsbBot < LsbTop).toBe(true);
    expect(LsbTop < Lchrome).toBe(true);
    expect(Lchrome < Lactive).toBe(true);
  });

  test("INV-HUE: hue coherence within 8 degrees", () => {
    const seed = "#b58ad8";
    const seedHue = getHue(seed);
    const theme = deriveMahoTheme(seed);

    expect(hueDelta(getHue(theme.window), seedHue)).toBeLessThan(8);
    expect(hueDelta(getHue(theme.chromeSurface), seedHue)).toBeLessThan(8);
    expect(hueDelta(getHue(theme.accent), seedHue)).toBeLessThan(8);
  });

  test("INV-GAMUT & INV-SCENE over stress seed set", () => {
    const seeds = ["#000000", "#ffffff", "#ff0000", "#00ff00", "#0000ff", "#b58ad8", "#ff6f91", "#7fa879"];
    for (const seed of seeds) {
      const theme = deriveMahoTheme(seed);
      expect(Object.keys(theme).length).toBe(18);

      expect(/^#[0-9a-f]{6}$/.test(theme.window)).toBe(true);
      expect(/^#[0-9a-f]{6}$/.test(theme.chromeSurface)).toBe(true);
      expect(/^rgba\(\d+, \d+, \d+, (0|1|0?\.\d+)\)$/.test(theme.hoverSurface)).toBe(true);
    }
  });
});
