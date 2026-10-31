import { useEffect, useRef } from "react";
import { X } from "lucide-react";
import { useTranslation } from "react-i18next";
import { cn } from "../../lib/utils";
import { AccountSetupFlow } from "./setup/AccountSetupFlow";

interface AddAccountModalProps {
  isOpen: boolean;
  onClose: () => void;
  onAccountAdded: () => void;
  fullscreen?: boolean;
}

export function AddAccountModal({ isOpen, onClose, onAccountAdded, fullscreen = false }: AddAccountModalProps) {
  const modalRef = useRef<HTMLDivElement>(null);
  const { t } = useTranslation();

  useEffect(() => {
    if (isOpen) {
      requestAnimationFrame(() => {
        const firstEl = modalRef.current?.querySelector<HTMLElement>("button, input, select");
        firstEl?.focus();
      });
    }
  }, [isOpen]);

  if (!isOpen) return null;

  function handleComplete(_accountId: string) {
    onAccountAdded();
    onClose();
  }

  return (
    <div
      className={cn(
        "fixed inset-0 z-50 animate-fade-in",
        fullscreen ? "flex flex-col bg-background" : "flex items-center justify-center bg-black/60 backdrop-blur-sm",
      )}
      role="dialog"
      aria-modal="true"
      aria-labelledby="add-account-title"
      onClick={fullscreen ? undefined : onClose}
      onKeyDown={(e) => {
        if (e.key === "Escape") { onClose(); return; }
        if (e.key !== "Tab") return;
        const focusable = modalRef.current?.querySelectorAll<HTMLElement>(
          'input:not([disabled]), select:not([disabled]), button:not([disabled]), [tabindex]:not([tabindex="-1"])'
        );
        if (!focusable || focusable.length === 0) return;
        const first = focusable[0];
        const last = focusable[focusable.length - 1];
        if (e.shiftKey && document.activeElement === first) {
          e.preventDefault();
          last.focus();
        } else if (!e.shiftKey && document.activeElement === last) {
          e.preventDefault();
          first.focus();
        }
      }}
    >
      <div
        ref={modalRef}
        onClick={(e) => e.stopPropagation()}
        className={cn(
          "flex h-full w-full flex-col bg-background",
          fullscreen ? "rounded-none" : "shadow-2xl animate-scale-in md:h-auto md:max-h-[90vh] md:max-w-lg md:rounded-xl",
        )}
      >
        <div
          className={cn(
            "flex items-center justify-between border-b border-border",
            fullscreen ? "px-4 py-3" : "px-6 py-4",
          )}
          style={fullscreen ? { paddingTop: "env(safe-area-inset-top)" } : undefined}
        >
          <h2 id="add-account-title" className="text-lg font-semibold text-foreground">{t("account.addAccount")}</h2>
          <button
            onClick={onClose}
            className="mail-pressable flex h-8 w-8 items-center justify-center rounded-lg text-muted-foreground hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
          >
            <X size={20} />
          </button>
        </div>
        <div
          className={cn(
            "overflow-y-auto",
            fullscreen ? "flex-1 px-4 py-4" : "max-h-[70vh] p-6",
          )}
          style={fullscreen ? { paddingBottom: "env(safe-area-inset-bottom)" } : undefined}
        >
          <AccountSetupFlow onComplete={handleComplete} onCancel={onClose} />
        </div>
      </div>
    </div>
  );
}
