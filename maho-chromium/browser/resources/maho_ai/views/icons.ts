export type IconSymbol =
    'arrowUpRight'|'composerSpark'|'copy'|'edit'|'ellipsis'|'globe'|
    'panelSpark'|'plus'|'retry'|'settingsGear'|'stop'|'thumbDown'|
    'thumbUp'|'trash'|'viewModeToggle'|'xmark';


export function getSFSymbolUrl(symbol: IconSymbol): string {
  return `chrome://maho-ai/sf-symbol/${encodeURIComponent(symbol)}`;
}

export function createIcon(symbol: IconSymbol): HTMLSpanElement {
  const icon = document.createElement('span');
  icon.className = 'maho-ai-icon';
  icon.setAttribute('aria-hidden', 'true');
  icon.dataset['symbol'] = symbol;
  icon.style.setProperty('--maho-ai-icon-url', `url("${getSFSymbolUrl(symbol)}")`);
  return icon;
}
