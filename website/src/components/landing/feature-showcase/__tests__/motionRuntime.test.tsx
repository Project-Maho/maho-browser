import { describe, expect, test } from "bun:test";
import React from "react";
import { renderToStaticMarkup } from "react-dom/server";
import { MahoBrowserStage } from "../MahoBrowserStage";
import { MotionRuntime, type MotionAnimation, type MotionElement } from "../motion/MotionRuntime";
import type { SceneMotionSpec } from "../motionContract";

class FakeAnimation implements MotionAnimation {
  public currentTime: number | null = 0;
  public playState: AnimationPlayState = "running";
  public readonly finished: Promise<void>;
  public commitCount = 0;
  public cancelCount = 0;
  private finishRun: (() => void) | null = null;

  public constructor(private readonly onCancel: () => void = () => undefined) {
    this.finished = new Promise((resolve) => {
      this.finishRun = resolve;
    });
  }

  public pause(): void {
    this.playState = "paused";
  }

  public play(): void {
    this.playState = "running";
  }

  public cancel(): void {
    this.cancelCount += 1;
    this.playState = "idle";
    this.onCancel();
  }

  public commitStyles(): void {
    this.commitCount += 1;
  }

  public finish(): void {
    this.playState = "finished";
    this.finishRun?.();
  }
}

function createElement(animations: FakeAnimation[], retainedAnimations?: Set<FakeAnimation>): MotionElement {
  return {
    style: {
      setProperty: () => undefined,
      removeProperty: () => undefined,
    },
    animate: () => {
      let animation: FakeAnimation;
      animation = new FakeAnimation(() => retainedAnimations?.delete(animation));
      animations.push(animation);
      retainedAnimations?.add(animation);
      return animation;
    },
  };
}

const SPEC: SceneMotionSpec = {
  sceneKey: "spaces",
  cameraTarget: { focusX: 0.18, focusY: 0.58, maxScale: 1.16 },
  representativeHoldProgress: 0.78,
  segments: [
    {
      name: "zoom-in",
      owner: "camera",
      durationMs: 1000,
      easing: "linear",
      keyframes: [{ transform: "scale(1)" }, { transform: "scale(1.16)" }],
    },
  ],
};

describe("MotionRuntime", () => {
  test("renders stable transform owners without a live progress prop", () => {
    const html = renderToStaticMarkup(<MahoBrowserStage profile="showcase" scene="spaces" />);
    const owners = ["responsive-shell", "camera-stage", "scene-action", "pointer-drag", "text-track"];
    const positions = owners.map((owner) => html.indexOf(`data-owner=\"${owner}\"`));
    expect(positions.every((position) => position >= 0)).toBe(true);
    expect(positions).toEqual([...positions].sort((left, right) => left - right));
    expect(html).not.toContain("data-live-progress");
  });

  test("pauses and resumes the same finite WAAPI run at exact currentTime", async () => {
    const animations: FakeAnimation[] = [];
    const camera = createElement(animations);
    const runtime = new MotionRuntime({ surface: "hero", owners: { camera, action: camera } });
    runtime.registerSpec(SPEC);

    const run = runtime.playSegment("zoom-in");
    const animation = animations[0];
    expect(animation).toBeDefined();
    if (!animation) return;
    animation.currentTime = 437;
    runtime.pause();
    runtime.resume();

    expect(animation.currentTime).toBe(437);
    expect(animation.playState).toBe("running");
    animation.finish();
    await run;
    expect(runtime.getRunningAnimationCount()).toBe(0);
    expect(animation.commitCount).toBe(1);
    expect(animation.cancelCount).toBe(1);
  });

  test("generation tokens prevent stale completion from clearing a retargeted run", async () => {
    const animations: FakeAnimation[] = [];
    const camera = createElement(animations);
    const runtime = new MotionRuntime({ surface: "showcase", owners: { camera, action: camera } });
    runtime.registerSpec(SPEC);

    const first = runtime.playSegment("zoom-in");
    const second = runtime.playSegment("zoom-in");
    animations[0]?.finish();
    await first;
    expect(runtime.getRunningAnimationCount()).toBe(1);
    animations[1]?.finish();
    await second;
    expect(runtime.getRunningAnimationCount()).toBe(0);
  });

  test("repeated completed segments release finished effects instead of accumulating them", async () => {
    const animations: FakeAnimation[] = [];
    const retainedAnimations = new Set<FakeAnimation>();
    const camera = createElement(animations, retainedAnimations);
    const runtime = new MotionRuntime({ surface: "hero", owners: { camera, action: camera } });
    runtime.registerSpec(SPEC);

    for (let loop = 0; loop < 4; loop += 1) {
      const run = runtime.playSegment("zoom-in");
      animations[loop]?.finish();
      await run;
      expect(retainedAnimations.size).toBe(0);
    }

    expect(animations).toHaveLength(4);
    expect(animations.every(({ commitCount, cancelCount }) => commitCount === 1 && cancelCount === 1)).toBe(true);
  });

  test("resolves scene readiness only after the matching spec is registered", async () => {
    const animations: FakeAnimation[] = [];
    const camera = createElement(animations);
    const runtime = new MotionRuntime({ surface: "showcase", owners: { camera, action: camera } });
    let ready = false;

    const waiting = runtime.waitForSpec("agent").then(() => {
      ready = true;
    });
    runtime.registerSpec(SPEC);
    await Promise.resolve();
    expect(ready).toBe(false);

    runtime.registerSpec({ ...SPEC, sceneKey: "agent" });
    await waiting;
    expect(ready).toBe(true);
  });
});
