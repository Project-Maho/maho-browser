import { useState, useEffect, useRef } from "react";
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
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const request = useRef(0);

  useEffect(() => {
    setAutoDraft(null);
    setExpanded(false);
    setLoading(false);
    setError(null);
    return () => { request.current += 1; };
  }, [emailId, accountId]);

  async function handleGenerate() {
    const current = ++request.current;
    setLoading(true);
    setError(null);
    try {
      // This API returns a cached draft or generates one, so call only on intent.
      const draft = accountId
        ? await api.getAutoDraftForEmail(accountId, emailId)
        : await api.getAutoDraftForEmail(emailId);
      if (current === request.current) {
        setAutoDraft(draft);
        if (!draft) setError("No draft is available for this email.");
      }
    } catch {
      if (current === request.current) setError("Could not generate a reply. Please try again.");
    } finally {
      if (current === request.current) setLoading(false);
    }
  }

  if (!autoDraft) return (
    <div className="border-b border-border px-6 py-3">
      <button
        onClick={() => void handleGenerate()}
        disabled={loading}
        className="rounded-lg border border-primary/30 px-2.5 py-1 text-xs font-medium text-primary disabled:opacity-50"
      >
        {loading ? "Generating..." : "Generate AI reply"}
      </button>
      <p className="mt-1 text-xs text-muted-foreground">Uses the configured AI provider to process this email.</p>
      {error && <p role="alert" className="mt-1 text-xs text-muted-foreground">{error}</p>}
    </div>
  );

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
          className="flex items-center gap-1 mail-pressable flex h-7 rounded-lg border border-primary/30 bg-primary/10 px-2.5 text-xs font-medium text-primary hover:bg-primary/15"
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
          className="mail-pressable flex h-8 w-8 items-center justify-center rounded-lg text-muted-foreground hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring ml-auto h-7 w-7"
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
