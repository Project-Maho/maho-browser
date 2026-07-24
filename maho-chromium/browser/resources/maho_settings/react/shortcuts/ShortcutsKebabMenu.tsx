import React from 'react';
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuTrigger,
} from '@ui/dropdown-menu';
import {Button} from '@ui/button';

interface ShortcutsKebabMenuProps {
  onExport: () => void;
  onImport: () => void;
  onResetAll: () => void;
  onResetCustomized: () => void;
  onShowCheatSheet: () => void;
}

export function ShortcutsKebabMenu({
  onExport,
  onImport,
  onResetAll,
  onResetCustomized,
  onShowCheatSheet,
}: ShortcutsKebabMenuProps) {
  return (
    <DropdownMenu>
      <DropdownMenuTrigger asChild>
        <Button variant="outline" size="sm" className="h-9">
          Actions ▾
        </Button>
      </DropdownMenuTrigger>
      <DropdownMenuContent align="end" className="w-56 bg-card border shadow-md p-1">
        <DropdownMenuItem onClick={onShowCheatSheet} className="hover:bg-surface-hover cursor-pointer rounded-sm px-2 py-1.5 text-sm">
          Show Cheat Sheet
        </DropdownMenuItem>
        <DropdownMenuItem onClick={onExport} className="hover:bg-surface-hover cursor-pointer rounded-sm px-2 py-1.5 text-sm">
          Export JSON
        </DropdownMenuItem>
        <DropdownMenuItem onClick={onImport} className="hover:bg-surface-hover cursor-pointer rounded-sm px-2 py-1.5 text-sm">
          Import JSON
        </DropdownMenuItem>
        <DropdownMenuItem onClick={onResetCustomized} className="hover:bg-surface-hover cursor-pointer rounded-sm px-2 py-1.5 text-sm">
          Reset Customized Only
        </DropdownMenuItem>
        <DropdownMenuItem onClick={onResetAll} className="hover:bg-surface-hover cursor-pointer text-destructive rounded-sm px-2 py-1.5 text-sm">
          Reset All
        </DropdownMenuItem>
      </DropdownMenuContent>
    </DropdownMenu>
  );
}
