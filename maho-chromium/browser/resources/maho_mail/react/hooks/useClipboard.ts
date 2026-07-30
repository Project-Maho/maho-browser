import { useCallback } from "react";
import { useTranslation } from "react-i18next";

import { useToast } from "../components/ui/Toast";

interface UseClipboardResult {
  copy: (text: string) => Promise<void>;
}

export function useClipboard(): UseClipboardResult {
  const { toast } = useToast();
  const { t } = useTranslation();

  const copy = useCallback(
    async (text: string) => {
      await navigator.clipboard.writeText(text);
      toast("success", t("common.copied"));
    },
    [toast, t],
  );

  return { copy };
}
