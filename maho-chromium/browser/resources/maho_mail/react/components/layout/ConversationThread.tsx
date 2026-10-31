import { useEffect, useMemo, useRef, useState } from "react";
import { ChevronDown, Paperclip, Star } from "lucide-react";

import type { EmailDetail, EmailSummary } from "../../types";
import * as api from "../../api";
import { useToast } from "../ui/Toast";
import { EmailBodyView } from "./EmailBodyView";

interface ConversationThreadProps {
  threadEmails: EmailSummary[];
  selectedEmailId: string;
  selectedDetail: EmailDetail | null;
  pgpDecryptedText?: string | null;
  smimeDecryptedText?: string | null;
  translatedText?: string | null;
  translatedHtml?: string | null;
  showOriginalBody?: boolean;
  onToggleShowOriginal?: () => void;
  onEmailAddressClick?: (email: string) => void;
}

function initialFor(name: string | null, address: string): string {
  const source = (name && name.trim()) || address || "?";
  const ch = source.trim().charAt(0);
  return ch ? ch.toUpperCase() : "?";
}

function formatMessageTime(date: string): string {
  const d = new Date(date);
  if (Number.isNaN(d.getTime())) return "";
  return d.toLocaleString("en-US", {
    month: "short",
    day: "numeric",
    hour: "numeric",
    minute: "2-digit",
  });
}

// Regression contract: a conversation MUST be ordered by message date. The
// backend returns thread messages grouped by sender (non-chronological); render
// order is established by the date sort below, never by backend order.
export function ConversationThread({
  threadEmails,
  selectedEmailId,
  selectedDetail,
  pgpDecryptedText,
  smimeDecryptedText,
  translatedText,
  translatedHtml,
  showOriginalBody,
  onToggleShowOriginal,
  onEmailAddressClick,
}: ConversationThreadProps) {
  const { toast } = useToast();

  const ordered = useMemo(
    () =>
      [...threadEmails].sort(
        (a, b) => new Date(a.date).getTime() - new Date(b.date).getTime(),
      ),
    [threadEmails],
  );
  const latestId = ordered.length ? ordered[ordered.length - 1].id : null;

  const [expanded, setExpanded] = useState<Set<string>>(new Set());
  useEffect(() => {
    const next = new Set<string>();
    for (const m of ordered) {
      if (m.id === selectedEmailId || m.id === latestId || !m.is_read) {
        next.add(m.id);
      }
    }
    if (next.size === 0 && latestId) next.add(latestId);
    setExpanded(next);
  }, [ordered, selectedEmailId, latestId]);

  const [details, setDetails] = useState<Record<string, EmailDetail>>({});
  const inFlight = useRef<Set<string>>(new Set());
  const mountedRef = useRef(true);
  useEffect(() => {
    mountedRef.current = true;
    return () => {
      mountedRef.current = false;
    };
  }, []);

  useEffect(() => {
    const id = selectedDetail?.email?.id;
    if (!id) return;
    setDetails((prev) => (prev[id] ? prev : { ...prev, [id]: selectedDetail }));
  }, [selectedDetail]);

  useEffect(() => {
    for (const id of expanded) {
      if (details[id] || inFlight.current.has(id)) continue;
      inFlight.current.add(id);
      api
        .getEmail(id)
        .then((detail) => {
          if (mountedRef.current) setDetails((prev) => ({ ...prev, [id]: detail }));
        })
        .catch((err) => {
          if (mountedRef.current) {
            toast("error", err instanceof Error ? err.message : "Failed to load message");
          }
        })
        .finally(() => {
          inFlight.current.delete(id);
        });
    }
  }, [expanded, details, toast]);

  const toggle = (id: string) =>
    setExpanded((prev) => {
      const n = new Set(prev);
      if (n.has(id)) n.delete(id);
      else n.add(id);
      return n;
    });

  return (
    <ul className="flex flex-col gap-2 px-4 py-4" aria-label="Conversation">
      {ordered.map((m) => {
        const isExpanded = expanded.has(m.id);
        const detail = details[m.id];
        const isSelected = m.id === selectedEmailId;
        return (
          <li
            key={m.id}
            className={`overflow-hidden rounded-xl border transition-colors ${
              isSelected ? "border-border bg-primary/[0.06]" : "border-border/70 bg-card/40"
            }`}
          >
            <button
              type="button"
              onClick={() => toggle(m.id)}
              aria-expanded={isExpanded}
              aria-label={`${m.from_name || m.from_address}, ${formatMessageTime(m.date)}`}
              className="flex min-h-14 w-full items-center gap-3 px-4 py-3 text-left transition-colors hover:bg-surface-hover focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-inset focus-visible:ring-ring"
            >
              <span
                aria-hidden="true"
                className="flex h-8 w-8 shrink-0 items-center justify-center rounded-full bg-primary/15 text-xs font-semibold text-foreground"
              >
                {initialFor(m.from_name, m.from_address)}
              </span>
              <span className="flex min-w-0 flex-1 flex-col">
                <span
                  className={`truncate text-sm ${
                    m.is_read ? "font-normal text-foreground" : "font-semibold text-foreground"
                  }`}
                >
                  {m.from_name || m.from_address}
                </span>
                {!isExpanded && (
                  <span className="truncate text-xs text-muted-foreground">{m.snippet}</span>
                )}
              </span>
              {m.has_attachments && (
                <Paperclip className="h-3.5 w-3.5 shrink-0 text-muted-foreground" aria-hidden="true" />
              )}
              {m.is_starred && (
                <Star className="h-3.5 w-3.5 shrink-0 fill-amber-400 text-amber-400" aria-hidden="true" />
              )}
              <span className="shrink-0 text-xs tabular-nums text-muted-foreground">{formatMessageTime(m.date)}</span>
              <ChevronDown
                className={`mail-disclosure h-4 w-4 shrink-0 text-muted-foreground ${
                  isExpanded ? "rotate-180" : ""
                }`}
                aria-hidden="true"
              />
            </button>

            {isExpanded && (
              <div className="border-t border-border/60 animate-fade-in">
                {detail ? (
                  <EmailBodyView
                    emailDetail={detail}
                    onEmailAddressClick={onEmailAddressClick}
                    {...(isSelected
                      ? {
                          pgpDecryptedText,
                          smimeDecryptedText,
                          translatedText,
                          translatedHtml,
                          showOriginalBody,
                          onToggleShowOriginal,
                        }
                      : {})}
                  />
                ) : (
                  <div className="px-6 py-8 text-sm text-muted-foreground">Loading message…</div>
                )}
              </div>
            )}
          </li>
        );
      })}
    </ul>
  );
}
