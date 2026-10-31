import { useState } from "react";
import { UserPlus, X, Send } from "lucide-react";
import { Dialog, DialogContent, DialogTitle, DialogDescription } from "@/components/ui/dialog";

interface DelegateEmailDialogProps {
  isOpen: boolean;
  onClose: () => void;
  onDelegate: (email: string, note: string) => void;
}

export function DelegateEmailDialog({ isOpen, onClose, onDelegate }: DelegateEmailDialogProps) {
  const [delegateEmail, setDelegateEmail] = useState("");
  const [note, setNote] = useState("");

  function handleSubmit(e: React.FormEvent) {
    e.preventDefault();
    if (!delegateEmail.trim()) return;
    onDelegate(delegateEmail.trim(), note.trim());
    setDelegateEmail("");
    setNote("");
  }

  return (
    <Dialog open={isOpen} onOpenChange={(open) => !open && onClose()}>
      <DialogContent hideDefaultClose className="w-full max-w-sm rounded-xl border border-border bg-background p-6 shadow-2xl sm:rounded-xl">
        <DialogDescription className="sr-only">Forward this email to someone else</DialogDescription>
        <div className="mb-4 flex items-center justify-between">
          <div className="flex items-center gap-2">
            <UserPlus size={18} className="text-primary" />
            <DialogTitle className="text-sm font-semibold text-foreground">Delegate Email</DialogTitle>
          </div>
          <button
            type="button"
            onClick={onClose}
            className="mail-pressable flex h-8 w-8 items-center justify-center rounded-lg text-muted-foreground hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
          >
            <X size={16} />
          </button>
        </div>

        <form onSubmit={handleSubmit}>
          <input
            type="email"
            placeholder="Delegate to email..."
            value={delegateEmail}
            onChange={(e) => setDelegateEmail(e.target.value)}
            className="mb-3 w-full rounded-lg border border-border bg-card px-3 py-2 text-sm text-foreground placeholder:text-muted-foreground focus:border-ring focus:outline-none"
            autoFocus
          />
          <textarea
            placeholder="Add a note (optional)..."
            value={note}
            onChange={(e) => setNote(e.target.value)}
            rows={3}
            className="mb-4 w-full resize-none rounded-lg border border-border bg-card px-3 py-2 text-sm text-foreground placeholder:text-muted-foreground focus:border-ring focus:outline-none"
          />
          <div className="flex justify-end gap-2">
            <button
              type="button"
              onClick={onClose}
              className="mail-pressable flex h-9 items-center rounded-lg px-3 text-sm text-muted-foreground hover:bg-surface-hover hover:text-foreground"
            >
              Cancel
            </button>
            <button
              type="submit"
              disabled={!delegateEmail.trim()}
              className="mail-pressable flex h-9 items-center gap-1.5 rounded-lg bg-primary px-4 text-sm font-medium text-primary-foreground shadow-sm hover:bg-primary/90 disabled:opacity-50"
            >
              <Send size={14} />
              Delegate
            </button>
          </div>
        </form>
      </DialogContent>
    </Dialog>
  );
}
