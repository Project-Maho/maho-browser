import {
  InvalidSeedError,
  contrastRatio,
  deltaEOk,
  formatHex,
  formatRgba,
  gamutClampToSrgb,
  hueDelta,
  linearSrgbToOklab,
  oklabToOklch,
  parseSeed,
  srgbToLinearChannel,
} from "../oklch";

type Matcher = {
  readonly toBe: (expected: unknown) => void;
  readonly toEqual: (expected: unknown) => void;
  readonly toBeCloseTo: (expected: number, precision?: number) => void;
  readonly toBeGreaterThan: (expected: number) => void;
  readonly toBeLessThan: (expected: number) => void;
  readonly toThrow: (expected?: unknown) => void;
};

declare function describe(name: string, callback: () => void): void;
declare function test(name: string, callback: () => void): void;
declare function expect(value: unknown): Matcher;

describe("OKLCH Color Math Pipeline", () => {
  test("parseSeed validates and normalizes hex input", () => {
    expect(parseSeed("#b58ad8")).toEqual([181, 138, 216]);
    expect(parseSeed("b58ad8")).toEqual([181, 138, 216]);
    expect(parseSeed("#b5d")).toEqual([187, 85, 221]);
    expect(parseSeed("B58AD8")).toEqual([181, 138, 216]);

    expect(() => parseSeed("xyz")).toThrow(InvalidSeedError);
    expect(() => parseSeed("#12")).toThrow(InvalidSeedError);
    expect(() => parseSeed("")).toThrow(InvalidSeedError);
    expect(() => parseSeed("#1234567")).toThrow(InvalidSeedError);
  });

  test("#b58ad8 round-trips through sRGB -> OKLab -> OKLCH -> sRGB with <= 1 byte error", () => {
    const srgb = parseSeed("#b58ad8");
    const linRgb: [number, number, number] = [
      srgbToLinearChannel(srgb[0]),
      srgbToLinearChannel(srgb[1]),
      srgbToLinearChannel(srgb[2]),
    ];
    const lab = linearSrgbToOklab(linRgb);
    const lch = oklabToOklch(lab);
    const srgbClamped = gamutClampToSrgb(lch);
    const hex = formatHex(srgbClamped);

    expect(hex).toBe("#b58ad8");
  });

  test("contrastRatio matches WCAG 2.1 bounds", () => {
    const blackWhite = contrastRatio("#ffffff", "#000000");
    expect(blackWhite).toBeGreaterThan(20.5);
    expect(blackWhite).toBeLessThan(21.1);

    const same = contrastRatio("#b58ad8", "#b58ad8");
    expect(same).toBeCloseTo(1.0, 2);
  });

  test("deltaEOk measures color distance", () => {
    expect(deltaEOk("#b58ad8", "#b58ad8")).toBeCloseTo(0, 4);
    expect(deltaEOk("#ffffff", "#000000")).toBeGreaterThan(0.9);
  });

  test("hueDelta calculates minimum angular distance", () => {
    expect(hueDelta(10, 20)).toBe(10);
    expect(hueDelta(350, 10)).toBe(20);
    expect(hueDelta(0, 180)).toBe(180);
    expect(hueDelta(10, 200)).toBe(170);
  });

  test("formatRgba formats correct string", () => {
    expect(formatRgba([100, 150, 200], 0.5)).toBe("rgba(100, 150, 200, 0.5)");
  });
});
