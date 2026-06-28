export type IconSymbol =
    'arrowUpRight'|'composerSpark'|'copy'|'edit'|'ellipsis'|'globe'|
    'panelSpark'|'plus'|'retry'|'send'|'settingsGear'|'stop'|'thumbDown'|
    'thumbUp'|'tidyWand'|'trash'|'viewModeToggle'|'xmark';

const ICON_MAP: Record<IconSymbol, string> = {
  arrowUpRight: 'arrow-up-right',
  composerSpark: 'sparkles',
  copy: 'copy',
  edit: 'pencil',
  ellipsis: 'ellipsis',
  globe: 'globe',
  panelSpark: 'sparkles',
  plus: 'plus',
  retry: 'rotate-cw',
  send: 'send',
  settingsGear: 'settings',
  stop: 'circle-stop',
  thumbDown: 'thumbs-down',
  thumbUp: 'thumbs-up',
  tidyWand: 'wand-2',
  trash: 'trash-2',
  viewModeToggle: 'panel-right',
  xmark: 'x',
};

export function getIconUrl(symbol: IconSymbol): string {
  return `chrome://maho-ai/icons/${ICON_MAP[symbol]}.svg`;
}

export function createIcon(symbol: IconSymbol): HTMLSpanElement {
  const icon = document.createElement('span');
  icon.className = 'maho-ai-icon';
  icon.setAttribute('aria-hidden', 'true');
  icon.dataset['symbol'] = symbol;
  icon.style.setProperty('--maho-ai-icon-url', `url("${getIconUrl(symbol)}")`);
  return icon;
}
