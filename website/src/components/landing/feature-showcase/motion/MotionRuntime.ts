import type { SceneMotionSpec, SegmentSpec } from "../motionContract";

export interface MotionStyle {
  readonly setProperty: (name: string, value: string) => void;
  readonly removeProperty: (name: string) => void;
}

export interface MotionAnimation {
  currentTime: CSSNumberish | null;
  readonly playState: AnimationPlayState;
  readonly finished: Promise<unknown>;
  pause(): void;
  play(): void;
  commitStyles(): void;
  cancel(): void;
}

export interface MotionElement {
  readonly style: MotionStyle;
  animate(keyframes: Keyframe[], options: KeyframeAnimationOptions): MotionAnimation;
}

export interface MotionOwners {
  readonly camera: MotionElement;
  readonly action: MotionElement;
  readonly pointer?: MotionElement;
  readonly text?: MotionElement;
}

export interface MotionRuntimeOptions {
  readonly surface: "hero" | "showcase" | "capture";
  readonly owners: MotionOwners;
  readonly sample?: (element: MotionElement) => { readonly transform: string; readonly opacity: string };
  readonly mark?: (name: string) => void;
}

type ActiveRun = {
  readonly generation: number;
  readonly animation: MotionAnimation;
  readonly owner: MotionElement;
};

export class MotionRuntime {
  private spec: SceneMotionSpec | null = null;
  private readonly specWaiters = new Map<string, Set<() => void>>();
  private generation = 0;
  private activeRun: ActiveRun | null = null;
  private paused = false;

  public constructor(private readonly options: MotionRuntimeOptions) {}

  public registerSpec(spec: SceneMotionSpec): void {
    this.cancel();
    this.spec?.dispose?.();
    this.spec = spec;
    const waiters = this.specWaiters.get(spec.sceneKey);
    if (!waiters) return;
    this.specWaiters.delete(spec.sceneKey);
    for (const resolve of waiters) resolve();
  }

  public getSpec(): SceneMotionSpec | null {
    return this.spec;
  }

  public waitForSpec(sceneKey: string, signal?: AbortSignal): Promise<void> {
    if (this.spec?.sceneKey === sceneKey || signal?.aborted) return Promise.resolve();
    return new Promise((resolve) => {
      const waiters = this.specWaiters.get(sceneKey) ?? new Set<() => void>();
      const complete = (): void => {
        waiters.delete(complete);
        resolve();
      };
      waiters.add(complete);
      this.specWaiters.set(sceneKey, waiters);
      signal?.addEventListener("abort", complete, { once: true });
    });
  }

  public async playSequence(segmentNames: readonly string[]): Promise<void> {
    for (const segmentName of segmentNames) {
      await this.playSegment(segmentName);
    }
  }

  public async playSegment(segmentName: string): Promise<void> {
    const segment = this.spec?.segments.find(({ name }) => name === segmentName);
    if (!segment) return;

    const generation = ++this.generation;
    this.cancelActiveAnimation();
    this.paused = false;
    const owner = this.resolveOwner(segment);
    this.setWillChange(owner, true);
    this.mark(`${segmentName}:start`);

    const keyframes = segment.name === "handoff"
      ? [{ ...this.sampleCurrentState() }, ...segment.keyframes.slice(1)]
      : segment.keyframes;
    const animation = owner.animate([...keyframes], {
      duration: segment.durationMs,
      easing: segment.easing,
      fill: "forwards",
    });
    this.activeRun = { generation, animation, owner };

    try {
      await animation.finished;
    } catch (error) {
      if (generation === this.generation) throw error;
      return;
    }

    if (generation !== this.generation) return;
    animation.commitStyles();
    animation.cancel();
    this.activeRun = null;
    this.setWillChange(owner, false);
    this.mark(`${segmentName}:end`);
  }

  public pause(): void {
    if (this.paused) return;
    this.paused = true;
    const run = this.activeRun;
    if (!run) return;
    const currentTime = run.animation.currentTime;
    run.animation.pause();
    run.animation.currentTime = currentTime;
    this.setWillChange(run.owner, false);
  }

  public resume(): void {
    if (!this.paused) return;
    this.paused = false;
    const run = this.activeRun;
    if (!run) return;
    const currentTime = run.animation.currentTime;
    this.setWillChange(run.owner, true);
    run.animation.play();
    run.animation.currentTime = currentTime;
  }

  public sampleCurrentState(): { readonly transform: string; readonly opacity: string } {
    const sample = this.options.sample;
    if (sample) return sample(this.options.owners.camera);
    if (typeof window === "undefined" || !(this.options.owners.camera instanceof Element)) {
      return { transform: "matrix(1, 0, 0, 1, 0, 0)", opacity: "1" };
    }
    const computed = window.getComputedStyle(this.options.owners.camera);
    return { transform: computed.transform, opacity: computed.opacity };
  }

  public getRunningAnimationCount(): number {
    return this.activeRun?.animation.playState === "running" ? 1 : 0;
  }

  public cancel(): void {
    this.generation += 1;
    this.cancelActiveAnimation();
    this.paused = false;
  }

  public dispose(): void {
    this.cancel();
    this.spec?.dispose?.();
    this.spec = null;
    for (const waiters of this.specWaiters.values()) {
      for (const resolve of waiters) resolve();
    }
    this.specWaiters.clear();
  }

  private cancelActiveAnimation(): void {
    const run = this.activeRun;
    if (!run) return;
    this.activeRun = null;
    run.animation.cancel();
    this.setWillChange(run.owner, false);
  }

  private resolveOwner(segment: SegmentSpec): MotionElement {
    switch (segment.owner) {
      case "camera": return this.options.owners.camera;
      case "action": return this.options.owners.action;
      case "pointer": return this.options.owners.pointer ?? this.options.owners.action;
      case "text": return this.options.owners.text ?? this.options.owners.action;
    }
  }

  private setWillChange(owner: MotionElement, enabled: boolean): void {
    if (enabled) owner.style.setProperty("will-change", "transform, opacity");
    else owner.style.removeProperty("will-change");
  }

  private mark(segment: string): void {
    const name = `maho-motion:${this.options.surface}:${segment}`;
    if (this.options.mark) this.options.mark(name);
    else if (typeof performance !== "undefined") performance.mark(name);
  }
}
