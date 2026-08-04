// Copyright 2026 Maho Browser. All rights reserved.

import React, {useEffect, useMemo, useRef, useState} from 'react';

import {
  HEART_PULSE,
  HEART_PULSE_KEYFRAMES,
} from '../motion-constants.js';
import {getBrowserIconKey} from './import-data.js';
import {PASSWORD_PROVIDER_KIND} from '../types.js';
import type {PasswordSetupState} from '../types.js';
import type {MahoWelcomeStore} from '../store.js';

const ASSET_ORIGIN = 'chrome://maho-' + 'welcome';
const MAHO_LOGO_SRC = `${ASSET_ORIGIN}/icons/onboarding/maho-logo.png`;

const CONFETTI_COLORS = ['#7c6cff', '#34d399', '#4c8dff', '#f9c74f', '#ff6b9d', '#ffffff'];

const COMPLETION_MOTION_CSS = `
@keyframes maho-c-logo-pop {
  0% { opacity: 0; transform: scale(.4); }
  60% { transform: scale(1.12); }
  100% { opacity: 1; transform: scale(1); }
}
@keyframes maho-c-glow {
  0%, 100% { opacity: .4; transform: scale(1); }
  50% { opacity: .85; transform: scale(1.15); }
}
@keyframes maho-c-draw { from { stroke-dashoffset: 24; } to { stroke-dashoffset: 0; } }
@keyframes maho-c-flash {
  0% { box-shadow: 0 0 0 0 rgba(52,211,153,0); }
  35% { box-shadow: 0 0 0 3px rgba(52,211,153,.5); }
  100% { box-shadow: 0 0 0 0 rgba(52,211,153,0); }
}
.maho-c-row { opacity: 0; transform: translateX(14px); }
.maho-c-row.on { opacity: 1; transform: translateX(0); }
.maho-c-check svg { stroke-dasharray: 24; stroke-dashoffset: 0; }
@media (prefers-reduced-motion: no-preference) {
  .maho-c-logo { animation: maho-c-logo-pop .7s cubic-bezier(.34,1.56,.64,1); }
  .maho-c-glow { animation: maho-c-glow 4s ease-in-out infinite; }
  .maho-c-row.on { transition: opacity .45s ease, transform .45s cubic-bezier(.34,1.56,.64,1); }
  .maho-c-row.done { animation: maho-c-flash .6s ease-out; }
  .maho-c-row.done .maho-c-check svg { animation: maho-c-draw .4s ease-out both; }
}
`;

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

function resolveVaultLabel(passwordSetup: PasswordSetupState): string {
  if (passwordSetup.passwordManagerSkipped) {
    return 'Passwords set for later';
  }
  const provider = passwordSetup.providerStatus?.provider;
  if (provider === PASSWORD_PROVIDER_KIND.Bitwarden) {
    return 'Bitwarden connected';
  }
  if (provider === PASSWORD_PROVIDER_KIND.OnePassword) {
    return '1Password connected';
  }
  return 'Local Vault secured';
}

interface CompletionRow {
  readonly key: string;
  readonly label: string;
  readonly count?: number;
}

function fireConfetti(container: HTMLElement | null): () => void {
  if (!container) {
    return () => undefined;
  }
  const nodes: HTMLSpanElement[] = [];
  for (let i = 0; i < 80; i++) {
    const el = document.createElement('span');
    el.style.cssText =
        'position:absolute;left:50%;top:32%;width:9px;height:13px;border-radius:2px;pointer-events:none;';
    el.style.background = CONFETTI_COLORS[i % CONFETTI_COLORS.length];
    container.appendChild(el);
    nodes.push(el);
    const angle = Math.random() * Math.PI * 2;
    const speed = 120 + Math.random() * 250;
    const tx = Math.cos(angle) * speed;
    const ty = Math.sin(angle) * speed - 120;
    const rot = Math.random() * 720 - 360;
    const anim = el.animate(
        [
          {opacity: 1, transform: 'translate(-50%,-50%) rotate(0deg)'},
          {opacity: 1, offset: 0.7, transform: `translate(calc(-50% + ${tx}px), calc(-50% + ${ty}px)) rotate(${rot}deg)`},
          {opacity: 0, transform: `translate(calc(-50% + ${tx * 1.1}px), calc(-50% + ${ty + 320}px)) rotate(${rot * 1.3}deg)`},
        ],
        {duration: 1500 + Math.random() * 700, easing: 'cubic-bezier(.2,.6,.3,1)', fill: 'forwards'});
    anim.finished.then(() => el.remove()).catch(() => el.remove());
  }
  return () => nodes.forEach(n => { n.remove(); });
}

export function CompletionContent({store}: {store: MahoWelcomeStore}) {
  const snapshot = store.getSnapshot();
  const selectedEngine = snapshot.searchEngines.find(
      engine => engine.keyword === snapshot.selectedEngine);
  const engineName = selectedEngine?.name || 'Google';
  const importedBrowserName = snapshot.availableBrowsers[0]?.name.split(' - ')[0] || '';
  const browserDetected = getBrowserIconKey(importedBrowserName) !== '';
  const essentialsCount = snapshot.selectedEssentials.size;

  const rows = useMemo<CompletionRow[]>(() => {
    const list: CompletionRow[] = [
      {key: 'search', label: `${engineName} search configured`},
      {key: 'vault', label: resolveVaultLabel(snapshot.passwordSetup)},
    ];
    if (browserDetected) {
      list.push({key: 'browser', label: `${importedBrowserName} imported`});
    }
    list.push({key: 'essentials', label: 'Essentials pinned', count: essentialsCount});
    return list;
  }, [engineName, browserDetected, importedBrowserName, essentialsCount, snapshot.passwordSetup]);

  const prefersReduced = useMemo(() =>
      typeof window !== 'undefined' && window.matchMedia
        ? window.matchMedia('(prefers-reduced-motion: reduce)').matches
        : false, []);

  const [revealed, setRevealed] = useState(prefersReduced ? rows.length : 0);
  const [essShown, setEssShown] = useState(prefersReduced ? essentialsCount : 0);
  const confettiRef = useRef<HTMLDivElement>(null);

  const rowCount = rows.length;
  const essIndex = rows.findIndex(r => r.key === 'essentials');

  useEffect(() => {
    if (prefersReduced) {
      setRevealed(rowCount);
      setEssShown(essentialsCount);
      return;
    }
    setRevealed(0);
    setEssShown(0);
    const cleanupConfetti = fireConfetti(confettiRef.current);
    const timers: number[] = [];
    for (let i = 0; i < rowCount; i++) {
      timers.push(window.setTimeout(() => setRevealed(c => Math.max(c, i + 1)), 500 + i * 300));
    }
    const startCount = 620 + essIndex * 300;
    timers.push(window.setTimeout(() => {
      const step = Math.max(1, Math.round(essentialsCount / 16));
      const id = window.setInterval(() => {
        setEssShown(v => {
          const next = Math.min(essentialsCount, v + step);
          if (next >= essentialsCount) {
            window.clearInterval(id);
          }
          return next;
        });
      }, 30);
      timers.push(id);
    }, startCount));
    return () => {
      cleanupConfetti();
      timers.forEach(t => { window.clearTimeout(t); });
    };
  }, [prefersReduced, rowCount, essIndex, essentialsCount]);

  return (
    <div className="relative flex h-full w-full items-center justify-center overflow-hidden">
      <style>{COMPLETION_MOTION_CSS}</style>
      <div id="completion-heart" className="pointer-events-none absolute inset-0" aria-hidden="true" />
      <div id="completion-content" className="relative z-10 h-full w-full overflow-hidden">
        <div ref={confettiRef} className="pointer-events-none absolute inset-0 z-20" aria-hidden="true" />
        <div className="relative z-10 flex h-full flex-col items-center justify-center gap-6 px-6">
          <div className="relative">
            <div className="maho-c-glow absolute -inset-8 rounded-full bg-primary/25 blur-2xl" aria-hidden="true" />
            <div className="maho-c-logo relative">
              <img src={MAHO_LOGO_SRC} width={88} height={88} className="size-[88px] rounded-3xl shadow-2xl shadow-primary/30" alt="" />
            </div>
          </div>
          <h2 className="m-0 text-lg font-semibold tracking-tight text-foreground">
            Your Maho workspace is ready
          </h2>
          <ul className="m-0 w-[360px] max-w-full list-none space-y-3 p-0">
            {rows.map((row, index) => {
              const done = index < revealed;
              return (
                <li
                  key={row.key}
                  className={`maho-c-row flex items-center gap-3 rounded-2xl border border-border bg-card/70 px-4 py-3 ${done ? 'on done' : ''}`}
                >
                  <span className={`maho-c-check flex size-6 shrink-0 items-center justify-center rounded-full border ${done ? 'border-emerald-500 bg-emerald-500 text-white' : 'border-border text-transparent'}`}>
                    <svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" strokeWidth={3} strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
                      <path d="M20 6 9 17l-5-5" />
                    </svg>
                  </span>
                  <span className="text-sm font-medium text-foreground">{row.label}</span>
                  {row.count !== undefined ? (
                    <span className="ml-auto font-semibold tabular-nums text-emerald-400">{done ? essShown : 0}</span>
                  ) : null}
                </li>
              );
            })}
          </ul>
        </div>
      </div>
    </div>
  );
}
