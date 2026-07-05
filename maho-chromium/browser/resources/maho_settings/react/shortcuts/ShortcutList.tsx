import React from 'react';
import {cn} from '@lib/utils';
import {ShortcutBinding} from '../../mojo.js';
import {getIconForAction} from './action_icons.js';
import {formatActionName} from './format_action_name.js';
import {formatKeyCombo} from './key_combo.js';
import {ShortcutContextMenu} from './ShortcutContextMenu.js';
import {ShortcutActionId} from './types.js';

interface ShortcutListProps {
  shortcuts: ShortcutBinding[];
  mode: 'editor' | 'cheat-sheet';
  onSelect?: (binding: ShortcutBinding) => void;
  onEdit?: (binding: ShortcutBinding) => void;
  onContextMenu?: (e: React.MouseEvent, binding: ShortcutBinding) => void;
  selectedActionId?: string | null;
  onCopy?: (binding: ShortcutBinding) => void | Promise<void>;
  onReset?: (action: string) => void | Promise<void>;
}

interface ShortcutEditorRowProps extends Omit<React.HTMLAttributes<HTMLDivElement>, 'onSelect'> {
  binding: ShortcutBinding;
  selectedActionId?: string | null;
  onSelect?: (binding: ShortcutBinding) => void;
  onEdit?: (binding: ShortcutBinding) => void;
}

const ShortcutEditorRow = React.forwardRef<HTMLDivElement, ShortcutEditorRowProps>(({ 
  binding,
  selectedActionId,
  onSelect,
  onEdit,
  className,
  ...props
}, ref) => {
  const IconComponent = getIconForAction(binding.action as ShortcutActionId);
  const isSelectable = !!onSelect;
  const label = binding.label || formatActionName(binding.action);

  const content = (
    <>
      <div className="rounded-md bg-muted p-1.5 text-muted-foreground">
        <IconComponent className="h-3.5 w-3.5" />
      </div>
      <div className="flex min-w-0 flex-col gap-0.5">
        <span className="truncate text-sm font-medium">{label}</span>
        <span className="text-xs text-muted-foreground">
          {binding.isCustom ? 'Custom' : 'Default'}
        </span>
      </div>
    </>
  );

  return (
    <div
      ref={ref}
      {...props}
      className={cn(
          'flex items-center justify-between gap-3 px-4 py-2.5 transition-colors select-none',
          isSelectable && 'cursor-pointer hover:bg-muted/20',
          selectedActionId === binding.action && 'bg-muted/30',
          className
      )}
    >
      {isSelectable ? (
        <button
          type="button"
          onClick={() => onSelect(binding)}
          className="flex min-w-0 flex-1 items-center gap-3 text-left focus-visible:outline-none"
        >
          {content}
        </button>
      ) : (
        <div className="flex min-w-0 flex-1 items-center gap-3">
          {content}
        </div>
      )}

      <button
        type="button"
        title="Click to edit shortcut"
        disabled={!onEdit}
        onClick={event => {
          event.stopPropagation();
          onEdit?.(binding);
        }}
        className="cursor-pointer rounded border bg-muted px-2 py-0.5 font-mono text-xs select-none hover:bg-muted/70 disabled:pointer-events-none"
      >
        {formatKeyCombo(binding.keyCombo)}
      </button>
    </div>
  );
});
ShortcutEditorRow.displayName = 'ShortcutEditorRow';

function ShortcutCheatSheetRow({binding}: {binding: ShortcutBinding}) {
  return (
    <div className="flex items-center justify-between gap-4 text-xs">
      <span className="text-muted-foreground">
        {binding.label || formatActionName(binding.action)}
      </span>
      <kbd className="rounded border bg-muted px-1.5 py-0.5 font-mono not-italic print:border-none print:bg-transparent">
        {formatKeyCombo(binding.keyCombo)}
      </kbd>
    </div>
  );
}

export function ShortcutList({
  shortcuts,
  mode,
  onSelect,
  onEdit,
  selectedActionId,
  onCopy,
  onReset,
}: ShortcutListProps) {
  return (
    <div className={cn(mode === 'cheat-sheet' && 'space-y-1.5')}>
      {shortcuts.map(binding => {
        if (mode === 'cheat-sheet') {
          return (
            <ShortcutCheatSheetRow key={binding.action} binding={binding} />
          );
        }

        const row = (
          <ShortcutEditorRow
            binding={binding}
            selectedActionId={selectedActionId}
            onSelect={onSelect}
            onEdit={onEdit}
          />
        );

        if (onCopy && onReset) {
          return (
            <ShortcutContextMenu
              key={binding.action}
              binding={binding}
              onCopy={onCopy}
              onReset={onReset}
            >
              {row}
            </ShortcutContextMenu>
          );
        }

        return <React.Fragment key={binding.action}>{row}</React.Fragment>;
      })}
    </div>
  );
}
