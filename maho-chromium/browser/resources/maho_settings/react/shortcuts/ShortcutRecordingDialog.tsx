import React, {useEffect, useRef} from 'react';
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
  DialogDescription,
  DialogFooter,
} from '@ui/dialog';
import {Button} from '@ui/button';
import {AlertCircle} from '@icons/lucide';
import {MojoKeyCombo, ShortcutBinding} from '../../mojo.js';
import {formatActionName} from './format_action_name.js';
import {formatKeyCombo} from './key_combo.js';

interface ShortcutRecordingDialogProps {
  open: boolean;
  binding: ShortcutBinding | null;
  recordedCombo: MojoKeyCombo | null;
  conflictAction: string | null;
  onSave: () => void;
  onCancel: () => void;
  onTryAgain: () => void;
  onClear: () => void;
}

export function ShortcutRecordingDialog({
  open,
  binding,
  recordedCombo,
  conflictAction,
  onSave,
  onCancel,
  onTryAgain,
  onClear,
}: ShortcutRecordingDialogProps) {
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const resetWatchdog = () => {
    if (timerRef.current) clearTimeout(timerRef.current);
    timerRef.current = setTimeout(() => {
      onCancel();
    }, 5000);
  };

  useEffect(() => {
    if (open) {
      resetWatchdog();
    }
    return () => {
      if (timerRef.current) clearTimeout(timerRef.current);
    };
  }, [open]);

  useEffect(() => {
    if (recordedCombo) {
      resetWatchdog();
    }
  }, [recordedCombo]);

  useEffect(() => {
    if (!open) return;
    const handleKeyDown = (e: KeyboardEvent) => {
      resetWatchdog();
      if (e.key === 'Escape') {
        e.preventDefault();
        onCancel();
      } else if (e.key === 'Backspace' || e.key === 'Delete') {
        const hasModifiers = e.ctrlKey || e.metaKey || e.altKey || e.shiftKey;
        if (!hasModifiers) {
          e.preventDefault();
          onClear();
        }
      }
    };
    window.addEventListener('keydown', handleKeyDown, true);
    return () => window.removeEventListener('keydown', handleKeyDown, true);
  }, [open, onCancel, onClear]);

  return (
    <Dialog open={open} onOpenChange={(o) => { if (!o) onCancel(); }}>
      <DialogContent className="max-w-md border border-border bg-card shadow-lg rounded-xl p-6">
        <DialogHeader className="space-y-2">
          <DialogTitle className="text-xl font-bold flex items-center gap-2">
            Record Shortcut
          </DialogTitle>
          <DialogDescription>
            Assign a new keyboard shortcut for: <span className="font-semibold text-foreground">{binding ? (binding.label || formatActionName(binding.action)) : ''}</span>
          </DialogDescription>
        </DialogHeader>

        <div className="flex flex-col items-center justify-center p-8 bg-muted/30 border border-dashed rounded-lg my-4 gap-4">
          <div className="w-16 h-16 rounded-full bg-primary/10 flex items-center justify-center animate-pulse">
            <span className="text-2xl">⌨️</span>
          </div>
          {conflictAction ? (
            <div className="text-center space-y-2">
              <p className="text-sm text-destructive font-medium flex items-center justify-center gap-1">
                <AlertCircle className="w-4 h-4" /> Conflict Detected
              </p>
              <p className="text-xs text-muted-foreground">
                The shortcut is currently assigned to: <span className="font-semibold text-foreground">{formatActionName(conflictAction)}</span>
              </p>
            </div>
          ) : (
            <p className="text-sm text-muted-foreground text-center">
              Press your key combination on your keyboard now... (5s timeout)
            </p>
          )}
          <div className="font-mono text-2xl font-bold px-4 py-2 bg-background border rounded-lg min-w-[150px] text-center shadow-inner">
            {recordedCombo ? formatKeyCombo(recordedCombo) : 'None'}
          </div>
        </div>

        <DialogFooter className="gap-2">
          {conflictAction ? (
            <>
              <Button onClick={onSave}>Reassign</Button>
              <Button variant="secondary" onClick={onTryAgain}>Cancel</Button>
            </>
          ) : (
            <>
              {recordedCombo && (
                <Button onClick={onSave}>Save</Button>
              )}
              <Button variant="secondary" onClick={onCancel}>Cancel</Button>
            </>
          )}
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
