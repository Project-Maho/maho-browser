import { useEffect, useState } from "react";

export type Platform = "ios" | "android" | "desktop";

const MOBILE_BREAKPOINT_QUERY = "(max-width: 768px)";
declare global {
  interface Window {
    __TAURI_INTERNALS__?: unknown;
  }

  interface Navigator {
    userAgentData?: {
      mobile?: boolean;
      platform?: string;
    };
  }
}

function normalizePlatform(value: unknown): Platform | null {
  if (typeof value !== "string") {
    return null;
  }

  const normalized = value.toLowerCase();

  if (["ios", "iphone", "ipad", "ipod"].some((token) => normalized.includes(token))) {
    return "ios";
  }

  if (normalized.includes("android")) {
    return "android";
  }

  if (["desktop", "macos", "darwin", "windows", "linux"].some((token) => normalized.includes(token))) {
    return "desktop";
  }

  return null;
}

function detectTauriPlatform(value: unknown, depth = 0, seen = new WeakSet<object>()): Platform | null {
  if (depth > 3 || value == null) {
    return null;
  }

  const directMatch = normalizePlatform(value);
  if (directMatch) {
    return directMatch;
  }

  if (typeof value !== "object") {
    return null;
  }

  if (seen.has(value)) {
    return null;
  }

  seen.add(value);

  if (Array.isArray(value)) {
    for (const item of value) {
      const match = detectTauriPlatform(item, depth + 1, seen);
      if (match) {
        return match;
      }
    }

    return null;
  }

  const prioritizedKeys = ["platform", "osName", "os", "currentPlatform", "target", "name"];
  for (const key of prioritizedKeys) {
    const record = value as Record<string, unknown>;
    if (!(key in record)) {
      continue;
    }

    const match = detectTauriPlatform(record[key], depth + 1, seen);
    if (match) {
      return match;
    }
  }

  for (const nestedValue of Object.values(value as Record<string, unknown>)) {
    const match = detectTauriPlatform(nestedValue, depth + 1, seen);
    if (match) {
      return match;
    }
  }

  return null;
}

function detectUserAgentPlatform(): Platform {
  if (typeof navigator === "undefined") {
    return "desktop";
  }

  const userAgent = navigator.userAgent ?? "";
  const userAgentPlatform = navigator.userAgentData?.platform ?? navigator.platform ?? "";
  const combinedSource = `${userAgent} ${userAgentPlatform}`;

  if (/android/i.test(combinedSource)) {
    return "android";
  }

  if (/iPad|iPhone|iPod/i.test(combinedSource)) {
    return "ios";
  }

  return navigator.userAgentData?.mobile ? "android" : "desktop";
}

function detectPlatform(): Platform {
  if (typeof window !== "undefined") {
    const tauriPlatform = detectTauriPlatform(window.__TAURI_INTERNALS__);
    if (tauriPlatform) {
      return tauriPlatform;
    }
  }

  return detectUserAgentPlatform();
}

function getResponsiveMobileState(): boolean {
  if (typeof window === "undefined" || typeof window.matchMedia !== "function") {
    return isMobile();
  }

  return isMobile() || window.matchMedia(MOBILE_BREAKPOINT_QUERY).matches;
}

const STATIC_PLATFORM = detectPlatform();

export function getPlatform(): Platform {
  return STATIC_PLATFORM;
}

export function isMobile(): boolean {
  const platform = getPlatform();
  return platform === "ios" || platform === "android";
}

export function isIOS(): boolean {
  return getPlatform() === "ios";
}

export function isAndroid(): boolean {
  return getPlatform() === "android";
}

export function useIsMobile(): boolean {
  const [mobile, setMobile] = useState(getResponsiveMobileState);

  useEffect(() => {
    if (typeof window === "undefined" || typeof window.matchMedia !== "function") {
      return undefined;
    }

    const mediaQuery = window.matchMedia(MOBILE_BREAKPOINT_QUERY);

    const updateMobileState = () => {
      setMobile(getResponsiveMobileState());
    };

    updateMobileState();

    if (typeof mediaQuery.addEventListener === "function") {
      mediaQuery.addEventListener("change", updateMobileState);

      return () => {
        mediaQuery.removeEventListener("change", updateMobileState);
      };
    }

    mediaQuery.addListener(updateMobileState);

    return () => {
      mediaQuery.removeListener(updateMobileState);
    };
  }, []);

  return mobile;
}
