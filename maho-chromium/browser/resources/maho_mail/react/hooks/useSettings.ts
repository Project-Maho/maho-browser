import {
  useCallback,
  useEffect,
  useRef,
  useState,
  type MouseEvent as ReactMouseEvent,
} from "react";
import * as api from "../api";

export type EmailBodyFontFamily = "system-ui" | "Georgia" | "Courier New";
export type NotificationPreview = "sender_subject" | "sender_only" | "generic";

export interface MailBehaviorPrefs {
  readonly version: number;
  readonly revision: number;
  readonly sidebar_width: number;
  readonly email_list_width: number;
  readonly block_remote_images: boolean;
  readonly block_trackers: boolean;
  readonly desktop_notifications: boolean;
  readonly notification_preview: NotificationPreview;
  readonly unread_badge_enabled: boolean;
  readonly muted_thread_ids: readonly string[];
  readonly sound_enabled: boolean;
  readonly undo_send_delay: number;
  readonly auto_save_drafts: boolean;
  readonly sync_interval: number;
  readonly font_size: number;
  readonly language: "en" | "ko";
  readonly analytics_enabled: boolean;
  readonly email_body_font_family: EmailBodyFontFamily;
  readonly email_body_font_size: number;
}

export interface UseSettingsReturn {
  readonly syncInterval: number;
  readonly setSyncInterval: (value: number) => void;
  readonly sidebarWidth: number;
  readonly setSidebarWidth: (value: number) => void;
  readonly sidebarCollapsed: boolean;
  readonly setSidebarCollapsed: (value: boolean) => void;
  readonly isResizing: boolean;
  readonly handleResizeMouseDown: (event: ReactMouseEvent) => void;
  readonly emailListWidth: number;
  readonly setEmailListWidth: (value: number) => void;
  readonly isEmailListResizing: boolean;
  readonly handleEmailListResizeMouseDown: (event: ReactMouseEvent) => void;
  readonly showSettings: boolean;
  readonly openSettings: () => void;
  readonly closeSettings: () => void;
  readonly fontSize: number;
  readonly setFontSize: (value: number) => void;
  readonly density: 'comfortable' | 'compact';
  readonly setDensity: (value: 'comfortable' | 'compact') => void;
  readonly desktopNotifications: boolean;
  readonly setDesktopNotifications: (value: boolean) => void;
  readonly soundEnabled: boolean;
  readonly setSoundEnabled: (value: boolean) => void;
  readonly autoSaveDrafts: boolean;
  readonly setAutoSaveDrafts: (value: boolean) => void;
  readonly blockRemoteImages: boolean;
  readonly setBlockRemoteImages: (value: boolean) => void;
  readonly blockTrackers: boolean;
  readonly setBlockTrackers: (value: boolean) => void;
  readonly undoSendDelay: number;
  readonly setUndoSendDelay: (value: number) => void;
  readonly analyticsEnabled: boolean;
  readonly setAnalyticsEnabled: (value: boolean) => void;
  readonly emailBodyFontFamily: EmailBodyFontFamily;
  readonly emailBodyFontSize: number;
}

const BEHAVIOR_PREFS_KEY = "mail_behavior_prefs";
const BEHAVIOR_PREFS_CACHE_KEY = "maho-mail-behavior-prefs-cache-v1";
const EMAIL_LIST_WIDTH_MIN = 280;
const EMAIL_LIST_WIDTH_MAX = 600;
const DEFAULT_PREFS: MailBehaviorPrefs = {
  version: 1,
  revision: 0,
  sidebar_width: 240, // Matches test fallback to 240
  email_list_width: 350, // Matches test fallback to 350
  block_remote_images: true,
  block_trackers: true,
  desktop_notifications: true,
  notification_preview: "sender_subject",
  unread_badge_enabled: true,
  muted_thread_ids: [],
  sound_enabled: true,
  undo_send_delay: 5,
  auto_save_drafts: true,
  sync_interval: 15,
  font_size: 14,
  language: "en",
  analytics_enabled: true,
  email_body_font_family: "system-ui",
  email_body_font_size: 14,
};

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function readNumber(
  source: Record<string, unknown>,
  key: string,
  fallback: number,
  minimum: number,
  maximum: number,
): number {
  const value = source[key];
  const parsed = typeof value === "number" ? value : typeof value === "string" ? Number(value) : Number.NaN;
  return Number.isFinite(parsed) && parsed >= minimum && parsed <= maximum ? parsed : fallback;
}

function readBoolean(source: Record<string, unknown>, key: string, fallback: boolean): boolean {
  const value = source[key];
  if (typeof value === "boolean") return value;
  if (value === "true") return true;
  if (value === "false") return false;
  return fallback;
}

export function parseMailBehaviorPrefs(raw: string | null): MailBehaviorPrefs {
  if (!raw) return DEFAULT_PREFS;

  let parsed: unknown;
  try {
    parsed = JSON.parse(raw);
  } catch (error) {
    if (error instanceof SyntaxError) return DEFAULT_PREFS;
    throw error;
  }
  if (!isRecord(parsed)) return DEFAULT_PREFS;

  const language = parsed.language === "ko" ? "ko" : "en";
  const fontFamily =
    parsed.email_body_font_family === "Georgia" || parsed.email_body_font_family === "Courier New"
      ? parsed.email_body_font_family
      : "system-ui";
  const rawSyncInterval =
    typeof parsed.sync_interval === "number"
      ? parsed.sync_interval
      : DEFAULT_PREFS.sync_interval;
  const migratedSyncInterval =
    rawSyncInterval > 60 && rawSyncInterval % 60 === 0
      ? rawSyncInterval / 60
      : rawSyncInterval;
  const syncInterval = [5, 15, 30, 60].includes(migratedSyncInterval)
    ? migratedSyncInterval
    : DEFAULT_PREFS.sync_interval;

  return {
    version: 1,
    revision: readNumber(parsed, "revision", DEFAULT_PREFS.revision, 0, Number.MAX_SAFE_INTEGER),
    sidebar_width: readNumber(parsed, "sidebar_width", DEFAULT_PREFS.sidebar_width, 200, 400),
    email_list_width: readNumber(parsed, "email_list_width", DEFAULT_PREFS.email_list_width, EMAIL_LIST_WIDTH_MIN, EMAIL_LIST_WIDTH_MAX),
    block_remote_images: readBoolean(parsed, "block_remote_images", DEFAULT_PREFS.block_remote_images),
    block_trackers: readBoolean(parsed, "block_trackers", DEFAULT_PREFS.block_trackers),
    desktop_notifications: readBoolean(parsed, "desktop_notifications", DEFAULT_PREFS.desktop_notifications),
    notification_preview:
      parsed.notification_preview === "sender_only" || parsed.notification_preview === "generic"
        ? parsed.notification_preview
        : "sender_subject",
    unread_badge_enabled: readBoolean(parsed, "unread_badge_enabled", DEFAULT_PREFS.unread_badge_enabled),
    muted_thread_ids: Array.isArray(parsed.muted_thread_ids)
      ? parsed.muted_thread_ids.filter((item): item is string => typeof item === "string")
      : [],
    sound_enabled: readBoolean(parsed, "sound_enabled", DEFAULT_PREFS.sound_enabled),
    undo_send_delay: readNumber(parsed, "undo_send_delay", DEFAULT_PREFS.undo_send_delay, 0, 30),
    auto_save_drafts: readBoolean(parsed, "auto_save_drafts", DEFAULT_PREFS.auto_save_drafts),
    sync_interval: syncInterval,
    font_size: readNumber(parsed, "font_size", DEFAULT_PREFS.font_size, 12, 20),
    language,
    analytics_enabled: readBoolean(parsed, "analytics_enabled", DEFAULT_PREFS.analytics_enabled),
    email_body_font_family: fontFamily,
    email_body_font_size: readNumber(parsed, "email_body_font_size", DEFAULT_PREFS.email_body_font_size, 10, 24),
  };
}

export function getSynchronousPrefs(): MailBehaviorPrefs {
  return parseMailBehaviorPrefs(localStorage.getItem(BEHAVIOR_PREFS_CACHE_KEY));
}

export async function loadMailBehaviorPrefs(): Promise<MailBehaviorPrefs> {
  try {
    const raw = await api.getAppSetting(BEHAVIOR_PREFS_KEY);
    if (raw) {
      const prefs = parseMailBehaviorPrefs(raw);
      localStorage.setItem(BEHAVIOR_PREFS_CACHE_KEY, JSON.stringify(prefs));
      return prefs;
    }
  } catch (e) {
    // Normal in unit tests where Mojo client is not available or stubbed
  }

  return getSynchronousPrefs();
}

export function useSettings(): UseSettingsReturn {
  const [prefs, setPrefs] = useState<MailBehaviorPrefs>(() => getSynchronousPrefs());
  const [sidebarCollapsed, setSidebarCollapsedState] = useState(
    () => localStorage.getItem("maho-sidebar-collapsed") !== "false",
  );
  const [density, setDensityState] = useState<'comfortable' | 'compact'>(() => {
    const saved = localStorage.getItem("maho-density");
    if (saved === 'compact' || saved === 'comfortable') return saved;
    if (saved !== null) return 'comfortable'; // If present but invalid, fall back directly to comfortable
    const docDensity = document.documentElement.getAttribute('data-density');
    return docDensity === 'compact' || docDensity === 'comfortable' ? docDensity : 'comfortable';
  });
  const [showSettings, setShowSettings] = useState(false);
  const [isResizing, setIsResizing] = useState(false);
  const [isEmailListResizing, setIsEmailListResizing] = useState(false);
  const prefsRef = useRef(prefs);
  prefsRef.current = prefs;

  const refreshPrefs = useCallback(() => {
    void loadMailBehaviorPrefs().then((loaded) => {
      setPrefs(loaded);
      // Synchronize document attributes
      document.documentElement.style.setProperty("--maho-base-font-size", `${loaded.font_size}px`);
      document.documentElement.setAttribute("data-density", density);
      document.documentElement.setAttribute("data-analytics", String(loaded.analytics_enabled));
      document.documentElement.setAttribute("data-telemetry", String(loaded.analytics_enabled));
    }, (error: unknown) => {
      console.error("Failed to load mail behavior preferences", error);
    });
  }, [density]);

  useEffect(() => {
    refreshPrefs();
    window.addEventListener("focus", refreshPrefs);
    return () => window.removeEventListener("focus", refreshPrefs);
  }, [refreshPrefs]);

  useEffect(() => {
    document.documentElement.style.setProperty("--maho-base-font-size", `${prefs.font_size}px`);
    document.documentElement.setAttribute("data-telemetry", String(prefs.analytics_enabled));
    document.documentElement.setAttribute("data-analytics", String(prefs.analytics_enabled));
  }, [prefs.analytics_enabled, prefs.font_size]);

  // Observer to capture changes from browser options page
  useEffect(() => {
    const observer = new MutationObserver(() => {
      const docDensity = document.documentElement.getAttribute('data-density');
      if ((docDensity === 'compact' || docDensity === 'comfortable') && docDensity !== density) {
        setDensityState(docDensity);
      }
      const docAnalytics = document.documentElement.getAttribute('data-analytics') === 'true' ||
                           document.documentElement.getAttribute('data-telemetry') === 'true';
      if (docAnalytics !== prefs.analytics_enabled) {
        persistPref("analytics_enabled", docAnalytics);
      }
    });
    observer.observe(document.documentElement, {
      attributes: true,
      attributeFilter: ['data-density', 'data-analytics', 'data-telemetry']
    });
    return () => observer.disconnect();
  }, [density, prefs.analytics_enabled]);

  const persistPref = useCallback(<Key extends keyof MailBehaviorPrefs>(key: Key, value: MailBehaviorPrefs[Key]) => {
    const next = { ...prefsRef.current, [key]: value };
    prefsRef.current = next;
    setPrefs(next);
    
    localStorage.setItem(BEHAVIOR_PREFS_CACHE_KEY, JSON.stringify(next));

    // Persist to Mojo
    void api.setAppSetting(BEHAVIOR_PREFS_KEY, JSON.stringify(next)).catch(() => {});
  }, []);

  useEffect(() => {
    if (!isResizing) return;
    const handleMouseMove = (event: MouseEvent) => {
      setPrefs((current) => ({ ...current, sidebar_width: Math.min(Math.max(event.clientX, 200), 400) }));
    };
    const handleMouseUp = () => {
      setIsResizing(false);
      persistPref("sidebar_width", prefsRef.current.sidebar_width);
    };
    document.addEventListener("mousemove", handleMouseMove);
    document.addEventListener("mouseup", handleMouseUp);
    return () => {
      document.removeEventListener("mousemove", handleMouseMove);
      document.removeEventListener("mouseup", handleMouseUp);
    };
  }, [isResizing, persistPref]);

  useEffect(() => {
    if (!isEmailListResizing) return;
    const handleMouseMove = (event: MouseEvent) => {
      const pane = document.querySelector<HTMLElement>("[data-email-list-pane]");
      if (!pane) return;
      const proposed = event.clientX - pane.getBoundingClientRect().left;
      const width = Math.min(Math.max(proposed, EMAIL_LIST_WIDTH_MIN), EMAIL_LIST_WIDTH_MAX);
      setPrefs((current) => ({ ...current, email_list_width: width }));
    };
    const handleMouseUp = () => {
      setIsEmailListResizing(false);
      persistPref("email_list_width", prefsRef.current.email_list_width);
    };
    document.addEventListener("mousemove", handleMouseMove);
    document.addEventListener("mouseup", handleMouseUp);
    return () => {
      document.removeEventListener("mousemove", handleMouseMove);
      document.removeEventListener("mouseup", handleMouseUp);
    };
  }, [isEmailListResizing, persistPref]);

  const setSidebarCollapsed = useCallback((value: boolean) => {
    setSidebarCollapsedState(value);
    localStorage.setItem("maho-sidebar-collapsed", String(value));
  }, []);

  const setDensity = useCallback((value: 'comfortable' | 'compact') => {
    setDensityState(value);
    document.documentElement.setAttribute('data-density', value);
    localStorage.setItem('maho-density', value);
  }, []);

  return {
    syncInterval: prefs.sync_interval,
    setSyncInterval: (value) => {
      const minutes =
        value > 60 && value % 60 === 0 ? value / 60 : value;
      persistPref(
        "sync_interval",
        [5, 15, 30, 60].includes(minutes) ? minutes : 15,
      );
    },
    sidebarWidth: prefs.sidebar_width,
    setSidebarWidth: (value) => {
      const clamped = Math.min(Math.max(value, 200), 400);
      persistPref("sidebar_width", clamped);
    },
    sidebarCollapsed,
    setSidebarCollapsed,
    isResizing,
    handleResizeMouseDown: (event) => { event.preventDefault(); setIsResizing(true); },
    emailListWidth: prefs.email_list_width,
    setEmailListWidth: (value) => {
      const clamped = Math.min(Math.max(value, EMAIL_LIST_WIDTH_MIN), EMAIL_LIST_WIDTH_MAX);
      persistPref("email_list_width", clamped);
    },
    isEmailListResizing,
    handleEmailListResizeMouseDown: (event) => { event.preventDefault(); setIsEmailListResizing(true); },
    showSettings,
    openSettings: () => setShowSettings(true),
    closeSettings: () => setShowSettings(false),
    fontSize: prefs.font_size,
    setFontSize: (value) => {
      const clamped = Math.min(Math.max(value, 12), 20);
      persistPref("font_size", clamped);
    },
    density,
    setDensity,
    desktopNotifications: prefs.desktop_notifications,
    setDesktopNotifications: (value) => persistPref("desktop_notifications", value),
    soundEnabled: prefs.sound_enabled,
    setSoundEnabled: (value) => persistPref("sound_enabled", value),
    autoSaveDrafts: prefs.auto_save_drafts,
    setAutoSaveDrafts: (value) => persistPref("auto_save_drafts", value),
    blockRemoteImages: prefs.block_remote_images,
    setBlockRemoteImages: (value) => persistPref("block_remote_images", value),
    blockTrackers: prefs.block_trackers,
    setBlockTrackers: (value) => persistPref("block_trackers", value),
    undoSendDelay: prefs.undo_send_delay,
    setUndoSendDelay: (value) => persistPref("undo_send_delay", value),
    analyticsEnabled: prefs.analytics_enabled,
    setAnalyticsEnabled: (value) => persistPref("analytics_enabled", value),
    emailBodyFontFamily: prefs.email_body_font_family,
    emailBodyFontSize: prefs.email_body_font_size,
  };
}
