// Copyright 2026 Maho Browser. All rights reserved.

import React from 'react';
import type {MahoWelcomeStore} from '../store.js';

interface Props {
  store: MahoWelcomeStore;
  selectedTheme: string | null;
  onSelectTheme: (themeJson: string) => void;
  onPreviewTheme: (themeJson: string) => void;
}

export function SpaceThemeSidebar({store}: {store: MahoWelcomeStore}) {
  return (
    <>
      <h1>{store.getString('IDS_MAHO_WELCOME_THEME_TITLE')}</h1>
      <p>{store.getString('IDS_MAHO_WELCOME_THEME_BODY')}</p>
    </>
  );
}

export function SpaceThemeContent({store, selectedTheme, onSelectTheme, onPreviewTheme}: Props) {
  return (
    <div className="welcome-theme-picker-content">
      {selectedTheme ? (
        <div className="welcome-theme-preview" style={{
          background: deriveGradient(selectedTheme),
        }} />
      ) : null}
      <button
        type="button"
        className="welcome-theme-picker-button"
        onClick={async () => {
          const currentJson = selectedTheme ?? '';
          const result = await store.openThemePickerDialog(currentJson);
          if (result) {
            onPreviewTheme(result);
            onSelectTheme(result);
          }
        }}>
        {selectedTheme
          ? store.getString('IDS_MAHO_WELCOME_THEME_CHANGE')
          : store.getString('IDS_MAHO_WELCOME_THEME_PICK')}
      </button>
    </div>
  );
}

function deriveGradient(themeJson: string): string {
  try {
    const parsed = JSON.parse(themeJson);
    if (parsed.type === 'solid') {
      const {hue, saturation, brightness} = parsed.color;
      const h = hue * 360;
      const s = saturation * 100;
      const l = brightness * 100;
      return `hsl(${h.toFixed(1)}, ${s.toFixed(1)}%, ${l.toFixed(1)}%)`;
    } else if (parsed.type === 'gradient' || parsed.type === 'zen') {
      const stops = parsed.gradientColors;
      if (Array.isArray(stops) && stops.length > 0) {
        const colors = stops.map((stop: any) => {
          const h = stop.hue * 360;
          const s = stop.saturation * 100;
          const l = typeof stop.lightness === 'number' ? stop.lightness : (stop.brightness * 100);
          return `hsl(${h.toFixed(1)}, ${s.toFixed(1)}%, ${l.toFixed(1)}%)`;
        });
        if (colors.length === 1) {
          return colors[0];
        }
        return `linear-gradient(135deg, ${colors.join(', ')})`;
      }
    }
  } catch (e) {
    console.error('Failed to parse theme JSON', e);
  }
  return 'var(--maho-toolbar-element-bg)';
}

export function getThemeJsonById(id: string): string {
  return id;
}
