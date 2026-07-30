import { useMemo, useState } from "react";
import { Loader2 } from "lucide-react";
import { useTranslation } from "react-i18next";
import * as api from "../../api";
import type { AccountSummary, AuthError } from "../../types";
import { getErrorMessage } from "../../lib/utils";
import { useToast } from "../ui/Toast";

interface AuthExpiredBannerProps {
  authErrors: AuthError[];
  accounts: AccountSummary[];
}

export function AuthExpiredBanner({ authErrors, accounts }: AuthExpiredBannerProps) {
  const { t } = useTranslation();
  const { toast } = useToast();
  const [reconnectingIds, setReconnectingIds] = useState<Set<string>>(new Set());

  const accountEmailById = useMemo(
    () => new Map(accounts.map((account) => [account.id, account.email])),
    [accounts],
  );

  if (authErrors.length === 0) {
    return null;
  }

  function reasonLabel(reason: AuthError["reason"]) {
    return t(`auth.reasons.${reason}`);
  }

  async function handleReconnect(accountId: string) {
    setReconnectingIds((prev) => {
      const next = new Set(prev);
      next.add(accountId);
      return next;
    });

    try {
      await api.reconnectAccount(accountId);
    } catch (error) {
      toast("error", getErrorMessage(error, t("auth.reconnectFailed")));
    } finally {
      setReconnectingIds((prev) => {
        const next = new Set(prev);
        next.delete(accountId);
        return next;
      });
    }
  }

  return (
    <div
      role="alert"
      aria-live="assertive"
      className="w-full border-b border-amber-500/30 bg-amber-500/10 text-amber-700 dark:text-amber-300"
    >
      <div className="divide-y divide-amber-500/20">
        {authErrors.map((authError) => {
          const accountEmail = accountEmailById.get(authError.account_id) ?? authError.provider ?? authError.account_id;
          const loading = reconnectingIds.has(authError.account_id);

          return (
            <div key={authError.account_id} className="flex min-h-11 items-center gap-3 px-4 py-2 text-sm">
              <span className="min-w-0 flex-1 truncate">
                {t("auth.reconnectRequired", {
                  accountEmail,
                  reason: reasonLabel(authError.reason),
                })}
              </span>
              <button
                type="button"
                onClick={() => void handleReconnect(authError.account_id)}
                disabled={loading}
                className="inline-flex shrink-0 items-center justify-center gap-2 rounded-lg bg-primary px-3 py-1.5 text-xs font-medium text-primary-foreground shadow-sm transition-colors hover:bg-primary/90 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring disabled:opacity-60"
              >
                {loading && <Loader2 size={14} className="animate-spin" />}
                {t("auth.reconnect")}
              </button>
            </div>
          );
        })}
      </div>
    </div>
  );
}
