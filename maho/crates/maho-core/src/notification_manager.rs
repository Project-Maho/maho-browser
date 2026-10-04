use std::collections::HashMap;

use maho_types::common::ImageData;
use maho_types::traits::shell_renderer::{NotificationAction, NotificationViewModel};

/// Web notification with origin tracking
pub struct Notification {
    pub id: String,
    pub origin: String,
    pub title: String,
    pub message: String,
    pub icon: Option<ImageData>,
    pub actions: Vec<NotificationAction>,
    pub timestamp: u64,
    pub read: bool,
}

const MAX_ACTIVE_NOTIFICATIONS: usize = 200;

/// Manages web notifications: queue, dismiss, filter by origin
pub struct NotificationManager {
    notifications: Vec<Notification>,
    /// Per-origin notification permission: true = allowed, false = blocked
    filters: HashMap<String, bool>,
    next_id: u64,
}

impl Default for NotificationManager {
    fn default() -> Self {
        Self::new()
    }
}

impl NotificationManager {
    pub fn new() -> Self {
        Self {
            notifications: Vec::new(),
            filters: HashMap::new(),
            next_id: 1,
        }
    }

    /// Queue a new notification. Returns None if origin is blocked.
    pub fn queue_notification(
        &mut self,
        origin: String,
        title: String,
        message: String,
        icon: Option<ImageData>,
        actions: Vec<NotificationAction>,
    ) -> Option<NotificationViewModel> {
        // Check if origin is blocked
        if let Some(false) = self.filters.get(&origin) {
            return None;
        }

        let id = format!("notif-{}", self.next_id);
        self.next_id += 1;

        let notification = Notification {
            id: id.clone(),
            origin,
            title: title.clone(),
            message: message.clone(),
            icon: icon.clone(),
            actions: actions.clone(),
            timestamp: std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap_or_default()
                .as_secs(),
            read: false,
        };

        let view_model = NotificationViewModel {
            id: id.clone(),
            title,
            message,
            icon,
            actions,
        };

        self.notifications.push(notification);
        if self.notifications.len() > MAX_ACTIVE_NOTIFICATIONS {
            let excess = self.notifications.len() - MAX_ACTIVE_NOTIFICATIONS;
            let mut read_indices: Vec<usize> = self
                .notifications
                .iter()
                .enumerate()
                .filter(|(_, n)| n.read)
                .map(|(i, _)| i)
                .take(excess)
                .collect();
            read_indices.reverse();
            for idx in read_indices {
                self.notifications.remove(idx);
            }
            if self.notifications.len() > MAX_ACTIVE_NOTIFICATIONS {
                let remaining_excess = self.notifications.len() - MAX_ACTIVE_NOTIFICATIONS;
                self.notifications.drain(0..remaining_excess);
            }
        }
        Some(view_model)
    }

    /// Dismiss a single notification by ID
    pub fn dismiss(&mut self, notification_id: &str) -> bool {
        let len_before = self.notifications.len();
        self.notifications.retain(|n| n.id != notification_id);
        self.notifications.len() < len_before
    }

    /// Dismiss all notifications
    pub fn dismiss_all(&mut self) {
        self.notifications.clear();
    }

    /// Handle a notification action button click
    pub fn handle_action(&mut self, notification_id: &str, _action_id: &str) -> bool {
        // Remove the notification after action is taken
        self.dismiss(notification_id)
    }

    /// Set filter for an origin (true = allow, false = block)
    pub fn set_filter(&mut self, origin: String, allowed: bool) {
        self.filters.insert(origin, allowed);
    }

    /// Check if an origin is allowed to show notifications
    pub fn is_origin_allowed(&self, origin: &str) -> bool {
        self.filters.get(origin).copied().unwrap_or(true)
    }

    /// Get all active notifications as view models
    pub fn get_all(&self) -> Vec<NotificationViewModel> {
        self.notifications
            .iter()
            .map(|n| NotificationViewModel {
                id: n.id.clone(),
                title: n.title.clone(),
                message: n.message.clone(),
                icon: n.icon.clone(),
                actions: n.actions.clone(),
            })
            .collect()
    }

    /// Get notification count
    pub fn count(&self) -> usize {
        self.notifications.len()
    }

    /// Get unread notification count
    pub fn unread_count(&self) -> usize {
        self.notifications.iter().filter(|n| !n.read).count()
    }

    /// Mark a notification as read
    pub fn mark_read(&mut self, notification_id: &str) {
        if let Some(n) = self
            .notifications
            .iter_mut()
            .find(|n| n.id == notification_id)
        {
            n.read = true;
        }
    }
}
