/**
 * Minimal clsx-compatible utility — concatenates truthy class strings.
 * Avoids an npm dependency for a trivial utility.
 */
export function clsx(...classes: (string | undefined | null | false)[]): string {
  return classes.filter(Boolean).join(' ');
}
