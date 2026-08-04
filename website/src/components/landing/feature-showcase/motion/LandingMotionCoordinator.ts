import { isSceneKey } from "../data/scenes";
import {
  initialPlaybackState,
  LANDING_INTERSECTION_THRESHOLDS,
  selectMotionOwner,
  substateForSegment,
  transitionPlaybackState,
  type PlaybackEvent,
  type PlaybackState,
  type RuntimeSubstate,
} from "../motionContract";
import { SCENES, type SceneKey } from "../timeline";
import { isMotionSuppressed, readConnectionTarget, readSaveData, type MotionPolicy } from "./LandingMotionPolicy";
import type { CoordinatorState, SurfaceId, SurfaceRegistration } from "./LandingMotionTypes";

export type { CoordinatorState, CoordinatorRuntime, SurfaceId, SurfaceRegistration } from "./LandingMotionTypes";

export class LandingMotionCoordinator {
  private static instance: LandingMotionCoordinator | null = null;
  private readonly surfaces = new Map<SurfaceId, SurfaceRegistration>(); private readonly ratios = new Map<SurfaceId, number>();
  private readonly manualPriority = new Map<SurfaceId, boolean>(); private readonly sceneBySurface = new Map<SurfaceId, SceneKey>();
  private readonly listeners = new Set<(state: CoordinatorState) => void>(); private readonly targetIds = new WeakMap<Element, SurfaceId>();
  private readonly observer: IntersectionObserver | null; private readonly motionQuery: MediaQueryList | null; private readonly connection: EventTarget | null;
  private activeOwner: SurfaceId | null = null;
  private playbackState: PlaybackState = initialPlaybackState("autoplay");
  private frozen = false; private visible = true;
  private policy: MotionPolicy = { reducedMotion: false, saveData: false };
  private leasePromise: Promise<void> = Promise.resolve(); private playbackPromise: Promise<void> = Promise.resolve();
  private playbackAbort: AbortController | null = null;
  private playbackGeneration = 0;

  public static getInstance(): LandingMotionCoordinator {
    LandingMotionCoordinator.instance ??= new LandingMotionCoordinator(true); return LandingMotionCoordinator.instance;
  }

  public static createForTests(): LandingMotionCoordinator { return new LandingMotionCoordinator(false); }

  private constructor(enableBrowserBindings: boolean) {
    this.motionQuery = enableBrowserBindings && typeof window !== "undefined" ? window.matchMedia("(prefers-reduced-motion: reduce)") : null;
    this.connection = enableBrowserBindings ? readConnectionTarget() : null;
    if (enableBrowserBindings && typeof window !== "undefined") {
      const scene = new URLSearchParams(window.location.search).get("scene");
      if (isSceneKey(scene)) {
        this.playbackState = { ...transitionPlaybackState(this.playbackState, { type: "freeze" }), scene };
        this.frozen = true;
      }
      this.policy = { reducedMotion: this.motionQuery?.matches ?? false, saveData: readSaveData() };
      document.addEventListener("visibilitychange", this.handleVisibility);
      this.motionQuery?.addEventListener("change", this.handleMotionPolicy); this.connection?.addEventListener("change", this.handleMotionPolicy);
    }
    this.observer = enableBrowserBindings && typeof IntersectionObserver !== "undefined"
      ? new IntersectionObserver(this.handleIntersections, { threshold: [...LANDING_INTERSECTION_THRESHOLDS] })
      : null;
  }

  public registerSurface(registration: SurfaceRegistration): () => void {
    this.surfaces.set(registration.id, registration);
    this.sceneBySurface.set(registration.id, "spaces");
    if (registration.element) {
      registration.element.dataset.coordinatorSurface = registration.id;
      this.targetIds.set(registration.element, registration.id);
      this.observer?.observe(registration.element);
    }
    return () => this.unregisterSurface(registration);
  }

  public setIntersection(id: SurfaceId, ratio: number): void { this.ratios.set(id, ratio); this.arbitrateLease(); }

  public setManualPriority(id: SurfaceId, active: boolean): void {
    if (active) this.manualPriority.set(id === "hero" ? "showcase" : "hero", false);
    this.manualPriority.set(id, active);
    this.arbitrateLease();
  }

  public selectScene(id: SurfaceId, scene: SceneKey): void {
    this.transition({ type: "select", scene });
    this.manualPriority.set(id === "hero" ? "showcase" : "hero", false);
    this.manualPriority.set(id, true);
    this.sceneBySurface.set(id, scene);
    this.surfaces.get(id)?.setScene(scene);
    this.arbitrateLease();
    this.startPlayback(id, scene, ["handoff", "zoom-in", "action"], "hold");
  }

  public pause(id: SurfaceId): void {
    if (this.activeOwner !== id || this.playbackState.mode === "frozen") return;
    if (this.playbackState.mode === "manual" && this.playbackState.substate === "hold") return;
    this.surfaces.get(id)?.runtime.pause();
    this.transition({ type: "pause" });
    this.syncPlaybackState();
  }

  public resume(id: SurfaceId): void {
    if (this.activeOwner !== id) return;
    if (this.playbackState.mode === "manual" && this.playbackState.substate === "hold") {
      this.manualPriority.set(id, false);
      this.transition({ type: "resume" });
      this.startPlayback(id, this.playbackState.scene, ["zoom-out"], "idle", id === "showcase");
      return;
    }
    if (this.playbackState.mode !== "paused") return;
    this.surfaces.get(id)?.runtime.resume();
    this.transition({ type: "resume" });
    this.syncPlaybackState();
  }

  public replayHero(): void {
    this.transition({ type: "replay" });
    this.manualPriority.set("hero", false);
    this.sceneBySurface.set("hero", "spaces");
    this.surfaces.get("hero")?.setScene("spaces");
    this.arbitrateLease();
    this.startPlayback("hero", "spaces", ["handoff", "hero-intro"], "idle");
  }

  public stopAgent(id: SurfaceId): void {
    if (this.sceneBySurface.get(id) !== "agent") return;
    this.surfaces.get(id)?.runtime.cancel();
    this.playbackState = { ...this.playbackState, mode: "manual", resumeMode: "manual", substate: "hold", resumeSubstate: "hold" };
    this.syncPlaybackState();
  }

  public setDocumentVisible(visible: boolean): void {
    this.visible = visible;
    if (!visible && this.activeOwner) this.pause(this.activeOwner);
    else if (visible && this.activeOwner) this.resume(this.activeOwner);
    this.arbitrateLease();
  }

  public setMotionPolicy(policy: MotionPolicy): void {
    this.policy = policy;
    if (policy.reducedMotion || policy.saveData) {
      this.revokeCurrentOwner();
      this.playbackState = { ...this.playbackState, mode: "manual", resumeMode: "manual", substate: "idle", resumeSubstate: "idle" };
      this.syncPlaybackState();
    } else {
      this.arbitrateLease();
    }
  }

  public getState(): CoordinatorState {
    return { activeOwner: this.activeOwner, mode: this.playbackState.mode, substate: this.playbackState.substate, currentScene: this.playbackState.scene, isFrozen: this.frozen };
  }

  public subscribe(listener: (state: CoordinatorState) => void): () => void { this.listeners.add(listener); return () => this.listeners.delete(listener); }

  public async flushLease(): Promise<void> { await this.leasePromise; } public async flushPlayback(): Promise<void> { await this.playbackPromise; }

  public dispose(): void {
    this.playbackGeneration += 1;
    this.playbackAbort?.abort();
    this.observer?.disconnect();
    if (typeof document !== "undefined") document.removeEventListener("visibilitychange", this.handleVisibility);
    this.motionQuery?.removeEventListener("change", this.handleMotionPolicy); this.connection?.removeEventListener("change", this.handleMotionPolicy);
    for (const surface of this.surfaces.values()) surface.runtime.cancel();
    this.surfaces.clear();
    this.activeOwner = null;
    this.listeners.clear();
  }

  private readonly handleVisibility = (): void => this.setDocumentVisible(document.visibilityState === "visible");
  private readonly handleMotionPolicy = (): void => this.setMotionPolicy({ reducedMotion: this.motionQuery?.matches ?? false, saveData: readSaveData() });
  private readonly handleIntersections = (entries: IntersectionObserverEntry[]): void => {
    for (const entry of entries) {
      const id = this.targetIds.get(entry.target);
      if (id) this.ratios.set(id, entry.intersectionRatio);
    }
    this.arbitrateLease();
  };

  private arbitrateLease(): void {
    if (!this.visible || isMotionSuppressed(this.frozen, this.policy)) return;
    const winner = this.pickWinner();
    if (winner === this.activeOwner) return;
    this.revokeCurrentOwner();
    if (!winner) return;
    this.leasePromise = Promise.resolve().then(() => {
      if (this.activeOwner !== null || !this.surfaces.has(winner)) return;
      this.activeOwner = winner;
      this.playbackState = { ...this.playbackState, scene: this.sceneBySurface.get(winner) ?? "spaces" };
      this.notify();
      if (this.playbackState.mode === "autoplay") this.startAutoplay(winner);
    });
  }

  private pickWinner(): SurfaceId | null {
    return selectMotionOwner({
      activeOwner: this.activeOwner,
      heroRatio: this.ratios.get("hero") ?? 0,
      showcaseRatio: this.ratios.get("showcase") ?? 0,
      heroManual: this.manualPriority.get("hero") ?? false,
      showcaseManual: this.manualPriority.get("showcase") ?? false,
    });
  }

  private revokeCurrentOwner(): void {
    if (!this.activeOwner) return;
    this.playbackGeneration += 1;
    this.playbackAbort?.abort();
    const registration = this.surfaces.get(this.activeOwner);
    registration?.runtime.pause();
    if (registration?.runtime.getRunningAnimationCount() !== 0) registration?.runtime.cancel();
    this.activeOwner = null;
    this.notify();
  }

  private startAutoplay(id: SurfaceId): void {
    const scene = this.sceneBySurface.get(id) ?? "spaces";
    if (id === "hero") this.startPlayback(id, scene, ["hero-intro"], "idle");
    else this.startPlayback(id, scene, ["zoom-in", "action", "zoom-out"], "idle", true);
  }

  private startPlayback(id: SurfaceId, scene: SceneKey, segments: readonly string[], terminal: RuntimeSubstate, advance = false): void {
    const registration = this.surfaces.get(id);
    if (!registration || isMotionSuppressed(this.frozen, this.policy)) return;
    const generation = ++this.playbackGeneration;
    this.playbackAbort?.abort();
    const controller = new AbortController();
    this.playbackAbort = controller;
    registration.runtime.cancel();
    this.playbackPromise = this.runPlayback(registration, scene, segments, generation, controller.signal).then(() => {
      if (!this.canPlay(registration.id, scene, generation, controller.signal)) return;
      this.setSubstate(terminal);
      if (advance) this.advanceShowcase(id);
      else if (id === "hero" && this.playbackState.mode === "autoplay") this.startAutoplay(id);
    });
  }

  private async runPlayback(registration: SurfaceRegistration, scene: SceneKey, segments: readonly string[], generation: number, signal: AbortSignal): Promise<void> {
    await this.leasePromise;
    if (!this.canPlay(registration.id, scene, generation, signal)) return;
    await registration.waitForScene(scene, signal);
    for (const segment of segments) {
      if (!this.canPlay(registration.id, scene, generation, signal)) return;
      this.setSubstate(substateForSegment(segment));
      await registration.runtime.playSequence([segment]);
    }
  }

  private canPlay(id: SurfaceId, scene: SceneKey, generation: number, signal: AbortSignal): boolean {
    return !signal.aborted && generation === this.playbackGeneration && this.activeOwner === id
      && this.sceneBySurface.get(id) === scene && this.surfaces.has(id);
  }

  private advanceShowcase(id: SurfaceId): void {
    const current = this.sceneBySurface.get(id) ?? "spaces";
    const index = SCENES.findIndex(({ key }) => key === current);
    const next = SCENES[(index + 1) % SCENES.length]?.key ?? "spaces";
    this.sceneBySurface.set(id, next);
    this.playbackState = { ...this.playbackState, scene: next };
    this.surfaces.get(id)?.setScene(next);
    if (this.activeOwner === id && this.playbackState.mode === "autoplay") this.startAutoplay(id);
  }

  private setSubstate(substate: RuntimeSubstate): void {
    this.playbackState = { ...this.playbackState, substate };
    this.syncPlaybackState();
  }

  private transition(event: PlaybackEvent): void {
    this.playbackState = transitionPlaybackState(this.playbackState, event);
  }

  private syncPlaybackState(): void {
    if (this.activeOwner) this.surfaces.get(this.activeOwner)?.setSubstate?.(this.playbackState.substate);
    this.notify();
  }

  private unregisterSurface(registration: SurfaceRegistration): void {
    if (registration.element) this.observer?.unobserve(registration.element);
    if (this.activeOwner === registration.id) this.revokeCurrentOwner();
    registration.runtime.cancel();
    this.surfaces.delete(registration.id);
    this.ratios.delete(registration.id);
    this.manualPriority.delete(registration.id);
    this.sceneBySurface.delete(registration.id);
  }

  private notify(): void {
    const state = this.getState();
    for (const listener of this.listeners) listener(state);
  }
}
