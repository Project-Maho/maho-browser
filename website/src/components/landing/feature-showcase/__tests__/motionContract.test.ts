import { describe, expect, test } from "bun:test";
import {
  CAMERA_TARGETS,
  HERO_PHASE_WINDOWS,
  SHOWCASE_PHASE_WINDOWS,
  initialPlaybackState,
  parseStrictProgress,
  resolveCameraTransform,
  sampleQuadraticTrajectory,
  selectMotionOwner,
  transitionPlaybackState,
} from "../motionContract";

describe("motionContract", () => {
  test("defines the binding camera and phase constants", () => {
    expect(CAMERA_TARGETS.hero).toEqual({ focusX: 0.63, focusY: 0.46, maxScale: 1.06 });
    expect(CAMERA_TARGETS.spaces).toEqual({ focusX: 0.18, focusY: 0.58, maxScale: 1.16 });
    expect(CAMERA_TARGETS["split-view"]).toEqual({ focusX: 0.59, focusY: 0.5, maxScale: 1.06 });
    expect(CAMERA_TARGETS["command-bar"]).toEqual({ focusX: 0.59, focusY: 0.51, maxScale: 1.05 });
    expect(CAMERA_TARGETS.agent).toEqual({ focusX: 0.82, focusY: 0.54, maxScale: 1.06 });
    expect(HERO_PHASE_WINDOWS.establish[0]).toBe(0);
    expect(HERO_PHASE_WINDOWS.rest[1]).toBe(1);
    expect(SHOWCASE_PHASE_WINDOWS.establish[0]).toBe(0);
    expect(SHOWCASE_PHASE_WINDOWS["zoom-out"][1]).toBe(1);
  });

  test("transitions autoplay through the exhaustive runtime substates", () => {
    const ready = transitionPlaybackState(initialPlaybackState("autoplay"), { type: "ready" });
    const zoom = transitionPlaybackState(ready, { type: "segment-finished", segment: "handoff" });
    const action = transitionPlaybackState(zoom, { type: "segment-finished", segment: "zoom-in" });
    const hold = transitionPlaybackState(action, { type: "segment-finished", segment: "action" });
    const exit = transitionPlaybackState(hold, { type: "resume" });
    const idle = transitionPlaybackState(exit, { type: "segment-finished", segment: "zoom-out" });

    expect([ready.substate, zoom.substate, action.substate, hold.substate, exit.substate, idle.substate]).toEqual([
      "handoff",
      "zoom-in",
      "action",
      "hold",
      "zoom-out",
      "idle",
    ]);
  });

  test("preserves resume mode across pause and resumes manual hold into zoom-out", () => {
    const manual = transitionPlaybackState(initialPlaybackState("autoplay"), { type: "select", scene: "agent" });
    const paused = transitionPlaybackState(manual, { type: "pause" });
    const resumed = transitionPlaybackState(paused, { type: "resume" });
    const hold = transitionPlaybackState(
      { ...resumed, mode: "manual", substate: "hold" },
      { type: "resume" },
    );

    expect(paused).toMatchObject({ mode: "paused", resumeMode: "manual", substate: "suspended" });
    expect(resumed).toMatchObject({ mode: "manual", substate: "waiting-ready", scene: "agent" });
    expect(hold).toMatchObject({ mode: "autoplay", substate: "zoom-out" });
  });

  test("retains current owners until their loss thresholds", () => {
    const autoplay = { activeOwner: "hero", heroRatio: 0.25, showcaseRatio: 0, heroManual: false, showcaseManual: false } as const;
    const manual = { activeOwner: "showcase", heroRatio: 0, showcaseRatio: 0.1, heroManual: false, showcaseManual: true } as const;

    expect(selectMotionOwner(autoplay)).toBe("hero");
    expect(selectMotionOwner({ ...autoplay, heroRatio: 0.249 })).toBeNull();
    expect(selectMotionOwner(manual)).toBe("showcase");
    expect(selectMotionOwner({ ...manual, showcaseRatio: 0.099 })).toBeNull();
  });

  test("replay, visibility, and motion-policy events settle deterministically", () => {
    const replay = transitionPlaybackState(
      { ...initialPlaybackState("manual"), scene: "agent", substate: "action" },
      { type: "replay" },
    );
    const hidden = transitionPlaybackState(replay, { type: "suspend" });
    const visible = transitionPlaybackState(hidden, { type: "restore" });
    const frozen = transitionPlaybackState(visible, { type: "freeze" });

    expect(replay).toMatchObject({ mode: "autoplay", scene: "spaces", substate: "waiting-ready" });
    expect(hidden.substate).toBe("suspended");
    expect(visible.substate).toBe("waiting-ready");
    expect(frozen).toMatchObject({ mode: "frozen", substate: "idle" });
  });

  test("keeps capture math strict and finite", () => {
    expect(resolveCameraTransform(CAMERA_TARGETS.spaces, 0).transform).toBe("scale(1)");
    expect(resolveCameraTransform(CAMERA_TARGETS.spaces, 0.5)).toEqual({
      transform: "scale(1.0800)",
      transformOrigin: "18.0% 58.0%",
    });
    expect(parseStrictProgress("0.5extra")).toBe(0);
    expect(parseStrictProgress("Infinity")).toBe(0);
    expect(parseStrictProgress("2")).toBe(1);

    const trajectory = sampleQuadraticTrajectory({ x: 0, y: 0 }, { x: 50, y: 100 }, { x: 100, y: 0 });
    expect(trajectory).toHaveLength(25);
    expect(trajectory.every(({ x, y, opacity }) => [x, y, opacity].every(Number.isFinite))).toBe(true);
  });
});
