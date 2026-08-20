import React, { useEffect, useRef, useState } from "react";
import { readConnectionTarget, readSaveData } from "./motion/LandingMotionPolicy";

type PlaybackPolicy = "pending" | "motion" | "static";
type VideoState = "ready" | "unavailable";

const HERO_VIDEO_SRC = "/demo/hero/maho-hero-capture.mp4";
const HERO_POSTER_SRC = "/demo/hero/maho-hero-capture-poster.png";

export const HeroBrowserVisual: React.FC<{ readonly className?: string }> = ({ className = "" }) => {
  const containerRef = useRef<HTMLDivElement>(null);
  const videoRef = useRef<HTMLVideoElement>(null);
  const playbackRequestRef = useRef(0);
  const [policy, setPolicy] = useState<PlaybackPolicy>("pending");
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
  }, [shouldPlay, videoState]);

  const imageLabel = videoState === "unavailable"
    ? "Maho Browser product recording poster; video unavailable"
    : "Maho Browser product recording";

  return (
    <div
      ref={containerRef}
      className={`relative w-full ${className}`}
      data-motion-surface="hero"
      data-motion-owner={policy === "motion" && isVisible ? "hero" : "none"}
      data-motion-substate={shouldPlay ? "action" : "idle"}
      data-hero-policy={policy}
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
              src={HERO_POSTER_SRC}
              width={1920}
              height={1080}
              alt=""
              className="absolute inset-0 h-full w-full object-contain"
            />
          </picture>
          {shouldMountVideo && (
            <video
              ref={videoRef}
              src={HERO_VIDEO_SRC}
              width={1920}
              height={1080}
              poster={HERO_POSTER_SRC}
              muted
              playsInline
              preload="auto"
              loop
              onError={() => setVideoState("unavailable")}
              className="absolute inset-0 h-full w-full object-contain opacity-100 transition-opacity duration-[320ms]"
            />
          )}
        </div>
      </div>
      {shouldMountVideo && (
        <div className="mt-4 flex items-center justify-center">
          <button
            type="button"
            aria-pressed={isPaused}
            onClick={() => setIsPaused((value) => !value)}
            className="min-h-11 rounded-full border border-border px-4 text-sm text-muted-foreground transition-colors hover:border-foreground/40 hover:text-foreground"
          >
            {isPaused ? "Resume hero video" : "Pause hero video"}
          </button>
        </div>
      )}
      <p className="sr-only" aria-live="polite" aria-atomic="true">
        {videoState === "unavailable" ? "Hero video unavailable; showing poster" : "Showing Maho Browser product recording"}
      </p>
    </div>
  );
};
