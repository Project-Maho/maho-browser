import { useState, useRef, useEffect, useCallback } from "react";
import { Sparkles, Undo2, Loader2, ChevronDown } from "lucide-react";
import * as api from "../../api";
import { useToast } from "../ui/Toast";

interface ToneAdjustButtonProps {
  body: string;
  setBody: (text: string) => void;
  setBodyHtml?: (html: string) => void;
  plainTextToHtml: (text: string) => string;
}

type ToneType = "professional" | "friendly" | "concise" | "formal";

export function ToneAdjustButton({
  body,
  setBody,
  setBodyHtml,
  plainTextToHtml,
}: ToneAdjustButtonProps) {
  const { toast } = useToast();
  const [loading, setLoading] = useState(false);
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

  const handleAdjust = useCallback(async (tone: ToneType) => {
    if (!body.trim()) {
      toast("error", "Please add text before adjusting the tone.");
      return;
    }

    setIsOpen(false);
    setLoading(true);
    try {
      const result = await api.adjustTone({
        text: body,
        tone,
      });
      setPreviousBody(body);
      setBody(result.adjusted_text);
      if (setBodyHtml) {
        setBodyHtml(plainTextToHtml(result.adjusted_text));
      }
      toast("success", `Tone adjusted to ${tone}`);
    } catch (err) {
      toast("error", err instanceof Error ? err.message : "Failed to adjust tone");
    } finally {
      setLoading(false);
    }
  }, [body, toast, setBody, setBodyHtml, plainTextToHtml]);

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
        setBodyHtml(plainTextToHtml(previousBody));
      }
      setPreviousBody(current); // Toggle redo
      toast("success", "Tone adjustment undone");
    }
  }, [body, previousBody, toast, setBody, setBodyHtml, plainTextToHtml]);

  return (
    <div className="relative flex items-center gap-1.5" ref={dropdownRef} data-testid="tone-adjust-container">
      <button
        type="button"
        onClick={() => setIsOpen(!isOpen)}
        disabled={loading}
        className="flex items-center gap-1.5 rounded-lg border border-border bg-card/50 px-3 py-2 text-sm text-muted-foreground transition-colors hover:bg-muted hover:text-foreground disabled:opacity-50"
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
              className="w-full rounded-lg px-2.5 py-1.5 text-left text-sm text-foreground hover:bg-muted capitalize transition-colors"
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
          className="flex h-9 w-9 items-center justify-center rounded-lg border border-border bg-card/50 text-muted-foreground transition-colors hover:bg-muted hover:text-foreground"
          title="Undo rewrite"
          aria-label="Undo Tone"
        >
          <Undo2 size={14} />
        </button>
      )}
    </div>
  );
}
