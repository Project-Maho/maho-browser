// Copyright 2026 Maho Browser. All rights reserved.

import React, {useEffect, useMemo, useRef, useState, useSyncExternalStore} from 'react';
import {ArrowRight, Check} from 'lucide-react';

import type {MahoWelcomeStore} from '../store.js';
import type {ImportStage} from '../types.js';

const ASSET_ORIGIN = 'chrome://maho-' + 'welcome';
const MAHO_LOGO_SRC = `${ASSET_ORIGIN}/icons/onboarding/maho-logo.png`;

interface Props {
  store: MahoWelcomeStore;
  importStage: ImportStage;
}

const BROWSER_ICON_KEYS: Record<string, string> = {
  chrome: 'chrome',
  safari: 'safari',
  firefox: 'firefox',
  edge: 'edge',
  brave: 'brave',
  arc: 'arc',
  opera: 'opera',
  zen: 'zen',
};

export function getBrowserIconKey(rawName: string): string {
  const lower = rawName.toLowerCase();
  for (const key of Object.keys(BROWSER_ICON_KEYS)) {
    if (lower.includes(key)) {
      return key;
    }
  }
  if (lower.includes('mozilla')) {
    return 'firefox';
  }
  return '';
}

function getIconSequence(availableBrowsers: readonly {name: string}[]): string[] {
  const detectedKeys = Array.from(
    new Set(
      availableBrowsers
        .map(b => b.name.split(' - ')[0])
        .map(getBrowserIconKey)
        .filter(Boolean)
    )
  );

  if (detectedKeys.length === 0) {
    return ['chrome'];
  }

  return detectedKeys;
}

export function AnimatedBrowserIconStack({
  availableBrowsers,
}: {
  availableBrowsers: readonly {name: string}[];
}) {
  const sequence = useMemo(
    () => getIconSequence(availableBrowsers),
    [availableBrowsers]
  );
  const [prefersReducedMotion, setPrefersReducedMotion] = useState(() => {
    if (typeof window === 'undefined' || !window.matchMedia) return false;
    return window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  });

  const [currentIndex, setCurrentIndex] = useState(0);
  const iconRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (typeof window === 'undefined' || !window.matchMedia) return;
    const mediaQuery = window.matchMedia('(prefers-reduced-motion: reduce)');
    const handleChange = (e: MediaQueryListEvent) => setPrefersReducedMotion(e.matches);
    mediaQuery.addEventListener('change', handleChange);
    return () => mediaQuery.removeEventListener('change', handleChange);
  }, []);

  useEffect(() => {
    if (prefersReducedMotion) {
      setCurrentIndex(sequence.length - 1);
      return;
    }

    setCurrentIndex(0);

    if (sequence.length <= 1) {
      return;
    }

    const intervalId = window.setInterval(() => {
      setCurrentIndex(index => (index + 1) % sequence.length);
    }, 1200);

    return () => {
      window.clearInterval(intervalId);
    };
  }, [sequence, prefersReducedMotion]);

  useEffect(() => {
    if (prefersReducedMotion || !iconRef.current) return;

    const el = iconRef.current;
    const anim = el.animate(
      [
        {transform: 'translateY(-56px) scale(0.92)', opacity: 0, filter: 'blur(5px)'},
        {transform: 'translateY(6px) scale(1.04)', opacity: 1, filter: 'blur(0px)', offset: 0.72},
        {transform: 'translateY(0px) scale(1)', opacity: 1, filter: 'blur(0px)'},
      ],
      {duration: 720, easing: 'cubic-bezier(0.22, 1.12, 0.36, 1)', fill: 'both'},
    );
    return () => anim.cancel();
  }, [currentIndex, prefersReducedMotion]);

  const currentKey = sequence[currentIndex];
  const iconSrc = `${ASSET_ORIGIN}/icons/onboarding/browsers/${currentKey}.png`;
  return (
    <div className="relative flex size-28 items-center justify-center overflow-visible">
      <div
        ref={iconRef}
        key={`${currentKey}-${currentIndex}`}
        className="flex items-center justify-center"
        style={{
          transform: prefersReducedMotion
            ? 'translateY(0px)'
            : undefined,
          opacity: prefersReducedMotion ? 1 : undefined,
        }}
      >
        <img
          src={iconSrc}
          alt=""
          width="64"
          height="64"
          className="size-16 object-contain drop-shadow-2xl"
        />
      </div>
    </div>
  );
}

export function ImportDataSidebar({store, importStage}: {store: MahoWelcomeStore; importStage: ImportStage}) {
  if (importStage === 'B') {
    return (
      <>
        <h1>{store.getString('IDS_MAHO_WELCOME_DEFAULT_BROWSER_TITLE')}</h1>
        <p>{store.getString('IDS_MAHO_WELCOME_DEFAULT_BROWSER_BODY')}</p>
      </>
    );
  }

  return (
    <>
      <h1>{store.getString('IDS_MAHO_WELCOME_IMPORT_TITLE')}</h1>
      <p>{store.getString('IDS_MAHO_WELCOME_IMPORT_BODY1')}</p>
      <p>{store.getString('IDS_MAHO_WELCOME_IMPORT_BODY2')}</p>
    </>
  );
}

export function ImportDataContent(props: Props) {
  const {store, importStage} = props;
  const snapshot = useSyncExternalStore(
    listener => store.subscribe(listener),
    () => store.getSnapshot()
  );

  if (importStage === 'B') {
    return <DefaultBrowserChoice />;
  }

  return (
    <div className="flex h-full w-full items-center justify-center">
      <div className="relative z-10 flex items-center gap-6">
        <AnimatedBrowserIconStack availableBrowsers={snapshot.availableBrowsers} />
        <div className="flex size-12 items-center justify-center text-muted-foreground">
          <ArrowRight className="size-6" aria-hidden="true" />
        </div>
        <div className="flex size-28 items-center justify-center">
          <img src={MAHO_LOGO_SRC} alt="" width="72" height="72" className="size-20" />
        </div>
      </div>
    </div>
  );
}

const DEFAULT_BROWSER_MOTION_CSS = `
@keyframes maho-def-breathe { 0%,100%{transform:scale(1)} 50%{transform:scale(1.04)} }
@media (prefers-reduced-motion: no-preference){ .maho-def-logo{animation:maho-def-breathe 3.6s ease-in-out infinite} }
.maho-def-glow{position:absolute;width:280px;height:280px;border-radius:50%;background:radial-gradient(closest-side,rgba(124,108,255,.30),transparent);filter:blur(34px);z-index:1}
.maho-def-chip{position:absolute;left:50%;top:50%;z-index:20;display:flex;align-items:center;gap:7px;white-space:nowrap;font-size:12px;font-weight:500;color:var(--foreground);border:1px solid var(--border);background:var(--card);border-radius:999px;padding:7px 12px;box-shadow:0 10px 26px rgba(0,0,0,.5);opacity:0}
.maho-def-chip svg{width:14px;height:14px;stroke:currentColor;fill:none;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}
.maho-def-ring{position:absolute;left:50%;top:50%;z-index:5;width:132px;height:132px;border-radius:50%;border:2px solid rgba(124,108,255,.55);pointer-events:none}
`;

const DEF_CHIP_ICONS = {
  link: '<path d="M9 15l6-6"/><path d="M10.5 6.5l1-1a4 4 0 015.9 5.4l-1.4 1.4"/><path d="M13.5 17.5l-1 1a4 4 0 01-5.9-5.4l1.4-1.4"/>',
  mail: '<rect x="3" y="5" width="18" height="14" rx="2"/><path d="m3 7 9 6 9-6"/>',
  doc: '<path d="M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z"/><path d="M14 3v5h5"/>',
  play: '<circle cx="12" cy="12" r="9"/><path d="m10 9 5 3-5 3z"/>',
  chat: '<path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"/>',
} as const;

const DEF_CHIPS: ReadonlyArray<{icon: string; label: string}> = [
  {icon: DEF_CHIP_ICONS.link, label: 'https://maho.app'},
  {icon: DEF_CHIP_ICONS.mail, label: 'Mail link'},
  {icon: DEF_CHIP_ICONS.doc, label: 'PDF'},
  {icon: DEF_CHIP_ICONS.play, label: 'Video'},
  {icon: DEF_CHIP_ICONS.chat, label: 'Chat invite'},
  {icon: DEF_CHIP_ICONS.link, label: 'news.com'},
];

function DefaultBrowserChoice() {
  const stageRef = useRef<HTMLDivElement>(null);
  const logoRef = useRef<HTMLDivElement>(null);
  const badgeRef = useRef<HTMLDivElement>(null);

  const prefersReduced = useMemo(() =>
      typeof window !== 'undefined' && window.matchMedia
        ? window.matchMedia('(prefers-reduced-motion: reduce)').matches
        : false, []);

  useEffect(() => {
    const stage = stageRef.current;
    const logo = logoRef.current;
    const badge = badgeRef.current;
    if (prefersReduced || !stage) {
      if (badge) {
        badge.style.opacity = '1';
      }
      return;
    }

    const timers: number[] = [];
    const radius = 210;

    const ripple = () => {
      const ring = document.createElement('div');
      ring.className = 'maho-def-ring';
      stage.appendChild(ring);
      ring.animate(
          [
            {opacity: 0.7, transform: 'translate(-50%,-50%) scale(.5)'},
            {opacity: 0, transform: 'translate(-50%,-50%) scale(2.1)'},
          ],
          {duration: 900, easing: 'cubic-bezier(.2,.6,.3,1)', fill: 'forwards'})
          .finished.then(() => { ring.remove(); }).catch(() => { ring.remove(); });
      if (logo) {
        logo.animate(
            [{transform: 'scale(1)'}, {transform: 'scale(1.1)'}, {transform: 'scale(1)'}],
            {duration: 420, easing: 'ease-out'});
      }
    };

    const APPEAR_STAGGER = 90;
    const APPEAR_DUR = 460;
    const HOLD = 650;
    const ABSORB_STAGGER = 190;
    const ABSORB_DUR = 900;
    const absorbBase = (DEF_CHIPS.length - 1) * APPEAR_STAGGER + APPEAR_DUR + HOLD;
    const total = absorbBase + (DEF_CHIPS.length - 1) * ABSORB_STAGGER + ABSORB_DUR;

    const play = () => {
      stage.querySelectorAll('.maho-def-chip, .maho-def-ring').forEach(n => { n.remove(); });
      if (badge) {
        badge.style.opacity = '0';
      }
      DEF_CHIPS.forEach((c, i) => {
        const angle = (i / DEF_CHIPS.length) * Math.PI * 2 - Math.PI / 2;
        const sx = Math.cos(angle) * radius;
        const sy = Math.sin(angle) * radius;
        const orbit = `translate(calc(-50% + ${sx}px), calc(-50% + ${sy}px))`;
        const chip = document.createElement('div');
        chip.className = 'maho-def-chip';
        chip.innerHTML = `<svg viewBox="0 0 24 24">${c.icon}</svg><span>${c.label}</span>`;
        stage.appendChild(chip);

        timers.push(window.setTimeout(() => {
          chip.animate(
              [
                {transform: `${orbit} scale(.85)`, opacity: 0},
                {transform: `${orbit} scale(1)`, opacity: 1},
              ],
              {duration: APPEAR_DUR, easing: 'cubic-bezier(.34,1.56,.64,1)', fill: 'forwards'});
        }, i * APPEAR_STAGGER));

        const absorbStart = absorbBase + i * ABSORB_STAGGER;
        timers.push(window.setTimeout(() => {
          chip.animate(
              [
                {transform: `${orbit} scale(1)`, opacity: 1},
                {transform: 'translate(-50%,-50%) scale(.4)', opacity: 0.85, offset: 0.82},
                {transform: 'translate(-50%,-50%) scale(0)', opacity: 0},
              ],
              {duration: ABSORB_DUR, easing: 'cubic-bezier(.45,0,.2,1)', fill: 'forwards'});
          timers.push(window.setTimeout(ripple, ABSORB_DUR * 0.72));
          timers.push(window.setTimeout(() => { chip.remove(); }, ABSORB_DUR + 60));
        }, absorbStart));
      });

      timers.push(window.setTimeout(() => {
        if (badge) {
          badge.animate(
              [
                {opacity: 0, transform: 'translateX(-50%) scale(.8)'},
                {opacity: 1, transform: 'translateX(-50%) scale(1)'},
              ],
              {duration: 500, easing: 'cubic-bezier(.34,1.56,.64,1)', fill: 'forwards'});
        }
      }, total + 120));
    };

    play();
    const loopId = window.setInterval(play, total + 2400);
    return () => {
      window.clearInterval(loopId);
      timers.forEach(t => { window.clearTimeout(t); });
      stage.querySelectorAll('.maho-def-chip, .maho-def-ring').forEach(n => { n.remove(); });
    };
  }, [prefersReduced]);

  return (
    <div className="flex h-full w-full items-center justify-center">
      <style>{DEFAULT_BROWSER_MOTION_CSS}</style>
      <div ref={stageRef} className="relative flex h-full w-full items-center justify-center">
        <div className="maho-def-glow" aria-hidden="true" />
        <div ref={logoRef} className="maho-def-logo relative z-10 size-28 drop-shadow-2xl">
          <img src={MAHO_LOGO_SRC} alt="" width="112" height="112" className="size-28 rounded-[26px]" />
        </div>
        <div
          ref={badgeRef}
          className="absolute left-1/2 top-[calc(50%+92px)] z-10 flex -translate-x-1/2 items-center gap-1.5 rounded-full border border-emerald-500/30 bg-emerald-500/12 px-3 py-1.5 text-xs font-semibold text-emerald-400"
          style={{opacity: 0}}
        >
          <Check className="size-3.5" aria-hidden="true" />
          Default browser
        </div>
      </div>
    </div>
  );
}
