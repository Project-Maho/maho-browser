export interface MahoTheme {
  readonly window: string;
  readonly sidebarTop: string;
  readonly sidebarBottom: string;
  readonly sidebarGradient: string;
  readonly contentTint: string;
  readonly chromeSurface: string;
  readonly activeSurface: string;
  readonly hoverSurface: string;
  readonly borderStrong: string;
  readonly borderSubtle: string;
  readonly textPrimary: string;
  readonly textSecondary: string;
  readonly textMuted: string;
  readonly accent: string;
  readonly focusRing: string;
  readonly shadowColor: string;
  readonly backdropScrim: string;
  readonly backdropBlur: string;
}

export type MahoThemeKey = keyof MahoTheme;

export const TOKEN_TO_CSSVAR: Readonly<Record<MahoThemeKey, `--maho-${string}`>> = Object.freeze({
  window: "--maho-window",
  sidebarTop: "--maho-sidebar-top",
  sidebarBottom: "--maho-sidebar-bottom",
  sidebarGradient: "--maho-sidebar-gradient",
  contentTint: "--maho-content-tint",
  chromeSurface: "--maho-chrome-surface",
  activeSurface: "--maho-active-surface",
  hoverSurface: "--maho-hover-surface",
  borderStrong: "--maho-border-strong",
  borderSubtle: "--maho-border-subtle",
  textPrimary: "--maho-text-primary",
  textSecondary: "--maho-text-secondary",
  textMuted: "--maho-text-muted",
  accent: "--maho-accent",
  focusRing: "--maho-focus-ring",
  shadowColor: "--maho-shadow-color",
  backdropScrim: "--maho-backdrop-scrim",
  backdropBlur: "--maho-backdrop-blur",
});

/**
 * Convert a MahoTheme object into a dictionary of CSS custom properties (`--maho-*`).
 */
export function themeToCssVars(theme: MahoTheme): Record<`--maho-${string}`, string> {
  const vars: Record<`--maho-${string}`, string> = {};
  const keys = Object.keys(TOKEN_TO_CSSVAR) as MahoThemeKey[];
  for (const key of keys) {
    vars[TOKEN_TO_CSSVAR[key]] = theme[key];
  }
  return vars;
}
