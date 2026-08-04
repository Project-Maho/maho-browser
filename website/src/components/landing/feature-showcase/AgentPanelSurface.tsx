import React from "react";
import {
  ChevronDown,
  Copy,
  Edit3,
  Grid3X3,
  Mic,
  Plus,
  RefreshCw,
  Send,
  Settings,
  UserRound,
  X,
} from "lucide-react";
import { TIMELINE_THRESHOLDS } from "./timeline";

export interface AgentPanelSurfaceProps {
  readonly captureProgress: number;
  readonly position: "right-docked" | "standalone";
  readonly className?: string;
  readonly style?: React.CSSProperties;
}

const PROMPT = "What are the key ideas on this page?";
const RESPONSE =
  "Maho keeps the page in context while you work. Spaces organize tabs, the command bar brings actions close, and the agent only reads the page when you ask it to.";

export function streamTextByCharacter(
  text: string,
  progress: number,
): string {
  const clampedProgress = Math.max(0, Math.min(1, progress));
  return text.slice(0, Math.floor(text.length * clampedProgress));
}

function ratio(value: number, start: number, end: number): number {
  return Math.max(0, Math.min(1, (value - start) / (end - start)));
}

export const AgentPanelSurface: React.FC<AgentPanelSurfaceProps> = ({
  captureProgress,
  position,
  className = "",
  style,
}) => {
  const isCard = position === "standalone";
  const { typingStart, typingEnd, streamStart, streamEnd } =
    TIMELINE_THRESHOLDS.agent;
  const typingRatio = ratio(captureProgress, typingStart, typingEnd);
  const streamRatio = ratio(captureProgress, streamStart, streamEnd);
  const isTyping = typingRatio > 0 && captureProgress < typingEnd;
  const isSent = captureProgress >= typingEnd;
  const typedPrompt = PROMPT.slice(0, Math.round(PROMPT.length * typingRatio));
  const streamedResponse = streamTextByCharacter(RESPONSE, streamRatio);

  return (
    <section
      data-scene-overlay="ai-panel"
      data-agent-panel-position={position}
      data-agent-panel-density={isCard ? "card" : "showcase"}
      aria-label="Maho Agent conversation"
      className={`flex h-full w-full flex-col overflow-hidden bg-[#1b1b1d] text-[#f1f1f2] ${className}`}
      style={style}
    >
      <div
        data-agent-topbar="true"
        className={`flex items-center border-b border-white/[0.08] ${
          isCard ? "gap-3 px-4 py-3" : "gap-[0.8cqw] px-[1.1cqw] py-[0.9cqw]"
        }`}
      >
        <span
          className={`flex items-center rounded-full border border-white/[0.08] font-medium text-white/90 ${
            isCard
              ? "h-8 gap-2 px-3 text-xs"
              : "h-[2cqw] gap-[0.45cqw] px-[0.7cqw] text-[0.76cqw]"
          }`}
        >
          <UserRound
            className={`${isCard ? "h-4 w-4" : "h-[0.9cqw] w-[0.9cqw]"} text-white/70`}
          />
          Blank
          <ChevronDown
            className={`${isCard ? "h-3.5 w-3.5" : "h-[0.8cqw] w-[0.8cqw]"} text-white/55`}
          />
        </span>
        <span
          className={`ml-auto flex items-center text-white/70 ${
            isCard ? "gap-3" : "gap-[1.05cqw]"
          }`}
        >
          <Edit3 className={isCard ? "h-4 w-4" : "h-[1.05cqw] w-[1.05cqw]"} />
          <Settings className={isCard ? "h-4 w-4" : "h-[1.05cqw] w-[1.05cqw]"} />
          <Grid3X3 className={isCard ? "h-4 w-4" : "h-[1.05cqw] w-[1.05cqw]"} />
          <X className={isCard ? "h-4 w-4" : "h-[1.05cqw] w-[1.05cqw]"} />
        </span>
      </div>

      <div
        className={`flex min-h-0 flex-1 flex-col ${
          isCard ? "px-4 pb-4" : "px-[1.25cqw] pb-[1.05cqw]"
        }`}
      >
        <div
          data-agent-conversation="true"
          className={`min-h-0 flex-1 overflow-hidden ${
            isCard ? "space-y-3 pt-4" : "space-y-[0.85cqw] pt-[1.15cqw]"
          }`}
        >
          {isSent ? (
            <div
              data-agent-user-message="true"
              className={`ml-auto max-w-[82%] bg-white/[0.1] text-white/90 ${
                isCard
                  ? "rounded-2xl rounded-tr-md px-3 py-2.5 text-sm leading-5"
                  : "rounded-[1.1cqw] rounded-tr-[0.35cqw] px-[0.85cqw] py-[0.7cqw] text-[0.78cqw] leading-[1.45]"
              }`}
            >
              {PROMPT}
            </div>
          ) : null}
          {streamRatio > 0 ? (
            <div
              data-agent-assistant-message="true"
              data-agent-stream-mode="characters"
              className={`text-white/85 ${
                isCard
                  ? "space-y-2 px-1 text-sm leading-6"
                  : "space-y-[0.35cqw] px-[0.1cqw] text-[0.78cqw] leading-[1.55]"
              }`}
            >
              <p>
                {streamedResponse}
                {streamRatio < 1 ? (
                  <span
                    className={`ml-0.5 inline-block w-px translate-y-[0.13cqw] bg-white/65 ${
                      isCard ? "h-4" : "h-[0.8cqw]"
                    }`}
                  />
                ) : null}
              </p>
              {streamRatio >= 1 ? (
                <div
                  className={`flex text-white/50 ${
                    isCard ? "gap-2" : "gap-[0.5cqw]"
                  }`}
                >
                  <Copy
                    data-agent-response-action="copy"
                    className={isCard ? "h-4 w-4" : "h-[0.9cqw] w-[0.9cqw]"}
                  />
                  <RefreshCw
                    data-agent-response-action="regenerate"
                    className={isCard ? "h-4 w-4" : "h-[0.9cqw] w-[0.9cqw]"}
                  />
                </div>
              ) : null}
            </div>
          ) : null}
        </div>

        {typingRatio === 0 ? (
          <div
            className={`flex justify-center ${
              isCard ? "mb-3 gap-2" : "mb-[0.9cqw] gap-[0.55cqw]"
            }`}
          >
            <span
              data-agent-quick-action="summarize"
              className={`rounded-full border border-white/[0.09] bg-white/[0.035] font-medium text-white/85 ${
                isCard
                  ? "px-3 py-2 text-xs"
                  : "px-[1cqw] py-[0.5cqw] text-[0.75cqw]"
              }`}
            >
              Summarize
            </span>
            <span
              data-agent-quick-action="quiz-me"
              className={`rounded-full border border-white/[0.09] bg-white/[0.035] font-medium text-white/85 ${
                isCard
                  ? "px-3 py-2 text-xs"
                  : "px-[1cqw] py-[0.5cqw] text-[0.75cqw]"
              }`}
            >
              Quiz me
            </span>
          </div>
        ) : null}

        <div
          data-agent-composer="true"
          className={`border border-white/[0.12] bg-[#19191b] shadow-[0_0.6cqw_1.8cqw_rgba(0,0,0,0.17)] ${
            isCard ? "rounded-2xl p-4" : "rounded-[1.15cqw] p-[1cqw]"
          }`}
        >
          <div
            data-agent-composer-input="true"
            className={`font-medium tracking-[-0.015em] text-white/80 ${
              isCard ? "min-h-16 text-sm leading-5" : "min-h-[3.2cqw] text-[0.87cqw]"
            }`}
          >
            {isTyping ? typedPrompt : "Ask a question about this page…"}
            {isTyping ? (
              <span
                className={`ml-0.5 inline-block w-px translate-y-[0.14cqw] bg-white/65 ${
                  isCard ? "h-4" : "h-[0.9cqw]"
                }`}
              />
            ) : null}
          </div>
          <div
            className={`flex items-center text-white/55 ${
              isCard
                ? "mt-3 gap-3 text-xs"
                : "mt-[0.85cqw] gap-[0.55cqw] text-[0.66cqw]"
            }`}
          >
            <Plus
              className={`mr-auto text-white/75 ${
                isCard ? "h-4 w-4" : "h-[1cqw] w-[1cqw]"
              }`}
            />
            {!isCard ? (
              <>
                <span className="inline-flex items-center gap-[0.35cqw] rounded-full bg-white/[0.06] px-[0.65cqw] py-[0.42cqw]">
                  OpenAI-compatible
                  <ChevronDown className="h-[0.65cqw] w-[0.65cqw]" />
                </span>
                <span className="inline-flex items-center gap-[0.35cqw] rounded-full bg-white/[0.06] px-[0.65cqw] py-[0.42cqw]">
                  gpt-5.6-luna
                  <ChevronDown className="h-[0.65cqw] w-[0.65cqw]" />
                </span>
                <span className="inline-flex items-center gap-[0.35cqw] rounded-full bg-white/[0.06] px-[0.65cqw] py-[0.42cqw]">
                  Medium
                  <ChevronDown className="h-[0.65cqw] w-[0.65cqw]" />
                </span>
              </>
            ) : null}
            <Mic
              className={`text-white/75 ${
                isCard ? "h-4 w-4" : "ml-[0.1cqw] h-[0.9cqw] w-[0.9cqw]"
              }`}
            />
            <span
              data-agent-submit="true"
              className={`grid place-items-center rounded-full bg-white/75 text-[#242427] ${
                isCard ? "h-8 w-8" : "h-[1.8cqw] w-[1.8cqw]"
              }`}
            >
              <Send className={isCard ? "h-4 w-4" : "h-[0.9cqw] w-[0.9cqw]"} />
            </span>
          </div>
        </div>
      </div>
    </section>
  );
};
