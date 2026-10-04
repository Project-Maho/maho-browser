import { useState, useRef, useEffect, useCallback } from "react";
import { Sparkles, Undo2, Loader2, ChevronDown } from "lucide-react";
import * as api from "../../api";
import { useToast } from "../ui/Toast";

interface ToneAdjustButtonProps {
  body: string;
  bodyHtml?: string;
  splitBody?: (text: string, html: string) => { userText: string; signatureText: string; quoteText: string; signatureHtml: string; quoteHtml: string };
  setBody: (text: string) => void;
  setBodyHtml?: (html: string) => void;
  plainTextToHtml: (text: string) => string;
}

type ToneType = "professional" | "friendly" | "concise" | "formal";

export function ToneAdjustButton({
  body,
  bodyHtml,
  splitBody,
  setBody,
  setBodyHtml,
  plainTextToHtml,
}: ToneAdjustButtonProps) {
  const { toast } = useToast();
  const [loading, setLoading] = useState(false);
  const currentBodyRef = useRef(body);
  currentBodyRef.current = body;
  const currentHtmlRef = useRef(bodyHtml);
  currentHtmlRef.current = bodyHtml;
  const [previousHtml, setPreviousHtml] = useState<string | null>(null);
  const inFlightRef = useRef(false);
  const [previousBody, setPreviousBody] = useState<string | null>(null);
  const [isOpen, setIsOpen] = useState(false);
  const dropdownRef = useRef<HTMLDivElement | null>(null);

  useEffect(() => {
    function handleClickOutside(event: MouseEvent) {
      if (dropdownRef.current && !dropdownRef.current.contains(event.target as Node)) {
        setIsOpen(false);
      }
    }
    document.addEventListener("mousedown", handleClickOutside);
    return () => document.removeEventListener("mousedown", handleClickOutside);
  }, []);

  useEffect(() => {
    if (!isOpen) return;
    function handleKeyDown(event: KeyboardEvent) {
      if (event.key === "Escape") {
        event.preventDefault();
        event.stopPropagation();
        setIsOpen(false);
      }
    }
    document.addEventListener("keydown", handleKeyDown);
    return () => document.removeEventListener("keydown", handleKeyDown);
  }, [isOpen]);

  useEffect(() => {
    if (!isOpen) return;
    function handleKeyDown(event: KeyboardEvent) {
      if (event.key === "Escape") {
        event.preventDefault();
        event.stopPropagation();
        setIsOpen(false);
      }
    }
    document.addEventListener("keydown", handleKeyDown, true);
    return () => document.removeEventListener("keydown", handleKeyDown, true);
  }, [isOpen]);

  const handleAdjust = useCallback(async (tone: ToneType) => {
    if (inFlightRef.current) return;
    if (!body.trim()) {
      toast("error", "Please add text before adjusting the tone.");
      return;
    }

    setIsOpen(false);
    setLoading(true);
    inFlightRef.current = true;
    try {
      const parts = splitBody?.(body, bodyHtml ?? "");
      const result = await api.adjustTone({
        text: parts?.userText ?? body,
        tone,
      });
      if (currentBodyRef.current !== body || currentHtmlRef.current !== bodyHtml) {
        toast("error", "The message changed during rewrite. Your edits were kept.");
        return;
      }
      setPreviousBody(body);
      setPreviousHtml(bodyHtml ?? plainTextToHtml(body));
      setBody(result.adjusted_text + (parts?.signatureText ?? "") + (parts?.quoteText ?? ""));
      if (setBodyHtml) {
        setBodyHtml(plainTextToHtml(result.adjusted_text) + (parts?.signatureHtml ?? "") + (parts?.quoteHtml ?? ""));
      }
      toast("success", `Tone adjusted to ${tone}`);
    } catch (err) {
      toast("error", err instanceof Error ? err.message : "Failed to adjust tone");
    } finally {
      inFlightRef.current = false;
      setLoading(false);
    }
  }, [body, bodyHtml, splitBody, toast, setBody, setBodyHtml, plainTextToHtml]);

  useEffect(() => {
    const handleAdjustToneEvent = (e: Event) => {
      const customEvent = e as CustomEvent<{ tone?: ToneType }>;
      const targetTone = customEvent.detail?.tone ?? "professional";
      void handleAdjust(targetTone);
    };

    window.addEventListener("maho-adjust-tone", handleAdjustToneEvent);
    return () => {
      window.removeEventListener("maho-adjust-tone", handleAdjustToneEvent);
    };
  }, [handleAdjust]);

  const handleUndo = useCallback(() => {
    if (previousBody !== null) {
      const current = body;
      setBody(previousBody);
      if (setBodyHtml) {
        setBodyHtml(previousHtml ?? plainTextToHtml(previousBody));
      }
      setPreviousBody(current); // Toggle redo
      setPreviousHtml(bodyHtml ?? plainTextToHtml(current));
      toast("success", "Tone adjustment undone");
    }
  }, [body, bodyHtml, previousHtml, previousBody, toast, setBody, setBodyHtml, plainTextToHtml]);

  return (
    <div className="relative flex items-center gap-1.5" ref={dropdownRef} data-testid="tone-adjust-container">
      <button
        type="button"
        onClick={() => setIsOpen(!isOpen)}
        disabled={loading}
        className="mail-pressable flex h-10 items-center gap-1.5 rounded-lg px-3 text-sm text-muted-foreground hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring disabled:opacity-50"
        aria-label="Rewrite message"
      >
        {loading ? (
          <Loader2 size={14} className="animate-spin text-primary" />
        ) : (
          <Sparkles size={14} className="text-primary" />
        )}
        <span>Rewrite</span>
        <ChevronDown size={12} className="opacity-60" />
      </button>

      {isOpen && (
        <div className="absolute top-full left-0 z-50 mt-1 w-40 rounded-xl border border-border bg-background p-1.5 shadow-lg animate-in fade-in slide-in-from-top-1 duration-150">
          {(["professional", "friendly", "concise", "formal"] as ToneType[]).map((t) => (
            <button
              key={t}
              type="button"
              onClick={() => void handleAdjust(t)}
              className="flex h-8 w-full items-center rounded-md px-2.5 text-left text-sm text-foreground hover:bg-surface-hover capitalize transition-colors"
            >
              {t}
            </button>
          ))}
        </div>
      )}

      {previousBody !== null && (
        <button
          type="button"
          onClick={handleUndo}
          className="mail-pressable flex h-10 w-10 items-center justify-center rounded-lg text-muted-foreground hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
          title="Undo rewrite"
          aria-label="Undo Tone"
        >
          <Undo2 size={14} />
        </button>
      )}
    </div>
  );
}
