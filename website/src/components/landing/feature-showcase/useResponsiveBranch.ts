import { useSyncExternalStore } from "react";

export type ResponsiveBranch = "desktop" | "fallback";

export function getResponsiveBranch(
  matchMedia: (query: string) => Pick<MediaQueryList, "matches">,
): ResponsiveBranch {
  const isDesktopWidth = matchMedia("(min-width: 1280px)").matches;
  const isReducedMotion = matchMedia("(prefers-reduced-motion: reduce)").matches;

  return isDesktopWidth && !isReducedMotion ? "desktop" : "fallback";
}

export function subscribe(callback: () => void): () => void {
  if (typeof window === "undefined") {
    return () => {};
  }

  const mqWidth = window.matchMedia("(min-width: 1280px)");
  const mqMotion = window.matchMedia("(prefers-reduced-motion: reduce)");

  const listener = () => callback();

  mqWidth.addEventListener("change", listener);
  mqMotion.addEventListener("change", listener);

  return () => {
    mqWidth.removeEventListener("change", listener);
    mqMotion.removeEventListener("change", listener);
  };
}

export function getClientSnapshot(): ResponsiveBranch {
  if (typeof window === "undefined") {
    return "fallback";
  }

  return getResponsiveBranch(window.matchMedia.bind(window));
}

export function getServerSnapshot(): ResponsiveBranch {
  return "fallback";
}

/**
 * SSR-safe hook returning "desktop" | "fallback".
 * Evaluates matchMedia("(min-width: 1280px)") AND prefers-reduced-motion on client.
 * Always returns "fallback" on server.
 */
export function useResponsiveBranch(): ResponsiveBranch {
  return useSyncExternalStore(subscribe, getClientSnapshot, getServerSnapshot);
}
