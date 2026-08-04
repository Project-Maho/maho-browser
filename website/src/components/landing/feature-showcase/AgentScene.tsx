import React from "react";
import { AgentPanelSurface } from "./AgentPanelSurface";
import { TIMELINE_THRESHOLDS } from "./timeline";

export interface AgentSceneProps {
  readonly captureProgress: number;
}

function ratio(value: number, start: number, end: number): number {
  return Math.max(0, Math.min(1, (value - start) / (end - start)));
}

export const AgentScene: React.FC<AgentSceneProps> = ({ captureProgress }) => {
  const { slideStart, slideEnd } = TIMELINE_THRESHOLDS.agent;
  const slideRatio = ratio(captureProgress, slideStart, slideEnd);

  return (
    <div
      data-scene-root="agent"
      className="relative h-full w-full overflow-hidden bg-[var(--maho-window)]"
    >
      <img
        data-agent-background="subdued-page"
        src="/demo/youtube.png"
        alt="Current page behind the Maho Agent panel"
        width={1360}
        height={860}
        draggable={false}
        className="block h-full w-full object-cover object-top opacity-40 grayscale"
      />
      <div
        aria-hidden="true"
        className="absolute inset-0 bg-[var(--maho-backdrop-scrim)]"
      />
      <AgentPanelSurface
        captureProgress={captureProgress}
        position="right-docked"
        className="absolute inset-y-0 right-0 z-20 w-[48%] border-l border-white/[0.1] shadow-[-1.2cqw_0_3cqw_rgba(0,0,0,0.24)] transition-transform duration-[350ms] ease-out"
        style={{
          transform: `translate3d(${(1 - slideRatio) * 100}%, 0, 0)`,
        }}
      />
    </div>
  );
};
