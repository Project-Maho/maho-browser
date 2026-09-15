import { useCallback, useEffect, useState } from "react";
import { listen } from "../events.js";
import { flushOutbox, listAllPendingMutations } from "../api";

export type ConnectionStatus = "online" | "offline" | "partial";

interface NetworkStatus {
  isOnline: boolean;
  pendingMutationCount: number;
  staleMutationCount: number;
  connectionStatus: ConnectionStatus;
}

export function useNetworkStatus(): NetworkStatus {
  const [isOnline, setIsOnline] = useState(navigator.onLine);
  const [pendingMutationCount, setPendingMutationCount] = useState(0);
  const [staleMutationCount, setStaleMutationCount] = useState(0);
  const [accountStatuses, setAccountStatuses] = useState<Record<string, boolean>>({});

  const accountStatusList = Object.values(accountStatuses);
  const backendConnected =
    accountStatusList.length === 0 ? true : accountStatusList.some(Boolean);

  const connectionStatus: ConnectionStatus = !isOnline
    ? "offline"
    : !backendConnected
      ? "partial"
      : "online";

  const refreshMutationCount = useCallback(async () => {
    try {
      const mutations = await listAllPendingMutations();
      setPendingMutationCount(mutations.length);
      const oneDayAgo = Date.now() - 24 * 60 * 60 * 1000;
      const stale = mutations.filter((m) => new Date(m.created_at).getTime() < oneDayAgo).length;
      setStaleMutationCount(stale);
    } catch {
    }
  }, []);

  useEffect(() => {
    refreshMutationCount();
  }, [refreshMutationCount]);

  useEffect(() => {
    const handleOnline = async () => {
      setIsOnline(true);
      try {
        await flushOutbox();
        await refreshMutationCount();
      } catch {
      }
    };

    const handleOffline = () => {
      setIsOnline(false);
    };

    window.addEventListener("online", handleOnline);
    window.addEventListener("offline", handleOffline);

    return () => {
      window.removeEventListener("online", handleOnline);
      window.removeEventListener("offline", handleOffline);
    };
  }, [refreshMutationCount]);

  useEffect(() => {
    const unlisten = Promise.all([
      listen<{ account_id: string; connected: boolean }>("connection-status-changed", (event) => {
        setAccountStatuses((prev) => {
          if (prev[event.payload.account_id] === event.payload.connected) {
            return prev;
          }
          return { ...prev, [event.payload.account_id]: event.payload.connected };
        });
        if (event.payload.connected) {
          refreshMutationCount();
        }
      }),
      listen("mutations-flushed", () => {
        refreshMutationCount();
      }),
      listen("mutation-queued", () => {
        refreshMutationCount();
      }),
    ]);

    return () => {
      unlisten.then((fns) => fns.forEach((fn) => fn()));
    };
  }, [refreshMutationCount]);

  return { isOnline, pendingMutationCount, staleMutationCount, connectionStatus };
}
