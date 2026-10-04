import React from 'react';
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
  DialogDescription,
} from '@ui/dialog';
import {Button} from '@ui/button';
import {ShortcutBinding} from '../../mojo.js';
import {ShortcutList} from './ShortcutList.js';

interface ShortcutsCheatSheetProps {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  shortcuts: ShortcutBinding[];
}

export function ShortcutsCheatSheet({open, onOpenChange, shortcuts}: ShortcutsCheatSheetProps) {
  const categories = [
    {key: 'navigation', label: 'Navigation'},
    {key: 'tabs', label: 'Tabs'},
    {key: 'spaces', label: 'Spaces'},
    {key: 'window', label: 'Window'},
    {key: 'edit', label: 'Edit'},
    {key: 'view', label: 'View'},
    {key: 'developer', label: 'Developer'},
    {key: 'custom', label: 'Custom'},
  ];

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="max-w-4xl max-h-[85vh] overflow-y-auto border border-border bg-card shadow-xl rounded-xl p-6">
        <DialogHeader className="flex flex-row justify-between items-center border-b border-border/50 pb-3">
          <div>
            <DialogTitle className="text-xl font-bold">Shortcuts Cheat Sheet</DialogTitle>
            <DialogDescription>Overview of all keyboard shortcut mappings in Maho.</DialogDescription>
          </div>
          <Button size="sm" onClick={() => window.print()} className="print:hidden">
            Print Sheet
          </Button>
        </DialogHeader>

        <div className="grid grid-cols-1 md:grid-cols-2 gap-6 py-6 print:grid-cols-2 print:text-black">
          {categories.map(cat => {
            const categoryShortcuts = shortcuts.filter(b => b.category === cat.key);
            if (categoryShortcuts.length === 0) return null;
            return (
              <div key={cat.key} className="space-y-2">
                <h4 className="text-sm font-bold border-b border-border pb-1 text-primary">{cat.label}</h4>
                <ShortcutList shortcuts={categoryShortcuts} mode="cheat-sheet" />
              </div>
            );
          })}
        </div>
      </DialogContent>
    </Dialog>
  );
}
