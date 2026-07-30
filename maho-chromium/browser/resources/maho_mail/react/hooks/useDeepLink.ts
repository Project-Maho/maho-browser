import { useEffect } from "react";
import { onOpenUrl } from "@tauri-apps/plugin-deep-link";

export interface DeepLinkHandlers {
  onNavigateToEmail?: (messageId: string) => void;
  onComposeToRecipient?: (email: string) => void;
}

function parseDeepLink(url: string): { action: string; value: string } | null {
  try {
    const parsed = new URL(url);

    if (parsed.protocol === "mahomail:") {
      const path = parsed.hostname + parsed.pathname;

      const emailMatch = path.match(/^email\/(.+)/);
      if (emailMatch) {
        return { action: "navigate_email", value: decodeURIComponent(emailMatch[1]) };
      }

      if (path.startsWith("compose")) {
        const to = parsed.searchParams.get("to");
        if (to) {
          return { action: "compose", value: to };
        }
      }
    }
  } catch {
    return null;
  }

  return null;
}

export function useDeepLink(handlers: DeepLinkHandlers): void {
  useEffect(() => {
    let cancelled = false;

    const unlistenPromise = onOpenUrl((urls) => {
      if (cancelled) return;

      for (const url of urls) {
        const result = parseDeepLink(url);
        if (!result) continue;

        if (result.action === "navigate_email" && handlers.onNavigateToEmail) {
          handlers.onNavigateToEmail(result.value);
        } else if (result.action === "compose" && handlers.onComposeToRecipient) {
          handlers.onComposeToRecipient(result.value);
        }
      }
    });

    return () => {
      cancelled = true;
      void unlistenPromise.then((unlisten) => unlisten());
    };
  }, [handlers.onNavigateToEmail, handlers.onComposeToRecipient]);
}
