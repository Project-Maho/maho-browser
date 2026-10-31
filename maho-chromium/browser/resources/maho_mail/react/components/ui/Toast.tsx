import { toast as sonnerToast, Toaster as SonnerToaster, type ToasterProps } from "sonner";
import { isMobile } from "../../utils/platform";
import { useState } from "react";
import { ChevronDown, ChevronUp } from "lucide-react";

export type ToastType = "success" | "error" | "warning" | "info";

export interface StructuredMessage {
  summary: string;
  detail: string;
}

const TOAST_FN: Record<ToastType, typeof sonnerToast.success> = {
  success: sonnerToast.success,
  error: sonnerToast.error,
  warning: sonnerToast.warning,
  info: sonnerToast.info,
};

function StructuredToast({ summary, detail }: StructuredMessage) {
  const [expanded, setExpanded] = useState(false);

  return (
    <div className="flex flex-col gap-1 text-sm w-full" data-testid="structured-toast">
      <div className="flex items-center justify-between font-semibold gap-2">
        <span>{summary}</span>
        <button
          type="button"
          onClick={(e) => {
            e.stopPropagation();
            setExpanded(!expanded);
          }}
          className="mail-pressable flex h-6 w-6 shrink-0 items-center justify-center rounded-md text-muted-foreground hover:bg-surface-hover hover:text-foreground"
          aria-label={expanded ? "Collapse details" : "Expand details"}
        >
          {expanded ? <ChevronUp className="h-4 w-4" /> : <ChevronDown className="h-4 w-4" />}
        </button>
      </div>
      {expanded && (
        <pre className="mt-1 max-h-40 overflow-y-auto rounded bg-muted/50 p-2 font-mono text-xs text-muted-foreground whitespace-pre-wrap break-all">
          {detail}
        </pre>
      )}
    </div>
  );
}

function toastFn(
  type: ToastType,
  message: string | StructuredMessage,
  title?: string,
) {
  const fn = TOAST_FN[type];
  if (typeof message === "object" && message !== null && "summary" in message) {
    fn(<StructuredToast summary={message.summary} detail={message.detail} />);
  } else {
    if (title) {
      fn(title, { description: message as string });
    } else {
      fn(message as string);
    }
  }
}

export function useToast() {
  return { toast: toastFn };
}

export function Toaster(props: ToasterProps) {
  return <ToasterBase {...props} />;
}

function ToasterBase(props: ToasterProps) {
  return (
    <SonnerToaster
      {...props}
      position={isMobile() ? "top-center" : "bottom-right"}
      offset={isMobile() ? "calc(env(safe-area-inset-top) + 12px)" : 16}
    />
  );
}
