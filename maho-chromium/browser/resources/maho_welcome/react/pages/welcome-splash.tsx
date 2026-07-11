// Copyright 2026 Maho Browser. All rights reserved.

import React, {useEffect, useRef} from 'react';

import {
  SPLASH_CTA_DELAY,
  SPLASH_EXIT_EASE,
  SPLASH_SPAN_STAGGER,
} from '../motion-constants.js';

export function WelcomeSplash({onStart}: {onStart: () => void}) {
  const splashRef = useRef<HTMLDivElement>(null);
  const titleRef = useRef<HTMLHeadingElement>(null);
  const ctaRef = useRef<HTMLButtonElement>(null);

  useEffect(() => {
    const animations: Animation[] = [];
    const spans = Array.from(titleRef.current?.querySelectorAll('span') ?? []);

    if (spans.length) {
      spans.forEach((span, index) => {
        const animation = span.animate(
            [
              {opacity: 0, transform: 'translateY(20px)', filter: 'blur(2px)'},
              {opacity: 1, transform: 'translateY(0)', filter: 'blur(0px)'},
            ],
            {
              delay: (0.2 + index * SPLASH_SPAN_STAGGER) * 1000,
              duration: 700,
              easing: 'cubic-bezier(0.22, 1.18, 0.36, 1)',
              fill: 'forwards',
            });
        animations.push(animation);
      });
    }

    if (ctaRef.current) {
      const animation = ctaRef.current.animate(
          [
            {opacity: 0, transform: 'translateX(-50%) translateY(20px)', filter: 'blur(2px)'},
            {opacity: 1, transform: 'translateX(-50%) translateY(0)', filter: 'blur(0px)'},
          ],
          {
            delay: (0.2 + SPLASH_SPAN_STAGGER + SPLASH_CTA_DELAY) * 1000,
            duration: 700,
            easing: 'cubic-bezier(0.22, 1.18, 0.36, 1)',
            fill: 'forwards',
          });
      animations.push(animation);
    }

    return () => {
      animations.forEach(animation => {
        animation.cancel();
      });
    };
  }, []);

  return (
    <div className="welcome-splash" ref={splashRef}>
      <h1 className="welcome-splash__title" ref={titleRef}>
        <span>Built for people who</span>
        <span>took browsers seriously.</span>
      </h1>
      <button
        ref={ctaRef}
        type="button"
        className="welcome-splash__cta"
        aria-label="Get started"
        onClick={async () => {
          const container = splashRef.current;
          if (!container) {
            onStart();
            return;
          }
          const exit = container.animate(
              [
                {opacity: 1, transform: 'translateY(0)', filter: 'blur(0px)'},
                {opacity: 0, transform: 'translateY(-100%)', filter: 'blur(8px)'},
              ],
              {
                duration: 400,
                easing: `cubic-bezier(${SPLASH_EXIT_EASE.join(',')})`,
                fill: 'forwards',
              });
          await exit.finished.catch(() => undefined);
          onStart();
        }}>
        <svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
          <line x1="5" y1="12" x2="19" y2="12"></line>
          <polyline points="12 5 19 12 12 19"></polyline>
        </svg>
      </button>
    </div>
  );
}
