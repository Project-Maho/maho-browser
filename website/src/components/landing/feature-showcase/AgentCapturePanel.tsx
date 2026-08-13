import React from "react";

export function AgentCapturePanel(): React.JSX.Element {
  return (
    <div
      data-scene-overlay="ai-panel"
      data-agent-panel-position="right-docked"
      role="img"
      aria-label="Maho Agent conversation"
      className="absolute inset-y-0 right-0 z-20 w-[48%] overflow-hidden shadow-[-1.2cqw_0_3cqw_rgba(0,0,0,0.24)] transition-transform duration-[350ms] ease-out before:pointer-events-none before:absolute before:inset-y-0 before:left-0 before:z-10 before:w-px before:bg-white/[0.1]"
    >
      <img
        data-scene-image="agent-panel"
        src="/demo/ref-agent-panel.png"
        alt=""
        aria-hidden="true"
        width={653}
        height={860}
        draggable={false}
        className="block h-full w-full object-contain object-right"
      />
    </div>
  );
}
