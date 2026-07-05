import React from 'react';
import {
  ContextMenu,
  ContextMenuContent,
  ContextMenuItem,
  ContextMenuTrigger,
} from '@ui/context-menu.js';
import {ShortcutBinding} from '../../mojo.js';

interface ShortcutContextMenuProps {
  binding: ShortcutBinding;
  onCopy: (binding: ShortcutBinding) => void | Promise<void>;
  onReset: (action: string) => void | Promise<void>;
  children: React.ReactElement;
}

export function ShortcutContextMenu({
  binding,
  onCopy,
  onReset,
  children,
}: ShortcutContextMenuProps) {
  return (
    <ContextMenu>
      <ContextMenuTrigger asChild>{children}</ContextMenuTrigger>
      <ContextMenuContent className="w-48">
        <ContextMenuItem onSelect={() => void onCopy(binding)}>
          Copy Shortcut
        </ContextMenuItem>
        <ContextMenuItem
          disabled={!binding.isCustom}
          onSelect={() => void onReset(binding.action)}
        >
          Reset to Default
        </ContextMenuItem>
      </ContextMenuContent>
    </ContextMenu>
  );
}
