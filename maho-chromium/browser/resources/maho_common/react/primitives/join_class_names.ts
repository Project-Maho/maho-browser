// Copyright 2026 Maho Browser. All rights reserved.

/**
 * Shared className joiner used by all Maho WebUI React primitives.
 * Filters out undefined/empty values and joins with a space.
 */
export function joinClassNames(...values: Array<string | undefined>): string {
  return values.filter(Boolean).join(' ');
}
