import { useRef, useState } from "react";
import type { AccountSummary, ReplyContext, ComposeEmailRequest, ComposeAttachment, SendRequestedOptions } from "../../types";
import { useFocusTrap } from "../../hooks/useFocusTrap";
import { cn } from "../../lib/utils";
import { ComposeForm } from "./ComposeForm";

export { splitBodyForTone } from "./ComposeForm";

interface ComposeModalProps {
  isOpen: boolean;
  onClose: () => void;
  replyTo?: ReplyContext;
  forwardFrom?: ReplyContext;
  forwardEmailUid?: number;
  forwardFolderId?: string;
  accountId: string;
  accountEmail?: string;
  initialBody?: string;
  initialBodyHtml?: string;
  initialBodyIsComplete?: boolean;
  initialAttachments?: ComposeAttachment[];
  initialTo?: string;
  initialDraftId?: string;
  initialSubject?: string;
  initialCc?: string;
  initialBcc?: string;
  initialReadReceipt?: boolean;
  initialPgpEncrypt?: boolean;
  initialSmimeSign?: boolean;
  initialSmimeEncrypt?: boolean;
  initialInReplyTo?: string;
  initialReferences?: string;
  accounts: AccountSummary[];
  sourceAccountId?: string;
  onSendRequested?: (request: ComposeEmailRequest, options?: SendRequestedOptions) => void | Promise<void>;
  fullscreen?: boolean;
}

export function ComposeModal({ fullscreen: initialFullscreen, ...props }: ComposeModalProps) {
  const modalRef = useRef<HTMLDivElement>(null);
  const [fullscreen, setFullscreen] = useState(initialFullscreen ?? false);
  useFocusTrap(modalRef, props.isOpen);

  if (!props.isOpen) return null;

  return (
    <div
      ref={modalRef}
      className={cn(
        "fixed inset-0 z-50 flex items-center justify-center bg-black/50 backdrop-blur-sm animate-fade-in",
        fullscreen ? "p-0" : "p-4",
      )}
      role="dialog"
      aria-modal="true"
    >
      <div
        className={cn(
          "flex flex-col border-border bg-background shadow-2xl overflow-hidden dialog-enter",
          fullscreen
            ? "h-full w-full rounded-none border-0"
            : "h-[90vh] w-full max-w-4xl rounded-2xl border",
        )}
      >
        <ComposeForm
          {...props}
          fullscreen={fullscreen}
          onToggleFullscreen={() => setFullscreen((f) => !f)}
        />
      </div>
    </div>
  );
}
