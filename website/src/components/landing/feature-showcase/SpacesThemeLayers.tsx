import React from "react";
import type { BlendTriple } from "./spaces-carousel";
import { WORKSPACES } from "./data/workspaces";
import { blendMahoThemes } from "./theme/blendMahoThemes";
import type { MahoTheme } from "./theme/tokens";

export type ThemeScope = "sidebar" | "content";

export type SpacesThemeLayersProps = {
  readonly blend: BlendTriple;
  readonly scope: ThemeScope;
  readonly children: React.ReactNode;
};

type ContentTintStyle = React.CSSProperties & {
  readonly "--maho-content-tint": string;
};

type SidebarTintStyle = React.CSSProperties & {
  readonly "--maho-sidebar-top": string;
  readonly "--maho-sidebar-bottom": string;
  readonly "--maho-sidebar-gradient": string;
};

const WORKSPACE_THEMES: readonly MahoTheme[] = WORKSPACES.map((workspace) => workspace.theme);

export const SpacesThemeLayers: React.FC<SpacesThemeLayersProps> = ({ blend, scope, children }) => {
  const blendedTheme = blendMahoThemes(WORKSPACE_THEMES, blend);
  const style: ContentTintStyle | SidebarTintStyle =
    scope === "content"
      ? { "--maho-content-tint": blendedTheme.contentTint }
      : {
          "--maho-sidebar-top": blendedTheme.sidebarTop,
          "--maho-sidebar-bottom": blendedTheme.sidebarBottom,
          "--maho-sidebar-gradient": blendedTheme.sidebarGradient,
        };

  return (
    <div
      data-showcase="theme-scope"
      data-theme-scope={scope}
      className="contents"
      style={style}
    >
      {children}
    </div>
  );
};
