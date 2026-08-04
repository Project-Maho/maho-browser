import React from "react";
import type { SceneKey } from "../timeline";
import type { WorkspaceIndex } from "../spaces-carousel";
import { SpacesScene } from "../SpacesScene";
import { SplitViewScene } from "../SplitViewScene";
import { CommandBarScene } from "../CommandBarScene";
import { AgentScene } from "../AgentScene";
import { WORKSPACES } from "./workspaces";

export interface SceneEntry {
  readonly key: SceneKey;
  readonly Component: React.FC<{ captureProgress: number }>;
  readonly defaultAddress: string;
  readonly defaultActiveDot: 0 | 1 | 2 | 3;
  readonly defaultWorkspaceIndex: WorkspaceIndex;
  readonly defaultActiveRowLabel?: string;
  readonly usesSpacesCarousel: boolean;
  readonly representativeHoldProgress: number;
}

export const CAPTURE_SCENES: readonly SceneKey[] = [
  "spaces",
  "split-view",
  "command-bar",
  "agent",
] as const;

export const SCENE_REGISTRY: Readonly<Record<SceneKey, SceneEntry>> = Object.freeze({
  spaces: {
    key: "spaces",
    Component: SpacesScene,
    defaultAddress: WORKSPACES[0].address,
    defaultActiveDot: 0,
    defaultWorkspaceIndex: 0,
    usesSpacesCarousel: true,
    representativeHoldProgress: 0.78,
  },
  "split-view": {
    key: "split-view",
    Component: SplitViewScene,
    defaultAddress: "aeon.co",
    defaultActiveDot: 3,
    defaultWorkspaceIndex: 3,
    defaultActiveRowLabel: "Aeon",
    usesSpacesCarousel: false,
    representativeHoldProgress: 0.78,
  },
  "command-bar": {
    key: "command-bar",
    Component: CommandBarScene,
    defaultAddress: "longreads.com",
    defaultActiveDot: 3,
    defaultWorkspaceIndex: 3,
    defaultActiveRowLabel: "Longreads",
    usesSpacesCarousel: false,
    representativeHoldProgress: 0.78,
  },
  agent: {
    key: "agent",
    Component: AgentScene,
    defaultAddress: "youtube.com",
    defaultActiveDot: 2,
    defaultWorkspaceIndex: 1,
    usesSpacesCarousel: false,
    representativeHoldProgress: 0.95,
  },
});

export function isSceneKey(value: unknown): value is SceneKey {
  return typeof value === "string" && Object.prototype.hasOwnProperty.call(SCENE_REGISTRY, value);
}
