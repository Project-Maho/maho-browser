import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
  DialogDescription,
} from "./dialog";

interface ShortcutCheatsheetProps {
  isOpen: boolean;
  onClose: () => void;
}

const SHORTCUTS = [
  { key: "c", description: "Compose new email" },
  { key: "j", description: "Next email" },
  { key: "k", description: "Previous email" },
  { key: "Enter", description: "Open selected email" },
  { key: "/", description: "Search" },
  { key: "Escape", description: "Close modal/panel" },
  { key: "r", description: "Reply" },
  { key: "Shift+R", description: "Reply all" },
  { key: "f", description: "Forward" },
  { key: "e", description: "Archive" },
  { key: "#", description: "Delete" },
  { key: "u", description: "Toggle read/unread" },
  { key: "⌘/Ctrl+Enter", description: "Send email" },
  { key: "?", description: "Show shortcuts" },
];

export function ShortcutCheatsheet({ isOpen, onClose }: ShortcutCheatsheetProps) {
  return (
    <Dialog open={isOpen} onOpenChange={(open) => { if (!open) onClose(); }}>
      <DialogContent className="max-w-lg">
        <DialogHeader>
          <DialogTitle>Keyboard Shortcuts</DialogTitle>
          <DialogDescription className="sr-only">
            List of available keyboard shortcuts
          </DialogDescription>
        </DialogHeader>
        <div className="grid grid-cols-2 gap-2">
          {SHORTCUTS.map((s) => (
            <div key={s.key} className="flex items-center justify-between rounded-lg px-3 py-2 text-sm">
              <span className="text-muted-foreground">{s.description}</span>
              <kbd className="ml-3 rounded border border-border bg-muted px-2 py-0.5 font-mono text-xs text-foreground">
                {s.key}
              </kbd>
            </div>
          ))}
        </div>
      </DialogContent>
    </Dialog>
  );
}
