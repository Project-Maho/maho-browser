const IS_MAC = typeof navigator !== 'undefined' && /Mac/.test(navigator.platform);

export function isModKey(e: KeyboardEvent): boolean {
  return IS_MAC ? e.metaKey : e.ctrlKey;
}

export function matchShortcut(e: KeyboardEvent, shortcut: string): boolean {
  const parts = shortcut.split('+').map(p => p.trim().toLowerCase());
  const key = parts.pop();
  if (!key) return false;

  const needsMod = parts.includes('cmd') || parts.includes('ctrl');
  const needsShift = parts.includes('shift');
  const needsAlt = parts.includes('alt');

  if (needsMod && !isModKey(e)) return false;
  if (needsShift && !e.shiftKey) return false;
  if (needsAlt && !e.altKey) return false;

  return e.key.toLowerCase() === key;
}
