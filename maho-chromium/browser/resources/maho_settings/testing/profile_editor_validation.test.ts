import {describe, expect, test} from 'bun:test';

import {
  validateHomepageUrl,
  validateProfileColor,
  validateProfileName,
} from '../react/profile_validation.js';

const profiles = [
  {id: 'A', name: 'Personal', isDefault: true, isActive: true, spaceIds: []},
  {id: 'B', name: 'Work', isDefault: false, isActive: false, spaceIds: []},
];

describe('profile editor validation', () => {
  test('rejects empty, whitespace, and case-insensitive duplicate names while excluding self', () => {
    expect(validateProfileName('', profiles, 'B')).toBe('Profile name cannot be empty.');
    expect(validateProfileName('   ', profiles, 'B')).toBe('Profile name cannot be empty.');
    expect(validateProfileName(' personal ', profiles, 'B')).toBe('Another profile already uses this name.');
    expect(validateProfileName(' work ', profiles, 'B')).toBeNull();
  });

  test('accepts only the supported normalized hexadecimal color form', () => {
    expect(validateProfileColor('#007AFF')).toBeNull();
    expect(validateProfileColor('#abcdef')).toBeNull();
    expect(validateProfileColor('blue')).not.toBeNull();
    expect(validateProfileColor('#12345')).not.toBeNull();
  });

  test('rejects malformed or unsupported homepage URLs', () => {
    expect(validateHomepageUrl('https://example.com')).toBeNull();
    expect(validateHomepageUrl('chrome://newtab')).toBeNull();
    expect(validateHomepageUrl('not a url')).not.toBeNull();
    expect(validateHomepageUrl('file:///tmp/private')).not.toBeNull();
  });
});
