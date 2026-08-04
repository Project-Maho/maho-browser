import { describe, expect, test } from "bun:test";
import { LandingMotionCoordinator } from "../motion/LandingMotionCoordinator";
import { deferred, FakeRuntime, register } from "./landingMotionTestHarness";

describe("LandingMotionCoordinator", () => {
  test("uses thresholds and hysteresis while granting exactly one runtime", async () => {
    const coordinator = LandingMotionCoordinator.createForTests();
    const hero = new FakeRuntime();
    const showcase = new FakeRuntime({ segment: "zoom-in", fromOccurrence: 1 });
    register(coordinator, "hero", hero);
    register(coordinator, "showcase", showcase);

    coordinator.setIntersection("hero", 0.35);
    coordinator.setIntersection("showcase", 0.4);
    await coordinator.flushLease();
    expect(coordinator.getState().activeOwner).toBe("hero");

    coordinator.setIntersection("showcase", 0.44);
    await coordinator.flushLease();
    expect(coordinator.getState().activeOwner).toBe("hero");

    coordinator.setIntersection("showcase", 0.46);
    await coordinator.flushLease();
    expect(coordinator.getState().activeOwner).toBe("showcase");
    expect(hero.getRunningAnimationCount() + showcase.getRunningAnimationCount()).toBeLessThanOrEqual(1);
  });

  test("manual selection retargets rapidly and settles once at hold", async () => {
    const coordinator = LandingMotionCoordinator.createForTests();
    const runtime = new FakeRuntime();
    const surface = register(coordinator, "hero", runtime);
    coordinator.setIntersection("hero", 1);
    await coordinator.flushLease();

    coordinator.selectScene("hero", "split-view");
    coordinator.selectScene("hero", "command-bar");
    coordinator.selectScene("hero", "agent");
    await coordinator.flushPlayback();

    expect(surface.getScene()).toBe("agent");
    expect(coordinator.getState()).toMatchObject({ mode: "manual", substate: "hold", currentScene: "agent" });
    expect(runtime.played.slice(-3)).toEqual(["handoff", "zoom-in", "action"]);

    coordinator.resume("hero");
    await coordinator.flushPlayback();
    expect(coordinator.getState().mode).toBe("autoplay");
    expect(runtime.played).toContain("zoom-out");
  });

  test("waits for the requested scene spec and lease before starting playback", async () => {
    const coordinator = LandingMotionCoordinator.createForTests();
    const runtime = new FakeRuntime();
    const readiness = deferred();
    register(coordinator, "hero", runtime, async (scene) => {
      if (scene === "agent") await readiness.promise;
    });

    coordinator.setIntersection("hero", 1);
    coordinator.selectScene("hero", "agent");
    await Promise.resolve();
    expect(runtime.played).toEqual([]);

    readiness.resolve();
    await coordinator.flushPlayback();
    expect(coordinator.getState().activeOwner).toBe("hero");
    expect(runtime.played).toEqual(["handoff", "zoom-in", "action"]);
  });

  test("replay waits for the hero scene spec before playing", async () => {
    const coordinator = LandingMotionCoordinator.createForTests();
    const runtime = new FakeRuntime();
    const readiness = deferred();
    register(coordinator, "hero", runtime, async () => readiness.promise);
    coordinator.setIntersection("hero", 1);
    await coordinator.flushLease();
    runtime.clearPlayed();

    coordinator.replayHero();
    await Promise.resolve();
    expect(runtime.played).toEqual([]);
    readiness.resolve();
    await runtime.waitForPlayCount(2);
    expect(runtime.played).toEqual(["handoff", "hero-intro"]);
    coordinator.dispose();
  });

  test("loops the hero intro while hero remains the autoplay owner", async () => {
    const coordinator = LandingMotionCoordinator.createForTests();
    const runtime = new FakeRuntime();
    register(coordinator, "hero", runtime);
    coordinator.setIntersection("hero", 1);
    await coordinator.flushLease();
    await runtime.waitForPlayCount(1);
    expect(runtime.played).toEqual(["hero-intro"]);

    runtime.finishActive();
    await runtime.waitForPlayCount(2);
    expect(runtime.played).toEqual(["hero-intro", "hero-intro"]);
    coordinator.dispose();
  });

  test("waits for the next showcase spec before advancing autoplay", async () => {
    const coordinator = LandingMotionCoordinator.createForTests();
    const runtime = new FakeRuntime({ segment: "zoom-in", fromOccurrence: 2 });
    const readiness = deferred();
    register(coordinator, "showcase", runtime, async (scene) => {
      if (scene === "split-view") await readiness.promise;
    });
    coordinator.setIntersection("showcase", 1);
    await coordinator.flushLease();
    await coordinator.flushPlayback();
    expect(runtime.played).toEqual(["zoom-in", "action", "zoom-out"]);

    readiness.resolve();
    await runtime.waitForPlayCount(4);
    expect(runtime.played).toEqual(["zoom-in", "action", "zoom-out", "zoom-in"]);
    coordinator.dispose();
  });

  test("pause and resume restore the exact prior autoplay substate", async () => {
    const coordinator = LandingMotionCoordinator.createForTests();
    const runtime = new FakeRuntime();
    register(coordinator, "hero", runtime);
    coordinator.setIntersection("hero", 1);
    await coordinator.flushLease();
    await runtime.waitForPlayCount(1);
    expect(coordinator.getState()).toMatchObject({ mode: "autoplay", substate: "zoom-in" });

    coordinator.pause("hero");
    expect(coordinator.getState()).toMatchObject({ mode: "paused", substate: "suspended" });
    coordinator.resume("hero");
    expect(coordinator.getState()).toMatchObject({ mode: "autoplay", substate: "zoom-in" });
  });

  test("pause and resume restore the exact prior manual substate", async () => {
    const coordinator = LandingMotionCoordinator.createForTests();
    const runtime = new FakeRuntime({ segment: "action", fromOccurrence: 1 });
    register(coordinator, "hero", runtime);
    coordinator.setIntersection("hero", 1);
    coordinator.selectScene("hero", "agent");
    await runtime.waitForPlayCount(3);
    expect(coordinator.getState()).toMatchObject({ mode: "manual", substate: "action" });

    coordinator.pause("hero");
    coordinator.resume("hero");
    expect(coordinator.getState()).toMatchObject({ mode: "manual", substate: "action" });
    coordinator.dispose();
  });

  test("manual interaction transfers priority between surfaces", async () => {
    const coordinator = LandingMotionCoordinator.createForTests();
    register(coordinator, "hero", new FakeRuntime());
    register(coordinator, "showcase", new FakeRuntime());
    coordinator.setIntersection("hero", 1);
    coordinator.setIntersection("showcase", 1);
    coordinator.selectScene("hero", "agent");
    await coordinator.flushPlayback();
    expect(coordinator.getState().activeOwner).toBe("hero");

    coordinator.selectScene("showcase", "command-bar");
    await coordinator.flushPlayback();
    expect(coordinator.getState().activeOwner).toBe("showcase");
  });

  test("suspends owners at the loss thresholds and honors reduced-motion and saveData", async () => {
    const coordinator = LandingMotionCoordinator.createForTests();
    const hero = new FakeRuntime();
    const showcase = new FakeRuntime({ segment: "zoom-in", fromOccurrence: 1 });
    register(coordinator, "hero", hero);
    register(coordinator, "showcase", showcase);
    coordinator.setIntersection("hero", 0.35);
    await coordinator.flushLease();
    expect(coordinator.getState().activeOwner).toBe("hero");

    coordinator.setIntersection("hero", 0.24);
    await coordinator.flushLease();
    expect(coordinator.getState().activeOwner).toBeNull();

    coordinator.setIntersection("showcase", 0.4);
    await coordinator.flushLease();
    coordinator.setManualPriority("showcase", true);
    coordinator.setIntersection("showcase", 0.09);
    await coordinator.flushLease();
    expect(coordinator.getState().activeOwner).toBeNull();

    coordinator.setMotionPolicy({ reducedMotion: false, saveData: true });
    expect(coordinator.getState()).toMatchObject({ activeOwner: null, mode: "manual", substate: "idle" });
    coordinator.setMotionPolicy({ reducedMotion: true, saveData: false });
    expect(coordinator.getState().activeOwner).toBeNull();
  });

  test("pause resume replay visibility policy and cleanup command the owned runtime", async () => {
    const coordinator = LandingMotionCoordinator.createForTests();
    const runtime = new FakeRuntime();
    const surface = register(coordinator, "hero", runtime);
    coordinator.setIntersection("hero", 1);
    await coordinator.flushLease();
    await runtime.waitForPlayCount(1);

    coordinator.pause("hero");
    expect(runtime.paused).toBe(true);
    coordinator.resume("hero");
    expect(runtime.paused).toBe(false);
    coordinator.replayHero();
    await runtime.waitForPlayCount(3);
    expect(surface.getScene()).toBe("spaces");
    expect(runtime.played).toContain("hero-intro");

    coordinator.setDocumentVisible(false);
    expect(runtime.getRunningAnimationCount()).toBe(0);
    coordinator.setMotionPolicy({ reducedMotion: true, saveData: false });
    expect(coordinator.getState()).toMatchObject({ activeOwner: null, mode: "manual", substate: "idle" });
    coordinator.dispose();
    expect(runtime.getRunningAnimationCount()).toBe(0);
  });
});
