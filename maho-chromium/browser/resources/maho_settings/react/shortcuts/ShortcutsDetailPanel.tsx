import React from 'react';
import {Card, CardContent, CardDescription, CardHeader, CardTitle} from '@ui/card';
import {Button} from '@ui/button';
import {MojoKeyCombo, ShortcutBinding} from '../../mojo.js';
import {formatKeyCombo} from './key_combo.js';
import {getIconForAction} from './action_icons.js';
import {formatActionName} from './format_action_name.js';
import {ShortcutActionId} from './types.js';

interface ShortcutsDetailPanelProps {
  selectedAction: ShortcutBinding | null;
  onEdit: (binding: ShortcutBinding) => void;
  onReset: (action: string) => void;
  onToggleEnabled: (action: string, enabled: boolean) => void;
}

export function ShortcutsDetailPanel({
  selectedAction,
  onEdit,
  onReset,
  onToggleEnabled,
}: ShortcutsDetailPanelProps) {
  if (!selectedAction) {
    return (
      <div className="border border-dashed border-border/70 rounded-lg p-6 text-center text-sm text-muted-foreground">
        Select a shortcut to view details or modify.
      </div>
    );
  }

  const IconComponent = getIconForAction(selectedAction.action as ShortcutActionId);
  const defaultKeyCombo = selectedAction.defaultKeyCombo;

  return (
    <Card className="border-border/70 bg-card/95 shadow-sm sticky top-4">
      <CardHeader className="flex flex-row items-center gap-3">
        <div className="p-2 bg-muted rounded-lg">
          <IconComponent className="w-5 h-5 text-muted-foreground" />
        </div>
        <div>
          <CardTitle className="text-base">{selectedAction.label || formatActionName(selectedAction.action)}</CardTitle>
          <CardDescription className="text-xs font-mono">{selectedAction.action}</CardDescription>
        </div>
      </CardHeader>
      <CardContent className="space-y-4 text-sm">
        <div className="flex items-center justify-between pb-3 border-b border-border/30">
          <span className="text-muted-foreground">Current Combo</span>
          <div className="flex flex-col items-end">
            <span className="font-mono text-sm px-2 py-0.5 bg-muted rounded border">
              {formatKeyCombo(selectedAction.keyCombo)}
            </span>
            {selectedAction.isCustom && defaultKeyCombo?.key && (
              <p className="text-xs text-muted-foreground mt-1">
                Overridden from <kbd className="px-1 rounded bg-muted font-mono">{formatKeyCombo(defaultKeyCombo)}</kbd>
              </p>
            )}
          </div>
        </div>
        <div className="flex items-center justify-between pb-3 border-b border-border/30">
          <span className="text-muted-foreground">Status</span>
          <span className="text-xs font-semibold">
            {selectedAction.isCustom ? 'Customized' : 'Default'}
          </span>
        </div>

        <div className="flex items-center justify-between pb-3 border-b border-border/30">
          <span className="text-muted-foreground">Enabled</span>
          <Button
            variant="outline"
            size="sm"
            onClick={() => onToggleEnabled(selectedAction.action, !selectedAction.enabled)}
          >
            {selectedAction.enabled ? 'Disable' : 'Enable'}
          </Button>
        </div>

        <div className="flex gap-2 pt-2">
          <Button className="flex-1" onClick={() => onEdit(selectedAction)}>
            Edit
          </Button>
          <Button
            variant="secondary"
            disabled={!selectedAction.isCustom}
            onClick={() => onReset(selectedAction.action)}
          >
            Reset
          </Button>
        </div>
      </CardContent>
    </Card>
  );
}
