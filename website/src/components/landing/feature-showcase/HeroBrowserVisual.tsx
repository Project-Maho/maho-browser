import React, { useEffect, useRef, useState } from "react";
import { readConnectionTarget, readSaveData } from "./motion/LandingMotionPolicy";

type PlaybackPolicy = "pending" | "motion" | "static";
type HeroScene = "spaces" | "split-view" | "command-bar-ai";
type VideoState = "ready" | "unavailable";

type HeroSceneAsset = {
  readonly label: string;
  readonly videoSrc: string;
  readonly posterSrc: string;
};

const HERO_SCENES = {
  spaces: {
    label: "Spaces",
    videoSrc: "/demo/hero/dom-hyperframes/maho-spaces-dom.mp4",
    posterSrc: "/demo/hero/dom-hyperframes/maho-spaces-dom-poster.webp",
  },
  "split-view": {
    label: "Split View",
    videoSrc: "/demo/hero/dom-hyperframes/maho-split-view-dom.mp4",
    posterSrc: "/demo/hero/dom-hyperframes/maho-split-view-dom-poster.webp",
  },
  "command-bar-ai": {
    label: "Command Bar + AI",
    videoSrc: "/demo/hero/dom-hyperframes/maho-command-ai-dom.mp4",
    posterSrc: "/demo/hero/dom-hyperframes/maho-command-ai-dom-poster.webp",
  },
} as const satisfies Record<HeroScene, HeroSceneAsset>;

const HERO_SCENE_KEYS = ["spaces", "split-view", "command-bar-ai"] as const satisfies readonly HeroScene[];

function nextScene(scene: HeroScene): HeroScene {
  switch (scene) {
    case "spaces": return "split-view";
    case "split-view": return "command-bar-ai";
    case "command-bar-ai": return "spaces";
  }
}

export const HeroBrowserVisual: React.FC<{ readonly className?: string }> = ({ className = "" }) => {
  const containerRef = useRef<HTMLDivElement>(null);
  const videoRef = useRef<HTMLVideoElement>(null);
  const playbackRequestRef = useRef(0);
  const [policy, setPolicy] = useState<PlaybackPolicy>("pending");
  const [scene, setScene] = useState<HeroScene>("spaces");
  const [isVisible, setIsVisible] = useState(false);
  const [isPaused, setIsPaused] = useState(false);
  const [videoState, setVideoState] = useState<VideoState>("ready");

  useEffect(() => {
    const motionQuery = window.matchMedia("(prefers-reduced-motion: reduce)");
    const desktopInputQuery = window.matchMedia("(hover: hover) and (pointer: fine)");
    const connection = readConnectionTarget();
    const resolvePolicy = (): void => {
      setPolicy(motionQuery.matches || readSaveData() || !desktopInputQuery.matches ? "static" : "motion");
    };

    resolvePolicy();
    motionQuery.addEventListener("change", resolvePolicy);
    desktopInputQuery.addEventListener("change", resolvePolicy);
    connection?.addEventListener("change", resolvePolicy);

    return () => {
      motionQuery.removeEventListener("change", resolvePolicy);
      desktopInputQuery.removeEventListener("change", resolvePolicy);
      connection?.removeEventListener("change", resolvePolicy);
    };
  }, []);

  useEffect(() => {
    const element = containerRef.current;
    if (!element || typeof IntersectionObserver === "undefined") {
      setIsVisible(true);
      return;
    }

    const observer = new IntersectionObserver(
      ([entry]) => setIsVisible((entry?.intersectionRatio ?? 0) >= 0.35),
      { threshold: [0, 0.35, 1] },
    );
    observer.observe(element);

    return () => observer.disconnect();
  }, []);

  const shouldMountVideo = policy === "motion" && videoState === "ready";
  const activeScene = HERO_SCENES[scene];
  const shouldPlay = shouldMountVideo && isVisible && !isPaused;

  useEffect(() => {
    const video = videoRef.current;
    const playbackRequest = playbackRequestRef.current + 1;
    playbackRequestRef.current = playbackRequest;
    if (video === null) return;
    if (!shouldPlay) {
      video.pause();
      return;
    }

    void video.play().catch((error: unknown) => {
      if (playbackRequest !== playbackRequestRef.current) return;
      if (error instanceof DOMException && (error.name === "NotAllowedError" || error.name === "NotSupportedError")) {
        setVideoState("unavailable");
      }
    });

    return () => {
      playbackRequestRef.current += 1;
      video.pause();
    };
  }, [scene, shouldPlay, videoState]);

  const selectScene = (next: HeroScene): void => {
    setScene(next);
    setVideoState("ready");
    setIsPaused(false);
  };

  const imageLabel = videoState === "unavailable"
    ? `Maho Browser hero showing ${activeScene.label} poster; video unavailable`
    : `Maho Browser hero showing ${activeScene.label}`;

  return (
    <div
      ref={containerRef}
      className={`relative w-full ${className}`}
      data-motion-surface="hero"
      data-motion-owner={policy === "motion" && isVisible ? "hero" : "none"}
      data-motion-substate={shouldPlay ? "action" : "idle"}
      data-hero-policy={policy}
      data-hero-scene={scene}
      data-hero-video-state={videoState}
    >
      <div
        role="img"
        aria-label={imageLabel}
        className="relative mx-auto aspect-video w-full overflow-hidden rounded-xl border border-border bg-card shadow-[0_28px_90px_rgba(0,0,0,0.28)]"
      >
        <div aria-hidden="true" className="absolute inset-0">
          <picture>
            <img
              src={activeScene.posterSrc}
              width={1920}
              height={1080}
              alt=""
              className="absolute inset-0 h-full w-full object-contain"
            />
          </picture>
          {shouldMountVideo && (
            <video
              ref={videoRef}
              key={scene}
              src={activeScene.videoSrc}
              width={1920}
              height={1080}
              poster={activeScene.posterSrc}
              muted
              playsInline
              preload="auto"
              onEnded={() => selectScene(nextScene(scene))}
              onError={() => setVideoState("unavailable")}
              className="absolute inset-0 h-full w-full object-contain opacity-100 transition-opacity duration-[320ms]"
            />
          )}
        </div>
      </div>
      <div className="mt-4 flex flex-wrap items-center justify-center gap-2" aria-label="Hero scenes">
        {HERO_SCENE_KEYS.map((key) => {
          const item = HERO_SCENES[key];
          return (
          <button
            key={key}
            type="button"
            aria-pressed={scene === key}
            onClick={() => selectScene(key)}
            className="min-h-11 rounded-full border border-border px-4 text-sm text-muted-foreground transition-colors hover:border-foreground/40 hover:text-foreground aria-pressed:border-foreground aria-pressed:bg-foreground aria-pressed:text-background"
          >
            {item.label}
          </button>
          );
        })}
        {shouldMountVideo && (
          <button
            type="button"
            aria-pressed={isPaused}
            onClick={() => setIsPaused((value) => !value)}
            className="min-h-11 rounded-full border border-border px-4 text-sm text-muted-foreground transition-colors hover:border-foreground/40 hover:text-foreground"
          >
            {isPaused ? "Resume hero video" : "Pause hero video"}
          </button>
        )}
        <button
          type="button"
          onClick={() => selectScene("spaces")}
          className="min-h-11 rounded-full border border-border px-4 text-sm text-muted-foreground transition-colors hover:border-foreground/40 hover:text-foreground"
        >
          Replay hero sequence
        </button>
      </div>
      <p className="sr-only" aria-live="polite" aria-atomic="true">
        {videoState === "unavailable" ? `${activeScene.label} video unavailable; showing poster` : `Showing ${activeScene.label}`}
      </p>
    </div>
  );
};
