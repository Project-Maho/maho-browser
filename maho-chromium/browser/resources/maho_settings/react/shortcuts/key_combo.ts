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

export function formatKeyCombo(keyCombo: MojoKeyCombo | null | undefined): string {
  if (!keyCombo || (!keyCombo.key && (!keyCombo.modifiers || keyCombo.modifiers.length === 0))) {
    return 'None';
  }
  const parts: string[] = [];
  const mods = keyCombo.modifiers || [];
  // Mac style symbols: ⌃⌥⇧⌘
  if (mods.includes('ctrl')) parts.push('⌃');
  if (mods.includes('alt')) parts.push('⌥');
  if (mods.includes('shift')) parts.push('⇧');
  if (mods.includes('meta')) parts.push('⌘');
  
  if (keyCombo.key) {
    if (keyCombo.key === 'space') {
      parts.push('Space');
    } else if (keyCombo.key === 'escape') {
      parts.push('Esc');
    } else if (keyCombo.key === 'backspace') {
      parts.push('⌫');
    } else if (keyCombo.key === 'delete') {
      parts.push('⌦');
    } else if (keyCombo.key === 'up') {
      parts.push('↑');
    } else if (keyCombo.key === 'down') {
      parts.push('↓');
    } else if (keyCombo.key === 'left') {
      parts.push('←');
    } else if (keyCombo.key === 'right') {
      parts.push('→');
    } else {
      parts.push(keyCombo.key.toUpperCase());
    }
  }
  return parts.join('');
}
