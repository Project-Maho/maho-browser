import React, { useEffect, useRef, useState } from "react";
import { AgentPanelSurface } from "./AgentPanelSurface";

export const AGENT_PREVIEW_DURATION_MS = 9_000;

export function getAgentPreviewProgress(elapsedMs: number): number {
  if (!Number.isFinite(elapsedMs) || elapsedMs <= 0) {
    return 0;
  }

  return (elapsedMs % AGENT_PREVIEW_DURATION_MS) / AGENT_PREVIEW_DURATION_MS;
}

export function getAgentReducedMotionProgress(reducedMotion: boolean): number {
  return reducedMotion ? 1 : 0;
}

export const AgentPanelPreview: React.FC = () => {
  const rootRef = useRef<HTMLDivElement>(null);
  const [progress, setProgress] = useState(0);

  useEffect(() => {
    const reducedMotion = window.matchMedia(
      "(prefers-reduced-motion: reduce)",
    );
    if (reducedMotion.matches) {
      setProgress(getAgentReducedMotionProgress(true));
      return;
    }

    let animationFrame = 0;
    let elapsedMs = 0;
    let lastTimestamp: number | null = null;
    let isVisible = true;

    const observer = new IntersectionObserver(([entry]) => {
      isVisible = entry?.isIntersecting ?? false;
    });
    if (rootRef.current) {
      observer.observe(rootRef.current);
    }

    const animate = (timestamp: number) => {
      if (lastTimestamp !== null && isVisible) {
        elapsedMs += timestamp - lastTimestamp;
        setProgress(getAgentPreviewProgress(elapsedMs));
      }
      lastTimestamp = timestamp;
      animationFrame = window.requestAnimationFrame(animate);
    };

    animationFrame = window.requestAnimationFrame(animate);
    return () => {
      observer.disconnect();
      window.cancelAnimationFrame(animationFrame);
    };
  }, []);

  return (
    <div
      ref={rootRef}
      data-agent-panel-preview="true"
      className="h-full w-full [container-type:inline-size]"
    >
      <AgentPanelSurface captureProgress={progress} position="standalone" />
    </div>
  );
};
