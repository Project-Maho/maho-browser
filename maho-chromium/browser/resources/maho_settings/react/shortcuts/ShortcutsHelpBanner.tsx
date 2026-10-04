import React from 'react';
import {Sparkles} from '@icons/lucide';
import {Card, CardContent, CardHeader, CardTitle} from '@ui/card';

interface ShortcutsHelpBannerProps {
  onDismiss: () => void;
}

export function ShortcutsHelpBanner({onDismiss}: ShortcutsHelpBannerProps) {
  return (
    <Card className="border-border/70 bg-card/95 shadow-sm relative overflow-hidden">
      <CardHeader className="pb-2">
        <CardTitle className="text-sm font-semibold flex items-center gap-2">
          <Sparkles className="w-4 h-4 text-primary" /> Shortcuts Help
        </CardTitle>
      </CardHeader>
      <CardContent className="text-xs text-muted-foreground pr-8">
        Click any shortcut row and press "Edit" to record a custom combination. Press Escape during recording to cancel, or Backspace/Delete to clear.
        <button
          onClick={onDismiss}
          className="absolute right-3 top-3 text-muted-foreground hover:text-foreground">
          ✕
        </button>
      </CardContent>
    </Card>
  );
}
