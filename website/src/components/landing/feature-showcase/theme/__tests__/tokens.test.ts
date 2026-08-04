import { readFileSync } from "fs";
import { join } from "path";
import { TOKEN_TO_CSSVAR, type MahoTheme, themeToCssVars } from "../tokens";

type Matcher = {
  readonly toBe: (expected: unknown) => void;
  readonly not: { readonly toContain: (expected: string) => void };
};

declare function describe(name: string, callback: () => void): void;
declare function test(name: string, callback: () => void): void;
declare function expect(value: unknown): Matcher;

describe("Semantic Token Contract", () => {
  test("TOKEN_TO_CSSVAR has exactly 18 token entries matching spec", () => {
    const keys = Object.keys(TOKEN_TO_CSSVAR);
    expect(keys.length).toBe(18);

    expect(TOKEN_TO_CSSVAR.window).toBe("--maho-window");
    expect(TOKEN_TO_CSSVAR.sidebarTop).toBe("--maho-sidebar-top");
    expect(TOKEN_TO_CSSVAR.sidebarBottom).toBe("--maho-sidebar-bottom");
    expect(TOKEN_TO_CSSVAR.sidebarGradient).toBe("--maho-sidebar-gradient");
    expect(TOKEN_TO_CSSVAR.contentTint).toBe("--maho-content-tint");
    expect(TOKEN_TO_CSSVAR.chromeSurface).toBe("--maho-chrome-surface");
    expect(TOKEN_TO_CSSVAR.activeSurface).toBe("--maho-active-surface");
    expect(TOKEN_TO_CSSVAR.hoverSurface).toBe("--maho-hover-surface");
    expect(TOKEN_TO_CSSVAR.borderStrong).toBe("--maho-border-strong");
    expect(TOKEN_TO_CSSVAR.borderSubtle).toBe("--maho-border-subtle");
    expect(TOKEN_TO_CSSVAR.textPrimary).toBe("--maho-text-primary");
    expect(TOKEN_TO_CSSVAR.textSecondary).toBe("--maho-text-secondary");
    expect(TOKEN_TO_CSSVAR.textMuted).toBe("--maho-text-muted");
    expect(TOKEN_TO_CSSVAR.accent).toBe("--maho-accent");
    expect(TOKEN_TO_CSSVAR.focusRing).toBe("--maho-focus-ring");
    expect(TOKEN_TO_CSSVAR.shadowColor).toBe("--maho-shadow-color");
    expect(TOKEN_TO_CSSVAR.backdropScrim).toBe("--maho-backdrop-scrim");
    expect(TOKEN_TO_CSSVAR.backdropBlur).toBe("--maho-backdrop-blur");
  });

  test("themeToCssVars converts a MahoTheme to CSS variable record", () => {
    const sampleTheme: MahoTheme = {
      window: "#2b2130",
      sidebarTop: "#302d32",
      sidebarBottom: "#242328",
      sidebarGradient: "linear-gradient(180deg, #302d32, #242328)",
      contentTint: "#453849",
      chromeSurface: "#554057",
      activeSurface: "#725477",
      hoverSurface: "rgba(180, 150, 190, 0.08)",
      borderStrong: "rgba(200, 180, 210, 0.14)",
      borderSubtle: "rgba(200, 180, 210, 0.06)",
      textPrimary: "#f6f1f7",
      textSecondary: "#c6bac8",
      textMuted: "#928697",
      accent: "#8ea2ff",
      focusRing: "rgba(142, 162, 255, 0.65)",
      shadowColor: "rgba(10, 8, 12, 0.38)",
      backdropScrim: "rgba(15, 12, 18, 0.55)",
      backdropBlur: "30px",
    };

    const vars = themeToCssVars(sampleTheme);
    expect(Object.keys(vars).length).toBe(18);
    expect(vars["--maho-window"]).toBe("#2b2130");
    expect(vars["--maho-chrome-surface"]).toBe("#554057");
    expect(vars["--maho-backdrop-blur"]).toBe("30px");
  });

  test("INV-NODEP: theme/tokens.ts source does not import oklch or color math", () => {
    const filePath = join(__dirname, "../tokens.ts");
    const code = readFileSync(filePath, "utf-8");
    expect(code).not.toContain("oklch");
    expect(code).not.toContain("deriveMahoTheme");
    expect(code).not.toContain("blend");
  });
});
