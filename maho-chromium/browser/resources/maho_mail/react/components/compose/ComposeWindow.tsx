import { useState, useEffect, useRef } from "react";
import { Loader2 } from "lucide-react";
import * as api from "../../api";
import type { AccountSummary } from "../../types";
import {
  useComposeInitListener,
  announceComposeReady,
  closeSelf,
} from "../../hooks/usePopoutWindow";
import type { ComposeInitPayload } from "../../events";
import { useFocusTrap } from "../../hooks/useFocusTrap";
import { ComposeForm } from "./ComposeForm";

export function ComposeWindow() {
  const [initPayload, setInitPayload] = useState<ComposeInitPayload | null>(null);
  const [accounts, setAccounts] = useState<AccountSummary[]>([]);
  const [initialized, setInitialized] = useState(false);
  const windowRef = useRef<HTMLDivElement>(null);

  // Set up focus trap for popout window accessibility
  useFocusTrap(windowRef, initialized);

  useComposeInitListener((payload) => {
    setInitPayload(payload as ComposeInitPayload);
    setInitialized(true);
  });

  useEffect(() => {
    void api.listAccounts().then((accts) => {
      setAccounts(accts);
    });
  }, []);

  useEffect(() => {
    return announceComposeReady();
  }, []);

  // Safe fallback if initialization takes too long
  useEffect(() => {
    if (!initialized) {
      const timer = setTimeout(() => {
        setInitialized(true);
      }, 2000);
      return () => clearTimeout(timer);
    }
  }, [initialized]);

  if (!initialized) {
    return (
      <div className="flex h-screen items-center justify-center bg-background text-foreground">
        <Loader2 className="animate-spin text-muted-foreground" />
      </div>
    );
  }

  const handleClose = async () => {
    closeSelf();
  };

  return (
    <div
      ref={windowRef}
      className="flex h-screen flex-col bg-background text-foreground"
    >
      <div className="h-8 shrink-0 select-none bg-background/50" data-tauri-drag-region />
      <ComposeForm
        onClose={handleClose}
        accountId={initPayload?.accountId ?? ""}
        initialTo={initPayload?.to}
        initialCc={initPayload?.cc}
        initialBcc={initPayload?.bcc}
        initialSubject={initPayload?.subject}
        initialBody={initPayload?.body}
        initialBodyHtml={initPayload?.bodyHtml}
        initialDraftId={initPayload?.draftId}
        replyTo={initPayload?.replyTo}
        forwardFrom={initPayload?.forwardFrom}
        forwardEmailUid={initPayload?.forwardEmailUid}
        forwardFolderId={initPayload?.forwardFolderId}
        initialAttachments={initPayload?.initialAttachments}
        initialReadReceipt={initPayload?.initialReadReceipt}
        accounts={accounts}
        isPopout={true}
      />
    </div>
  );
}
