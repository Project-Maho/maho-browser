import type { PlaybackMode, RuntimeSubstate } from "../motionContract";
import type { SceneKey } from "../timeline";

export type SurfaceId = "hero" | "showcase";

export interface CoordinatorRuntime {
  playSequence(segments: readonly string[]): Promise<void>;
  pause(): void;
  resume(): void;
  cancel(): void;
  getRunningAnimationCount(): number;
}

export interface SurfaceRegistration {
  readonly id: SurfaceId;
  readonly element: HTMLElement | null;
  readonly runtime: CoordinatorRuntime;
  readonly setScene: (scene: SceneKey) => void;
  readonly waitForScene: (scene: SceneKey, signal: AbortSignal) => Promise<void>;
  readonly setSubstate?: (substate: RuntimeSubstate) => void;
}

export interface CoordinatorState {
  readonly activeOwner: SurfaceId | null;
  readonly mode: PlaybackMode;
  readonly substate: RuntimeSubstate;
  readonly currentScene: SceneKey;
  readonly isFrozen: boolean;
}
