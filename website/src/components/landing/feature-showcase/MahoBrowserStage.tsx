import React, { forwardRef, useEffect, useImperativeHandle, useRef, useState } from "react";
import { MahoBrowserMock } from "./MahoBrowserMock";
import { SCENE_REGISTRY } from "./data/scenes";
import { MotionRuntime } from "./motion/MotionRuntime";
import { CAMERA_TARGETS, EASING_FAMILIES, HANDOFF_TIMING_MS, type RuntimeSubstate, type SceneMotionSpec } from "./motionContract";
import type { SceneKey } from "./timeline";

export interface MahoBrowserStageProps {
  readonly profile?: "hero" | "showcase" | "capture";
  readonly scene?: SceneKey;
  readonly themeColor?: string;
  readonly progress?: number;
  readonly substate?: RuntimeSubstate;
  readonly className?: string;
  readonly children?: React.ReactNode;
}

export interface MahoBrowserStageHandle {
  readonly runtime: MotionRuntime;
}

function actionTransform(scene: SceneKey): string {
  switch (scene) {
    case "spaces": return "translate3d(-2%, 0, 0) scale(0.99)";
    case "split-view": return "translate3d(0, 1.5%, 0) scale(0.985)";
    case "command-bar": return "translate3d(0, -0.6%, 0) scale(0.997)";
    case "agent": return "translate3d(1%, 0, 0) scale(0.997)";
  }
}

function createSpec(scene: SceneKey, profile: "hero" | "showcase" | "capture"): SceneMotionSpec {
  const target = profile === "hero" && scene === "spaces" ? CAMERA_TARGETS.hero : CAMERA_TARGETS[scene];
  const targetOrigin = `${target.focusX * 100}% ${target.focusY * 100}%`;
  const neutral = { transform: "scale(1)", transformOrigin: "50% 50%", opacity: 1 };
  const focused = { transform: `scale(${target.maxScale})`, transformOrigin: targetOrigin, opacity: 1 };
  return {
    sceneKey: scene,
    cameraTarget: target,
    representativeHoldProgress: SCENE_REGISTRY[scene].representativeHoldProgress,
    segments: [
      {
        name: "handoff",
        owner: "camera",
        durationMs: HANDOFF_TIMING_MS,
        easing: EASING_FAMILIES.contentTransition,
        keyframes: [neutral, { transform: "translate3d(0, 10px, 0) scale(0.985)", opacity: 0.82, offset: 0.3 }, neutral],
      },
      {
        name: "zoom-in",
        owner: "camera",
        durationMs: 900,
        easing: EASING_FAMILIES.zoomIn,
        keyframes: [neutral, focused],
      },
      {
        name: "action",
        owner: "action",
        durationMs: scene === "agent" ? 1700 : 1100,
        easing: EASING_FAMILIES.contentTransition,
        keyframes: [{ transform: actionTransform(scene), opacity: 0.72 }, { transform: "translate3d(0, 0, 0) scale(1)", opacity: 1 }],
      },
      {
        name: "zoom-out",
        owner: "camera",
        durationMs: 800,
        easing: EASING_FAMILIES.zoomOut,
        keyframes: [focused, neutral],
      },
      {
        name: "hero-intro",
        owner: "camera",
        durationMs: 8000,
        easing: "linear",
        keyframes: [neutral, { ...focused, offset: 0.32 }, { ...focused, offset: 0.68 }, { ...neutral, offset: 0.84 }, neutral],
      },
    ],
  };
}

function snapshotProgress(scene: SceneKey, substate: RuntimeSubstate, captureProgress: number | undefined): number {
  if (captureProgress !== undefined) return captureProgress;
  switch (substate) {
    case "action": return scene === "agent" ? 0.58 : 0.62;
    case "hold": return SCENE_REGISTRY[scene].representativeHoldProgress;
    case "idle":
    case "waiting-ready":
    case "handoff":
    case "zoom-in":
    case "zoom-out":
    case "suspended": return 0;
  }
}

export const MahoBrowserStage = forwardRef<MahoBrowserStageHandle, MahoBrowserStageProps>(
  ({ profile = "showcase", scene = "spaces", themeColor = "#b58ad8", progress, substate = "idle", className = "", children }, ref) => {
    const responsiveShellRef = useRef<HTMLDivElement>(null);
    const cameraRef = useRef<HTMLDivElement>(null);
    const actionRef = useRef<HTMLDivElement>(null);
    const pointerRef = useRef<HTMLDivElement>(null);
    const textRef = useRef<HTMLDivElement>(null);
    const [runtime] = useState(() => new MotionRuntime({
      surface: profile,
      owners: {
        get camera() { return cameraRef.current ?? detachedMotionElement; },
        get action() { return actionRef.current ?? detachedMotionElement; },
        get pointer() { return pointerRef.current ?? detachedMotionElement; },
        get text() { return textRef.current ?? detachedMotionElement; },
      },
    }));

    useImperativeHandle(ref, () => ({ runtime }), [runtime]);
    useEffect(() => {
      runtime.registerSpec(createSpec(scene, profile));
    }, [profile, runtime, scene]);
    useEffect(() => {
      return () => runtime.dispose();
    }, [runtime]);

    const target = profile === "hero" && scene === "spaces" ? CAMERA_TARGETS.hero : CAMERA_TARGETS[scene];
    const captureProgress = snapshotProgress(scene, substate, progress);
    return (
      <div ref={responsiveShellRef} data-owner="responsive-shell" data-stage-profile={profile} data-stage-scene={scene}
        data-camera-focus-x={target.focusX} data-camera-focus-y={target.focusY} data-camera-max-scale={target.maxScale}
        className={`w-full h-full relative overflow-hidden ${className}`} style={{ containerType: "inline-size" }}>
        <div ref={cameraRef} data-owner="camera-stage" className="w-full h-full relative">
          <div ref={actionRef} data-owner="scene-action" className="w-full h-full relative">
            <MahoBrowserMock
              themeColor={themeColor}
              scene={scene}
              captureProgress={captureProgress}
              staticCapture={profile === "capture"}
            />
            <div ref={pointerRef} data-owner="pointer-drag" className="absolute inset-0 pointer-events-none z-30" />
            <div ref={textRef} data-owner="text-track" className="absolute inset-0 pointer-events-none z-40" />
            {children}
          </div>
        </div>
      </div>
    );
  },
);

const detachedMotionElement = {
  style: { setProperty: () => undefined, removeProperty: () => undefined },
  animate: () => ({ currentTime: 0, playState: "finished" as const, finished: Promise.resolve(), pause: () => undefined, play: () => undefined, commitStyles: () => undefined, cancel: () => undefined }),
};

MahoBrowserStage.displayName = "MahoBrowserStage";
