import {
  getActiveDotIndex,
  getAddressIndex,
  getBodyTransform,
  getCarouselOffset,
  getContentBlend,
  getThemeBlend,
  type WorkspaceIndex,
} from "../spaces-carousel";

type Matcher = {
  readonly toBe: (expected: unknown) => void;
  readonly toEqual: (expected: unknown) => void;
  readonly toBeCloseTo: (expected: number, precision?: number) => void;
  readonly toBeGreaterThan: (expected: number) => void;
};

declare function describe(name: string, callback: () => void): void;
declare function test(name: string, callback: () => void): void;
declare function expect(value: unknown): Matcher;

const ANCHORS = [0.1, 0.3, 0.36, 0.51, 0.57, 0.72, 0.78, 0.9] as const;
const WORKSPACE_INDICES = [0, 1, 2, 3] as const satisfies readonly WorkspaceIndex[];

describe("Spaces carousel motion", () => {
  test("slides workspace bodies across the four recorded spaces", () => {
    expect(getCarouselOffset(0.1)).toBe(0);
    expect(getCarouselOffset(0.3)).toBeCloseTo(0.5);
    expect(getCarouselOffset(0.36)).toBe(1);
    expect(getCarouselOffset(0.51)).toBeCloseTo(1.5);
    expect(getCarouselOffset(0.57)).toBe(2);
    expect(getCarouselOffset(0.72)).toBeCloseTo(2.5);
    expect(getCarouselOffset(0.78)).toBe(3);

    expect(getBodyTransform(0, 0.3).translateXPct).toBeCloseTo(-50);
    expect(getBodyTransform(0, 0.3).opacity).toBeCloseTo(0.5);
    expect(getBodyTransform(1, 0.3).translateXPct).toBeCloseTo(50);
    expect(getBodyTransform(1, 0.3).opacity).toBeCloseTo(0.5);
    expect(getBodyTransform(3, 0.78)).toEqual({ translateXPct: 0, opacity: 1 });
  });

  test("crossfades themes and content without blank coverage", () => {
    expect(getThemeBlend(0.1)).toEqual([1, 0, 0, 0]);
    expect(getThemeBlend(0.305)[0]).toBeCloseTo(0.5);
    expect(getThemeBlend(0.305)[1]).toBeCloseTo(0.5);
    expect(getThemeBlend(0.45)).toEqual([0, 1, 0, 0]);
    expect(getThemeBlend(0.515)[1]).toBeCloseTo(0.5);
    expect(getThemeBlend(0.515)[2]).toBeCloseTo(0.5);
    expect(getThemeBlend(0.725)[2]).toBeCloseTo(0.5);
    expect(getThemeBlend(0.725)[3]).toBeCloseTo(0.5);

    expect(getContentBlend(0.1)).toEqual([1, 0, 0, 0]);
    expect(getContentBlend(0.31)[0]).toBeCloseTo(0.5);
    expect(getContentBlend(0.31)[1]).toBeCloseTo(0.5);
    expect(getContentBlend(0.46)).toEqual([0, 1, 0, 0]);
    expect(getContentBlend(0.52)[1]).toBeCloseTo(0.5);
    expect(getContentBlend(0.52)[2]).toBeCloseTo(0.5);
    expect(getContentBlend(0.73)[2]).toBeCloseTo(0.5);
    expect(getContentBlend(0.73)[3]).toBeCloseTo(0.5);

    for (const progress of ANCHORS) {
      for (const blend of [getThemeBlend(progress), getContentBlend(progress)]) {
        expect(blend.reduce((sum, opacity) => sum + opacity, 0)).toBeCloseTo(1, 10);
        expect(Math.max(...blend)).toBeGreaterThan(0);
        expect(blend.every((opacity) => opacity >= 0 && opacity <= 1)).toBe(true);
      }

      const bodyCoverage = WORKSPACE_INDICES.reduce<number>(
        (sum, index) => sum + getBodyTransform(index, progress).opacity,
        0,
      );
      expect(bodyCoverage).toBeCloseTo(1, 10);
    }
  });

  test("changes footer dots at slide ends and addresses at content dominance points", () => {
    expect(ANCHORS.map(getActiveDotIndex)).toEqual([0, 0, 1, 1, 2, 2, 3, 3]);
    expect(ANCHORS.map(getAddressIndex)).toEqual([0, 0, 1, 1, 2, 2, 3, 3]);
  });

  test("clamps NaN and out-of-range progress", () => {
    expect(getCarouselOffset(Number.NaN)).toBe(0);
    expect(getThemeBlend(Number.NaN)).toEqual([1, 0, 0, 0]);
    expect(getContentBlend(-1)).toEqual([1, 0, 0, 0]);
    expect(getCarouselOffset(2)).toBe(3);
    expect(getActiveDotIndex(2)).toBe(3);
    expect(getAddressIndex(2)).toBe(3);
  });
});
