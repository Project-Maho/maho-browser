import { describe, expect, test } from "bun:test";
import React from "react";
import { renderToStaticMarkup } from "react-dom/server";
import {
  AgentPanelSurface,
  streamTextByCharacter,
} from "../AgentPanelSurface";
import {
  AGENT_PREVIEW_DURATION_MS,
  getAgentReducedMotionProgress,
  getAgentPreviewProgress,
} from "../AgentPanelPreview";
import { TIMELINE_THRESHOLDS } from "../timeline";

describe("Agent panel preview", () => {
  test("fills the card with only the open panel surface", () => {
    const html = renderToStaticMarkup(
      React.createElement(AgentPanelSurface, {
        captureProgress: 0,
        position: "standalone",
      }),
    );

    expect(html).toContain('data-agent-panel-position="standalone"');
    expect(html).toContain('data-agent-composer="true"');
    expect(html).toContain('data-agent-quick-action="summarize"');
    expect(html).not.toContain('data-agent-background="subdued-page"');
    expect(html).not.toContain('src="/demo/youtube.png"');
  });

  test("types in the composer before creating the user bubble", () => {
    const typingProgress =
      (TIMELINE_THRESHOLDS.agent.typingStart +
        TIMELINE_THRESHOLDS.agent.typingEnd) /
      2;
    const html = renderToStaticMarkup(
      React.createElement(AgentPanelSurface, {
        captureProgress: typingProgress,
        position: "standalone",
      }),
    );

    expect(html).toContain('data-agent-composer-input="true"');
    expect(html).toContain('data-agent-panel-density="card"');
    expect(html).toContain("text-sm");
    expect(html).not.toContain("text-[0.87cqw]");
    expect(html).not.toContain('data-agent-user-message="true"');
    expect(html).not.toContain('data-agent-assistant-message="true"');
  });

  test("keeps canonical showcase sizing for the docked panel", () => {
    const html = renderToStaticMarkup(
      React.createElement(AgentPanelSurface, {
        captureProgress: TIMELINE_THRESHOLDS.agent.typingStart + 0.01,
        position: "right-docked",
      }),
    );

    expect(html).toContain('data-agent-panel-density="showcase"');
    expect(html).toContain("text-[0.87cqw]");
  });

  test("renders the sent user message and completed plain response", () => {
    const html = renderToStaticMarkup(
      React.createElement(AgentPanelSurface, {
        captureProgress: 1,
        position: "standalone",
      }),
    );

    expect(html).toContain('data-agent-user-message="true"');
    expect(html).toContain('data-agent-assistant-message="true"');
    expect(html).toContain('data-agent-stream-mode="characters"');
    expect(html).toContain('data-agent-response-action="copy"');
    expect(html).toContain('data-agent-response-action="regenerate"');
    expect(html).not.toContain('data-agent-assistant-bubble="true"');
  });

  test("streams response text with character-level cadence", () => {
    expect(streamTextByCharacter("stream", 0)).toBe("");
    expect(streamTextByCharacter("stream", 0.5)).toBe("str");
    expect(streamTextByCharacter("stream", 1)).toBe("stream");
  });

  test("maps elapsed time to a deterministic looping progress value", () => {
    expect(getAgentPreviewProgress(0)).toBe(0);
    expect(getAgentPreviewProgress(AGENT_PREVIEW_DURATION_MS / 2)).toBe(0.5);
    expect(getAgentPreviewProgress(AGENT_PREVIEW_DURATION_MS)).toBe(0);
    expect(getAgentPreviewProgress(-1)).toBe(0);
  });

  test("holds the completed response when motion is reduced", () => {
    expect(getAgentReducedMotionProgress(true)).toBe(1);
    expect(getAgentReducedMotionProgress(false)).toBe(0);
  });
});
