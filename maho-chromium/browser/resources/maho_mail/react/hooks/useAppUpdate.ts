import { useCallback, useEffect, useRef, useState } from "react";
import { checkForAppUpdate, relaunchApp } from "../api";

const INITIAL_CHECK_DELAY_MS = 2_000;
const CHECK_INTERVAL_MS = 30 * 60 * 1_000;

interface AppUpdateHookState {
  updateAvailable: boolean;
  version: string | null;
  releaseNotes: string | null;
  downloading: boolean;
  downloadProgress: number;
  error: string | null;
  checkForUpdate: () => Promise<void>;
  installUpdate: () => Promise<void>;
  dismissUpdate: () => void;
}

interface UpdateHandle {
  downloadAndInstall: (
    onProgress: (event: {
      event: "Started" | "Progress" | "Finished";
      data: { contentLength?: number; chunkLength: number };
    }) => void,
  ) => Promise<void>;
  version: string;
  body: string | null;
}

export function useAppUpdate(): AppUpdateHookState {
  const [updateAvailable, setUpdateAvailable] = useState(false);
  const [version, setVersion] = useState<string | null>(null);
  const [releaseNotes, setReleaseNotes] = useState<string | null>(null);
  const [downloading, setDownloading] = useState(false);
  const [downloadProgress, setDownloadProgress] = useState(0);
  const [error, setError] = useState<string | null>(null);

  const updateRef = useRef<UpdateHandle | null>(null);

  const checkForUpdate = useCallback(async () => {
    try {
      setError(null);
      const update = await checkForAppUpdate();
      if (update) {
        updateRef.current = update as UpdateHandle;
        setUpdateAvailable(true);
        setVersion(update.version);
        setReleaseNotes(update.body ?? null);
      }
    } catch (err) {
      setError(err instanceof Error ? err.message : "Failed to check for updates");
    }
  }, []);

  const installUpdate = useCallback(async () => {
    if (!updateRef.current) return;

    setDownloading(true);
    setDownloadProgress(0);
    setError(null);
    let totalLength = 0;
    let downloadedLength = 0;

    try {
      await updateRef.current.downloadAndInstall((event) => {
        if (event.event === "Started") {
          totalLength = event.data.contentLength ?? 0;
        } else if (event.event === "Progress") {
          downloadedLength += event.data.chunkLength;
          if (totalLength > 0) {
            setDownloadProgress(Math.round((downloadedLength / totalLength) * 100));
          }
        } else if (event.event === "Finished") {
          setDownloadProgress(100);
        }
      });

      await relaunchApp();
    } catch (err) {
      setDownloading(false);
      setDownloadProgress(0);
      setError(err instanceof Error ? err.message : "Failed to install update");
    }
  }, []);

  const dismissUpdate = useCallback(() => {
    setUpdateAvailable(false);
    setDownloading(false);
    setDownloadProgress(0);
    setError(null);
  }, []);

  useEffect(() => {
    const initialTimeout = setTimeout(() => {
      void checkForUpdate();
    }, INITIAL_CHECK_DELAY_MS);

    const interval = setInterval(() => {
      void checkForUpdate();
    }, CHECK_INTERVAL_MS);

    return () => {
      clearTimeout(initialTimeout);
      clearInterval(interval);
    };
  }, [checkForUpdate]);

  return {
    updateAvailable,
    version,
    releaseNotes,
    downloading,
    downloadProgress,
    error,
    checkForUpdate,
    installUpdate,
    dismissUpdate,
  };
}
