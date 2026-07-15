// Copyright 2026 Maho Browser. All rights reserved.

import type {ReactNode} from 'react';
import {Check} from 'lucide-react';

import {cn} from '@lib/utils';
import type {MahoWelcomeStore} from '../store.js';
import type {AppearanceMode} from '../types.js';

interface AppearanceSidebarProps {
  store: MahoWelcomeStore;
}

interface AppearanceContentProps {
  store: MahoWelcomeStore;
  appearance: AppearanceMode;
}

interface AppearanceOption {
  mode: AppearanceMode;
  titleId: string;
  descriptionId?: string;
  renderIcon: () => ReactNode;
}

const APPEARANCE_OPTIONS: readonly AppearanceOption[] = [
  {
    mode: 'system',
    titleId: 'IDS_MAHO_WELCOME_APPEARANCE_SYSTEM',
    descriptionId: 'IDS_MAHO_WELCOME_APPEARANCE_SYSTEM_DESC',
    renderIcon: SystemIcon,
  },
  {
    mode: 'light',
    titleId: 'IDS_MAHO_WELCOME_APPEARANCE_LIGHT',
    renderIcon: LightIcon,
  },
  {
    mode: 'dark',
    titleId: 'IDS_MAHO_WELCOME_APPEARANCE_DARK',
    renderIcon: DarkIcon,
  },
];

export function AppearanceSidebar({store}: AppearanceSidebarProps) {
  return (
    <>
      <h1>{store.getString('IDS_MAHO_WELCOME_APPEARANCE_TITLE')}</h1>
      <p>{store.getString('IDS_MAHO_WELCOME_APPEARANCE_BODY')}</p>
    </>
  );
}

export function AppearanceContent({store, appearance}: AppearanceContentProps) {
  return (
    <div className="flex w-full flex-col items-stretch gap-5">
      <div className="grid w-full grid-cols-1 gap-4 lg:grid-cols-3">
        {APPEARANCE_OPTIONS.map(option => {
          const isSelected = appearance === option.mode;
          const descriptionId = option.descriptionId
              ? `appearance-${option.mode}-description`
              : undefined;
          return (
            <button
              key={option.mode}
              type="button"
              aria-pressed={isSelected}
              aria-describedby={descriptionId}
              className={cn(
                  'group relative flex min-h-52 flex-col gap-3 rounded-xl border border-border bg-background/60 p-4 text-left text-foreground shadow-sm transition duration-150 hover:-translate-y-0.5 hover:bg-muted/50 hover:shadow-xl focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring',
                  isSelected && 'scale-[1.015] border-primary bg-muted shadow-xl ring-2 ring-primary/20')}
              onClick={() => store.setAppearance(option.mode)}
            >
              {isSelected ? (
                <span className="absolute right-3 top-3 z-20 flex size-6 items-center justify-center rounded-full bg-primary text-primary-foreground shadow-lg shadow-primary/20">
                  <Check className="size-3.5" aria-hidden="true" />
                </span>
              ) : null}
              <span className={cn(
                  'relative flex min-h-28 items-center justify-center overflow-hidden rounded-lg border border-border shadow-inner',
                  getPreviewClasses(option.mode))} aria-hidden="true">
                <span className="absolute left-3 top-3 z-10 flex gap-1.5">
                  <span className="size-2 rounded-full bg-red-400 shadow-sm" />
                  <span className="size-2 rounded-full bg-yellow-400 shadow-sm" />
                  <span className="size-2 rounded-full bg-green-400 shadow-sm" />
                </span>
                <span className="relative z-10 flex size-16 items-center justify-center rounded-full border border-border bg-background/40 text-current shadow-lg backdrop-blur-md transition group-hover:scale-105">
                  {option.renderIcon()}
                </span>
              </span>
              <span className="flex flex-col gap-1">
                <span className="text-base font-semibold tracking-tight">
                  {store.getString(option.titleId)}
                </span>
                {option.descriptionId ? (
                  <span id={descriptionId} className="text-sm leading-5 text-muted-foreground">
                    {store.getString(option.descriptionId)}
                  </span>
                ) : null}
              </span>
            </button>
          );
        })}
      </div>
      <p className="mx-auto m-0 max-w-lg text-center text-sm leading-6 text-muted-foreground">
        {store.getString('IDS_MAHO_WELCOME_APPEARANCE_HELPER')}
      </p>
    </div>
  );
}

function getPreviewClasses(mode: AppearanceMode): string {
  switch (mode) {
    case 'system':
      return 'bg-[linear-gradient(135deg,var(--color-zinc-100)_0_49%,var(--color-zinc-900)_51%_100%)] text-zinc-200';
    case 'light':
      return 'bg-gradient-to-br from-zinc-50 to-zinc-200 text-zinc-700';
    case 'dark':
      return 'bg-gradient-to-br from-zinc-800 to-zinc-950 text-zinc-100';
  }
}

function SystemIcon() {
  return (
    <svg xmlns="http://www.w3.org/2000/svg" width="34" height="34" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true" focusable="false">
      <rect x="3" y="4" width="18" height="12" rx="2"></rect>
      <path d="M8 20h8"></path>
      <path d="M12 16v4"></path>
    </svg>
  );
}

function LightIcon() {
  return (
    <svg xmlns="http://www.w3.org/2000/svg" width="34" height="34" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true" focusable="false">
      <circle cx="12" cy="12" r="4"></circle>
      <path d="M12 2v2"></path>
      <path d="M12 20v2"></path>
      <path d="m4.93 4.93 1.41 1.41"></path>
      <path d="m17.66 17.66 1.41 1.41"></path>
      <path d="M2 12h2"></path>
      <path d="M20 12h2"></path>
      <path d="m6.34 17.66-1.41 1.41"></path>
      <path d="m19.07 4.93-1.41 1.41"></path>
    </svg>
  );
}

function DarkIcon() {
  return (
    <svg xmlns="http://www.w3.org/2000/svg" width="34" height="34" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true" focusable="false">
      <path d="M12 3a6 6 0 0 0 9 9 9 9 0 1 1-9-9Z"></path>
    </svg>
  );
}
