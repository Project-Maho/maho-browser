import { useCallback, useEffect, useState } from "react";
import { listen } from "@tauri-apps/api/event";
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
  const [backendConnected, setBackendConnected] = useState(true);

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
        setBackendConnected(event.payload.connected);
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
