// Copyright 2026 Maho Browser. All rights reserved.

export type NotificationPreview = 'sender_subject' | 'sender_only' | 'generic';

export interface BehaviorPrefs {
  version: number;
  revision: number;
  sidebar_width: number;
  email_list_width: number;
  block_remote_images: boolean;
  block_trackers: boolean;
  desktop_notifications: boolean;
  notification_preview: NotificationPreview;
  unread_badge_enabled: boolean;
  muted_thread_ids: string[];
  sound_enabled: boolean;
  undo_send_delay: number;
  auto_save_drafts: boolean;
  sync_interval: number;
  email_body_font_family: string;
  email_body_font_size: number;
}

export const DEFAULT_BEHAVIOR_PREFS: BehaviorPrefs = {
  version: 1,
  revision: 0,
  sidebar_width: 240,
  email_list_width: 350,
  block_remote_images: true,
  block_trackers: true,
  desktop_notifications: true,
  notification_preview: 'sender_subject',
  unread_badge_enabled: true,
  muted_thread_ids: [],
  sound_enabled: true,
  undo_send_delay: 5,
  auto_save_drafts: true,
  sync_interval: 15,
  email_body_font_family: 'system-ui',
  email_body_font_size: 14,
};

export const SYNC_INTERVAL_OPTIONS = [
  {value: 5, label: 'Every 5 minutes'},
  {value: 15, label: 'Every 15 minutes'},
  {value: 30, label: 'Every 30 minutes'},
  {value: 60, label: 'Hourly'},
] as const;

export type BehaviorSnapshot = BehaviorPrefs;

export type BehaviorUpdateResult =
    {status: 'applied'; snapshot: BehaviorSnapshot}|
    {status: 'conflict'; snapshot: BehaviorSnapshot}|
    {status: 'invalid'; snapshot: BehaviorSnapshot};

const booleanValue = (value: unknown, fallback: boolean): boolean => {
  if (value === true || value === 'true') return true;
  if (value === false || value === 'false') return false;
  return fallback;
};

const previewValue = (value: unknown): NotificationPreview => {
  return value === 'sender_only' || value === 'generic' ? value :
      'sender_subject';
};

const stringArrayValue = (value: unknown): string[] => {
  return Array.isArray(value) ?
      value.filter((item): item is string => typeof item === 'string') : [];
};

export function parseBehaviorPrefs(raw: string): BehaviorPrefs | null {
  try {
    const value: unknown = JSON.parse(raw);
    if (typeof value !== 'object' || value === null) return null;
    const record = value as Record<string, unknown>;
    return {
      version: 1,
      revision: Number(record.revision ?? DEFAULT_BEHAVIOR_PREFS.revision),
      sidebar_width: Number(record.sidebar_width ?? DEFAULT_BEHAVIOR_PREFS.sidebar_width),
      email_list_width: Number(record.email_list_width ?? DEFAULT_BEHAVIOR_PREFS.email_list_width),
      block_remote_images: booleanValue(record.block_remote_images, DEFAULT_BEHAVIOR_PREFS.block_remote_images),
      block_trackers: booleanValue(record.block_trackers, DEFAULT_BEHAVIOR_PREFS.block_trackers),
      desktop_notifications: booleanValue(record.desktop_notifications, DEFAULT_BEHAVIOR_PREFS.desktop_notifications),
      notification_preview: previewValue(record.notification_preview),
      unread_badge_enabled: booleanValue(record.unread_badge_enabled, DEFAULT_BEHAVIOR_PREFS.unread_badge_enabled),
      muted_thread_ids: stringArrayValue(record.muted_thread_ids),
      sound_enabled: booleanValue(record.sound_enabled, DEFAULT_BEHAVIOR_PREFS.sound_enabled),
      undo_send_delay: Number(record.undo_send_delay ?? DEFAULT_BEHAVIOR_PREFS.undo_send_delay),
      auto_save_drafts: booleanValue(record.auto_save_drafts, DEFAULT_BEHAVIOR_PREFS.auto_save_drafts),
      sync_interval: Number(record.sync_interval ?? DEFAULT_BEHAVIOR_PREFS.sync_interval),
      email_body_font_family:
          typeof record.email_body_font_family === 'string' ?
          record.email_body_font_family : DEFAULT_BEHAVIOR_PREFS.email_body_font_family,
      email_body_font_size: Number(record.email_body_font_size ?? DEFAULT_BEHAVIOR_PREFS.email_body_font_size),
    };
  } catch (error) {
    if (error instanceof SyntaxError) return null;
    throw error;
  }
}

export function parseBehaviorSnapshot(
    raw: string, revision?: number): BehaviorSnapshot|null {
  const parsed = parseBehaviorPrefs(raw);
  if (!parsed) return null;
  return {...parsed, version: 1, revision: revision ?? parsed.revision};
}

export function applyBehaviorUpdate(
    snapshot: BehaviorSnapshot, expectedRevision: number,
    key: keyof BehaviorPrefs,
    value: BehaviorPrefs[keyof BehaviorPrefs]): BehaviorUpdateResult {
  if (snapshot.revision !== expectedRevision) {
    return {status: 'conflict', snapshot};
  }
  if (key === 'version' || key === 'revision') {
    return {status: 'invalid', snapshot};
  }
  return {
    status: 'applied',
    snapshot: {
      ...snapshot,
      [key]: value,
      version: 1,
      revision: snapshot.revision + 1,
    },
  };
}
