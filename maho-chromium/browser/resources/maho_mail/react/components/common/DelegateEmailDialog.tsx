import { useState } from "react";
import { useTranslation } from "react-i18next";
import { UserPlus, X, Send } from "lucide-react";
import { Dialog, DialogContent, DialogTitle, DialogDescription } from "@/components/ui/dialog";

interface DelegateEmailDialogProps {
  isOpen: boolean;
  onClose: () => void;
  /** Rejects when delegation fails; the dialog then keeps the typed input. */
  onDelegate: (email: string, note: string) => void | Promise<void>;
}

export function DelegateEmailDialog({ isOpen, onClose, onDelegate }: DelegateEmailDialogProps) {
  const [delegateEmail, setDelegateEmail] = useState("");
  const [note, setNote] = useState("");
  const [submitting, setSubmitting] = useState(false);
  const { t } = useTranslation();

  async function handleSubmit(e: React.FormEvent) {
    e.preventDefault();
    if (!delegateEmail.trim() || submitting) return;
    setSubmitting(true);
    try {
      await onDelegate(delegateEmail.trim(), note.trim());
      setDelegateEmail("");
      setNote("");
    } catch {
      // The caller reported the error; keep the input so the user can retry.
    } finally {
      setSubmitting(false);
    }
  }

  return (
    <Dialog open={isOpen} onOpenChange={(open) => !open && onClose()}>
      <DialogContent hideDefaultClose className="w-full max-w-sm rounded-xl border border-border bg-background p-6 shadow-2xl sm:rounded-xl">
        <DialogDescription className="sr-only">{t("email.delegateDescription")}</DialogDescription>
        <div className="mb-4 flex items-center justify-between">
          <div className="flex items-center gap-2">
            <UserPlus size={18} className="text-primary" />
            <DialogTitle className="text-sm font-semibold text-foreground">{t("email.delegateTitle")}</DialogTitle>
          </div>
          <button
            type="button"
            onClick={onClose}
            aria-label={t("common.close")}
            title={t("common.close")}
            className="mail-pressable flex h-8 w-8 items-center justify-center rounded-lg text-muted-foreground hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
          >
            <X size={16} />
          </button>
        </div>

        <form onSubmit={(e) => void handleSubmit(e)}>
          <input
            type="email"
            placeholder={t("email.delegateEmailPlaceholder")}
            value={delegateEmail}
            onChange={(e) => setDelegateEmail(e.target.value)}
            className="mb-3 w-full rounded-lg border border-border bg-card px-3 py-2 text-sm text-foreground placeholder:text-muted-foreground focus:border-ring focus:outline-none"
            autoFocus
          />
          <textarea
            placeholder={t("email.delegateNotePlaceholder")}
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
              {t("common.cancel")}
            </button>
            <button
              type="submit"
              disabled={!delegateEmail.trim() || submitting}
              className="mail-pressable flex h-9 items-center gap-1.5 rounded-lg bg-primary px-4 text-sm font-medium text-primary-foreground shadow-sm hover:bg-primary/90 disabled:opacity-50"
            >
              <Send size={14} />
              {t("email.delegateAction")}
            </button>
          </div>
        </form>
      </DialogContent>
    </Dialog>
  );
}
