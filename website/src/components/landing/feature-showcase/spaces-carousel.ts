import { clampProgress } from "./timeline";

export type WorkspaceIndex = 0 | 1 | 2 | 3;
export type BlendTriple = readonly [number, number, number, number];
export type BodyTransform = {
  readonly translateXPct: number;
  readonly opacity: number;
};

const TRANSITIONS = {
  first: {
    slide: { start: 0.24, end: 0.36 },
    tint: { start: 0.24, end: 0.37 },
    content: { start: 0.25, end: 0.37 },
    address: 0.32,
    dot: 0.32,
  },
  second: {
    slide: { start: 0.45, end: 0.57 },
    tint: { start: 0.45, end: 0.58 },
    content: { start: 0.46, end: 0.58 },
    address: 0.53,
    dot: 0.53,
  },
  third: {
    slide: { start: 0.66, end: 0.78 },
    tint: { start: 0.66, end: 0.79 },
    content: { start: 0.67, end: 0.79 },
    address: 0.74,
    dot: 0.74,
  },
} as const;

function easeInOutCubic(value: number): number {
  return value < 0.5
    ? 4 * value * value * value
    : 1 - (-2 * value + 2) ** 3 / 2;
}

function easedRange(progress: number, start: number, end: number): number {
  const normalized = Math.max(0, Math.min(1, (progress - start) / (end - start)));
  return easeInOutCubic(normalized);
}

function getBlend(
  progress: number,
  firstRange: { readonly start: number; readonly end: number },
  secondRange: { readonly start: number; readonly end: number },
  thirdRange: { readonly start: number; readonly end: number },
): BlendTriple {
  const clamped = clampProgress(progress);
  const first = easedRange(clamped, firstRange.start, firstRange.end);
  const second = easedRange(clamped, secondRange.start, secondRange.end);
  const third = easedRange(clamped, thirdRange.start, thirdRange.end);
  return [1 - first, first * (1 - second), first * second * (1 - third), first * second * third];
}

export function getCarouselOffset(progress: number): number {
  const clamped = clampProgress(progress);
  return (
    easedRange(clamped, TRANSITIONS.first.slide.start, TRANSITIONS.first.slide.end) +
    easedRange(clamped, TRANSITIONS.second.slide.start, TRANSITIONS.second.slide.end) +
    easedRange(clamped, TRANSITIONS.third.slide.start, TRANSITIONS.third.slide.end)
  );
}

export function getBodyTransform(
  index: WorkspaceIndex,
  progress: number,
): BodyTransform {
  const offset = getCarouselOffset(progress);
  return {
    translateXPct: (index - offset) * 100,
    opacity: Math.max(0, Math.min(1, 1 - Math.abs(index - offset))),
  };
}

export function getThemeBlend(progress: number): BlendTriple {
  return getBlend(progress, TRANSITIONS.first.tint, TRANSITIONS.second.tint, TRANSITIONS.third.tint);
}

export function getContentBlend(progress: number): BlendTriple {
  return getBlend(progress, TRANSITIONS.first.content, TRANSITIONS.second.content, TRANSITIONS.third.content);
}

export function getActiveDotIndex(progress: number): WorkspaceIndex {
  const clamped = clampProgress(progress);
  if (clamped >= TRANSITIONS.third.dot) return 3;
  if (clamped >= TRANSITIONS.second.dot) return 2;
  if (clamped >= TRANSITIONS.first.dot) return 1;
  return 0;
}

export function getAddressIndex(progress: number): WorkspaceIndex {
  const clamped = clampProgress(progress);
  if (clamped >= TRANSITIONS.third.address) return 3;
  if (clamped >= TRANSITIONS.second.address) return 2;
  if (clamped >= TRANSITIONS.first.address) return 1;
  return 0;
}
