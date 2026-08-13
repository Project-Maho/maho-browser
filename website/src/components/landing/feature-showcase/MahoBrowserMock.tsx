import React from "react";
import { AgentScene } from "./AgentScene";
import type { SceneKey } from "./timeline";
import { clampProgress, getSplitMotion } from "./timeline";
import { themeToCssVars } from "./theme/tokens";
import { SCENE_REGISTRY } from "./data/scenes";
import { getActiveDotIndex, getAddressIndex } from "./spaces-carousel";
import { WORKSPACES } from "./data/workspaces";
import { MahoContent, MahoWindow } from "./MahoMockFrame";
import { MahoSidebar } from "./MahoSidebar";

export interface MahoBrowserMockProps {
  /** Single color seed. 3/6-digit hex with or without leading '#'. Default "#b58ad8" (launch seed). */
  readonly themeColor?: string;
  /** Which scene to render. One of SceneKey. Default "spaces". */
  readonly scene?: SceneKey;
  /** Frozen progress 0..1 for this scene (clamped via clampProgress). Default 0. */
  readonly captureProgress?: number;
  /** Renders deterministic artwork for capture-only composition. */
  readonly staticCapture?: boolean;
  /** Optional passthrough class on the root wrapper (layout only, never color). */
  readonly className?: string;
}

export const MahoBrowserMock: React.FC<MahoBrowserMockProps> = ({
  scene = "spaces",
  captureProgress = 0,
  staticCapture = false,
  className = "",
}) => {
  const clampedP = clampProgress(captureProgress);
  const sceneEntry = SCENE_REGISTRY[scene];
  const activeWorkspaceIndex = sceneEntry.usesSpacesCarousel ? getAddressIndex(clampedP) : sceneEntry.defaultWorkspaceIndex;
  const theme = scene === "spaces" ? WORKSPACES[activeWorkspaceIndex].theme : WORKSPACES[sceneEntry.defaultWorkspaceIndex].theme;
  const cssVars = themeToCssVars(theme);

  const SceneComponent = sceneEntry.Component;
  const sceneContent = scene === "agent" && staticCapture
    ? <AgentScene captureProgress={clampedP} staticCapture />
    : <SceneComponent captureProgress={clampedP} />;

  let addressText = sceneEntry.defaultAddress;
  let activeDotIndex = sceneEntry.defaultActiveDot;

  if (sceneEntry.usesSpacesCarousel) {
    const addressIdx = getAddressIndex(clampedP);
    addressText = WORKSPACES[addressIdx]?.address || WORKSPACES[0].address;
    activeDotIndex = getActiveDotIndex(clampedP);
  }

  if (scene === "spaces") {
    return (
      <div
        data-showcase="browser-mock"
        className={`relative h-full w-full overflow-hidden ${className}`}
        style={{ width: "100%", height: "100%", containerType: "inline-size", ...cssVars }}
      >
        <div className="flex h-full w-full bg-[var(--maho-window)] text-[var(--maho-text-primary)]">
          <MahoSidebar
            addressText={addressText}
            activeDotIndex={activeDotIndex}
            spacesProgress={clampedP}
            recordingMode
            pillSurface={WORKSPACES[activeWorkspaceIndex].pill}
            tileSurface={WORKSPACES[activeWorkspaceIndex].tile}
          />
          <MahoContent>{sceneContent}</MahoContent>
        </div>
      </div>
    );
  }

  if (scene === "split-view") {
    const splitMotion = getSplitMotion(clampedP);
    const { dragRatio, phase } = splitMotion;
    const reading = WORKSPACES[sceneEntry.defaultWorkspaceIndex];
    const splitAddress = phase === "settling" || phase === "settled" ? "longreads.com" : "aeon.co";
    const dragging = phase === "drag";
    const dragScale = 1 + Math.min(dragRatio, 0.55) * 0.05;
    return (
      <div
        data-showcase="browser-mock"
        className={`relative h-full w-full overflow-hidden ${className}`}
        style={{ width: "100%", height: "100%", containerType: "inline-size", ...cssVars }}
      >
        <div className="flex h-full w-full bg-[var(--maho-window)] text-[var(--maho-text-primary)]">
          <MahoSidebar
            addressText={splitAddress}
            activeDotIndex={sceneEntry.defaultActiveDot}
            workspaceIndex={sceneEntry.defaultWorkspaceIndex}
            spacesProgress={null}
            recordingMode
            splitPhase={phase}
            splitDragRatio={dragRatio}
            pillSurface={reading.pill}
            tileSurface={reading.tile}
          />
          <MahoContent>{sceneContent}</MahoContent>
          {dragging && (
            <div
              data-split-sidebar-drag="true"
              className="absolute left-[0.6cqw] top-[35cqw] z-20 flex h-[2.55cqw] w-[19.2cqw] items-center gap-[0.6cqw] rounded-[0.7cqw] border border-white/20 bg-[var(--maho-active-surface)] px-[0.55cqw] text-[1cqw] font-medium text-[var(--maho-text-primary)] shadow-[0_1.2cqw_2.4cqw_rgba(0,0,0,0.22)]"
              style={{
                opacity: 1,
                transform: `translate3d(${dragRatio * 5.2}cqw, ${dragRatio * -2.55}cqw, 0) scale(${dragScale})`,
              }}
            >
              <span className="grid h-[1.25cqw] w-[1.25cqw] place-items-center rounded-[0.3cqw] bg-[#b91c3b] text-[0.7cqw] font-black text-white">a</span>
              <span>Aeon</span>
            </div>
          )}
        </div>
      </div>
    );
  }

  return (
    <div
      data-showcase="browser-mock"
      className={`w-full h-full relative overflow-hidden ${className}`}
      style={{
        width: "100%",
        height: "100%",
        containerType: "inline-size",
        ...cssVars,
      }}
    >
      <div className="h-full w-full">
        <MahoWindow>
          <MahoSidebar
            addressText={addressText}
            activeDotIndex={activeDotIndex}
            workspaceIndex={sceneEntry.defaultWorkspaceIndex}
            activeRowLabel={sceneEntry.defaultActiveRowLabel}
            spacesProgress={sceneEntry.usesSpacesCarousel ? clampedP : null}
          />
          <MahoContent>{sceneContent}</MahoContent>
        </MahoWindow>
      </div>
    </div>
  );
};
