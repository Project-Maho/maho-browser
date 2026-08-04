import React, { useEffect, useRef, useState } from "react";
import { Pause, Play, Square } from "lucide-react";
import type { FeatureItem } from "../FeaturesSection";
import { MahoBrowserStage, type MahoBrowserStageHandle } from "./MahoBrowserStage";
import { LandingMotionCoordinator, type CoordinatorState } from "./motion/LandingMotionCoordinator";
import type { RuntimeSubstate } from "./motionContract";
import { SCENES, type SceneKey } from "./timeline";

export interface ShowcaseLiveDesktopProps {
  readonly eyebrow?: string;
  readonly headline?: string;
  readonly items?: readonly FeatureItem[];
}

export const ShowcaseLiveDesktop: React.FC<ShowcaseLiveDesktopProps> = ({
  eyebrow = "EVERY FEATURE, READY TO USE",
  headline = "Built for modern web workflows",
  items,
}) => {
  const containerRef = useRef<HTMLElement>(null);
  const stageRef = useRef<MahoBrowserStageHandle>(null);
  const [activeScene, setActiveScene] = useState<SceneKey>("spaces");
  const [substate, setSubstate] = useState<RuntimeSubstate>("idle");
  const [coordinatorState, setCoordinatorState] = useState<CoordinatorState>(() =>
    LandingMotionCoordinator.getInstance().getState(),
  );

  useEffect(() => {
    const element = containerRef.current;
    const stage = stageRef.current;
    if (!element || !stage) return;
    const coordinator = LandingMotionCoordinator.getInstance();
    const unsubscribe = coordinator.subscribe(setCoordinatorState);
    const unregister = coordinator.registerSurface({
      id: "showcase",
      element,
      runtime: stage.runtime,
      setScene: setActiveScene,
      waitForScene: (scene, signal) => stage.runtime.waitForSpec(scene, signal),
      setSubstate,
    });
    return () => {
      unsubscribe();
      unregister();
    };
  }, []);

  const coordinator = LandingMotionCoordinator.getInstance();
  const isPaused = coordinatorState.activeOwner === "showcase" && (
    coordinatorState.mode === "paused" || (coordinatorState.mode === "manual" && substate === "hold")
  );
  const isAgentProcessing = activeScene === "agent" && substate === "action";
  const desktopScenes = SCENES.map((scene, index) => ({
    ...scene,
    badge: items?.[index]?.title ?? scene.badge,
    title: items?.[index]?.title ?? scene.title,
    description: items?.[index]?.body ?? scene.description,
  }));

  return (
    <section ref={containerRef} data-motion-surface="showcase" data-motion-owner={coordinatorState.activeOwner ?? "none"}
      data-motion-substate={substate} className="w-full relative py-12 md:py-20 overflow-hidden">
      <div className="max-w-[1440px] mx-auto px-6">
        <div className="text-center max-w-3xl mx-auto mb-12 space-y-3">
          <p className="landing-eyebrow text-muted-foreground">{eyebrow}</p>
          <h2 className="landing-section-title">{headline}</h2>
        </div>

        <div className="grid grid-cols-12 gap-8 items-center">
          <fieldset aria-label="Feature showcase scenes" className="col-span-4 flex flex-col gap-4">
            {desktopScenes.map((scene, index) => {
              const active = scene.key === activeScene;
              return (
                <button key={scene.key} type="button" aria-pressed={active} onClick={() => coordinator.selectScene("showcase", scene.key)}
                  className={`rounded-squircle-lg squircle-enhanced border p-5 text-left transition-colors duration-200 ${active ? "border-foreground/25 bg-card text-foreground ring-1 ring-foreground/10" : "border-border bg-card/45 text-foreground/90 hover:border-foreground/15 hover:bg-card/70"}`}>
                  <div className="flex items-center justify-between mb-2">
                    <span className={`landing-eyebrow rounded-squircle-sm border px-2.5 py-1 text-[0.65rem] ${active ? "border-foreground/20 bg-foreground/[0.08] text-foreground" : "border-border bg-background/60 text-muted-foreground"}`}>{scene.badge}</span>
                    <span className="text-xs font-mono text-muted-foreground">0{index + 1} / 04</span>
                  </div>
                  <h3 className={`landing-subsection-title mb-1 ${active ? "text-foreground" : "text-foreground/90"}`}>{scene.title}</h3>
                  <p className="text-sm leading-relaxed text-muted-foreground">{scene.description}</p>
                </button>
              );
            })}

            <div className="flex items-center gap-3 pt-2">
              <button type="button" aria-pressed={isPaused} onClick={() => isPaused ? coordinator.resume("showcase") : coordinator.pause("showcase")}
                aria-label={isPaused ? "Resume showcase animation" : "Pause showcase animation"}
                className="flex items-center gap-2 px-4 py-2 rounded-full border border-white/10 bg-white/5 text-xs font-medium text-white hover:bg-white/10 transition-colors">
                {isPaused ? <Play className="w-3.5 h-3.5" /> : <Pause className="w-3.5 h-3.5" />}
                {isPaused ? "Resume" : "Pause"}
              </button>
              {isAgentProcessing && (
                <button type="button" onClick={() => coordinator.stopAgent("showcase")} aria-label="Stop Agent demo"
                  className="flex items-center gap-2 px-4 py-2 rounded-full border border-red-500/30 bg-red-500/10 text-xs font-medium text-red-300 hover:bg-red-500/20 transition-colors">
                  <Square className="w-3.5 h-3.5 fill-current" /> Stop Agent demo
                </button>
              )}
            </div>
          </fieldset>

          <div className="col-span-8">
            <div role="img" aria-label={`Maho Browser feature showcase illustration showing ${activeScene} preview`}
              className="w-full aspect-[1440/1118] max-h-[700px] relative overflow-hidden rounded-[22px] shadow-[0_32px_90px_rgba(0,0,0,0.42)]">
              <div aria-hidden="true" className="w-full h-full pointer-events-none">
                <MahoBrowserStage ref={stageRef} profile="showcase" scene={activeScene} substate={substate} themeColor="#b58ad8" />
              </div>
            </div>
          </div>
        </div>
      </div>
    </section>
  );
};
