// Copyright 2026 Maho Browser. All rights reserved.

import React from 'react';
import {Heart} from 'lucide-react';

import {
  FINISH_SLIDE_OUT,
  HEART_PULSE,
  HEART_PULSE_KEYFRAMES,
} from '../motion-constants.js';
import type {MahoWelcomeStore} from '../store.js';

interface CompletionSequenceOptions {
  store: MahoWelcomeStore;
  selectedTheme: string | null;
}

export function CompletionSidebar() {
  return (
    <>
      <h1>All set?<br />Let's get rolling!</h1>
      <p>You're all set up and ready to go. Click the button below to start browsing with Maho.</p>
    </>
  );
}

export async function runCompletionSequence(
    {store, selectedTheme}: CompletionSequenceOptions): Promise<void> {
  await animateSelector(
      '#completion-content',
      [
        {transform: 'translateX(0%)'},
        {transform: 'translateX(100%)'},
      ],
      {
        duration: 500,
        easing: 'cubic-bezier(0.32, 0.72, 0, 1)',
        fill: 'forwards',
      });
  await animateSelector(
      '#completion-heart',
      [
        {opacity: HEART_PULSE_KEYFRAMES.opacity[0], transform: `scale(${HEART_PULSE_KEYFRAMES.scale[0]})`},
        {opacity: HEART_PULSE_KEYFRAMES.opacity[1], transform: `scale(${HEART_PULSE_KEYFRAMES.scale[1]})`},
        {opacity: HEART_PULSE_KEYFRAMES.opacity[2], transform: `scale(${HEART_PULSE_KEYFRAMES.scale[2]})`},
        {opacity: HEART_PULSE_KEYFRAMES.opacity[3], transform: `scale(${HEART_PULSE_KEYFRAMES.scale[3]})`},
        {opacity: HEART_PULSE_KEYFRAMES.opacity[4], transform: `scale(${HEART_PULSE_KEYFRAMES.scale[4]})`},
      ],
      {
        delay: HEART_PULSE.delay * 1000,
        duration: HEART_PULSE.duration * 1000,
        easing: 'cubic-bezier(0.32, 0.72, 0, 1)',
        fill: 'forwards',
      });

  store.favoriteSelectedEssentials();
  if (selectedTheme) {
    store.applyTheme(selectedTheme);
  }

  await animateSelector(
      '#onboarding-pages',
      [
        {opacity: 1},
        {opacity: 0},
      ],
      {
        duration: 400,
        easing: 'ease',
        fill: 'forwards',
      });
  store.openMainBrowser();
}

async function animateSelector(
    selector: string, keyframes: Keyframe[], options: KeyframeAnimationOptions) {
  const element = document.querySelector(selector);
  if (!element) {
    return;
  }

  const animation = element.animate(keyframes, options);
  await animation.finished.catch(() => undefined);
}

export function CompletionContent({store}: {store: MahoWelcomeStore}) {
  return (
    <div className="relative flex h-full w-full items-center justify-center">
      <div id="completion-heart" className="absolute inset-0 flex items-center justify-center text-primary/15" aria-hidden="true">
        <Heart className="size-44 fill-current" aria-hidden="true" />
      </div>
      <div id="completion-content" className="absolute inset-0 z-10 flex flex-col items-center justify-center rounded-xl border border-border bg-background/70 p-8 text-center shadow-2xl shadow-black/20 backdrop-blur-md" aria-hidden="true">
        <h2 className="m-0 mb-3 text-2xl font-semibold tracking-tight">{store.getString('IDS_MAHO_WELCOME_COMPLETION_TITLE')}</h2>
        <p className="m-0 max-w-sm text-base leading-7 text-muted-foreground">{store.getString('IDS_MAHO_WELCOME_COMPLETION_BODY')}</p>
      </div>
    </div>
  );
}
