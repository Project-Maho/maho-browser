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
      <CardHeader
        onClick={onToggleCollapse}
        role="button"
        aria-expanded={!isCollapsed}
        aria-controls={`shortcuts-section-content-${categoryKey}`}
        className="py-3 px-4 flex flex-row items-center justify-between cursor-pointer hover:bg-muted/10 select-none"
      >
        <CardTitle className="text-sm font-semibold">{categoryLabel}</CardTitle>
        <span className="text-xs text-muted-foreground">{isCollapsed ? 'Expand' : 'Collapse'}</span>
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
