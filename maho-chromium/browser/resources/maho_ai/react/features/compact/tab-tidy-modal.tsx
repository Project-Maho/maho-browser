import React, {useState} from 'react';
import {cn} from '@lib/utils';
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
  DialogFooter,
} from '@ui/dialog';
import {Button} from '@ui/button';
import {Checkbox} from '@ui/checkbox';

export interface TidyFolderSuggestion {
  name: string;
  tabIds: number[];
}

interface Props {
  folders: TidyFolderSuggestion[];
  onApply: (folders: TidyFolderSuggestion[]) => void;
  onDismiss: () => void;
}

export function TabTidyModal({folders, onApply, onDismiss}: Props) {
  const [checked, setChecked] = useState<Set<number>>(() => new Set(folders.map((_, i) => i)));

  function toggle(index: number) {
    setChecked(prev => {
      const next = new Set(prev);
      if (next.has(index)) {
        next.delete(index);
      } else {
        next.add(index);
      }
      return next;
    });
  }

  function handleApply() {
    onApply(folders.filter((_, i) => checked.has(i)));
  }

  return (
    <Dialog open={true} onOpenChange={(open: boolean) => { if (!open) onDismiss(); }}>
      <DialogContent className="sm:max-w-[425px] glass-strong">
        <DialogHeader>
          <DialogTitle>Tab Tidy Suggestions</DialogTitle>
        </DialogHeader>

        <p className="text-sm text-muted-foreground">Select folders to create:</p>
        <ul className="flex flex-col gap-2 max-h-60 overflow-y-auto py-2">
          {folders.map((folder, i) => (
            <li key={i} className="flex items-center space-x-2 p-2 border rounded hover:bg-surface-hover cursor-pointer" onClick={() => toggle(i)}>
              <Checkbox
                checked={checked.has(i)}
                onCheckedChange={() => toggle(i)}
              />
              <span className="flex-1 text-sm font-medium">{folder.name}</span>
              <span className="text-xs text-muted-foreground">{folder.tabIds.length} tab{folder.tabIds.length !== 1 ? 's' : ''}</span>
            </li>
          ))}
        </ul>

        <DialogFooter className="gap-2">
          <Button variant="outline" onClick={onDismiss}>Cancel</Button>
          <Button
            variant="default"
            onClick={handleApply}
            disabled={checked.size === 0}
          >
            Apply
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}

