// Copyright 2026 Maho Browser. All rights reserved.

import React, {useEffect, useRef} from 'react';
import {ArrowRight} from 'lucide-react';

import {Button} from '@ui/button';

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
    <div
      className="relative isolate flex min-h-screen w-full items-center justify-center overflow-hidden bg-background px-6 text-center text-foreground"
      ref={splashRef}
    >
      <div aria-hidden="true" className="absolute inset-0 bg-gradient-to-br from-background via-background to-muted" />
      <div aria-hidden="true" className="absolute left-[18%] top-[18%] h-72 w-72 rounded-full bg-primary/10 blur-3xl" />
      <div aria-hidden="true" className="absolute bottom-[12%] right-[16%] h-80 w-80 rounded-full bg-muted/60 blur-3xl" />
      <h1
        className="relative z-10 m-0 max-w-5xl text-balance text-[clamp(3.5rem,6vw,8rem)] font-medium leading-[1.05] tracking-tight"
        ref={titleRef}
      >
        <span className="block opacity-0">Built for people who</span>
        <span className="block opacity-0">took browsers seriously.</span>
      </h1>
      <Button
        ref={ctaRef}
        type="button"
        size="icon"
        className="absolute bottom-[10%] left-1/2 z-10 size-24 min-w-24 -translate-x-1/2 rounded-full border-border bg-card/80 p-0 text-foreground opacity-0 shadow-2xl shadow-black/30 backdrop-blur-xl transition-transform hover:-translate-x-1/2 hover:scale-105 hover:bg-muted [&_svg]:size-8"
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
        <ArrowRight className="size-8 translate-x-[1px]" aria-hidden="true" />
      </Button>
    </div>
  );
}
