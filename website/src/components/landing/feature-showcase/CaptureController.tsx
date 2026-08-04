import React, { useEffect, useRef, useState } from "react";
import { MahoBrowserStage } from "./MahoBrowserStage";
import {
  DEFAULT_CAPTURE_PARAMS,
  type CaptureParams,
  parseCaptureParams,
} from "./captureParams";
import {
  type ReadinessResult,
  type ReadinessStage,
  waitForCaptureReadiness,
} from "./captureReadiness";

type CaptureReadinessState =
  | { readonly status: "loading" }
  | { readonly status: "ready" }
  | { readonly status: "error"; readonly stage: ReadinessStage; readonly error: string };

export const CaptureController: React.FC = () => {
  const containerRef = useRef<HTMLDivElement>(null);
  const [params, setParams] = useState<CaptureParams | null>(null);
  const [readinessState, setReadinessState] = useState<CaptureReadinessState>({ status: "loading" });

  useEffect(() => {
    setParams(parseCaptureParams(new URLSearchParams(window.location.search)));
  }, []);

  useEffect(() => {
    setReadinessState({ status: "loading" });
    if (params === null) {
      return;
    }

    const controller = new AbortController();
    void waitForCaptureReadiness({
      signal: controller.signal,
      fontsReady: document.fonts.ready,
      getImages: () => Array.from(containerRef.current?.querySelectorAll("img") ?? []),
      requestFrame: (callback) => window.requestAnimationFrame(callback),
      cancelFrame: (handle) => window.cancelAnimationFrame(handle),
    }).then((result: ReadinessResult) => {
      if (controller.signal.aborted) {
        return;
      }
      if (result.ready) {
        setReadinessState({ status: "ready" });
      } else {
        setReadinessState({
          status: "error",
          stage: result.stage,
          error: result.error,
        });
      }
    });

    return () => controller.abort();
  }, [params]);

  const renderParams = params ?? DEFAULT_CAPTURE_PARAMS;

  const width = renderParams.profile === "reference" ? 1440 : 1360;
  const height = renderParams.profile === "reference" ? 1118 : 860;

  return (
    <div
      ref={containerRef}
      id="capture-host"
      data-capture-state={readinessState.status}
      data-capture-ready={readinessState.status === "ready" ? "true" : "false"}
      data-capture-error-stage={readinessState.status === "error" ? readinessState.stage : undefined}
      data-capture-error={readinessState.status === "error" ? `${readinessState.stage}:${readinessState.error}` : undefined}
      data-surface={renderParams.surface}
      data-scene={renderParams.scene}
      data-theme={renderParams.theme}
      data-progress={renderParams.progress}
      data-profile={renderParams.profile}
      style={{
        width: `${width}px`,
        height: `${height}px`,
        position: "relative",
        overflow: "hidden",
        background: "#000",
      }}
    >
      <MahoBrowserStage
        profile="capture"
        scene={renderParams.scene}
        themeColor={renderParams.theme}
        progress={renderParams.progress}
      />
    </div>
  );
};
