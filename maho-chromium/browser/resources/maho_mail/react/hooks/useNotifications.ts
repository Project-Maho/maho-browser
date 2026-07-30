import { useEffect, useRef } from "react";
import {
  isPermissionGranted,
  requestPermission,
  sendNotification,
} from "@tauri-apps/plugin-notification";
import { isMobile } from "../utils/platform";

interface UseNotificationsOptions {
  enabled: boolean;
  onNewEmail?: (title: string, body: string) => void;
}

export function useNotifications({ enabled }: UseNotificationsOptions) {
  const permissionRequested = useRef(false);

  // On mobile, eagerly request notification permission so the OS prompt
  // appears early rather than on the first incoming email.
  useEffect(() => {
    if (!enabled || !isMobile() || permissionRequested.current) return;
    permissionRequested.current = true;

    void (async () => {
      try {
        const granted = await isPermissionGranted();
        if (!granted) {
          await requestPermission();
        }
      } catch {
        // Permission API unavailable — silently degrade
      }
    })();
  }, [enabled]);

  async function notify(title: string, body: string) {
    if (!enabled) return;

    try {
      let permitted = await isPermissionGranted();
      if (!permitted) {
        const permission = await requestPermission();
        permitted = permission === "granted";
      }
      if (permitted) {
        sendNotification({ title, body: body || "" });
      }
    } catch {
      // Native notification unavailable — silently degrade
    }
  }

  return { notify };
}
