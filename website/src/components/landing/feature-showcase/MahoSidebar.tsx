import React from "react";
import {
  Archive,
  ChevronLeft,
  ChevronRight,
  Mail,
  MessageCircle,
  PanelLeft,
  Plus,
  RotateCw,
  Search,
} from "lucide-react";
import { getBodyTransform, getThemeBlend } from "./spaces-carousel";
import type { WorkspaceIndex } from "./spaces-carousel";
import type { SplitPhase } from "./timeline";
import { SpacesThemeLayers } from "./SpacesThemeLayers";
import { WorkspaceBody } from "./WorkspaceBody";
import { FAVORITE_TILES, WORKSPACES } from "./data/workspaces";

export const SIDEBAR_TINT_OPACITY = 0.82;

export type MahoWindowControlsProps = {
  readonly recordingMode?: boolean;
  readonly className?: string;
};

export const MahoWindowControls: React.FC<MahoWindowControlsProps> = ({ recordingMode = false, className = "" }) => (
  <div className={`${recordingMode ? "h-[2.4cqw]" : "h-[1.7cqw]"} flex items-center gap-[0.55cqw] px-[0.3cqw] ${className}`}>
    <div className="flex gap-[0.55cqw]">
      <i className="w-[0.8cqw] h-[0.8cqw] rounded-full block bg-[#ff5f57]" />
      <i className="w-[0.8cqw] h-[0.8cqw] rounded-full block bg-[#febc2e]" />
      <i className="w-[0.8cqw] h-[0.8cqw] rounded-full block bg-[#28c840]" />
    </div>
    <span className="w-[1.4cqw] h-[1.4cqw] rounded-[0.5cqw] grid place-items-center opacity-85 hover:bg-[var(--maho-hover-surface)] cursor-pointer">
      <PanelLeft className="w-[1cqw] h-[1cqw] text-[var(--maho-text-secondary)]" />
    </span>
    <div className="ml-auto flex gap-[0.3cqw] text-[var(--maho-text-secondary)]">
      <span className="w-[1.4cqw] h-[1.4cqw] rounded-[0.5cqw] grid place-items-center opacity-85 hover:bg-[var(--maho-hover-surface)] cursor-pointer"><ChevronLeft className="w-[1cqw] h-[1cqw]" /></span>
      <span className="w-[1.4cqw] h-[1.4cqw] rounded-[0.5cqw] grid place-items-center opacity-85 hover:bg-[var(--maho-hover-surface)] cursor-pointer"><ChevronRight className="w-[1cqw] h-[1cqw]" /></span>
      <span className="w-[1.4cqw] h-[1.4cqw] rounded-[0.5cqw] grid place-items-center opacity-85 hover:bg-[var(--maho-hover-surface)] cursor-pointer"><RotateCw className="w-[1cqw] h-[1cqw]" /></span>
      {recordingMode ? (
        <span className="w-[1.4cqw] h-[1.4cqw] rounded-[0.5cqw] grid place-items-center opacity-85 hover:bg-[var(--maho-hover-surface)] cursor-pointer"><MessageCircle className="w-[1cqw] h-[1cqw]" /></span>
      ) : (
        <span className="w-[1.4cqw] h-[1.4cqw] rounded-[0.5cqw] grid place-items-center opacity-85 hover:bg-[var(--maho-hover-surface)] cursor-pointer"><Mail className="w-[1cqw] h-[1cqw]" /></span>
      )}
    </div>
  </div>
);

export type MahoSearchPillProps = {
  readonly addressText?: string;
  readonly recordingMode?: boolean;
  readonly surface?: string;
  readonly className?: string;
};

export const MahoSearchPill: React.FC<MahoSearchPillProps> = ({
  addressText = "en.wikipedia.org/wiki/Osaka",
  recordingMode = false,
  surface,
  className = "",
}) => (
  <div
    style={surface ? { background: surface } : undefined}
    className={`${recordingMode ? "h-[2.9cqw] rounded-[0.75cqw] gap-[0.6cqw] px-[1cqw]" : "h-[2.4cqw] rounded-[0.7cqw] gap-[0.55cqw] px-[0.8cqw]"} flex items-center text-[var(--maho-text-muted)] ${surface ? "" : "bg-[var(--maho-chrome-surface)]"} border border-[var(--maho-border-strong)] overflow-hidden ${className}`}
  >
    {!recordingMode && <Search className="w-[1cqw] h-[1cqw] shrink-0 text-[var(--maho-text-muted)]" />}
    <span data-showcase="address" className={`truncate font-sans ${recordingMode ? "text-[1cqw]" : "text-[0.86cqw]"} font-medium tracking-[-0.012em] text-[var(--maho-text-secondary)]`}>
      {addressText}
    </span>
  </div>
);

export type MahoFavoriteTileProps = {
  readonly children: React.ReactNode;
  readonly surface?: string;
  readonly className?: string;
};

export const MahoFavoriteTile: React.FC<MahoFavoriteTileProps> = ({
  children,
  surface,
  className = "",
}) => (
  <div
    data-showcase="favorite-tile"
    style={surface ? { background: surface } : undefined}
    className={`relative h-[3.2cqw] overflow-hidden rounded-[0.7cqw] grid place-items-center ${surface ? "" : "bg-[var(--maho-chrome-surface)]"} border border-[var(--maho-border-subtle)] ${className}`}
  >
    <span className="relative z-10">{children}</span>
  </div>
);

export type MahoFavoriteGridProps = {
  readonly recordingMode?: boolean;
  readonly tileSurface?: string;
  readonly className?: string;
};

export const MahoFavoriteGrid: React.FC<MahoFavoriteGridProps> = ({
  recordingMode = false,
  tileSurface,
  className = "",
}) => (
  <div className={`grid grid-cols-3 ${recordingMode ? "gap-[0.75cqw]" : "gap-[0.55cqw]"} mt-[0.1cqw] ${className}`}>
    {FAVORITE_TILES.map((tile) => (
      <MahoFavoriteTile key={tile.label} surface={tileSurface}>
        {tile.badge.kind === "img" ? (
          <img
            src={tile.badge.src}
            alt=""
            title={tile.label}
            className="h-[1.25cqw] w-[1.25cqw] rounded-[0.35cqw] object-contain"
          />
        ) : (
          <span
            title={tile.label}
            className="grid h-[1.5cqw] w-[1.5cqw] place-items-center rounded-[0.4cqw] text-[0.54cqw] font-black leading-none"
            style={{ background: tile.badge.background, color: tile.badge.foreground }}
          >
            {tile.badge.label}
          </span>
        )}
      </MahoFavoriteTile>
    ))}
  </div>
);

export type MahoFooterDotsProps = {
  readonly activeDotIndex?: number;
  readonly className?: string;
};

export const MahoFooterDots: React.FC<MahoFooterDotsProps> = ({
  activeDotIndex = 1,
  className = "",
}) => (
  <div data-showcase="footer-dots" className={`relative flex gap-[0.4cqw] items-center ${className}`}>
    {[0, 1, 2, 3].map((index) => (
      <span
        key={index}
        data-showcase={`dot-${index}`}
        data-active={index === activeDotIndex}
        className={`w-[0.4cqw] h-[0.4cqw] rounded-full transition-all duration-300 ${
          index === activeDotIndex
            ? "bg-[var(--maho-text-primary)] scale-125 opacity-100"
            : "bg-[var(--maho-text-muted)] opacity-45"
        }`}
      />
    ))}
  </div>
);

export type MahoSidebarProps = {
  readonly addressText?: string;
  readonly activeDotIndex?: number;
  readonly workspaceIndex?: WorkspaceIndex;
  readonly activeRowLabel?: string;
  readonly recordingMode?: boolean;
  readonly pillSurface?: string;
  readonly tileSurface?: string;
  readonly splitPhase?: SplitPhase;
  readonly splitDragRatio?: number;
  readonly spacesProgress?: number | null;
  readonly className?: string;
};

export const MahoSidebar: React.FC<MahoSidebarProps> = ({
  addressText = "en.wikipedia.org/wiki/Osaka",
  activeDotIndex = 1,
  workspaceIndex = 0,
  activeRowLabel,
  recordingMode = false,
  pillSurface,
  tileSurface,
  splitPhase,
  splitDragRatio,
  spacesProgress = null,
  className = "",
}) => {
  const sidebar = <aside
    className={`${recordingMode ? "w-[20.4cqw] p-[0.6cqw_0.6cqw_0.8cqw] text-[1cqw]" : "w-[17.5cqw] p-[0.5cqw_0.7cqw_0.8cqw] text-[0.9cqw]"} shrink-0 border-r border-[var(--maho-border-subtle)] select-none relative z-10 text-[var(--maho-text-primary)] font-sans overflow-visible ${className}`}
  >
    <div
      data-showcase="sidebar-translucent-tint"
      aria-hidden="true"
      className="pointer-events-none absolute inset-0"
      style={{ background: "var(--maho-sidebar-gradient)", opacity: recordingMode ? 1 : SIDEBAR_TINT_OPACITY }}
    />
    <div className={`relative z-10 flex h-full min-h-0 flex-col ${recordingMode ? "gap-[0.65cqw]" : "gap-[0.5cqw]"}`}>
      {!recordingMode && <MahoWindowControls />}
      {recordingMode && <MahoWindowControls recordingMode />}

      <MahoSearchPill addressText={addressText} recordingMode={recordingMode} surface={pillSurface} />

      <MahoFavoriteGrid recordingMode={recordingMode} tileSurface={tileSurface} />

      <div className="relative flex-1 min-h-0 overflow-hidden">
        {spacesProgress === null ? (
          <div
            className={
              activeDotIndex === 1 || splitPhase !== undefined
                ? "h-full"
                : "h-full [&_[data-active-row]]:bg-transparent [&_[data-active-row]]:text-[var(--maho-text-secondary)] [&_[data-active-row]]:font-normal"
            }
          >
            <WorkspaceBody workspace={WORKSPACES[workspaceIndex]} activeRowLabel={activeRowLabel} recordingMode={recordingMode} splitPhase={splitPhase} splitDragRatio={splitDragRatio} />
          </div>
        ) : (
          WORKSPACES.map((workspace) => {
            const transform = getBodyTransform(workspace.index, spacesProgress);
            return (
              <div
                key={workspace.key}
                data-showcase="workspace-body"
                data-workspace={workspace.key}
                data-body-index={workspace.index}
                className="absolute inset-0"
                style={{
                  transform: `translateX(${transform.translateXPct}%)`,
                  opacity: transform.opacity,
                }}
              >
                <WorkspaceBody workspace={workspace} recordingMode={recordingMode} />
              </div>
            );
          })
        )}
      </div>

      <div className="mt-auto flex items-center justify-between px-[0.55cqw] pt-[0.55cqw] pb-[0.1cqw] text-[var(--maho-text-muted)]">
        <Archive className="w-[1cqw] h-[1cqw] cursor-pointer hover:text-[var(--maho-text-primary)]" />
        <MahoFooterDots activeDotIndex={activeDotIndex} />
        <Plus className="w-[1cqw] h-[1cqw] cursor-pointer hover:text-[var(--maho-text-primary)]" />
      </div>
    </div>
  </aside>;

  return spacesProgress === null ? (
    sidebar
  ) : (
    <SpacesThemeLayers blend={getThemeBlend(spacesProgress)} scope="sidebar">
      {sidebar}
    </SpacesThemeLayers>
  );
};
