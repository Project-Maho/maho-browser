// Copyright 2026 Maho Browser. All rights reserved.

/**
 * Shared store utilities for Maho WebUI React surfaces.
 *
 * All React-based WebUI stores share a common pattern: subscribe/notify with
 * a Set<Listener>, and error coercion via asErrorMessage. This module
 * consolidates those primitives to prevent drift across surfaces.
 */

/** A zero-argument callback used by store subscription. */
export type Listener = () => void;

/**
 * Coerce an unknown caught value into a user-facing error string.
 * Returns the Error.message if available, otherwise the provided fallback.
 */
export function asErrorMessage(error: unknown, fallback: string): string {
  return error instanceof Error ? error.message : fallback;
}

/**
 * Notify all listeners in a Set. Shared helper to keep store notify()
 * implementations consistent.
 */
export function notifyListeners(listeners: ReadonlySet<Listener>): void {
  for (const listener of listeners) {
    listener();
  }
}
