import React from 'react';
import {Card, CardContent, CardHeader, CardTitle} from '@ui/card';
import {ShortcutBinding} from '../../mojo.js';
import {ShortcutList} from './ShortcutList.js';

interface ShortcutsSectionProps {
  categoryKey: string;
  categoryLabel: string;
  shortcuts: ShortcutBinding[];
  isCollapsed: boolean;
  onToggleCollapse: () => void;
  selectedAction: ShortcutBinding | null;
  onSelectAction: (binding: ShortcutBinding) => void;
  onEdit: (binding: ShortcutBinding) => void;
  onCopy: (binding: ShortcutBinding) => void;
  onReset: (action: string) => void;
}

export function ShortcutsSection({
  categoryKey,
  categoryLabel,
  shortcuts,
  isCollapsed,
  onToggleCollapse,
  selectedAction,
  onSelectAction,
  onEdit,
  onCopy,
  onReset,
}: ShortcutsSectionProps) {
  return (
    <Card className="border-border/70 bg-card/95 shadow-sm">
      <CardHeader className="p-0">
        <button
          type="button"
          onClick={onToggleCollapse}
          aria-expanded={!isCollapsed}
          aria-controls={`shortcuts-section-content-${categoryKey}`}
          className="w-full py-3 px-4 flex flex-row items-center justify-between text-left hover:bg-muted/10 select-none rounded-t-xl focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
        >
          <CardTitle className="text-sm font-semibold">{categoryLabel}</CardTitle>
          <span className="text-xs text-muted-foreground">{isCollapsed ? 'Expand' : 'Collapse'}</span>
        </button>
      </CardHeader>
      {!isCollapsed && (
        <CardContent
          id={`shortcuts-section-content-${categoryKey}`}
          className="p-0 border-t border-border/50 divide-y divide-border/30"
        >
          <ShortcutList
            shortcuts={shortcuts}
            mode="editor"
            selectedActionId={selectedAction?.action ?? null}
            onSelect={onSelectAction}
            onEdit={onEdit}
            onCopy={onCopy}
            onReset={onReset}
          />
        </CardContent>
      )}
    </Card>
  );
}
