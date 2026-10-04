import React from 'react';
import {Input} from '@ui/input';

interface ShortcutsSearchBarProps {
  value: string;
  onChange: (val: string) => void;
}

export function ShortcutsSearchBar({value, onChange}: ShortcutsSearchBarProps) {
  return (
    <div className="relative w-full max-w-sm">
      <Input
        placeholder="Search shortcuts (e.g. 'new tab' or '⌘T')..."
        value={value}
        onChange={(e) => onChange(e.target.value)}
        className="h-9 pr-8"
      />
      {value && (
        <button
          onClick={() => onChange('')}
          className="absolute right-2.5 top-2.5 text-muted-foreground hover:text-foreground text-xs"
        >
          ✕
        </button>
      )}
    </div>
  );
}
