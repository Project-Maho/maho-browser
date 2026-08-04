import type { SceneKey } from "../src/components/landing/feature-showcase/timeline";

export type CaptureRequest = {
  readonly progress: number;
};

export declare const CAPTURE_REQUESTS: Readonly<Record<SceneKey, CaptureRequest>>;
