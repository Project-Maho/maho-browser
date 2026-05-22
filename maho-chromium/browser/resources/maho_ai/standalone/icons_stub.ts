// Standalone-only stub for views/icons.ts.
// In Chromium, getSFSymbolUrl returns a chrome://maho-ai/sf-symbol/... URL
// that only works inside the browser process. In the standalone dev harness
// that URL is blocked, flooding the console. This stub returns an empty
// string so the CSS url("") is a no-op and icons degrade gracefully.

export type IconSymbol =
    'arrowUpRight'|'composerSpark'|'copy'|'edit'|'ellipsis'|'globe'|
    'panelSpark'|'plus'|'retry'|'stop'|'thumbDown'|'thumbUp'|'trash';

export function getSFSymbolUrl(_symbol: IconSymbol): string {
  return '';
}

export function createIcon(symbol: IconSymbol): HTMLSpanElement {
  const icon = document.createElement('span');
  icon.className = 'maho-ai-icon';
  icon.setAttribute('aria-hidden', 'true');
  icon.dataset['symbol'] = symbol;
  return icon;
}
