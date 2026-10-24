import {MojoKeyCombo} from '../../mojo.js';

export function isModifierOnly(event: KeyboardEvent): boolean {
  return ['Control', 'Shift', 'Alt', 'Meta'].includes(event.key);
}

export function parseKeyComboFromEvent(event: KeyboardEvent): MojoKeyCombo {
  const modifiers: string[] = [];
  if (event.ctrlKey) modifiers.push('ctrl');
  if (event.shiftKey) modifiers.push('shift');
  if (event.altKey) modifiers.push('alt');
  if (event.metaKey) modifiers.push('meta');

  let key = event.key.toLowerCase();
  if (isModifierOnly(event)) {
    key = '';
  } else if (key === 'escape') {
    key = 'escape';
  } else if (key === 'backspace') {
    key = 'backspace';
  } else if (key === 'delete') {
    key = 'delete';
  } else if (key === ' ') {
    key = 'space';
  } else if (key === 'arrowup') {
    key = 'up';
  } else if (key === 'arrowdown') {
    key = 'down';
  } else if (key === 'arrowleft') {
    key = 'left';
  } else if (key === 'arrowright') {
    key = 'right';
  }
  return { key, modifiers };
}

export function parseKeyComboJson(json: string): MojoKeyCombo {
  try {
    return JSON.parse(json);
  } catch {
    return { key: '', modifiers: [] };
  }
}

export function serializeKeyCombo(combo: MojoKeyCombo): string {
  return JSON.stringify(combo);
}

function isMacPlatform(): boolean {
  if (typeof navigator === 'undefined') return false;
  const uaPlatform = (navigator as Navigator & {userAgentData?: {platform?: string}})
      .userAgentData?.platform;
  return /mac/i.test(uaPlatform || navigator.platform || '');
}

const MAC_KEY_GLYPHS: Record<string, string> = {
  backspace: '⌫', delete: '⌦', up: '↑', down: '↓', left: '←', right: '→',
};
const PC_KEY_NAMES: Record<string, string> = {
  backspace: 'Backspace', delete: 'Delete', up: 'Up', down: 'Down', left: 'Left', right: 'Right',
};

export function formatKeyCombo(
    keyCombo: MojoKeyCombo | null | undefined, mac: boolean = isMacPlatform()): string {
  if (!keyCombo || (!keyCombo.key && (!keyCombo.modifiers || keyCombo.modifiers.length === 0))) {
    return 'None';
  }
  const parts: string[] = [];
  const mods = keyCombo.modifiers || [];
  if (mac) {
    // Mac style symbols: ⌃⌥⇧⌘
    if (mods.includes('ctrl')) parts.push('⌃');
    if (mods.includes('alt')) parts.push('⌥');
    if (mods.includes('shift')) parts.push('⇧');
    if (mods.includes('meta')) parts.push('⌘');
  } else {
    if (mods.includes('ctrl')) parts.push('Ctrl');
    if (mods.includes('alt')) parts.push('Alt');
    if (mods.includes('shift')) parts.push('Shift');
    if (mods.includes('meta')) parts.push('Win');
  }

  if (keyCombo.key) {
    if (keyCombo.key === 'space') {
      parts.push('Space');
    } else if (keyCombo.key === 'escape') {
      parts.push('Esc');
    } else if (keyCombo.key in MAC_KEY_GLYPHS) {
      parts.push((mac ? MAC_KEY_GLYPHS : PC_KEY_NAMES)[keyCombo.key]!);
    } else {
      parts.push(keyCombo.key.toUpperCase());
    }
  }
  return parts.join(mac ? '' : '+');
}

// Folds modifier symbols and names onto one spelling so a search for "ctrl+t",
// "⌃T" or "cmd t" matches a formatted combo on any platform.
export function keyComboSearchText(text: string): string {
  return text.toLowerCase()
      .replace(/⌃|control/g, 'ctrl')
      .replace(/⌥|option|opt/g, 'alt')
      .replace(/⇧/g, 'shift')
      .replace(/⌘|command|cmd|win|super/g, 'meta')
      .replace(/[\s+]/g, '');
}
