// Copyright 2026 Maho Browser. All rights reserved.

import {describe, expect, it} from 'vitest';

import type {PasswordProviderOption} from '../../../maho_settings/maho_settings.mojom-webui.js';
import {
  getProviderHelperText,
  orderProviderOptions,
  PASSWORD_PROVIDER_KIND,
} from '../types.js';

const SHUFFLED_OPTIONS = [
  {
    provider: PASSWORD_PROVIDER_KIND.OnePassword,
    displayName: '1Password',
    description: 'Registry description for 1Password.',
    isAvailable: false,
  },
  {
    provider: PASSWORD_PROVIDER_KIND.MahoNative,
    displayName: 'Maho Native',
    description: 'Registry description for Maho Native.',
    isAvailable: true,
  },
  {
    provider: PASSWORD_PROVIDER_KIND.Bitwarden,
    displayName: 'Bitwarden',
    description: 'Registry description for Bitwarden.',
    isAvailable: false,
  },
] as const satisfies readonly PasswordProviderOption[];

describe('PasswordSetup provider options', () => {
  it('orders approved providers while preserving backend copy', () => {
    const ordered = orderProviderOptions(SHUFFLED_OPTIONS);

    expect(ordered).toEqual([
      SHUFFLED_OPTIONS[1],
      SHUFFLED_OPTIONS[2],
      SHUFFLED_OPTIONS[0],
    ]);
  });

  it('adds literal unavailable guidance only to unavailable providers', () => {
    const unavailableHelper = 'The extension is not installed or is disabled.';

    expect(getProviderHelperText(SHUFFLED_OPTIONS[2], unavailableHelper)).toBe(
        'Registry description for Bitwarden. The extension is not installed or is disabled.');
    expect(getProviderHelperText(SHUFFLED_OPTIONS[1], unavailableHelper)).toBe(
        'Registry description for Maho Native.');
  });
});
