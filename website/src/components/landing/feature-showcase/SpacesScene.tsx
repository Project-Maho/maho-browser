import React from "react";
import { getContentBlend } from "./spaces-carousel";
import { SpacesContentSurface } from "./SpacesContentSurface";
import { WORKSPACES } from "./data/workspaces";

export interface SpacesSceneProps {
  readonly captureProgress: number;
}

export const SpacesScene: React.FC<SpacesSceneProps> = ({ captureProgress }) => {
  const contentBlend = getContentBlend(captureProgress);

  return (
    <div data-scene-root="spaces" className="relative h-full w-full overflow-hidden bg-black">
      {WORKSPACES.map((workspace, idx) => {
        const opacity = contentBlend[idx] ?? 0;
        return (
          <SpacesContentSurface
            key={workspace.index}
            workspace={workspace}
            opacity={opacity}
            hidden={opacity <= 0}
          />
        );
      })}
    </div>
  );
};
