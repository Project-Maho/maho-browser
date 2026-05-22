// Copyright 2026 Maho Browser. All rights reserved.

/**
 * C1 Shared Theme Provider — React wrapper for maho_theme_utils.js.
 *
 * Provides theme color utilities to React surfaces via context. Wave 2+
 * surfaces that depend on //resources/maho_common/maho_theme_utils.js
 * should consume theme values through this provider rather than importing
 * the utility directly.
 */

import {createElement, createContext, useContext} from 'react';
import type {PropsWithChildren} from 'react';

/**
 * Minimal theme context shape. Surfaces needing theme colors receive them
 * through this context rather than importing maho_theme_utils directly.
 * The provider is responsible for calling hsbToCss and passing results down.
 */
export interface ThemeContextValue {
  /** CSS color string for the active space accent, or null if no space. */
  accentColor: string | null;
}

const ThemeContext = createContext<ThemeContextValue>({accentColor: null});

export function MahoThemeProvider(
    {accentColor = null, children}: PropsWithChildren<{accentColor?: string | null}>) {
  return createElement(ThemeContext.Provider, {value: {accentColor}}, children);
}

/** Read the current Maho theme context. */
export function useMahoTheme(): ThemeContextValue {
  return useContext(ThemeContext);
}
