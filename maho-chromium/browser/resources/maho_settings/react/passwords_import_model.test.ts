// Copyright 2026 Maho Browser. All rights reserved.

import {describe, expect, it} from 'vitest';

import {
  getDefaultPasswordImportFormat,
  getPasswordImportProvider,
  getPasswordImportProviders,
} from './passwords_import_model.js';

describe('password import provider model', () => {
  it('exposes the four supported providers for the compact selector', () => {
    expect(getPasswordImportProviders().map(provider => provider.providerId)).toEqual([
      'onepassword',
      'bitwarden',
      'apple_passwords',
      'keepass',
    ]);
  });

  it('gives each provider a selectable default file format', () => {
    for (const provider of getPasswordImportProviders()) {
      const defaultFormat = getDefaultPasswordImportFormat(provider);
      expect(provider.formats).toContain(defaultFormat);
      expect(defaultFormat.descriptor.label.length).toBeGreaterThan(0);
    }
  });

  it('keeps provider-specific guidance isolated to the selected provider', () => {
    const apple = getPasswordImportProvider('apple_passwords');
    expect(apple.displayName).toContain('Apple');
    expect(apple.instructions.join(' ')).toContain('Passwords on Mac');
    expect(apple.instructions.join(' ')).not.toContain('Bitwarden web app');
  });
});
