import { isSceneKey } from "./data/scenes";
import { clampProgress } from "./timeline";
import type { SceneKey } from "./timeline";
import { InvalidSeedError, parseSeed } from "./theme/oklch";

export type CaptureProfile = "feature" | "reference" | "hero";
export type CaptureSurface = "hero" | "showcase";

export type CaptureParams = {
  readonly scene: SceneKey;
  readonly theme: string;
  readonly progress: number;
  readonly profile: CaptureProfile;
  readonly surface: CaptureSurface;
};

export const DEFAULT_CAPTURE_PARAMS = {
  scene: "spaces",
  theme: "#b58ad8",
  progress: 0,
  profile: "feature",
  surface: "showcase",
} as const satisfies CaptureParams;

function normalizeTheme(theme: string | null): string {
  if (theme === null) {
    return DEFAULT_CAPTURE_PARAMS.theme;
  }

  try {
    parseSeed(theme);
    return theme;
  } catch (error) {
    if (error instanceof InvalidSeedError) {
      return DEFAULT_CAPTURE_PARAMS.theme;
    }
    throw error;
  }
}

function normalizeProgress(progress: string | null): number {
  if (progress === null) {
    return DEFAULT_CAPTURE_PARAMS.progress;
  }

  const parsed = Number(progress);
  return Number.isFinite(parsed)
    ? clampProgress(parsed)
    : DEFAULT_CAPTURE_PARAMS.progress;
}

export function parseCaptureParams(searchParams: URLSearchParams): CaptureParams {
  const scene = searchParams.get("scene");
  const profileParam = searchParams.get("profile");
  const surfaceParam = searchParams.get("surface");

  let profile: CaptureProfile = DEFAULT_CAPTURE_PARAMS.profile;
  if (profileParam === "reference" || profileParam === "hero") {
    profile = profileParam;
  }

  const surface: CaptureSurface = surfaceParam === "hero" ? "hero" : "showcase";

  return {
    scene: isSceneKey(scene) ? scene : DEFAULT_CAPTURE_PARAMS.scene,
    theme: normalizeTheme(searchParams.get("theme")),
    progress: normalizeProgress(searchParams.get("p")),
    profile,
    surface,
  };
}
