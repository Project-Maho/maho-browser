// Copyright 2026 Maho Browser. All rights reserved.

import { useEffect, useRef } from "react";
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
    if (typeof Notification === "undefined") return;
    permissionRequested.current = true;

    void (async () => {
      try {
        if (Notification.permission === "default") {
          await Notification.requestPermission();
        }
      } catch {
        // Permission API unavailable — silently degrade
      }
    })();
  }, [enabled]);

  async function notify(title: string, body: string) {
    if (!enabled || typeof Notification === "undefined") return;

    try {
      let permitted = Notification.permission === "granted";
      if (!permitted && Notification.permission !== "denied") {
        permitted = (await Notification.requestPermission()) === "granted";
      }
      if (permitted) {
        new Notification(title, { body: body || "" });
      }
    } catch {
      // Native notification unavailable — silently degrade
    }
  }

  return { notify };
}
