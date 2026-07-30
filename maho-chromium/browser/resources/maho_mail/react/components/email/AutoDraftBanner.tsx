import { useState, useEffect } from "react";
import { Sparkles, ChevronDown, ChevronUp, Check, X } from "lucide-react";
import * as api from "../../api";
import type { AutoDraft } from "../../types";

interface AutoDraftBannerProps {
  emailId: string;
  accountId?: string;
  onAccept: (draftContent: string) => void;
}

export function AutoDraftBanner({ emailId, accountId, onAccept }: AutoDraftBannerProps) {
  const [autoDraft, setAutoDraft] = useState<AutoDraft | null>(null);
  const [expanded, setExpanded] = useState(false);

  useEffect(() => {
    let cancelled = false;
    const fetchDraft = accountId 
      ? api.getAutoDraftForEmail(accountId, emailId) 
      : api.getAutoDraftForEmail(emailId);
      
    fetchDraft.then((draft) => {
      if (!cancelled) setAutoDraft(draft);
    }).catch(() => undefined);
    return () => { cancelled = true; };
  }, [emailId, accountId]);

  if (!autoDraft) return null;

  async function handleDismiss() {
    if (!autoDraft) return;
    await api.updateAutoDraftStatus(autoDraft.id, "dismissed").catch(() => undefined);
    setAutoDraft(null);
  }

  function handleAccept() {
    if (!autoDraft) return;
    api.updateAutoDraftStatus(autoDraft.id, "accepted").catch(() => undefined);
    onAccept(autoDraft.draft_content);
    setAutoDraft(null);
  }

  return (
    <div className="border-b border-border bg-primary/5 px-6 py-3">
      <div className="flex items-center gap-3">
        <Sparkles size={14} className="text-primary" />
        <span className="text-sm text-primary font-medium">
          AI drafted a reply for this email
        </span>
        <button
          onClick={() => setExpanded(!expanded)}
          className="flex items-center gap-1 rounded-lg border border-primary/30 bg-primary/10 px-2.5 py-1 text-xs font-medium text-primary transition-colors hover:bg-primary/20"
        >
          {expanded ? <ChevronUp size={12} /> : <ChevronDown size={12} />}
          Preview
        </button>
        <button
          onClick={handleAccept}
          className="flex items-center gap-1 rounded-lg border border-emerald-500/30 bg-emerald-500/10 px-2.5 py-1 text-xs font-medium text-emerald-600 dark:text-emerald-400 transition-colors hover:bg-emerald-500/20"
        >
          <Check size={12} />
          Accept
        </button>
        <button
          onClick={() => void handleDismiss()}
          className="ml-auto rounded p-1 text-muted-foreground hover:text-foreground"
          aria-label="Dismiss"
          title="Dismiss"
        >
          <X size={14} />
        </button>
      </div>
      {expanded && (
        <div className="mt-3 rounded-lg border border-border bg-card p-4 text-sm text-foreground whitespace-pre-wrap">
          {autoDraft.draft_content}
        </div>
      )}
    </div>
  );
}
