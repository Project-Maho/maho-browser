// Copyright 2026 Maho Browser. All rights reserved.

export interface BehaviorPrefs {
  sidebar_width: number;
  email_list_width: number;
  block_remote_images: boolean;
  block_trackers: boolean;
  desktop_notifications: boolean;
  unread_badge_enabled: boolean;
  sound_enabled: boolean;
  undo_send_delay: number;
  auto_save_drafts: boolean;
  sync_interval: number;
  email_body_font_family: string;
  email_body_font_size: number;
}

const booleanValue = (value: unknown, fallback: boolean): boolean => {
  if (value === true || value === 'true') return true;
  if (value === false || value === 'false') return false;
  return fallback;
};

export function parseBehaviorPrefs(raw: string): BehaviorPrefs | null {
  try {
    const value: unknown = JSON.parse(raw);
    if (typeof value !== 'object' || value === null) return null;
    const record = value as Record<string, unknown>;
    return {
      sidebar_width: Number(record.sidebar_width ?? 240),
      email_list_width: Number(record.email_list_width ?? 350),
      block_remote_images: booleanValue(record.block_remote_images, true),
      block_trackers: booleanValue(record.block_trackers, true),
      desktop_notifications: booleanValue(record.desktop_notifications, false),
      unread_badge_enabled: booleanValue(record.unread_badge_enabled, true),
      sound_enabled: booleanValue(record.sound_enabled, true),
      undo_send_delay: Number(record.undo_send_delay ?? 5),
      auto_save_drafts: booleanValue(record.auto_save_drafts, true),
      sync_interval: Number(record.sync_interval ?? 0),
      email_body_font_family:
          typeof record.email_body_font_family === 'string' ?
          record.email_body_font_family : 'system-ui',
      email_body_font_size: Number(record.email_body_font_size ?? 14),
    };
  } catch (error) {
    if (error instanceof SyntaxError) return null;
    throw error;
  }
}
