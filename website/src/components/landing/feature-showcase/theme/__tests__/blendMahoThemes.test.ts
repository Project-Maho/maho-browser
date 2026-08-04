import { blendMahoThemes } from "../blendMahoThemes";
import { deriveMahoTheme } from "../deriveMahoTheme";

type Matcher = {
  readonly toBe: (expected: unknown) => void;
  readonly toEqual: (expected: unknown) => void;
};

declare function describe(name: string, callback: () => void): void;
declare function test(name: string, callback: () => void): void;
declare function expect(value: unknown): Matcher;

describe("blendMahoThemes", () => {
  const launchTheme = deriveMahoTheme("#b58ad8");
  const mediaTheme = deriveMahoTheme("#ff6f91");
  const workTheme = deriveMahoTheme("#7fa879");
  const themes = [launchTheme, mediaTheme, workTheme] as const;

  test("weight [1, 0, 0] produces launchTheme tint tokens verbatim", () => {
    const blended = blendMahoThemes(themes, [1, 0, 0]);
    expect(blended.sidebarTop).toBe(launchTheme.sidebarTop);
    expect(blended.sidebarBottom).toBe(launchTheme.sidebarBottom);
    expect(blended.contentTint).toBe(launchTheme.contentTint);
    expect(blended.window).toBe(launchTheme.window);
  });

  test("weight [0, 1, 0] produces mediaTheme tint tokens verbatim", () => {
    const blended = blendMahoThemes(themes, [0, 1, 0]);
    expect(blended.sidebarTop).toBe(mediaTheme.sidebarTop);
    expect(blended.sidebarBottom).toBe(mediaTheme.sidebarBottom);
    expect(blended.contentTint).toBe(mediaTheme.contentTint);
    expect(blended.window).toBe(mediaTheme.window);
  });

  test("weight [0.5, 0.5, 0] produces valid blended hex strings", () => {
    const blended = blendMahoThemes(themes, [0.5, 0.5, 0]);
    expect(/^#[0-9a-f]{6}$/.test(blended.sidebarTop)).toBe(true);
    expect(/^#[0-9a-f]{6}$/.test(blended.sidebarBottom)).toBe(true);
    expect(/^#[0-9a-f]{6}$/.test(blended.contentTint)).toBe(true);
    expect(blended.sidebarGradient).toBe(
      `linear-gradient(180deg, ${blended.sidebarTop}, ${blended.sidebarBottom})`,
    );
  });
});
