import { useCallback } from "react";

const NOTIFICATION_SOUND_URI =
  "data:audio/wav;base64,UklGRnoGAABXQVZFZm10IBAAAAABAAEAQB8AAEAfAAABAAgAZGF0YUoGAACAgICAgICAgICAgICAf3hxam" +
  "RfW1lYWVteZGxxeICHjpOXmpydn56dnJiUj4mDfnl2dHR1d3p+goaKjpGTlJSUk5GQjoqGgoB/fn5+" +
  "f4GDhYeJiouLi4uKiomIh4WEg4KBgICAgIGBgoOEhYWGhoaGhYSEg4KCgYCAgICAgICBgYGCgoKCgo" +
  "KCgoKBgYGAgICAgICAgICAgICAgICAgIGBgYGBgYGBgYCBgICAgICAgICAgICAgICAgICAgICAgICAgI" +
  "CAgICAgICAgICAgICAgICAgICAgICAgICAgICBgYGBgYKCg4OEhIWFhYWFhISDgoGBgICAgIB/f39/f3" +
  "9/f4CAgICBgYKCg4OEhIWFhYWEhIOCgoGAgICAf39/f39/f39/gICAgIGBgoKDg4SEhYWFhYSEg4KCgY" +
  "CAgIB/f39/f39/f3+AgICAgYGCgoODhISFhYWFhISDgoKBgICAgH9/f39/f35+fn+AgICAgYGCgoODhI" +
  "SEhYWEhIOCgoGAgICAf39/f39/f39/gICAgIGBgoKDg4SEhISEhISEg4OCgYGAgICAgH9/f39/f39/gI" +
  "CAgIGBgoKDg4ODhISEhISDg4KCgYCAgICAf39/f39/f3+AgICAgYGBgoKDg4ODhISEhIODgoKBgYCAf3" +
  "+AgH9/f39/f4CAgICBgYGCgoODg4OEhISDg4OCgoGBgIB/f4CAf39/f39/gICAgIGBgYKCg4ODg4SEhI" +
  "SDg4KCgYGAgH9/gIB/f39/f3+AgICAgYGBgoKDg4ODhISDg4ODgoKBgYCAf3+AgH9/f39/f4CAgICBgY" +
  "GCgoODg4OEhIODg4KCgYGAgH9/f4B/f39/f3+AgICAgYGBgoKDg4ODg4SDg4OCgoGBgIB/f3+Af39/f3" +
  "9/gICAgIGBgYKCg4ODg4ODg4ODgoKBgYCAf39/gH9/f39/f4CAgICBgYGCgoKDg4ODg4ODg4KCgYGAgH" +
  "9/f4B/f39/f3+AgICAgYGBgoKDg4ODg4ODgoOCgoGBgIB/f3+Af39/f39/gICAgA==";

let notificationAudio: HTMLAudioElement | null = null;

function getAudio(): HTMLAudioElement {
  if (!notificationAudio) {
    notificationAudio = new Audio(NOTIFICATION_SOUND_URI);
    notificationAudio.volume = 0.5;
    notificationAudio.preload = "auto";
  }
  return notificationAudio;
}

export function useNotificationSound(enabled: boolean) {
  const playSound = useCallback(() => {
    if (!enabled) return;

    const audio = getAudio();
    audio.currentTime = 0;
    audio.play().catch(() => {});
  }, [enabled]);

  return { playSound };
}
