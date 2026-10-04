use std::collections::{HashMap, HashSet, VecDeque};

use maho_types::animation::performance_contract::TAB_LIFECYCLE_TIMEOUTS;
use maho_types::common::{DateTime, MemoryPressureLevel, ScrollPosition, TabSnapshot, Url};
use maho_types::events::core_update::{CoreUpdate, MemoryAction};
use maho_types::identifiers::{SpaceId, TabId};
use maho_types::tab::{Tab, TabLifecycleState, TabRole};
use maho_types::traits::shell_renderer::{TabStateUpdate, TabViewModel};

const MAX_CLOSED_STACK: usize = 25;
pub const MAX_FAVORITES: usize = 12;
pub const MAX_ARCHIVED_PER_SPACE: usize = 100;
const FREEZE_TIMEOUT_MS: u64 = TAB_LIFECYCLE_TIMEOUTS.active_to_frozen_minutes * 60 * 1000;
const SUSPEND_TIMEOUT_MS: u64 = TAB_LIFECYCLE_TIMEOUTS.frozen_to_suspended_minutes * 60 * 1000;
const ARCHIVE_TIMEOUT_MS: u64 = TAB_LIFECYCLE_TIMEOUTS.suspended_to_archived_hours * 60 * 60 * 1000;
const TODAY_TAB_TIMEOUT_MS: u64 = 12 * 60 * 60 * 1000; // 12 hours

fn now_millis() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as u64
}

pub(crate) fn timestamp_millis_to_iso8601(timestamp_ms: u64) -> String {
    chrono::DateTime::<chrono::Utc>::from_timestamp_millis(timestamp_ms as i64)
        .map(|timestamp| timestamp.to_rfc3339_opts(chrono::SecondsFormat::Millis, true))
        .unwrap_or_else(|| chrono::Utc::now().to_rfc3339_opts(chrono::SecondsFormat::Millis, true))
}

fn extract_host(url: &str) -> &str {
    let without_scheme = url
        .strip_prefix("https://")
        .or_else(|| url.strip_prefix("http://"))
        .unwrap_or(url);
    without_scheme.split('/').next().unwrap_or(without_scheme)
}

fn extract_registrable_domain(url: &str) -> String {
    let host = extract_host(url);
    match psl::domain(host.as_bytes()) {
        Some(domain) => std::str::from_utf8(domain.as_bytes())
            .unwrap_or(host)
            .to_string(),
        None => host.to_string(),
    }
}

fn is_auto_lifecycle_exempt(tab: &Tab) -> bool {
    tab.role.is_liveness_protected()
}

pub struct TabLifecycleManager {
    tabs: HashMap<TabId, Tab>,
    closed_stack: VecDeque<Tab>,
    state_timestamps: HashMap<TabId, u64>,
    creation_timestamps: HashMap<TabId, u64>,
    loading_states: HashMap<TabId, bool>,
    security_states: HashMap<TabId, bool>,
    pending_updates: Vec<CoreUpdate>,
    on_update: Box<dyn Fn(CoreUpdate)>,
    reader_mode_tabs: HashSet<TabId>,
}

impl TabLifecycleManager {
    fn push_closed_tab(&mut self, tab: Tab) {
        if tab.is_private {
            return;
        }
        self.closed_stack.push_back(tab);
        if self.closed_stack.len() > MAX_CLOSED_STACK {
            self.closed_stack.pop_front();
        }
    }

    pub fn new(on_update: Box<dyn Fn(CoreUpdate)>) -> Self {
        Self {
            tabs: HashMap::new(),
            closed_stack: VecDeque::new(),
            state_timestamps: HashMap::new(),
            creation_timestamps: HashMap::new(),
            loading_states: HashMap::new(),
            security_states: HashMap::new(),
            pending_updates: Vec::new(),
            on_update,
            reader_mode_tabs: HashSet::new(),
        }
    }

    pub fn queue_update(&mut self, update: CoreUpdate) {
        self.pending_updates.push(update);
    }

    pub fn create_tab(
        &mut self,
        space_id: SpaceId,
        url: Option<Url>,
        parent_id: Option<TabId>,
        window_id: Option<i64>,
        is_private: bool,
    ) -> Tab {
        self.create_tab_inner(
            TabId::generate(),
            space_id,
            url,
            parent_id,
            window_id,
            is_private,
        )
    }

    pub fn create_tab_with_id(
        &mut self,
        tab_id: TabId,
        space_id: SpaceId,
        url: Option<Url>,
        parent_id: Option<TabId>,
        window_id: Option<i64>,
        is_private: bool,
    ) -> Tab {
        if let Some(existing) = self.tabs.get(&tab_id) {
            return existing.clone();
        }
        self.create_tab_inner(tab_id, space_id, url, parent_id, window_id, is_private)
    }

    fn create_tab_inner(
        &mut self,
        id: TabId,
        space_id: SpaceId,
        url: Option<Url>,
        parent_id: Option<TabId>,
        window_id: Option<i64>,
        is_private: bool,
    ) -> Tab {
        let now = DateTime::now();
        let now_ms = now_millis();
        let tab = Tab {
            id,
            parent_id,
            space_id,
            url: url.unwrap_or_else(|| Url::new("about:blank")),
            title: "New Tab".to_string(),
            custom_title: None,
            custom_icon: None,
            favicon: None,
            state: TabLifecycleState::Active,
            role: TabRole::Normal,
            is_muted: false,
            zoom_level: 1.0,
            created_at: now.clone(),
            last_active_at: now,
            scroll_position: ScrollPosition::default(),
            pinned_url: None,
            window_id,
            is_private,
        };
        self.tabs.insert(tab.id.clone(), tab.clone());
        self.state_timestamps.insert(tab.id.clone(), now_ms);
        self.creation_timestamps.insert(tab.id.clone(), now_ms);
        (self.on_update)(CoreUpdate::TabCreated {
            tab: self.to_view_model(&tab),
        });
        tab
    }

    pub fn close_tab(&mut self, tab_id: &TabId) -> Option<Tab> {
        let child_ids = self.collect_child_ids(tab_id);
        for child_id in child_ids.iter().rev() {
            if let Some(child_tab) = self.tabs.remove(child_id) {
                self.state_timestamps.remove(child_id);
                self.creation_timestamps.remove(child_id);
                self.loading_states.remove(child_id);
                self.security_states.remove(child_id);
                self.reader_mode_tabs.remove(child_id);
                self.push_closed_tab(child_tab);
                (self.on_update)(CoreUpdate::TabClosed {
                    tab_id: child_id.clone(),
                    animated: true,
                });
            }
        }
        let tab = self.tabs.remove(tab_id)?;
        self.state_timestamps.remove(tab_id);
        self.creation_timestamps.remove(tab_id);
        self.loading_states.remove(tab_id);
        self.security_states.remove(tab_id);
        self.reader_mode_tabs.remove(tab_id);
        self.push_closed_tab(tab.clone());
        (self.on_update)(CoreUpdate::TabClosed {
            tab_id: tab_id.clone(),
            animated: true,
        });
        Some(tab)
    }

    pub fn collect_child_ids(&self, parent_id: &TabId) -> Vec<TabId> {
        let mut result = Vec::new();
        let mut queue = vec![parent_id.clone()];
        let mut visited = std::collections::HashSet::new();
        visited.insert(parent_id.clone());

        while let Some(current_parent) = queue.pop() {
            let children: Vec<TabId> = self
                .tabs
                .values()
                .filter(|t| t.parent_id.as_ref() == Some(&current_parent))
                .map(|t| t.id.clone())
                .collect();

            for child in children {
                if visited.insert(child.clone()) {
                    queue.push(child.clone());
                    result.push(child);
                }
            }
        }

        result
    }

    pub fn activate_tab(&mut self, tab_id: &TabId) {
        if let Some(tab) = self.tabs.get_mut(tab_id) {
            tab.state = TabLifecycleState::Active;
            tab.last_active_at = DateTime::now();
            self.state_timestamps.insert(tab_id.clone(), now_millis());
            (self.on_update)(CoreUpdate::TabLifecycleChanged {
                tab_id: tab_id.clone(),
                state: tab.state.clone(),
            });
        }
    }

    /// Reset a tab's lifecycle state to `Active` without touching `last_active_at`.
    /// Used on session-restore re-announce: Chromium spawns a fresh WebContents for a
    /// tab that was Frozen/Suspended at shutdown, so the lifecycle state must snap to
    /// Active, but the user-activation timestamp must be preserved so Clear time-range
    /// filters still see the tab as stale.
    pub fn restore_to_active(&mut self, tab_id: &TabId) {
        if let Some(tab) = self.tabs.get_mut(tab_id) {
            tab.state = TabLifecycleState::Active;
            self.state_timestamps.insert(tab_id.clone(), now_millis());
            (self.on_update)(CoreUpdate::TabLifecycleChanged {
                tab_id: tab_id.clone(),
                state: tab.state.clone(),
            });
        }
    }

    pub fn freeze_tab(&mut self, tab_id: &TabId) {
        if let Some(tab) = self.tabs.get_mut(tab_id) {
            if !matches!(tab.state, TabLifecycleState::Active) {
                return;
            }
            tab.state = TabLifecycleState::Frozen;
            self.state_timestamps.insert(tab_id.clone(), now_millis());
            (self.on_update)(CoreUpdate::TabLifecycleChanged {
                tab_id: tab_id.clone(),
                state: tab.state.clone(),
            });
        }
    }

    pub fn get_active_tab_id(&self) -> Option<&TabId> {
        self.tabs
            .iter()
            .find(|(_, tab)| matches!(tab.state, TabLifecycleState::Active))
            .map(|(id, _)| id)
    }

    pub fn state_timestamp_for_tab(&self, tab_id: &TabId) -> Option<u64> {
        self.state_timestamps.get(tab_id).copied()
    }

    pub fn suspend_tab(&mut self, tab_id: &TabId) {
        if let Some(tab) = self.tabs.get_mut(tab_id) {
            if !matches!(
                tab.state,
                TabLifecycleState::Frozen | TabLifecycleState::Active
            ) {
                return;
            }
            let snapshot = TabSnapshot {
                url: tab.url.0.clone(),
                title: tab.title.clone(),
                scroll_position: tab.scroll_position.clone(),
                interaction_state: vec![],
                captured_at: DateTime::now(),
            };
            tab.state = TabLifecycleState::Suspended { snapshot };
            self.state_timestamps.insert(tab_id.clone(), now_millis());
            (self.on_update)(CoreUpdate::TabLifecycleChanged {
                tab_id: tab_id.clone(),
                state: tab.state.clone(),
            });
        }
    }

    pub fn archive_tab(&mut self, tab_id: &TabId) {
        if let Some(tab) = self.tabs.get_mut(tab_id) {
            if !matches!(tab.state, TabLifecycleState::Suspended { .. }) {
                return;
            }
            tab.state = TabLifecycleState::Archived {
                metadata_only: false,
            };
            self.state_timestamps.insert(tab_id.clone(), now_millis());
            (self.on_update)(CoreUpdate::TabLifecycleChanged {
                tab_id: tab_id.clone(),
                state: tab.state.clone(),
            });
        }
    }

    pub fn get_tab(&self, tab_id: &TabId) -> Option<&Tab> {
        self.tabs.get(tab_id)
    }

    pub fn get_tab_snapshot_json(&self, tab_id: &TabId) -> Option<String> {
        let tab = self.tabs.get(tab_id)?;
        let snapshot = serde_json::json!({
            "id": tab_id.as_ref(),
            "url": tab.url.0.clone(),
            "title": tab.title.clone(),
            "isPinned": tab.role.is_pinned(),
            "scrollPosition": { "x": tab.scroll_position.x, "y": tab.scroll_position.y }
        });
        Some(snapshot.to_string())
    }

    pub fn get_tabs_by_space(&self, space_id: &SpaceId) -> Vec<&Tab> {
        self.tabs
            .values()
            .filter(|t| {
                t.space_id == *space_id && !matches!(t.state, TabLifecycleState::Archived { .. })
            })
            .collect()
    }

    pub fn clear(&mut self) {
        self.tabs.clear();
        self.closed_stack.clear();
        self.state_timestamps.clear();
        self.creation_timestamps.clear();
        self.loading_states.clear();
        self.security_states.clear();
        self.pending_updates.clear();
        self.reader_mode_tabs.clear();
    }

    pub fn get_all_tabs(&self) -> Vec<&Tab> {
        self.tabs.values().collect()
    }

    pub fn restore_tab(&mut self, tab: Tab) {
        let now_ms = now_millis();
        self.tabs.insert(tab.id.clone(), tab.clone());
        self.state_timestamps.insert(tab.id.clone(), now_ms);
        self.creation_timestamps.insert(tab.id.clone(), now_ms);
    }

    pub fn reopen_last_closed(&mut self) -> Option<Tab> {
        let mut tab = self.closed_stack.pop_back()?;
        tab.state = TabLifecycleState::Active;
        tab.last_active_at = DateTime::now();
        self.tabs.insert(tab.id.clone(), tab.clone());
        self.reset_active_timestamps(&tab.id);
        (self.on_update)(CoreUpdate::TabCreated {
            tab: self.to_view_model(&tab),
        });
        Some(tab)
    }

    pub fn duplicate_tab(&mut self, tab_id: &TabId) -> Option<Tab> {
        let original = self.tabs.get(tab_id)?;
        let space_id = original.space_id.clone();
        let url = Some(original.url.clone());
        let parent_id = original.parent_id.clone();
        let window_id = original.window_id;
        // Carry the privacy flag across: persistence, the closed-tab stack,
        // snapshots and sync all filter on `is_private`, so a duplicate that
        // drops it turns a private URL into ordinary persisted state.
        let is_private = original.is_private;
        Some(self.create_tab(space_id, url, parent_id, window_id, is_private))
    }

    pub fn move_tab(
        &mut self,
        tab_id: &TabId,
        target_space: SpaceId,
        _position: usize,
    ) -> Option<SpaceId> {
        let tab = self.tabs.get_mut(tab_id)?;
        let source_space = tab.space_id.clone();
        tab.space_id = target_space;
        Some(source_space)
    }

    pub fn set_tab_parent(&mut self, tab_id: &TabId, new_parent_id: Option<TabId>) -> bool {
        let _tab = match self.tabs.get(tab_id) {
            Some(t) => t,
            None => return false,
        };

        if let Some(ref parent_id) = new_parent_id {
            if !self.tabs.contains_key(parent_id) {
                return false;
            }

            if *parent_id == *tab_id {
                return false;
            }

            let mut current = parent_id;
            while let Some(t) = self.tabs.get(current) {
                if let Some(ref grandparent) = t.parent_id {
                    if *grandparent == *tab_id {
                        return false;
                    }
                    current = grandparent;
                } else {
                    break;
                }
            }
        }

        let tab = self.tabs.get_mut(tab_id).unwrap();
        tab.parent_id = new_parent_id;

        (self.on_update)(CoreUpdate::TabUpdated {
            tab_id: tab_id.clone(),
            changes: TabStateUpdate {
                role: Some(tab.role.clone()),
                ..Default::default()
            },
        });

        true
    }

    pub fn pin_tab(&mut self, tab_id: &TabId) {
        if let Some(tab) = self.tabs.get_mut(tab_id) {
            tab.role = TabRole::Pinned;
            tab.pinned_url = Some(tab.url.clone());
            (self.on_update)(CoreUpdate::TabUpdated {
                tab_id: tab_id.clone(),
                changes: TabStateUpdate {
                    title: None,
                    custom_title: None,
                    url: None,
                    favicon: None,
                    is_loading: None,
                    is_pinned: Some(true),
                    is_favorite: Some(false), // L3-EXEMPT: legacy compat
                    favorite_order: Some(None),
                    is_muted: None,
                    is_playing_audio: None,
                    lifecycle_state: None,
                    role: Some(TabRole::Pinned),
                },
            });
        }
    }

    pub fn unpin_tab(&mut self, tab_id: &TabId) {
        if let Some(tab) = self.tabs.get_mut(tab_id) {
            tab.role = TabRole::Normal;
            tab.pinned_url = None;
            (self.on_update)(CoreUpdate::TabUpdated {
                tab_id: tab_id.clone(),
                changes: TabStateUpdate {
                    title: None,
                    custom_title: None,
                    url: None,
                    favicon: None,
                    is_loading: None,
                    is_pinned: Some(false),
                    is_favorite: Some(false), // L3-EXEMPT: legacy compat
                    favorite_order: Some(None),
                    is_muted: None,
                    is_playing_audio: None,
                    lifecycle_state: None,
                    role: Some(TabRole::Normal),
                },
            });
        }
    }

    /// Sets `tab.role` to `new_role` and emits a `TabUpdated` event.
    ///
    /// Also manages `pinned_url` side-effect: seeds `pinned_url` on Normal/Favorite -> Pinned,
    /// and preserves it when transitioning away from Pinned so the user-authored value survives
    /// a demotion (re-favoriting only seeds an empty slot, so a cleared value was unrecoverable).
    /// See ADR 11 (docs/decisions/0011-favorites-close-protection-policy.md).
    pub fn transition_tab_role(&mut self, id: &TabId, new_role: TabRole) -> bool {
        if self.tabs.get(id).map(|t| &t.role) == Some(&new_role) {
            return false;
        }
        if let Some(tab) = self.tabs.get_mut(id) {
            tab.role = new_role.clone();
            if new_role.is_pinned() && tab.pinned_url.is_none() {
                tab.pinned_url = Some(tab.url.clone());
            }
            (self.on_update)(CoreUpdate::TabUpdated {
                tab_id: id.clone(),
                changes: TabStateUpdate {
                    title: None,
                    custom_title: None,
                    url: None,
                    favicon: None,
                    is_loading: None,
                    is_pinned: Some(new_role.is_pinned()),
                    is_favorite: Some(new_role.is_favorite()), // L3-EXEMPT: legacy compat
                    favorite_order: Some(new_role.favorite_order()),
                    is_muted: None,
                    is_playing_audio: None,
                    lifecycle_state: None,
                    role: Some(new_role),
                },
            });
            true
        } else {
            false
        }
    }

    pub fn favorite_tab(&mut self, tab_id: &TabId) {
        if self
            .tabs
            .get(tab_id)
            .map(|tab| tab.role.is_favorite())
            .unwrap_or(false)
        {
            // L3-EXEMPT: role query
            return;
        }
        let current_count = self
            .tabs
            .values()
            .filter(|t| {
                t.role.is_favorite() && !matches!(t.state, TabLifecycleState::Archived { .. })
            })
            .count(); // L3-EXEMPT: role query
        if current_count >= MAX_FAVORITES {
            return;
        }
        let max_order = self
            .tabs
            .values()
            .filter(|t| {
                t.role.is_favorite() && !matches!(t.state, TabLifecycleState::Archived { .. })
            }) // L3-EXEMPT: role query
            .filter_map(|t| t.role.favorite_order())
            .max();

        self.transition_tab_role(
            tab_id,
            TabRole::Favorite {
                order: max_order.map_or(0, |order| order + 1),
            },
        );
    }

    pub fn mute_tab(&mut self, tab_id: &TabId) {
        if let Some(tab) = self.tabs.get_mut(tab_id) {
            tab.is_muted = true;
            (self.on_update)(CoreUpdate::TabUpdated {
                tab_id: tab_id.clone(),
                changes: TabStateUpdate {
                    is_muted: Some(true),
                    ..Default::default()
                },
            });
        }
    }

    pub fn unmute_tab(&mut self, tab_id: &TabId) {
        if let Some(tab) = self.tabs.get_mut(tab_id) {
            tab.is_muted = false;
            (self.on_update)(CoreUpdate::TabUpdated {
                tab_id: tab_id.clone(),
                changes: TabStateUpdate {
                    is_muted: Some(false),
                    ..Default::default()
                },
            });
        }
    }

    pub fn handle_memory_pressure(&mut self, level: MemoryPressureLevel) -> MemoryAction {
        match level {
            MemoryPressureLevel::Normal => MemoryAction::FrozeTabs { count: 0 },
            MemoryPressureLevel::Warning => {
                let inactive: Vec<TabId> = self
                    .tabs
                    .values()
                    .filter(|t| {
                        matches!(t.state, TabLifecycleState::Active)
                            && !self.is_recently_active(&t.id)
                            && !t.role.is_liveness_protected()
                    })
                    .map(|t| t.id.clone())
                    .collect();
                let count = inactive.len();
                for tab_id in inactive {
                    self.freeze_tab(&tab_id);
                }
                MemoryAction::FrozeTabs { count }
            }
            MemoryPressureLevel::Critical => {
                let frozen: Vec<TabId> = self
                    .tabs
                    .values()
                    .filter(|t| {
                        matches!(t.state, TabLifecycleState::Frozen)
                            && !t.role.is_liveness_protected()
                    })
                    .map(|t| t.id.clone())
                    .collect();
                let count = frozen.len();
                for tab_id in frozen {
                    self.suspend_tab(&tab_id);
                }
                MemoryAction::SuspendedTabs { count }
            }
            MemoryPressureLevel::Extreme => {
                let suspendable: Vec<TabId> = self
                    .tabs
                    .values()
                    .filter(|t| {
                        !matches!(
                            t.state,
                            TabLifecycleState::Active
                                | TabLifecycleState::Suspended { .. }
                                | TabLifecycleState::Archived { .. }
                        ) && !t.role.is_liveness_protected()
                    })
                    .map(|t| t.id.clone())
                    .collect();
                let count = suspendable.len();
                for tab_id in suspendable {
                    self.suspend_tab(&tab_id);
                }
                MemoryAction::ReleasedWebviews { count }
            }
        }
    }

    /// Advance lifecycle timers. `archive_timeout_hours` comes from settings:
    /// - negative (e.g. -1.0) means all automatic archiving is disabled
    /// - positive value is hours until suspended tabs are archived, and today-tabs
    ///   follow the separate today-tab timeout while auto-archive remains enabled
    pub fn tick_with_settings(
        &mut self,
        archive_timeout_hours: f64,
        today_tab_timeout_hours: f64,
    ) -> Vec<CoreUpdate> {
        let auto_archive_disabled = archive_timeout_hours < 0.0;
        let archive_timeout_ms: u64 = if auto_archive_disabled {
            u64::MAX
        } else {
            (archive_timeout_hours * 3600.0 * 1000.0) as u64
        };

        let today_disabled = today_tab_timeout_hours < 0.0;
        let today_timeout_ms: u64 = if today_disabled {
            u64::MAX
        } else {
            (today_tab_timeout_hours * 3600.0 * 1000.0) as u64
        };

        let mut pending = Vec::new();
        let now = now_millis();
        let tab_ids: Vec<TabId> = self.state_timestamps.keys().cloned().collect();
        for tab_id in tab_ids {
            let timestamp = match self.state_timestamps.get(&tab_id) {
                Some(&ts) => ts,
                None => continue,
            };
            let tab = match self.tabs.get(&tab_id) {
                Some(t) => t,
                None => continue,
            };
            let elapsed = now - timestamp;
            let state_kind = tab.state.kind_str();

            if state_kind == "active"
                && elapsed > FREEZE_TIMEOUT_MS
                && !is_auto_lifecycle_exempt(tab)
            {
                if let Some(update) = self.freeze_tab_internal(&tab_id) {
                    pending.push(update);
                }
            } else if state_kind == "frozen"
                && elapsed > SUSPEND_TIMEOUT_MS
                && !is_auto_lifecycle_exempt(tab)
            {
                if let Some(update) = self.suspend_tab_internal(&tab_id) {
                    pending.push(update);
                }
            } else if state_kind == "suspended"
                && !auto_archive_disabled
                && elapsed > archive_timeout_ms
                && !is_auto_lifecycle_exempt(tab)
            {
                if let Some(update) = self.archive_tab_internal(&tab_id) {
                    pending.push(update);
                }
            }
        }

        // Today Tabs auto-archive: unpinned, unfavorited active tabs older than configured timeout.
        // This is part of the automatic archive path, so it is also disabled when archive timeout is off.
        if !auto_archive_disabled && !today_disabled {
            let today_tab_ids: Vec<TabId> = self
                .tabs
                .values()
                .filter(|t| {
                    t.role == TabRole::Normal && matches!(t.state, TabLifecycleState::Active)
                })
                .map(|t| t.id.clone())
                .collect();

            for tab_id in today_tab_ids {
                if let Some(&created_at) = self.creation_timestamps.get(&tab_id) {
                    let elapsed = now.saturating_sub(created_at);
                    if elapsed > today_timeout_ms {
                        // Suspend first to capture snapshot, then archive.
                        // This preserves restore fidelity for today-tab auto-archive.
                        if matches!(
                            self.tabs.get(&tab_id).map(|t| &t.state),
                            Some(TabLifecycleState::Active)
                        ) {
                            if let Some(update) = self.suspend_tab_internal(&tab_id) {
                                pending.push(update);
                            }
                        }
                        if let Some(update) = self.archive_tab_internal(&tab_id) {
                            pending.push(update);
                        }
                    }
                }
            }
        }

        for update in &pending {
            self.pending_updates.push(update.clone());
            (self.on_update)(update.clone());
        }
        std::mem::take(&mut self.pending_updates)
    }

    /// Legacy tick — uses hardcoded defaults. Prefer `tick_with_settings`.
    pub fn tick(&mut self) -> Vec<CoreUpdate> {
        self.tick_with_settings(
            ARCHIVE_TIMEOUT_MS as f64 / 3_600_000.0,
            TODAY_TAB_TIMEOUT_MS as f64 / 3_600_000.0,
        )
    }

    fn freeze_tab_internal(&mut self, tab_id: &TabId) -> Option<CoreUpdate> {
        let tab = self.tabs.get_mut(tab_id)?;
        if !matches!(tab.state, TabLifecycleState::Active) {
            return None;
        }
        tab.state = TabLifecycleState::Frozen;
        self.state_timestamps.insert(tab_id.clone(), now_millis());
        Some(CoreUpdate::TabLifecycleChanged {
            tab_id: tab_id.clone(),
            state: tab.state.clone(),
        })
    }

    fn suspend_tab_internal(&mut self, tab_id: &TabId) -> Option<CoreUpdate> {
        let tab = self.tabs.get_mut(tab_id)?;
        if !matches!(
            tab.state,
            TabLifecycleState::Frozen | TabLifecycleState::Active
        ) {
            return None;
        }
        let snapshot = TabSnapshot {
            url: tab.url.0.clone(),
            title: tab.title.clone(),
            scroll_position: tab.scroll_position.clone(),
            interaction_state: vec![],
            captured_at: DateTime::now(),
        };
        tab.state = TabLifecycleState::Suspended { snapshot };
        self.state_timestamps.insert(tab_id.clone(), now_millis());
        Some(CoreUpdate::TabLifecycleChanged {
            tab_id: tab_id.clone(),
            state: tab.state.clone(),
        })
    }

    fn archive_tab_internal(&mut self, tab_id: &TabId) -> Option<CoreUpdate> {
        let tab = self.tabs.get_mut(tab_id)?;
        let had_snapshot = matches!(tab.state, TabLifecycleState::Suspended { .. });
        if !had_snapshot && !matches!(tab.state, TabLifecycleState::Active) {
            return None;
        }
        tab.state = TabLifecycleState::Archived {
            metadata_only: !had_snapshot,
        };
        self.state_timestamps.insert(tab_id.clone(), now_millis());
        Some(CoreUpdate::TabLifecycleChanged {
            tab_id: tab_id.clone(),
            state: tab.state.clone(),
        })
    }

    fn is_recently_active(&self, tab_id: &TabId) -> bool {
        match self.state_timestamps.get(tab_id) {
            Some(&ts) => now_millis() - ts < 60_000,
            None => false,
        }
    }

    pub fn get_pinned_tabs(&self, space_id: &SpaceId) -> Vec<&Tab> {
        self.tabs
            .values()
            .filter(|t| {
                t.space_id == *space_id
                    && t.role == TabRole::Pinned
                    && !matches!(t.state, TabLifecycleState::Archived { .. })
            })
            .collect()
    }

    pub fn get_favorite_tabs(&self, space_id: &SpaceId) -> Vec<&Tab> {
        let mut tabs: Vec<&Tab> = self
            .tabs
            .values()
            .filter(|t| {
                t.space_id == *space_id
                    && t.role.is_favorite()
                    && !matches!(t.state, TabLifecycleState::Archived { .. })
            }) // L3-EXEMPT: role query
            .collect();
        tabs.sort_by(|a, b| {
            (a.role.favorite_order().unwrap_or(u32::MAX), a.id.as_ref())
                .cmp(&(b.role.favorite_order().unwrap_or(u32::MAX), b.id.as_ref()))
        });
        tabs
    }

    pub fn reorder_favorite(
        &mut self,
        tab_id: &TabId,
        new_index: usize,
        space_ids: &HashSet<SpaceId>,
    ) {
        let mut favorite_tabs: Vec<Tab> = self
            .tabs
            .values()
            .filter(|t| {
                t.role.is_favorite()
                    && space_ids.contains(&t.space_id)
                    && !matches!(t.state, TabLifecycleState::Archived { .. })
            }) // L3-EXEMPT: role query
            .cloned()
            .collect();
        favorite_tabs.sort_by(|a, b| {
            (a.role.favorite_order().unwrap_or(u32::MAX), a.id.as_ref())
                .cmp(&(b.role.favorite_order().unwrap_or(u32::MAX), b.id.as_ref()))
        });

        let Some(tab_pos) = favorite_tabs.iter().position(|t| &t.id == tab_id) else {
            return;
        };

        let tab = favorite_tabs.remove(tab_pos);
        let insert_index = new_index.min(favorite_tabs.len());
        favorite_tabs.insert(insert_index, tab);

        for (i, favorite_tab) in favorite_tabs.into_iter().enumerate() {
            let new_order = i as u32;
            if favorite_tab.role.favorite_order() != Some(new_order) {
                if let Some(existing_tab) = self.tabs.get_mut(&favorite_tab.id) {
                    existing_tab.role = TabRole::Favorite { order: new_order };
                }
                (self.on_update)(CoreUpdate::TabUpdated {
                    tab_id: favorite_tab.id.clone(),
                    changes: TabStateUpdate {
                        favorite_order: Some(Some(new_order)),
                        role: Some(TabRole::Favorite { order: new_order }),
                        ..Default::default()
                    },
                });
            } else if let Some(existing_tab) = self.tabs.get_mut(&favorite_tab.id) {
                existing_tab.role = TabRole::Favorite { order: new_order };
            }
        }
    }

    pub fn get_all_favorite_tabs(&self) -> Vec<&Tab> {
        self.tabs
            .values()
            .filter(|t| t.role.is_favorite())
            .collect() // L3-EXEMPT: role query
    }

    pub fn count_favorite_tabs_in_spaces(&self, space_ids: &HashSet<SpaceId>) -> usize {
        self.tabs
            .values()
            .filter(|t| {
                t.role.is_favorite()
                    && space_ids.contains(&t.space_id)
                    && !matches!(t.state, TabLifecycleState::Archived { .. })
            }) // L3-EXEMPT: role query
            .count()
    }

    pub fn get_today_tabs(&self, space_id: &SpaceId) -> Vec<&Tab> {
        self.tabs
            .values()
            .filter(|t| {
                t.space_id == *space_id
                    && !t.role.is_pinned()
                    && !matches!(t.state, TabLifecycleState::Archived { .. })
            })
            .collect()
    }

    /// Get unpinned, non-archived tabs for a space filtered by window ownership.
    /// Only returns tabs whose `window_id` matches the given value.
    /// Tabs with `window_id == None` are excluded (they are shared/pinned tabs).
    pub fn get_tabs_for_window(&self, space_id: &SpaceId, window_id: i64) -> Vec<&Tab> {
        self.tabs
            .values()
            .filter(|t| {
                t.space_id == *space_id
                    && !t.role.is_pinned()
                    && t.window_id == Some(window_id)
                    && !matches!(t.state, TabLifecycleState::Archived { .. })
            })
            .collect()
    }

    /// Reassign all tabs owned by `old_window_id` to `new_window_id`.
    /// Useful when a window is replaced or merged.
    pub fn reassign_window_tabs(&mut self, old_window_id: i64, new_window_id: i64) {
        for tab in self.tabs.values_mut() {
            if tab.window_id == Some(old_window_id) {
                tab.window_id = Some(new_window_id);
            }
        }
    }

    /// Release all tabs owned by a closed window. The tabs are moved to the
    /// closed stack so they can be restored later, and removed from the live
    /// tab map.
    pub fn release_window_tabs(&mut self, window_id: i64) -> Vec<TabId> {
        let tab_ids: Vec<TabId> = self
            .tabs
            .values()
            .filter(|t| t.window_id == Some(window_id))
            .map(|t| t.id.clone())
            .collect();
        let mut released = Vec::new();
        for tab_id in tab_ids {
            if let Some(tab) = self.tabs.remove(&tab_id) {
                self.state_timestamps.remove(&tab_id);
                self.creation_timestamps.remove(&tab_id);
                self.loading_states.remove(&tab_id);
                self.security_states.remove(&tab_id);
                self.reader_mode_tabs.remove(&tab_id);
                self.push_closed_tab(tab);
                released.push(tab_id);
            }
        }
        released
    }

    pub fn get_closed_tabs(&self) -> Vec<&Tab> {
        self.closed_stack.iter().collect()
    }

    pub fn get_archived_tabs(&self, space_id: &SpaceId) -> Vec<&Tab> {
        self.tabs
            .values()
            .filter(|t| {
                t.space_id == *space_id && matches!(t.state, TabLifecycleState::Archived { .. })
            })
            .collect()
    }

    /// Returns non-archived tabs ordered by `last_active_at` descending (most recent
    /// first), capped to `limit`. Tabs whose `last_active_at` fails RFC3339 parsing are
    /// deprioritized to the end rather than dropped, and never panic.
    pub fn get_recent_tabs(&self, limit: usize) -> Vec<&Tab> {
        let mut candidates: Vec<&Tab> = self
            .tabs
            .values()
            .filter(|tab| !matches!(tab.state, TabLifecycleState::Archived { .. }))
            .collect();
        candidates.sort_by(|a, b| {
            let parse = |tab: &Tab| {
                chrono::DateTime::parse_from_rfc3339(&tab.last_active_at.0)
                    .ok()
                    .map(|dt| dt.timestamp_millis())
            };
            match (parse(a), parse(b)) {
                (Some(a_ts), Some(b_ts)) => b_ts.cmp(&a_ts),
                (Some(_), None) => std::cmp::Ordering::Less,
                (None, Some(_)) => std::cmp::Ordering::Greater,
                (None, None) => std::cmp::Ordering::Equal,
            }
        });
        candidates.into_iter().take(limit).collect()
    }

    pub fn restore_archived_tab(&mut self, tab_id: &TabId) {
        let should_restore = self
            .tabs
            .get(tab_id)
            .map(|t| matches!(t.state, TabLifecycleState::Archived { .. }))
            .unwrap_or(false);

        if !should_restore {
            return;
        }

        if let Some(tab) = self.tabs.get_mut(tab_id) {
            tab.state = TabLifecycleState::Active;
            tab.last_active_at = DateTime::now();
        }

        self.reset_active_timestamps(tab_id);

        if let Some(tab) = self.tabs.get(tab_id) {
            (self.on_update)(CoreUpdate::TabLifecycleChanged {
                tab_id: tab_id.clone(),
                state: tab.state.clone(),
            });
        }
    }

    pub fn delete_archived_tab(&mut self, tab_id: &TabId) -> bool {
        let is_archived = self
            .tabs
            .get(tab_id)
            .map(|tab| matches!(tab.state, TabLifecycleState::Archived { .. }))
            .unwrap_or(false);

        if !is_archived {
            return false;
        }

        let removed = self.tabs.remove(tab_id).is_some();
        if removed {
            self.state_timestamps.remove(tab_id);
            self.creation_timestamps.remove(tab_id);
            self.loading_states.remove(tab_id);
            self.security_states.remove(tab_id);
            self.reader_mode_tabs.remove(tab_id);
            (self.on_update)(CoreUpdate::TabClosed {
                tab_id: tab_id.clone(),
                animated: false,
            });
        }

        removed
    }

    pub fn enforce_archive_cap(&mut self) -> Vec<(TabId, SpaceId)> {
        let mut per_space: HashMap<SpaceId, Vec<(TabId, u64)>> = HashMap::new();
        for tab in self.tabs.values() {
            if matches!(tab.state, TabLifecycleState::Archived { .. }) {
                let ts = self.state_timestamps.get(&tab.id).copied().unwrap_or(0);
                per_space
                    .entry(tab.space_id.clone())
                    .or_default()
                    .push((tab.id.clone(), ts));
            }
        }

        let mut evicted = Vec::new();
        for (_space_id, mut entries) in per_space {
            if entries.len() <= MAX_ARCHIVED_PER_SPACE {
                continue;
            }
            entries.sort_by_key(|(_, ts)| *ts);
            let evict_count = entries.len() - MAX_ARCHIVED_PER_SPACE;
            for (tab_id, _) in entries.into_iter().take(evict_count) {
                if let Some(tab) = self.tabs.remove(&tab_id) {
                    self.state_timestamps.remove(&tab_id);
                    self.creation_timestamps.remove(&tab_id);
                    self.loading_states.remove(&tab_id);
                    self.security_states.remove(&tab_id);
                    self.reader_mode_tabs.remove(&tab_id);
                    evicted.push((tab_id, tab.space_id));
                }
            }
        }
        evicted
    }

    pub(crate) fn to_view_model(&self, tab: &Tab) -> TabViewModel {
        let children: Vec<TabId> = self
            .tabs
            .values()
            .filter(|t| t.parent_id.as_ref() == Some(&tab.id))
            .map(|t| t.id.clone())
            .collect();

        TabViewModel {
            id: tab.id.clone(),
            title: tab.title.clone(),
            custom_title: tab.custom_title.clone(),
            custom_icon: tab.custom_icon.clone(),
            pinned_url: tab.pinned_url.as_ref().map(|u| u.0.clone()),
            url: tab.url.0.clone(),
            favicon: tab.favicon.clone(),
            is_loading: self.loading_states.get(&tab.id).copied().unwrap_or(false),
            is_pinned: tab.role.is_pinned(),
            is_favorite: tab.role.is_favorite(), // L3-EXEMPT: views display
            is_muted: tab.is_muted,
            is_playing_audio: false,
            lifecycle_state: tab.state.kind_str().to_string(),
            children,
            space_id: tab.space_id.clone(),
            created_at: tab.created_at.clone(),
            last_active_at: tab.last_active_at.clone(),
            favorite_order: tab.role.favorite_order(),
            role: tab.role.clone(),
            is_private: tab.is_private,
        }
    }

    pub fn update_tab_title(&mut self, tab_id: &TabId, title: String) {
        if let Some(tab) = self.tabs.get_mut(tab_id) {
            tab.title = title;
        }
    }

    /// Sets or clears the user-chosen title. Whitespace-only input clears the
    /// override (mirrors update_tab_custom_icon) so an erased field falls back
    /// to the derived tab title instead of persisting as authored state.
    pub fn update_tab_custom_title(&mut self, tab_id: &TabId, custom_title: Option<String>) {
        let normalized = custom_title.and_then(|title| {
            let trimmed = title.trim().to_string();
            if trimmed.is_empty() {
                None
            } else {
                Some(trimmed)
            }
        });
        if let Some(tab) = self.tabs.get_mut(tab_id) {
            tab.custom_title = normalized;
        }
    }

    /// Sets or clears the user-chosen glyph override. Empty/whitespace input
    /// clears the override so an erased field falls back to the favicon.
    pub fn update_tab_custom_icon(&mut self, tab_id: &TabId, custom_icon: Option<String>) {
        let normalized = custom_icon.and_then(|icon| {
            let trimmed = icon.trim().to_string();
            if trimmed.is_empty() {
                None
            } else {
                Some(trimmed)
            }
        });
        if let Some(tab) = self.tabs.get_mut(tab_id) {
            tab.custom_icon = normalized;
        }
    }

    /// Replaces the tab's home (pinned) URL - the target of the views'
    /// "Replace Pinned URL with Current" / "Edit Pinned Page" actions.
    pub fn set_tab_pinned_url(&mut self, tab_id: &TabId, url: Url) -> bool {
        if let Some(tab) = self.tabs.get_mut(tab_id) {
            tab.pinned_url = Some(url);
            return true;
        }
        false
    }

    pub fn update_tab_url(&mut self, tab_id: &TabId, url: Url) {
        if let Some(tab) = self.tabs.get_mut(tab_id) {
            tab.url = url;
        }
    }

    pub fn update_tab_favicon(
        &mut self,
        tab_id: &TabId,
        favicon: Option<maho_types::common::ImageData>,
    ) {
        if let Some(tab) = self.tabs.get_mut(tab_id) {
            tab.favicon = favicon;
        }
    }

    pub fn update_tab_loading(&mut self, tab_id: &TabId, is_loading: bool) {
        if self.tabs.contains_key(tab_id) {
            self.loading_states.insert(tab_id.clone(), is_loading);
        }
    }

    pub fn is_tab_loading(&self, tab_id: &TabId) -> bool {
        self.loading_states.get(tab_id).copied().unwrap_or(false)
    }

    pub fn update_tab_security(&mut self, tab_id: &TabId, is_secure: bool) {
        if self.tabs.contains_key(tab_id) {
            self.security_states.insert(tab_id.clone(), is_secure);
        }
    }

    pub fn update_tab_scroll_position(&mut self, tab_id: &TabId, x: f64, y: f64) {
        if let Some(tab) = self.tabs.get_mut(tab_id) {
            tab.scroll_position = ScrollPosition { x, y };
        }
    }

    pub fn is_tab_secure(&self, tab_id: &TabId) -> bool {
        self.security_states.get(tab_id).copied().unwrap_or(true)
    }

    pub fn close_other_tabs(&mut self, space_id: &SpaceId, tab_id: &TabId) -> Vec<TabId> {
        let to_close: Vec<TabId> = self
            .tabs
            .values()
            .filter(|t| {
                t.space_id == *space_id
                    && t.id != *tab_id
                    && !matches!(t.state, TabLifecycleState::Archived { .. })
                    && !t.role.is_pinned()
            })
            .map(|t| t.id.clone())
            .collect();

        let mut closed = Vec::new();
        for tab_id in to_close {
            if let Some(tab) = self.tabs.remove(&tab_id) {
                self.state_timestamps.remove(&tab_id);
                self.creation_timestamps.remove(&tab_id);
                self.loading_states.remove(&tab_id);
                self.security_states.remove(&tab_id);
                self.reader_mode_tabs.remove(&tab_id);
                self.push_closed_tab(tab);
                (self.on_update)(CoreUpdate::TabClosed {
                    tab_id: tab_id.clone(),
                    animated: true,
                });
                closed.push(tab_id);
            }
        }
        closed
    }

    pub fn close_tabs_to_right(&mut self, space_id: &SpaceId, tab_id: &TabId) -> Vec<TabId> {
        let reference_ts = match self.creation_timestamps.get(tab_id) {
            Some(&ts) => ts,
            None => return vec![],
        };

        let to_close: Vec<TabId> = self
            .tabs
            .values()
            .filter(|t| {
                t.space_id == *space_id
                    && t.id != *tab_id
                    && !matches!(t.state, TabLifecycleState::Archived { .. })
                    && !t.role.is_pinned()
                    && self.creation_timestamps.get(&t.id).copied().unwrap_or(0) > reference_ts
            })
            .map(|t| t.id.clone())
            .collect();

        let mut closed = Vec::new();
        for tid in to_close {
            if let Some(tab) = self.tabs.remove(&tid) {
                self.state_timestamps.remove(&tid);
                self.creation_timestamps.remove(&tid);
                self.loading_states.remove(&tid);
                self.security_states.remove(&tid);
                self.reader_mode_tabs.remove(&tid);
                self.push_closed_tab(tab);
                (self.on_update)(CoreUpdate::TabClosed {
                    tab_id: tid.clone(),
                    animated: true,
                });
                closed.push(tid);
            }
        }
        closed
    }

    pub fn close_tabs_to_left(&mut self, space_id: &SpaceId, tab_id: &TabId) -> Vec<TabId> {
        let reference_ts = match self.creation_timestamps.get(tab_id) {
            Some(&ts) => ts,
            None => return vec![],
        };

        let to_close: Vec<TabId> = self
            .tabs
            .values()
            .filter(|t| {
                t.space_id == *space_id
                    && t.id != *tab_id
                    && !matches!(t.state, TabLifecycleState::Archived { .. })
                    && !t.role.is_pinned()
                    && self.creation_timestamps.get(&t.id).copied().unwrap_or(0) < reference_ts
            })
            .map(|t| t.id.clone())
            .collect();

        let mut closed = Vec::new();
        for tid in to_close {
            if let Some(tab) = self.tabs.remove(&tid) {
                self.state_timestamps.remove(&tid);
                self.creation_timestamps.remove(&tid);
                self.loading_states.remove(&tid);
                self.security_states.remove(&tid);
                self.reader_mode_tabs.remove(&tid);
                self.push_closed_tab(tab);
                (self.on_update)(CoreUpdate::TabClosed {
                    tab_id: tid.clone(),
                    animated: true,
                });
                closed.push(tid);
            }
        }
        closed
    }

    /// Returns non-pinned, non-archived tab IDs whose `last_active_at` is strictly older
    /// than `cutoff_ts` (epoch seconds) and belong to the active `space_id`. Read-only —
    /// does NOT mutate state. Unparseable `last_active_at` is skipped so newly-created tabs
    /// (no activation yet) are kept.
    pub fn find_tabs_older_than(&self, cutoff_ts: i64, space_id: &SpaceId) -> Vec<TabId> {
        let mut ids: Vec<&TabId> = self
            .tabs
            .iter()
            .filter(|(_, tab)| {
                if tab.space_id != *space_id {
                    return false;
                }
                if tab.role.is_pinned() {
                    return false;
                }
                if matches!(tab.state, TabLifecycleState::Archived { .. }) {
                    return false;
                }
                match chrono::DateTime::parse_from_rfc3339(&tab.last_active_at.0) {
                    Ok(dt) => dt.timestamp() < cutoff_ts,
                    Err(_) => false,
                }
            })
            .map(|(id, _)| id)
            .collect();
        ids.sort_by(|a, b| a.as_ref().cmp(b.as_ref()));
        ids.into_iter().cloned().collect()
    }

    pub fn toggle_freeze(&mut self, tab_id: &TabId) {
        if let Some(tab) = self.tabs.get(tab_id) {
            match tab.state {
                TabLifecycleState::Active => {
                    self.freeze_tab(tab_id);
                }
                TabLifecycleState::Frozen => {
                    self.activate_tab(tab_id);
                }
                _ => {}
            }
        }
    }

    pub fn unfreeze_tab(&mut self, tab_id: &TabId) {
        if let Some(tab) = self.tabs.get(tab_id) {
            if matches!(tab.state, TabLifecycleState::Frozen) {
                self.activate_tab(tab_id);
            }
        }
    }

    pub fn toggle_reader_mode(&mut self, tab_id: &TabId) -> bool {
        if self.reader_mode_tabs.contains(tab_id) {
            self.reader_mode_tabs.remove(tab_id);
            false
        } else if self.tabs.contains_key(tab_id) {
            self.reader_mode_tabs.insert(tab_id.clone());
            true
        } else {
            false
        }
    }

    pub fn is_reader_mode(&self, tab_id: &TabId) -> bool {
        self.reader_mode_tabs.contains(tab_id)
    }

    pub fn exit_reader_mode(&mut self, tab_id: &TabId) {
        self.reader_mode_tabs.remove(tab_id);
    }

    pub fn reset_pinned_tab(&mut self, tab_id: &TabId) -> bool {
        if let Some(tab) = self.tabs.get_mut(tab_id) {
            if let Some(ref pinned_url) = tab.pinned_url.clone() {
                tab.url = pinned_url.clone();
                (self.on_update)(CoreUpdate::TabUpdated {
                    tab_id: tab_id.clone(),
                    changes: TabStateUpdate {
                        url: Some(pinned_url.0.clone()),
                        ..Default::default()
                    },
                });
                (self.on_update)(CoreUpdate::NavigateTab {
                    tab_id: tab_id.clone(),
                    url: pinned_url.clone(),
                });
                return true;
            }
        }
        false
    }

    /// Resets a favorite's live URL back to its authored home (pinned) URL
    /// without navigating. Called when a favorite is put to sleep by a close
    /// action: a closed favorite must reopen at its home page, not at the page
    /// it was left on. No-op for non-favorites and for tabs without a pinned
    /// URL, so pinned-tab close behavior and automatic lifecycle suspension
    /// keep their existing semantics. Emits a TabUpdated update when the URL
    /// actually changes.
    pub fn reset_favorite_url_to_pinned(&mut self, tab_id: &TabId) -> bool {
        let tab = match self.tabs.get_mut(tab_id) {
            Some(tab) => tab,
            None => return false,
        };
        let favorite_role = tab.role.is_favorite(); // L3-EXEMPT: role query
        if !favorite_role {
            return false;
        }
        let pinned_url = match tab.pinned_url.clone() {
            Some(pinned_url) => pinned_url,
            None => return false,
        };
        if tab.url == pinned_url {
            return false;
        }
        tab.url = pinned_url.clone();
        (self.on_update)(CoreUpdate::TabUpdated {
            tab_id: tab_id.clone(),
            changes: TabStateUpdate {
                url: Some(pinned_url.0),
                ..Default::default()
            },
        });
        true
    }

    fn reset_active_timestamps(&mut self, tab_id: &TabId) {
        let now = now_millis();
        self.state_timestamps.insert(tab_id.clone(), now);
        self.creation_timestamps.insert(tab_id.clone(), now);
    }

    pub fn should_open_in_peek(tab: &Tab, new_url: &Url) -> bool {
        if !tab.role.is_pinned() {
            return false;
        }
        let pinned_url = match &tab.pinned_url {
            Some(u) => u,
            None => return false,
        };
        let pinned_domain = extract_registrable_domain(&pinned_url.0);
        let new_domain = extract_registrable_domain(&new_url.0);
        pinned_domain != new_domain
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::RefCell;
    use std::rc::Rc;

    #[test]
    fn favorite_tab_is_idempotent_for_the_same_tab_and_emits_only_one_update() {
        let updates = Rc::new(RefCell::new(Vec::<CoreUpdate>::new()));
        let captured_updates = Rc::clone(&updates);
        let mut manager = TabLifecycleManager::new(Box::new(move |update| {
            captured_updates.borrow_mut().push(update);
        }));

        let space_id = SpaceId::new("space-1");
        let tab = manager.create_tab(
            space_id.clone(),
            Some(Url::new("https://example.com")),
            None,
            None,
            false,
        );
        let tab_id = tab.id.clone();

        manager.favorite_tab(&tab_id);

        let after_first_call = manager.get_tab(&tab_id).unwrap();
        assert!(after_first_call.role.is_favorite()); // L3-EXEMPT: test assert
        assert_eq!(after_first_call.role.favorite_order(), Some(0));
        assert_eq!(manager.get_favorite_tabs(&space_id).len(), 1);

        let first_call_updates = updates.borrow().clone();
        assert_eq!(first_call_updates.len(), 2);
        assert!(matches!(
            first_call_updates[0],
            CoreUpdate::TabCreated { .. }
        ));
        assert!(
            matches!(&first_call_updates[1], CoreUpdate::TabUpdated { tab_id: update_tab_id, changes } if update_tab_id == &tab_id && changes.is_favorite == Some(true) && changes.favorite_order == Some(Some(0)))
        ); // L3-EXEMPT: test assert

        manager.favorite_tab(&tab_id);

        let after_second_call = manager.get_tab(&tab_id).unwrap();
        assert!(after_second_call.role.is_favorite()); // L3-EXEMPT: test assert
        assert_eq!(after_second_call.role.favorite_order(), Some(0));
        assert_eq!(manager.get_favorite_tabs(&space_id).len(), 1);

        let all_updates = updates.borrow();
        assert_eq!(
            all_updates.len(),
            2,
            "the second favorite_tab call should not emit any update"
        );
        assert_eq!(
            all_updates
                .iter()
                .filter(|update| matches!(update, CoreUpdate::TabUpdated { .. }))
                .count(),
            1,
            "favorite_tab should emit exactly one CoreUpdate::TabUpdated for the first call only"
        );
    }

    #[test]
    fn get_favorite_tabs_tie_breaks_equal_order_by_tab_id() {
        let mut manager = TabLifecycleManager::new(Box::new(|_| {}));
        let space_id = SpaceId::new("space-1");
        let tab_b = manager.create_tab_with_id(
            TabId::new("tab-b"),
            space_id.clone(),
            Some(Url::new("https://b.example")),
            None,
            None,
            false,
        );
        let tab_a = manager.create_tab_with_id(
            TabId::new("tab-a"),
            space_id.clone(),
            Some(Url::new("https://a.example")),
            None,
            None,
            false,
        );
        manager.transition_tab_role(&tab_b.id, TabRole::Favorite { order: 0 });
        manager.transition_tab_role(&tab_a.id, TabRole::Favorite { order: 0 });

        let ordered_ids: Vec<TabId> = manager
            .get_favorite_tabs(&space_id)
            .into_iter()
            .map(|tab| tab.id.clone())
            .collect();

        assert_eq!(ordered_ids, vec![tab_a.id, tab_b.id]);
    }

    #[test]
    fn reorder_favorite_writes_contiguous_order_after_tie_break() {
        let mut manager = TabLifecycleManager::new(Box::new(|_| {}));
        let space_id = SpaceId::new("space-1");
        let tab_a = manager.create_tab_with_id(
            TabId::new("tab-a"),
            space_id.clone(),
            Some(Url::new("https://a.example")),
            None,
            None,
            false,
        );
        let tab_b = manager.create_tab_with_id(
            TabId::new("tab-b"),
            space_id.clone(),
            Some(Url::new("https://b.example")),
            None,
            None,
            false,
        );
        let tab_c = manager.create_tab_with_id(
            TabId::new("tab-c"),
            space_id.clone(),
            Some(Url::new("https://c.example")),
            None,
            None,
            false,
        );
        manager.transition_tab_role(&tab_c.id, TabRole::Favorite { order: 0 });
        manager.transition_tab_role(&tab_b.id, TabRole::Favorite { order: 0 });
        manager.transition_tab_role(&tab_a.id, TabRole::Favorite { order: 0 });
        let mut space_ids = HashSet::new();
        space_ids.insert(space_id.clone());

        manager.reorder_favorite(&tab_a.id, 2, &space_ids);

        let favorites = manager.get_favorite_tabs(&space_id);
        let ordered_ids: Vec<TabId> = favorites.iter().map(|tab| tab.id.clone()).collect();
        let orders: Vec<Option<u32>> = favorites
            .iter()
            .map(|tab| tab.role.favorite_order())
            .collect();
        println!("manager happy reorder B/C/A ids: {:?}", ordered_ids);
        println!("manager happy reorder B/C/A orders: {:?}", orders);
        assert_eq!(ordered_ids, vec![tab_b.id, tab_c.id, tab_a.id]);
        assert_eq!(orders, vec![Some(0), Some(1), Some(2)]);
    }

    #[test]
    fn restored_archived_tab_not_re_archived_by_today_tab_timeout() {
        let mut manager = TabLifecycleManager::new(Box::new(|_| {}));
        let space_id = SpaceId::new("space-1");
        let tab = manager.create_tab(
            space_id,
            Some(Url::new("https://example.com")),
            None,
            None,
            false,
        );
        let tab_id = tab.id.clone();

        let old_ts = now_millis() - 24 * 60 * 60 * 1000;
        manager.creation_timestamps.insert(tab_id.clone(), old_ts);

        manager.suspend_tab(&tab_id);
        manager.archive_tab(&tab_id);
        assert!(matches!(
            manager.get_tab(&tab_id).unwrap().state,
            TabLifecycleState::Archived { .. }
        ));

        manager.restore_archived_tab(&tab_id);
        assert!(matches!(
            manager.get_tab(&tab_id).unwrap().state,
            TabLifecycleState::Active
        ));

        manager.tick_with_settings(24.0, 12.0);

        assert!(matches!(
            manager.get_tab(&tab_id).unwrap().state,
            TabLifecycleState::Active
        ));
    }

    #[test]
    fn favorite_tab_exempt_from_frozen_to_suspended() {
        let mut manager = TabLifecycleManager::new(Box::new(|_| {}));
        let space_id = SpaceId::new("space-1");
        let tab = manager.create_tab(
            space_id,
            Some(Url::new("https://example.com")),
            None,
            None,
            false,
        );
        let tab_id = tab.id.clone();

        manager.favorite_tab(&tab_id);

        if let Some(t) = manager.tabs.get_mut(&tab_id) {
            t.state = TabLifecycleState::Frozen;
        }
        let old_ts = now_millis() - (SUSPEND_TIMEOUT_MS + 1000);
        manager.state_timestamps.insert(tab_id.clone(), old_ts);

        manager.tick_with_settings(24.0, 12.0);

        assert!(matches!(
            manager.get_tab(&tab_id).unwrap().state,
            TabLifecycleState::Frozen
        ));
    }

    #[test]
    fn archived_tabs_excluded_from_getters() {
        let mut manager = TabLifecycleManager::new(Box::new(|_| {}));
        let space_id = SpaceId::new("space-1");

        let tab1 = manager.create_tab(
            space_id.clone(),
            Some(Url::new("https://a.com")),
            None,
            None,
            false,
        );
        let tab1_id = tab1.id.clone();
        let tab2 = manager.create_tab(
            space_id.clone(),
            Some(Url::new("https://b.com")),
            None,
            None,
            false,
        );
        let tab2_id = tab2.id.clone();

        manager.favorite_tab(&tab1_id);
        manager.favorite_tab(&tab2_id);

        manager.suspend_tab(&tab1_id);
        manager.archive_tab(&tab1_id);

        let tabs = manager.get_tabs_by_space(&space_id);
        assert_eq!(tabs.len(), 1);
        assert_eq!(tabs[0].id, tab2_id);

        let favs = manager.get_favorite_tabs(&space_id);
        assert_eq!(favs.len(), 1);
        assert_eq!(favs[0].id, tab2_id);

        let mut space_ids = HashSet::new();
        space_ids.insert(space_id.clone());
        assert_eq!(manager.count_favorite_tabs_in_spaces(&space_ids), 1);
    }

    #[test]
    fn reopened_tab_not_re_archived_by_tick() {
        let mut manager = TabLifecycleManager::new(Box::new(|_| {}));
        let space_id = SpaceId::new("space-1");
        let tab = manager.create_tab(
            space_id,
            Some(Url::new("https://example.com")),
            None,
            None,
            false,
        );
        let tab_id = tab.id.clone();

        // Backdate creation timestamp so tab appears old
        let old_ts = now_millis() - 48 * 60 * 60 * 1000;
        manager.creation_timestamps.insert(tab_id.clone(), old_ts);

        // Close and reopen
        manager.close_tab(&tab_id);
        let reopened = manager.reopen_last_closed().unwrap();
        assert!(matches!(reopened.state, TabLifecycleState::Active));

        // Tick should NOT re-archive — timestamps were reset on reopen
        manager.tick_with_settings(24.0, 12.0);

        let tab_after = manager.get_tab(&reopened.id).unwrap();
        assert!(
            matches!(tab_after.state, TabLifecycleState::Active),
            "reopened tab must stay Active after tick, got {:?}",
            tab_after.state
        );
    }

    #[test]
    fn test_update_tab_url() {
        let mut manager = TabLifecycleManager::new(Box::new(|_| {}));
        let space_id = SpaceId::new("space-1");
        let tab = manager.create_tab(
            space_id,
            Some(Url::new("https://example.com")),
            None,
            None,
            false,
        );
        let tab_id = tab.id.clone();

        assert_eq!(
            manager.get_tab(&tab_id).unwrap().url.0.as_str(),
            "https://example.com"
        );

        manager.update_tab_url(&tab_id, Url::new("https://maho.dev"));
        assert_eq!(
            manager.get_tab(&tab_id).unwrap().url.0.as_str(),
            "https://maho.dev"
        );
    }

    #[test]
    fn collect_child_ids_terminates_on_cycle() {
        let mut manager = TabLifecycleManager::new(Box::new(|_| {}));
        let space_id = SpaceId::new("space-1");
        let tab1 = manager.create_tab(
            space_id.clone(),
            Some(Url::new("https://a.com")),
            None,
            None,
            false,
        );
        let tab1_id = tab1.id.clone();
        let tab2 = manager.create_tab(
            space_id.clone(),
            Some(Url::new("https://b.com")),
            None,
            None,
            false,
        );
        let tab2_id = tab2.id.clone();

        if let Some(t1) = manager.tabs.get_mut(&tab1_id) {
            t1.parent_id = Some(tab2_id.clone());
        }
        if let Some(t2) = manager.tabs.get_mut(&tab2_id) {
            t2.parent_id = Some(tab1_id.clone());
        }

        let children = manager.collect_child_ids(&tab1_id);
        assert!(children.len() <= 2);
    }

    #[test]
    fn find_tabs_older_than_skips_pinned() {
        let mut manager = TabLifecycleManager::new(Box::new(|_| {}));
        let space_id = SpaceId::new("space-1");
        let tab = manager.create_tab(
            space_id.clone(),
            Some(Url::new("https://example.com")),
            None,
            None,
            false,
        );
        let tab_id = tab.id.clone();

        manager.pin_tab(&tab_id);
        if let Some(t) = manager.tabs.get_mut(&tab_id) {
            t.last_active_at = DateTime::from_iso("2020-01-01T00:00:00.000Z");
        }

        let found = manager.find_tabs_older_than(1_700_000_000, &space_id);
        assert_eq!(found.len(), 0);
    }

    #[test]
    fn find_tabs_older_than_respects_cutoff() {
        let mut manager = TabLifecycleManager::new(Box::new(|_| {}));
        let space_id = SpaceId::new("space-1");

        let old_tab = manager.create_tab(
            space_id.clone(),
            Some(Url::new("https://old.com")),
            None,
            None,
            false,
        );
        let old_id = old_tab.id.clone();
        if let Some(t) = manager.tabs.get_mut(&old_id) {
            t.last_active_at = DateTime::from_iso("2023-01-01T00:00:00.000Z");
        }

        let recent_tab = manager.create_tab(
            space_id.clone(),
            Some(Url::new("https://recent.com")),
            None,
            None,
            false,
        );
        let recent_id = recent_tab.id.clone();
        if let Some(t) = manager.tabs.get_mut(&recent_id) {
            t.last_active_at = DateTime::from_iso("2024-06-01T00:00:00.000Z");
        }

        let found = manager.find_tabs_older_than(1_700_000_000, &space_id);
        assert_eq!(found.len(), 1);
        assert_eq!(found[0], old_id);
    }

    #[test]
    fn find_tabs_older_than_filters_by_space() {
        let mut manager = TabLifecycleManager::new(Box::new(|_| {}));
        let space_a = SpaceId::new("space-a");
        let space_b = SpaceId::new("space-b");

        let tab_a = manager.create_tab(
            space_a.clone(),
            Some(Url::new("https://a.com")),
            None,
            None,
            false,
        );
        let id_a = tab_a.id.clone();
        if let Some(t) = manager.tabs.get_mut(&id_a) {
            t.last_active_at = DateTime::from_iso("2020-01-01T00:00:00.000Z");
        }

        let tab_b = manager.create_tab(
            space_b.clone(),
            Some(Url::new("https://b.com")),
            None,
            None,
            false,
        );
        let id_b = tab_b.id.clone();
        if let Some(t) = manager.tabs.get_mut(&id_b) {
            t.last_active_at = DateTime::from_iso("2020-01-01T00:00:00.000Z");
        }

        let found_a = manager.find_tabs_older_than(1_700_000_000, &space_a);
        assert_eq!(found_a, vec![id_a]);

        let found_b = manager.find_tabs_older_than(1_700_000_000, &space_b);
        assert_eq!(found_b, vec![id_b]);
    }

    #[test]
    fn find_tabs_older_than_empty_returns_zero() {
        let manager = TabLifecycleManager::new(Box::new(|_| {}));
        let found = manager.find_tabs_older_than(1_700_000_000, &SpaceId::new("space-1"));
        assert_eq!(found.len(), 0);
    }

    #[test]
    fn find_tabs_older_than_integration_activate() {
        let mut manager = TabLifecycleManager::new(Box::new(|_| {}));
        let space_id = SpaceId::new("space-1");

        let tab = manager.create_tab(
            space_id.clone(),
            Some(Url::new("https://example.com")),
            None,
            None,
            false,
        );
        let tab_id = tab.id.clone();

        // 1. Initial tab activation updates last_active_at to now.
        manager.activate_tab(&tab_id);

        // 2. Querying tabs older than 1 hour ago should NOT return the tab.
        let cutoff = chrono::Utc::now().timestamp() - 3600;
        let found = manager.find_tabs_older_than(cutoff, &space_id);
        assert_eq!(found.len(), 0);
    }

    #[test]
    fn find_tabs_older_than_includes_suspended_sidebar_tabs() {
        let mut manager = TabLifecycleManager::new(Box::new(|_| {}));
        let space_id = SpaceId::new("space-1");

        let tab = manager.create_tab(
            space_id.clone(),
            Some(Url::new("https://example.com")),
            None,
            None,
            false,
        );
        let tab_id = tab.id.clone();

        manager.suspend_tab(&tab_id);

        if let Some(t) = manager.tabs.get_mut(&tab_id) {
            t.last_active_at = DateTime::from_iso("2020-01-01T00:00:00.000Z");
        }

        let found = manager.find_tabs_older_than(1_700_000_000, &space_id);
        assert_eq!(found, vec![tab_id.clone()]);

        // Clear must preserve the same pinned-tab protection after a tab
        // loses its live WebContents.
        manager.pin_tab(&tab_id);
        assert!(manager
            .find_tabs_older_than(1_700_000_000, &space_id)
            .is_empty());
    }

    #[test]
    fn find_tabs_older_than_includes_frozen() {
        let mut manager = TabLifecycleManager::new(Box::new(|_| {}));
        let space_id = SpaceId::new("space-1");

        let tab = manager.create_tab(
            space_id.clone(),
            Some(Url::new("https://example.com")),
            None,
            None,
            false,
        );
        let tab_id = tab.id.clone();

        manager.freeze_tab(&tab_id);

        if let Some(t) = manager.tabs.get_mut(&tab_id) {
            t.last_active_at = DateTime::from_iso("2020-01-01T00:00:00.000Z");
        }

        let found = manager.find_tabs_older_than(1_700_000_000, &space_id);
        assert_eq!(found.len(), 1);
        assert_eq!(found[0], tab_id);
    }

    #[test]
    fn restore_to_active_preserves_last_active_at() {
        let mut manager = TabLifecycleManager::new(Box::new(|_| {}));
        let space_id = SpaceId::new("space-1");

        let tab = manager.create_tab(
            space_id.clone(),
            Some(Url::new("https://example.com")),
            None,
            None,
            false,
        );
        let tab_id = tab.id.clone();

        manager.freeze_tab(&tab_id);
        let old_stamp = "2020-01-01T00:00:00.000Z";
        if let Some(t) = manager.tabs.get_mut(&tab_id) {
            t.last_active_at = DateTime::from_iso(old_stamp);
        }

        manager.restore_to_active(&tab_id);

        let t = manager.get_tab(&tab_id).unwrap();
        assert!(matches!(t.state, TabLifecycleState::Active));
        assert_eq!(t.last_active_at.0, old_stamp);
    }

    #[test]
    fn activate_tab_bumps_last_active_at() {
        let mut manager = TabLifecycleManager::new(Box::new(|_| {}));
        let space_id = SpaceId::new("space-1");

        let tab = manager.create_tab(
            space_id.clone(),
            Some(Url::new("https://example.com")),
            None,
            None,
            false,
        );
        let tab_id = tab.id.clone();

        manager.freeze_tab(&tab_id);
        let old_stamp = "2020-01-01T00:00:00.000Z";
        if let Some(t) = manager.tabs.get_mut(&tab_id) {
            t.last_active_at = DateTime::from_iso(old_stamp);
        }

        manager.activate_tab(&tab_id);

        let t = manager.get_tab(&tab_id).unwrap();
        assert!(matches!(t.state, TabLifecycleState::Active));
        assert_ne!(t.last_active_at.0, old_stamp);
    }

    #[test]
    fn get_recent_tabs_orders_by_recency_excludes_archived_and_caps() {
        let mut manager = TabLifecycleManager::new(Box::new(|_| {}));
        let space_id = SpaceId::new("space-1");

        let old = manager.create_tab(
            space_id.clone(),
            Some(Url::new("https://old.com")),
            None,
            None,
            false,
        );
        let mid = manager.create_tab(
            space_id.clone(),
            Some(Url::new("https://mid.com")),
            None,
            None,
            false,
        );
        let newest = manager.create_tab(
            space_id.clone(),
            Some(Url::new("https://new.com")),
            None,
            None,
            false,
        );
        let archived = manager.create_tab(
            space_id.clone(),
            Some(Url::new("https://archived.com")),
            None,
            None,
            false,
        );

        let old_id = old.id.clone();
        let mid_id = mid.id.clone();
        let new_id = newest.id.clone();
        let archived_id = archived.id.clone();

        if let Some(t) = manager.tabs.get_mut(&old_id) {
            t.last_active_at = DateTime::from_iso("2020-01-01T00:00:00.000Z");
        }
        if let Some(t) = manager.tabs.get_mut(&mid_id) {
            t.last_active_at = DateTime::from_iso("2023-01-01T00:00:00.000Z");
        }
        if let Some(t) = manager.tabs.get_mut(&new_id) {
            t.last_active_at = DateTime::from_iso("2024-06-01T00:00:00.000Z");
        }
        if let Some(t) = manager.tabs.get_mut(&archived_id) {
            t.last_active_at = DateTime::from_iso("2025-01-01T00:00:00.000Z");
            t.state = TabLifecycleState::Archived {
                metadata_only: false,
            };
        }

        let recent = manager.get_recent_tabs(10);
        let ids: Vec<TabId> = recent.iter().map(|t| t.id.clone()).collect();
        assert_eq!(ids, vec![new_id.clone(), mid_id, old_id]);
        assert!(!ids.contains(&archived_id));

        let capped = manager.get_recent_tabs(1);
        assert_eq!(capped.len(), 1);
        assert_eq!(capped[0].id, new_id);
    }

    #[test]
    fn get_recent_tabs_deprioritizes_unparseable_timestamps() {
        let mut manager = TabLifecycleManager::new(Box::new(|_| {}));
        let space_id = SpaceId::new("space-1");

        let good = manager.create_tab(
            space_id.clone(),
            Some(Url::new("https://good.com")),
            None,
            None,
            false,
        );
        let bad = manager.create_tab(
            space_id.clone(),
            Some(Url::new("https://bad.com")),
            None,
            None,
            false,
        );
        let good_id = good.id.clone();
        let bad_id = bad.id.clone();

        if let Some(t) = manager.tabs.get_mut(&good_id) {
            t.last_active_at = DateTime::from_iso("2024-06-01T00:00:00.000Z");
        }
        if let Some(t) = manager.tabs.get_mut(&bad_id) {
            t.last_active_at = DateTime::from_iso("not-a-timestamp");
        }

        let recent = manager.get_recent_tabs(10);
        let ids: Vec<TabId> = recent.iter().map(|t| t.id.clone()).collect();
        assert_eq!(ids, vec![good_id, bad_id]);
    }

    #[test]
    fn duplicate_tab_keeps_the_private_flag_of_the_original() {
        let mut manager = TabLifecycleManager::new(Box::new(|_| {}));
        let space_id = SpaceId::new("space-private");

        let private_tab = manager.create_tab(
            space_id.clone(),
            Some(Url::new("https://private.example.com")),
            None,
            None,
            true,
        );
        assert!(private_tab.is_private);

        let duplicated = manager
            .duplicate_tab(&private_tab.id)
            .expect("duplicating an existing tab returns the new tab");

        // A duplicate that drops is_private turns a private URL into ordinary
        // persisted state: push_closed_tab, saved state, and snapshot export
        // all filter on this flag, so losing it leaks the private URL.
        assert!(duplicated.is_private);
        assert_eq!(duplicated.url, private_tab.url);

        let normal_tab = manager.create_tab(
            space_id,
            Some(Url::new("https://normal.example.com")),
            None,
            None,
            false,
        );
        let normal_duplicate = manager
            .duplicate_tab(&normal_tab.id)
            .expect("duplicating an existing tab returns the new tab");
        assert!(!normal_duplicate.is_private);
    }
}
