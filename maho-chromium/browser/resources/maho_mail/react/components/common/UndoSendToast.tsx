import { useCallback, useEffect, useRef, useState } from "react";

interface UndoSendToastProps {
  delaySeconds: number;
  onUndo: () => void;
  onComplete: () => void;
}

export function UndoSendToast({ delaySeconds, onUndo, onComplete }: UndoSendToastProps) {
  const [remaining, setRemaining] = useState(delaySeconds);
  const [cancelled, setCancelled] = useState(false);
  const cancelledRef = useRef(false);
  const onCompleteRef = useRef(onComplete);
  onCompleteRef.current = onComplete;

  useEffect(() => {
    const interval = setInterval(() => {
      setRemaining((prev) => {
        if (prev <= 1) {
          clearInterval(interval);
          if (!cancelledRef.current) onCompleteRef.current();
          return 0;
        }
        return prev - 1;
      });
    }, 1000);

    return () => clearInterval(interval);
  }, [delaySeconds]);

  const handleUndo = useCallback(() => {
    cancelledRef.current = true;
    setCancelled(true);
    onUndo();
  }, [onUndo]);

  if (cancelled || remaining <= 0) return null;

  const progress = ((delaySeconds - remaining) / delaySeconds) * 100;

  return (
    <div className="fixed bottom-6 left-1/2 z-50 -translate-x-1/2">
      <div className="flex items-center gap-3 rounded-2xl border border-border bg-card px-4 py-3 shadow-xl">
        <div className="relative h-8 w-8">
          <svg className="h-8 w-8 -rotate-90" viewBox="0 0 32 32">
            <circle
              cx="16"
              cy="16"
              r="13"
              fill="none"
              stroke="currentColor"
              strokeWidth="2"
              className="text-muted"
            />
            <circle
              cx="16"
              cy="16"
              r="13"
              fill="none"
              stroke="currentColor"
              strokeWidth="2"
              strokeDasharray={`${2 * Math.PI * 13}`}
              strokeDashoffset={`${2 * Math.PI * 13 * (1 - progress / 100)}`}
              className="text-primary transition-all duration-1000 ease-linear"
            />
          </svg>
          <span className="absolute inset-0 flex items-center justify-center text-[10px] font-semibold text-muted-foreground">
            {remaining}
          </span>
        </div>
        <span className="text-sm text-foreground">Sending email…</span>
        <button
          type="button"
          onClick={handleUndo}
          className="rounded-lg bg-primary px-3 py-1 text-xs font-semibold text-primary-foreground hover:bg-primary/90"
        >
          Undo
        </button>
      </div>
    </div>
  );
}
