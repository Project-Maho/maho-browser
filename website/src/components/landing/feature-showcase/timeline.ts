export type SceneKey = 'spaces' | 'split-view' | 'command-bar' | 'agent';

export interface SceneConfig {
  key: SceneKey;
  title: string;
  badge: string;
  description: string;
  durationMs: number;
}

export const CANONICAL_CANVAS = {
  width: 1440,
  height: 1118,
  sidebarWidth: 252,
  sidebarRatio: 252 / 1440, // 0.175
  contentRatio: (1440 - 252) / 1440, // 0.825
  commandBarWidthRatio: 0.805,
  commandBarTopRatio: 0.0075,
  aiPanelWidthRatio: 0.3465,
} as const;

export const SCENES: SceneConfig[] = [
  {
    key: 'spaces',
    title: 'Spaces',
    badge: 'Spaces',
    description: 'Persistent workspaces in the sidebar. Group tabs by project, client, or context. Switch instantly with ⌃1–9. Pick up where you left off.',
    durationMs: 5200,
  },
  {
    key: 'split-view',
    title: 'Split View',
    badge: 'Split View',
    description: 'Two pages side by side. Compare, reference, and multitask without switching tabs.',
    durationMs: 5600,
  },
  {
    key: 'command-bar',
    title: 'Command Bar',
    badge: 'Command Bar',
    description: 'One shortcut for everything. Navigate, search open tabs, run actions, jump between Spaces. Keyboard-first from the start.',
    durationMs: 5200,
  },
  {
    key: 'agent',
    title: 'Agent',
    badge: 'Agent',
    description: 'Analyze the current page when you ask — user-started, with your own key or local models, and always under your control. Agent never clicks, purchases, submits forms, or uses tools on its own.',
    durationMs: 6400,
  },
];

export const TOTAL_SHOWCASE_DURATION_MS = SCENES.reduce((acc, scene) => acc + scene.durationMs, 0); // 22,400ms

export const TIMELINE_THRESHOLDS = {
  splitView: {
    dragStart: 0.25,
    dragEnd: 0.34,
    settleStart: 0.34,
    settleEnd: 0.34,
  },
  commandBar: {
    opacityStart: 0.12,
    opacityEnd: 0.18,
  },
  agent: {
    slideStart: 0.10,
    slideEnd: 0.18,
    typingStart: 0.20,
    typingEnd: 0.45,
    streamStart: 0.55,
    streamEnd: 0.72,
  },
} as const;

export function clampProgress(p: number): number {
  if (typeof p !== 'number' || isNaN(p)) return 0;
  return Math.max(0, Math.min(1, p));
}

export type SplitPhase = 'idle' | 'drag' | 'settling' | 'settled';

export type SplitMotion = {
  readonly phase: SplitPhase;
  readonly dragRatio: number;
  readonly revealRatio: number;
};

function progressWithin(value: number, start: number, end: number): number {
  return Math.max(0, Math.min(1, (value - start) / (end - start)));
}

export function getSplitMotion(p: number): SplitMotion {
  const { dragStart, dragEnd } = TIMELINE_THRESHOLDS.splitView;
  const clamped = clampProgress(p);
  const dragRatio = progressWithin(clamped, dragStart, dragEnd);
  const revealRatio = clamped >= dragEnd ? 1 : 0;
  const phase: SplitPhase = clamped < dragStart
    ? 'idle'
    : clamped < dragEnd
      ? 'drag'
      : 'settled';

  return { phase, dragRatio, revealRatio };
}

export function getSplitPhase(p: number): SplitPhase {
  return getSplitMotion(p).phase;
}
