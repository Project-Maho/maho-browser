import { useCallback, useEffect, useRef } from "react";
import { listen } from "../events.js";
import { useTranslation } from "react-i18next";
import { useToast } from "../components/ui/Toast";

const MAX_RETRIES_PER_ACCOUNT = 20;

interface AuthRefreshSucceededPayload {
  account_id: string;
}

interface RetryEntry {
  id: number;
  accountId: string;
  action: () => Promise<void>;
  label: string;
}

interface ReauthRetryQueue {
  enqueueRetry: (accountId: string, action: () => Promise<void>, label: string) => void;
}

export function useReauthRetryQueue(): ReauthRetryQueue {
  const { t } = useTranslation();
  const { toast } = useToast();
  const queueRef = useRef<RetryEntry[]>([]);
  const retryIdRef = useRef(0);

  const enqueueRetry = useCallback((accountId: string, action: () => Promise<void>, label: string) => {
    const entry: RetryEntry = {
      id: retryIdRef.current,
      accountId,
      action,
      label,
    };
    retryIdRef.current += 1;

    const otherAccounts = queueRef.current.filter((retry) => retry.accountId !== accountId);
    const accountRetries = queueRef.current
      .filter((retry) => retry.accountId === accountId)
      .concat(entry)
      .slice(-MAX_RETRIES_PER_ACCOUNT);

    queueRef.current = [...otherAccounts, ...accountRetries];
  }, []);

  useEffect(() => {
    const unlisten = listen<AuthRefreshSucceededPayload>("auth-refresh-succeeded", (event) => {
      const accountId = event.payload.account_id;
      const retries = queueRef.current.filter((retry) => retry.accountId === accountId);

      if (retries.length === 0) {
        return;
      }

      queueRef.current = queueRef.current.filter((retry) => retry.accountId !== accountId);

      void Promise.all(
        retries.map(async (retry) => {
          try {
            await retry.action();
            toast("success", t("auth.retrySucceeded", { label: retry.label }));
          } catch {
            toast("error", t("auth.retryStillFailing", { label: retry.label }));
          }
        }),
      );
    });

    return () => {
      void unlisten.then((fn) => fn());
    };
  }, [t, toast]);

  return { enqueueRetry };
}
