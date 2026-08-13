import React from "react";
import { DemoPageSurface } from "./DemoPageSurface";
import { getSplitMotion } from "./timeline";

const SPLIT_PREVIEW_ANIMATION_MS = 170;
const SPLIT_REVEAL_ANIMATION_MS = 200;

export interface SplitViewSceneProps {
  readonly captureProgress: number;
}

export const SplitViewScene: React.FC<SplitViewSceneProps> = ({ captureProgress }) => {
  const { phase, revealRatio } = getSplitMotion(captureProgress);

  return (
    <div
      data-scene-root="split-view"
      data-split-phase={phase}
      data-split-preview-duration-ms={SPLIT_PREVIEW_ANIMATION_MS}
      data-split-reveal-duration-ms={SPLIT_REVEAL_ANIMATION_MS}
      className="relative flex h-full w-full overflow-hidden bg-white"
    >
      <div
        data-split-pane="left"
        data-split-reveal-ratio={revealRatio}
        className="relative h-full shrink-0 overflow-hidden bg-white transition-[flex-basis,opacity] duration-[200ms] ease-out"
        style={{ flexBasis: `${revealRatio * 50}%`, opacity: revealRatio }}
      >
        <div className="h-full w-full">
          <DemoPageSurface variant="longreads" />
        </div>
      </div>

      {revealRatio > 0 && (
        <div
          data-split-divider="true"
          className="h-full w-[0.3cqw] shrink-0 bg-zinc-300"
        />
      )}

      <div data-split-pane="right" className="relative h-full min-w-0 flex-1 overflow-hidden bg-white">
        <DemoPageSurface variant="article" />
      </div>
    </div>
  );
};
