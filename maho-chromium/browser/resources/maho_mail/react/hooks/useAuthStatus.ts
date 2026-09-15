import { useCallback, useEffect, useState } from "react";
import { listen } from "../events.js";
import type { AuthError } from "../types";

interface ReauthRequiredPayload {
  account_id: string;
  provider: string;
  reason: AuthError["reason"];
}

interface AuthRefreshSucceededPayload {
  account_id: string;
}

interface AuthStatus {
  authErrors: AuthError[];
  clearAuthError: (accountId: string) => void;
}

export function useAuthStatus(): AuthStatus {
  const [authErrors, setAuthErrors] = useState<AuthError[]>([]);

  const clearAuthError = useCallback((accountId: string) => {
    setAuthErrors((prev) => prev.filter((error) => error.account_id !== accountId));
  }, []);

  useEffect(() => {
    const reauthRequiredUnlisten = listen<ReauthRequiredPayload>("auth-reauth-required", (event) => {
      setAuthErrors((prev) => {
        const nextError: AuthError = {
          account_id: event.payload.account_id,
          provider: event.payload.provider,
          reason: event.payload.reason,
          timestamp: Date.now(),
        };

        return [
          ...prev.filter((error) => error.account_id !== event.payload.account_id),
          nextError,
        ];
      });
    });

    const refreshSucceededUnlisten = listen<AuthRefreshSucceededPayload>("auth-refresh-succeeded", (event) => {
      setAuthErrors((prev) => prev.filter((error) => error.account_id !== event.payload.account_id));
    });

    return () => {
      void reauthRequiredUnlisten.then((fn) => fn());
      void refreshSucceededUnlisten.then((fn) => fn());
    };
  }, []);

  return { authErrors, clearAuthError };
}
