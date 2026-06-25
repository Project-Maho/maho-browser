// Copyright 2026 Maho Browser. All rights reserved.
// Locked spring/timing constants for welcome animations.
// Values derived from Zen reference dossier (.sisyphus/plans/zen-onboarding-reference-2026-06-10.md).
// DO NOT change without design approval — these are the parity contract.

/** Splash title span entry — Zen `motion` spring {stiffness:300, damping:20, mass:1.8} */
export const SPLASH_SPRING = {
  type: 'spring' as const,
  stiffness: 300,
  damping: 20,
  mass: 1.8,
};

/** Splash exit cubic-bezier ease + bounce */
export const SPLASH_EXIT_EASE = [0.755, 0.05, 0.855, 0.06] as const;
export const SPLASH_EXIT_BOUNCE = 0.4;

/** Stagger delays */
export const SPLASH_SPAN_STAGGER = 0.6; // seconds between word spans
export const SPLASH_CTA_DELAY = 0.1; // CTA fade-in after spans complete

/** Page transition (enter) — sidebar children, content fade */
export const PAGE_TRANSITION_ENTER = {
  type: 'spring' as const,
  bounce: 0.2,
};
export const PAGE_TRANSITION_ENTER_STAGGER_SIDEBAR = 0.05;
export const PAGE_TRANSITION_ENTER_STAGGER_BUTTONS = 0.1;
export const PAGE_TRANSITION_ENTER_BUTTON_START_DELAY = 0.4;

/** Page transition (exit) — bounce 0 for clean slide-out */
export const PAGE_TRANSITION_EXIT = {
  type: 'spring' as const,
  bounce: 0,
};
export const PAGE_TRANSITION_EXIT_START_DELAY = 0.3;
export const PAGE_TRANSITION_EXIT_DURATION = 0.1; // for opacity content fade

/** Heart pulse on completion — 1.5s + 0.2s delay */
export const HEART_PULSE = {
  duration: 1.5,
  delay: 0.2,
  bounce: 0,
};
export const HEART_PULSE_KEYFRAMES = {
  opacity: [0, 1, 1, 1, 0],
  scale: [0.5, 1, 1.2, 1, 1.2],
};

/** Final slide-out (welcome content sliding right before chrome reveal) */
export const FINISH_SLIDE_OUT = {
  type: 'spring' as const,
  bounce: 0,
};

/** Theme picker dot animation (color-wheel reposition) */
export const THEME_DOT_MOVE = {
  duration: 0.4,
  type: 'spring' as const,
  bounce: 0.3,
};
