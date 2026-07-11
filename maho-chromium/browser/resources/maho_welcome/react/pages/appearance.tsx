// Copyright 2026 Maho Browser. All rights reserved.

import type {ReactNode} from 'react';
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
    <div className="welcome-appearance-content">
      <div className="welcome-appearance__options">
        {APPEARANCE_OPTIONS.map(option => {
          const isSelected = appearance === option.mode;
          const descriptionId = option.descriptionId
              ? `welcome-appearance-${option.mode}-description`
              : undefined;
          return (
            <button
              key={option.mode}
              type="button"
              aria-pressed={isSelected}
              aria-describedby={descriptionId}
              className={`welcome-appearance-card welcome-appearance-card--${option.mode}${isSelected ? ' is-selected' : ''}`}
              onClick={() => store.setAppearance(option.mode)}>
              <span className="welcome-appearance-card__preview" aria-hidden="true">
                <span className="welcome-appearance-card__chrome">
                  <span></span><span></span><span></span>
                </span>
                <span className="welcome-appearance-card__icon">
                  {option.renderIcon()}
                </span>
              </span>
              <span className="welcome-appearance-card__copy">
                <span className="welcome-appearance-card__title">
                  {store.getString(option.titleId)}
                </span>
                {option.descriptionId ? (
                  <span id={descriptionId} className="welcome-appearance-card__description">
                    {store.getString(option.descriptionId)}
                  </span>
                ) : null}
              </span>
            </button>
          );
        })}
      </div>
      <p className="welcome-appearance__helper">
        {store.getString('IDS_MAHO_WELCOME_APPEARANCE_HELPER')}
      </p>
    </div>
  );
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
