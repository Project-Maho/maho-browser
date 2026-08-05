import React, { useEffect, useRef, useState } from "react";
import { readConnectionTarget, readSaveData } from "./motion/LandingMotionPolicy";

type PlaybackPolicy = "pending" | "motion" | "static";

const VIDEO_SRC = "/demo/hero/maho-hero-workflow.mp4";
const POSTER_SRC = "/demo/hero/maho-hero-workflow-poster.png";

export const HeroBrowserVisual: React.FC<{ readonly className?: string }> = ({ className = "" }) => {
  const containerRef = useRef<HTMLDivElement>(null);
  const videoRef = useRef<HTMLVideoElement>(null);
  const [policy, setPolicy] = useState<PlaybackPolicy>("pending");
  const [isVisible, setIsVisible] = useState(false);
  const [isVideoAvailable, setIsVideoAvailable] = useState(true);

  useEffect(() => {
    const motionQuery = window.matchMedia("(prefers-reduced-motion: reduce)");
    const connection = readConnectionTarget();
    const resolvePolicy = (): void => {
      setPolicy(motionQuery.matches || readSaveData() ? "static" : "motion");
    };

    resolvePolicy();
    motionQuery.addEventListener("change", resolvePolicy);
    connection?.addEventListener("change", resolvePolicy);

    return () => {
      motionQuery.removeEventListener("change", resolvePolicy);
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

  useEffect(() => {
    const video = videoRef.current;
    if (!video) return;
    if (!isVisible) {
      video.pause();
      return;
    }

    void video.play().catch((error: unknown) => {
      if (!(error instanceof DOMException)) throw error;
      if (error.name === "AbortError") return;
      if (error.name === "NotAllowedError" || error.name === "NotSupportedError") {
        setIsVideoAvailable(false);
        return;
      }
      throw error;
    });
  }, [isVisible]);

  const shouldMountVideo = policy === "motion" && isVideoAvailable;
  const videoState = policy !== "motion" || !isVideoAvailable
    ? "poster"
    : isVisible
      ? "playing"
      : "paused";

  return (
    <div
      ref={containerRef}
      className={`relative w-full ${className}`}
      data-motion-surface="hero"
      data-motion-owner={policy === "motion" && isVisible ? "hero" : "none"}
      data-motion-substate={policy === "motion" && isVisible ? "action" : "idle"}
      data-hero-policy={policy}
      data-hero-video-state={videoState}
    >
      <div
        role="img"
        aria-label="Maho Browser product demo"
        className="relative mx-auto aspect-[4/5] w-full max-w-6xl overflow-hidden rounded-squircle-xl border border-border/80 shadow-[0_1.5rem_4rem_rgba(0,0,0,0.28)] sm:aspect-[1040/720]"
      >
        <div aria-hidden="true" className="absolute inset-0">
          <img
            src={POSTER_SRC}
            width={1040}
            height={720}
            alt=""
            className="absolute inset-0 h-full w-full object-cover object-[32%_center] sm:object-center"
          />
          {shouldMountVideo && (
            <video
              ref={videoRef}
              src={VIDEO_SRC}
              width={1040}
              height={720}
              poster={POSTER_SRC}
              muted
              playsInline
              loop
              autoPlay={isVisible}
              preload="auto"
              onError={() => setIsVideoAvailable(false)}
              className="absolute inset-0 h-full w-full object-cover object-[32%_center] sm:object-center"
            />
          )}
        </div>
      </div>
      <p className="sr-only" aria-live="polite" aria-atomic="true">
        {videoState === "playing" ? "Playing Maho Browser product demo" : "Showing Maho Browser product demo poster"}
      </p>
    </div>
  );
};
