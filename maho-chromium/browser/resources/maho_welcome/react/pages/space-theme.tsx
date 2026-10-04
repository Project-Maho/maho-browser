// Copyright 2026 Maho Browser. All rights reserved.

import React from 'react';
import {Palette} from 'lucide-react';

import {Button} from '@ui/button';
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
    <div className="flex h-full w-full flex-col items-center justify-center gap-6 rounded-xl border border-border bg-background/50 p-8 text-center shadow-sm">
      <div
        className="relative size-52 overflow-hidden rounded-2xl border border-border bg-muted shadow-2xl shadow-black/20"
        style={selectedTheme ? {
          background: deriveGradient(selectedTheme),
        } : undefined}
        aria-hidden="true"
      >
        <div className="absolute inset-0 bg-gradient-to-br from-background/20 via-transparent to-background/40" />
        {!selectedTheme ? (
          <div className="flex h-full w-full items-center justify-center text-muted-foreground">
            <Palette className="size-14" aria-hidden="true" />
          </div>
        ) : null}
      </div>
      <Button
        type="button"
        variant={selectedTheme ? 'outline' : 'default'}
        className="h-11 rounded-lg px-6 font-semibold shadow-lg shadow-primary/10"
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
      </Button>
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
  return 'var(--muted)';
}

export function getThemeJsonById(id: string): string {
  return id;
}
