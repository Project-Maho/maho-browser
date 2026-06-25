// Copyright 2026 Maho Browser. All rights reserved.

import React from 'react';

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
      '#welcome-content',
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
      '#welcome-heart',
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
      '#welcome-pages',
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
    <div className="welcome-completion-stage">
      <div id="welcome-heart" className="welcome-heart" aria-hidden="true" />
      <div id="welcome-content" className="welcome-completion-pane" aria-hidden="true">
        <h2 className="welcome-completion__title">{store.getString('IDS_MAHO_WELCOME_COMPLETION_TITLE')}</h2>
        <p className="welcome-completion__tagline">{store.getString('IDS_MAHO_WELCOME_COMPLETION_BODY')}</p>
      </div>
    </div>
  );
}
