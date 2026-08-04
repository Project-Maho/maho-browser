import { LandingMotionCoordinator } from "../motion/LandingMotionCoordinator";
import type { CoordinatorRuntime, SurfaceId } from "../motion/LandingMotionTypes";
import type { SceneKey } from "../timeline";

export class FakeRuntime implements CoordinatorRuntime {
  public running = false;
  public paused = false;
  public readonly played: string[] = [];
  private finishRun: (() => void) | null = null;
  private readonly playCounts = new Map<string, number>();
  private readonly playWaiters = new Set<{ readonly count: number; readonly resolve: () => void }>();

  public constructor(private readonly block = { segment: "hero-intro", fromOccurrence: 1 }) {}

  public async playSequence(segments: readonly string[]): Promise<void> {
    this.running = true;
    this.played.push(...segments);
    for (const waiter of this.playWaiters) {
      if (this.played.length >= waiter.count) {
        this.playWaiters.delete(waiter);
        waiter.resolve();
      }
    }
    const segment = segments[0] ?? "";
    const occurrence = (this.playCounts.get(segment) ?? 0) + 1;
    this.playCounts.set(segment, occurrence);
    if (segment === this.block.segment && occurrence >= this.block.fromOccurrence) {
      await new Promise<void>((resolve) => {
        this.finishRun = resolve;
      });
    }
    this.running = false;
  }

  public pause(): void {
    this.paused = true;
    this.running = false;
  }

  public resume(): void {
    this.paused = false;
    this.running = true;
  }

  public cancel(): void {
    this.running = false;
    this.finishRun?.();
    this.finishRun = null;
  }

  public getRunningAnimationCount(): number {
    return this.running ? 1 : 0;
  }

  public finishActive(): void {
    this.finishRun?.();
    this.finishRun = null;
  }

  public clearPlayed(): void {
    this.played.length = 0;
  }

  public waitForPlayCount(count: number): Promise<void> {
    if (this.played.length >= count) return Promise.resolve();
    return new Promise((resolve) => {
      this.playWaiters.add({ count, resolve });
    });
  }
}

export function deferred(): { readonly promise: Promise<void>; readonly resolve: () => void } {
  let resolve = (): void => undefined;
  const promise = new Promise<void>((complete) => {
    resolve = complete;
  });
  return { promise, resolve };
}

export function register(
  coordinator: LandingMotionCoordinator,
  id: SurfaceId,
  runtime: FakeRuntime,
  waitForScene: (scene: SceneKey) => Promise<void> = async () => undefined,
): { readonly dispose: () => void; readonly getScene: () => SceneKey } {
  let scene: SceneKey = "spaces";
  const dispose = coordinator.registerSurface({
    id,
    element: null,
    runtime,
    setScene: (nextScene) => {
      scene = nextScene;
    },
    waitForScene: (nextScene) => waitForScene(nextScene),
  });
  return { dispose, getScene: () => scene };
}
