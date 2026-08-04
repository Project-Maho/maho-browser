import type {ProfileInfo} from '../maho_settings.mojom-webui.js';

export function validateProfileName(
    name: string, profiles: readonly ProfileInfo[], selectedId: string): string | null {
  const trimmed = name.trim();
  if (!trimmed) return 'Profile name cannot be empty.';
  if (profiles.some(profile => profile.id !== selectedId &&
      profile.name.trim().toLocaleLowerCase() === trimmed.toLocaleLowerCase())) {
    return 'Another profile already uses this name.';
  }
  return null;
}

export function validateProfileColor(color: string): string | null {
  return /^#[0-9a-f]{6}$/i.test(color.trim()) ? null :
      'Choose a six-digit hexadecimal color.';
}

export function validateHomepageUrl(value: string): string | null {
  try {
    const url = new URL(value.trim());
    return url.protocol === 'http:' || url.protocol === 'https:' ||
        url.protocol === 'chrome:' ? null : 'Use an HTTP, HTTPS, or chrome URL.';
  } catch {
    return 'Enter a valid homepage URL.';
  }
}
