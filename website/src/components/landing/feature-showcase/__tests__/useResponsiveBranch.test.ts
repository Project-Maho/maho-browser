import {
  getResponsiveBranch,
  getServerSnapshot,
} from "../useResponsiveBranch";

type Matcher = {
  readonly toBe: (expected: unknown) => void;
};

declare function describe(name: string, callback: () => void): void;
declare function test(name: string, callback: () => void): void;
declare function expect(value: unknown): Matcher;

describe("useResponsiveBranch SSR-safe hook logic", () => {
  test("getServerSnapshot returns 'fallback' for SSR", () => {
    expect(getServerSnapshot()).toBe("fallback");
  });

  test("getClientSnapshot returns 'desktop' when width >= 1280px AND motion is not reduced", () => {
    const matchMedia = (query: string): Pick<MediaQueryList, "matches"> => ({
      matches: query.includes("min-width: 1280px"),
    });

    expect(getResponsiveBranch(matchMedia)).toBe("desktop");
  });

  test("getClientSnapshot returns 'fallback' when prefers-reduced-motion is reduce", () => {
    const matchMedia = (query: string): Pick<MediaQueryList, "matches"> => ({
      matches:
        query.includes("min-width: 1280px") ||
        query.includes("prefers-reduced-motion"),
    });

    expect(getResponsiveBranch(matchMedia)).toBe("fallback");
  });
});
