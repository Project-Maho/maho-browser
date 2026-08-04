import React from "react";
import { Search, Sparkles } from "lucide-react";
import { DemoPageSurface } from "./DemoPageSurface";
import { CANONICAL_CANVAS, TIMELINE_THRESHOLDS } from "./timeline";

const COMMAND_PALETTE_HEIGHT_RATIO = 0.29;

export interface CommandBarSceneProps {
  readonly captureProgress: number;
}

export const CommandBarScene: React.FC<CommandBarSceneProps> = ({ captureProgress }) => {
  const { opacityStart, opacityEnd } = TIMELINE_THRESHOLDS.commandBar;
  const revealRatio = Math.max(0, Math.min(1, (captureProgress - opacityStart) / (opacityEnd - opacityStart)));

  return (
    <div data-scene-root="command-bar" className="relative h-full w-full overflow-hidden bg-[var(--maho-window)]">
      <div data-command-background="document" className="absolute inset-0">
        <DemoPageSurface variant="article" mode="compact" />
      </div>
      <div
        data-scene-overlay="command-palette"
        data-command-width-ratio={CANONICAL_CANVAS.commandBarWidthRatio}
        data-command-height-ratio={COMMAND_PALETTE_HEIGHT_RATIO}
        className="absolute top-[42%] z-20 overflow-hidden rounded-[1.1cqw] border border-white/12 bg-zinc-950/94 p-[1cqw] text-zinc-100 shadow-[0_1.7cqw_5cqw_rgba(0,0,0,0.48)] transition-opacity duration-[350ms] ease-out"
        style={{
          left: `${((1 - CANONICAL_CANVAS.commandBarWidthRatio) / 2) * 100}%`,
          width: `${CANONICAL_CANVAS.commandBarWidthRatio * 100}%`,
          height: `${COMMAND_PALETTE_HEIGHT_RATIO * 100}%`,
          opacity: 0.72 + revealRatio * 0.28,
        }}
      >
        <div className="flex items-center gap-[0.7cqw] border-b border-white/10 pb-[0.75cqw]">
          <Search className="h-[1cqw] w-[1cqw] text-zinc-400" />
          <div data-command-input="true" className="min-w-0 flex-1 truncate text-[0.95cqw] font-medium text-zinc-100 selection:bg-slate-500/70">https://longreads.com/</div>
          <div className="flex rounded-[0.55cqw] border border-white/10 bg-white/10 p-[0.16cqw] text-[0.68cqw] font-semibold">
            <span className="rounded-[0.42cqw] bg-zinc-950 px-[0.65cqw] py-[0.28cqw] text-white">Search</span>
            <span className="px-[0.65cqw] py-[0.28cqw] text-zinc-300">Ask Maho</span>
          </div>
        </div>
        <div className="mt-[0.55cqw] space-y-[0.18cqw]">
          {["The AI workspace that works for you. | Notion", "canva.com", "Homepage - Longreads", "Google", "Wolfram|Alpha: Computational Intelligence"].map((label) => {
            const selected = label === "Homepage - Longreads";
            return (
              <div
                key={label}
                data-command-result={selected ? "selected" : "item"}
                className={`flex h-[2.35cqw] items-center gap-[0.7cqw] rounded-[0.65cqw] px-[0.7cqw] text-[0.82cqw] ${selected ? "bg-rose-200/24 text-white" : "text-zinc-300"}`}
              >
                <span className="grid h-[1.15cqw] w-[1.15cqw] place-items-center rounded-[0.32cqw] bg-white/12 text-[0.62cqw] font-bold">{label[0]}</span>
                <span className="truncate font-medium">{label}</span>
                {selected && <Sparkles data-command-shortcut="true" className="ml-auto h-[0.9cqw] w-[0.9cqw] text-zinc-300" />}
              </div>
            );
          })}
        </div>
      </div>
    </div>
  );
};
