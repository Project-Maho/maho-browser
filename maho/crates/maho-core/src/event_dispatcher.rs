use maho_types::chat::ChatRequestContext;
use maho_types::common::{MemoryPressureLevel, Url};
use maho_types::events::core_update::{CoreUpdate, FolderUpdate, MemoryAction};
use maho_types::events::shell_event::{QuickAction, ShellEvent};
use maho_types::identifiers::{FolderId, NoteId, ProfileId, SpaceId, TabId};
use maho_types::settings::{NewTabPosition, Settings, SettingsUpdate};
use maho_types::space::{Space, SpaceColor};
use maho_types::tab::{Tab, TabRole};
use maho_types::traits::shell_renderer::{
    SearchContext, SpaceViewModel, SuggestionViewModel, TabStateUpdate, TabViewModel,
};

use crate::space_manager::FolderError;

pub trait TabManagerInterface {
    fn create_tab(
        &mut self,
        space_id: SpaceId,
        url: Option<Url>,
        parent_id: Option<TabId>,
        window_id: Option<i64>,
        is_private: bool,
    ) -> Tab;
    fn create_tab_with_id(
        &mut self,
        tab_id: TabId,
        space_id: SpaceId,
        url: Option<Url>,
        parent_id: Option<TabId>,
        window_id: Option<i64>,
        is_private: bool,
    ) -> Tab;
    fn close_tab(&mut self, tab_id: &TabId) -> Option<Tab>;
    fn activate_tab(&mut self, tab_id: &TabId);
    fn duplicate_tab(&mut self, tab_id: &TabId) -> Option<Tab>;
    fn move_tab(
        &mut self,
        tab_id: &TabId,
        target_space: SpaceId,
        position: usize,
    ) -> Option<SpaceId>;
    fn set_tab_parent(&mut self, tab_id: &TabId, new_parent_id: Option<TabId>) -> bool;
    fn pin_tab(&mut self, tab_id: &TabId);
    fn unpin_tab(&mut self, tab_id: &TabId);
    fn favorite_tab(&mut self, tab_id: &TabId);
    fn transition_tab_role(&mut self, tab_id: &TabId, new_role: TabRole) -> bool;
    fn count_favorite_tabs_in_spaces(
        &self,
        space_ids: &std::collections::HashSet<SpaceId>,
    ) -> usize;
    fn reorder_favorite(
        &mut self,
        tab_id: &TabId,
        new_index: usize,
        space_ids: &std::collections::HashSet<SpaceId>,
    );
    fn mute_tab(&mut self, tab_id: &TabId);
    fn unmute_tab(&mut self, tab_id: &TabId);
    fn freeze_tab(&mut self, tab_id: &TabId);
    fn suspend_tab(&mut self, tab_id: &TabId);
    fn archive_tab(&mut self, tab_id: &TabId);
    fn restore_to_active(&mut self, tab_id: &TabId);
    fn reopen_last_closed(&mut self) -> Option<Tab>;
    fn handle_memory_pressure(&mut self, level: MemoryPressureLevel) -> MemoryAction;
    fn update_tab_title(&mut self, tab_id: &TabId, title: String);
    fn update_tab_custom_title(&mut self, tab_id: &TabId, custom_title: Option<String>);
    fn update_tab_custom_icon(&mut self, tab_id: &TabId, custom_icon: Option<String>);
    fn set_tab_pinned_url(&mut self, tab_id: &TabId, url: Url) -> bool;
    fn reset_favorite_url_to_pinned(&mut self, tab_id: &TabId) -> bool;
    fn update_tab_url(&mut self, tab_id: &TabId, url: Url);
    fn update_tab_loading(&mut self, tab_id: &TabId, is_loading: bool);
    fn update_tab_favicon(
        &mut self,
        tab_id: &TabId,
        favicon: Option<maho_types::common::ImageData>,
    );
    fn restore_archived_tab(&mut self, tab_id: &TabId);
    fn delete_archived_tab(&mut self, tab_id: &TabId) -> bool;
    fn get_tab(&self, tab_id: &TabId) -> Option<&maho_types::tab::Tab>;
    fn to_tab_view_model(&self, tab: &Tab) -> TabViewModel;
    fn is_tab_loading(&self, tab_id: &TabId) -> bool;
    fn update_tab_security(&mut self, tab_id: &TabId, is_secure: bool);
    fn update_tab_scroll_position(&mut self, tab_id: &TabId, x: f64, y: f64);
    fn is_tab_secure(&self, tab_id: &TabId) -> bool;
    fn close_other_tabs(&mut self, space_id: &SpaceId, tab_id: &TabId) -> Vec<TabId>;
    fn close_tabs_to_right(&mut self, space_id: &SpaceId, tab_id: &TabId) -> Vec<TabId>;
    fn close_tabs_to_left(&mut self, space_id: &SpaceId, tab_id: &TabId) -> Vec<TabId>;
    fn toggle_freeze(&mut self, tab_id: &TabId);
    fn release_window_tabs(&mut self, window_id: i64) -> Vec<TabId>;
    fn collect_child_ids(&self, parent_id: &TabId) -> Vec<TabId>;
}

pub trait SpaceManagerInterface {
    fn create_space(&mut self, name: &str, color: SpaceColor, profile_id: ProfileId) -> Space;
    fn delete_space(&mut self, space_id: &SpaceId) -> Option<SpaceId>;
    fn activate_space(&mut self, space_id: &SpaceId);
    fn update_space_config(
        &mut self,
        changes: maho_types::space::SpaceConfigUpdate,
    ) -> Result<Option<maho_types::space::SpaceConfigUpdate>, crate::space_manager::SpaceUpdateError>;
    fn reorder_space(&mut self, space_id: &SpaceId, from: usize, to: usize);
    fn add_tab_to_space(&mut self, space_id: &SpaceId, tab_id: TabId, position: Option<usize>);
    fn remove_tab_from_space(&mut self, space_id: &SpaceId, tab_id: &TabId);
    fn reorder_tab(&mut self, space_id: &SpaceId, tab_id: &TabId, before_tab_id: Option<&TabId>);
    fn move_tab_to_root(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        tab_id: &TabId,
        before_tab_id: Option<&TabId>,
    );
    fn reorder_folder(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        parent_folder_id: Option<&FolderId>,
        before_folder_id: Option<&FolderId>,
    ) -> Result<(), FolderError>;
    fn move_folder_to_root(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        before_folder_id: Option<&FolderId>,
    );
    fn get_active_space_id(&self) -> &SpaceId;
    fn get_space_order(&self) -> &[SpaceId];
    fn get_space(&self, space_id: &SpaceId) -> Option<&maho_types::space::Space>;
    fn get_space_view_model(&self, space_id: &SpaceId) -> Option<SpaceViewModel>;
    fn get_space_ids_for_profile(
        &self,
        profile_id: &ProfileId,
    ) -> std::collections::HashSet<SpaceId>;
    fn create_folder(
        &mut self,
        space_id: &SpaceId,
        name: &str,
        is_pinned: bool,
        parent_folder_id: Option<FolderId>,
    ) -> Option<maho_types::traits::shell_renderer::FolderViewModel>;
    fn create_folder_with_provider(
        &mut self,
        space_id: &SpaceId,
        name: &str,
        is_pinned: bool,
        parent_folder_id: Option<FolderId>,
        provider_type: Option<String>,
        config_json: Option<String>,
    ) -> Option<maho_types::traits::shell_renderer::FolderViewModel>;
    fn rename_folder(&mut self, space_id: &SpaceId, folder_id: &FolderId, name: &str);
    fn delete_folder(&mut self, space_id: &SpaceId, folder_id: &FolderId);
    fn add_tab_to_folder(&mut self, space_id: &SpaceId, folder_id: &FolderId, tab_id: TabId);
    fn reorder_tab_in_folder(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        tab_id: &TabId,
        to: usize,
    );
    fn remove_tab_from_folder(&mut self, space_id: &SpaceId, folder_id: &FolderId, tab_id: &TabId);
    fn remove_empty_folders_in_space(&mut self, space_id: &SpaceId) -> Vec<FolderId>;
    fn toggle_folder_expanded(&mut self, space_id: &SpaceId, folder_id: &FolderId) -> bool;
    fn find_folder_expanded(&self, space_id: &SpaceId, folder_id: &FolderId) -> Option<bool>;
    fn set_folder_pinned(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        is_pinned: bool,
    ) -> bool;
    fn move_folder(
        &mut self,
        folder_id: &FolderId,
        new_parent: Option<FolderId>,
    ) -> Result<(), FolderError>;
    fn move_folder_in_space(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        new_parent: Option<FolderId>,
    ) -> Result<(), FolderError>;
    fn move_folder_to_space(
        &mut self,
        folder_id: &FolderId,
        source_space_id: &SpaceId,
        target_space_id: &SpaceId,
    );
    fn get_folder_view_models(
        &self,
        space_id: &SpaceId,
    ) -> Vec<maho_types::traits::shell_renderer::FolderViewModel>;
    fn reorder_root_item(
        &mut self,
        space_id: &SpaceId,
        item: &maho_types::space::RootItem,
        insertion_point: &maho_types::space::RootInsertionPoint,
    );
    fn set_last_active_tab(&mut self, space_id: &SpaceId, tab_id: Option<TabId>);
    fn get_last_active_tab(&self, space_id: &SpaceId) -> Option<&TabId>;
    fn is_tab_in_folder(&self, space_id: &SpaceId, tab_id: &TabId) -> bool;
    fn apply_tab_residency(
        &mut self,
        space_id: &SpaceId,
        tab_id: &TabId,
        is_favorite: bool, // L3-EXEMPT: local parameter
        is_in_folder: bool,
    );
    fn remove_tab_from_root_order(&mut self, space_id: &SpaceId, tab_id: &TabId);
}

pub trait CommandBarInterface {
    fn search(
        &self,
        query: &str,
        mode: Option<&str>,
        ctx: &SearchContext,
    ) -> Vec<SuggestionViewModel>;
}

pub trait NoteManagerInterface {
    fn create_note(&mut self, linked_tab: Option<TabId>, content: String)
        -> maho_types::note::Note;
    fn update_note(&mut self, note_id: &NoteId, content: String);
    fn delete_note(&mut self, note_id: &NoteId) -> Option<maho_types::note::Note>;
}

pub trait SettingsInterface {
    fn update_settings(&mut self, changes: SettingsUpdate) -> Settings;
    /// Where newly created tabs are inserted in the sidebar's normal section.
    fn new_tab_position(&self) -> NewTabPosition;
}

pub trait ContentBlockerInterface {
    fn set_enabled(&mut self, enabled: bool);
    fn is_enabled(&self) -> bool;
    fn mode(&self) -> maho_types::content_blocking::ContentBlockingMode;
    fn set_mode(&mut self, mode: maho_types::content_blocking::ContentBlockingMode);
    fn set_popup_blocking(&mut self, enabled: bool);
    fn is_popup_blocking_enabled(&self) -> bool;
    fn add_filter_list(&mut self, id: String, name: String, url: String);
    fn remove_filter_list(&mut self, id: &str);
    fn toggle_filter_list(&mut self, id: &str, enabled: bool);
    fn add_site_exception(&mut self, _domain: &str) {}
    fn remove_site_exception(&mut self, _domain: &str) {}
}

pub trait LLMManagerInterface {
    fn handle_llm_result(&mut self, request_id: &str, result: &str) -> Vec<CoreUpdate>;
    fn handle_llm_error(&mut self, request_id: &str, error: &str) -> Vec<CoreUpdate>;
    fn request_tidy_title(&mut self, tab_id: &TabId, title: &str, url: &str) -> CoreUpdate;
    fn request_tidy_download(
        &mut self,
        download_id: &str,
        filename: &str,
        url: &str,
        page_title: &str,
    ) -> CoreUpdate;
    fn request_page_preview(&mut self, url: &str) -> Option<CoreUpdate>;
    fn request_tidy_tabs(
        &mut self,
        space_id: &SpaceId,
        tabs: Vec<(TabId, String, String)>,
    ) -> CoreUpdate;
    fn take_pending_tidy_tabs(
        &mut self,
    ) -> Option<(SpaceId, Vec<maho_types::events::core_update::TidyTabFolder>)>;
    fn request_chat_completion(
        &mut self,
        message: &str,
        context: &ChatRequestContext,
    ) -> CoreUpdate;
}

pub trait NotificationManagerInterface {
    fn dismiss(&mut self, notification_id: &str) -> bool;
    fn dismiss_all(&mut self);
    fn handle_action(&mut self, notification_id: &str, action_id: &str) -> bool;
    fn set_filter(&mut self, origin: String, allowed: bool);
    fn get_all(&self) -> Vec<maho_types::traits::shell_renderer::NotificationViewModel>;
}

pub struct EventDispatcher {
    sidebar_visible: bool,
    sidebar_width: f64,
}

impl Default for EventDispatcher {
    fn default() -> Self {
        Self::new()
    }
}

impl EventDispatcher {
    pub fn new() -> Self {
        Self {
            sidebar_visible: true,
            sidebar_width: 260.0,
        }
    }

    pub fn notify_listeners(&mut self, _update: CoreUpdate) {}

    #[allow(clippy::too_many_arguments)]
    pub fn dispatch(
        &mut self,
        event: ShellEvent,
        tab_mgr: &mut dyn TabManagerInterface,
        space_mgr: &mut dyn SpaceManagerInterface,
        cmd_bar: &dyn CommandBarInterface,
        _note_mgr: &mut dyn NoteManagerInterface,
        settings_mgr: &mut dyn SettingsInterface,
        _content_blocker: &mut dyn ContentBlockerInterface,
        notification_mgr: &mut dyn NotificationManagerInterface,
        llm_mgr: &mut dyn LLMManagerInterface,
        ctx: &SearchContext,
    ) -> Vec<CoreUpdate> {
        match event {
            // Navigation
            ShellEvent::NavigateTo { tab_id, url } => vec![CoreUpdate::NavigateTab {
                tab_id,
                url,
            }],
            ShellEvent::GoBack { .. } => vec![],
            ShellEvent::GoForward { .. } => vec![],
            ShellEvent::Reload { .. } => vec![],
            ShellEvent::Stop { .. } => vec![],
            ShellEvent::ClearSplitView { .. }
            | ShellEvent::CreateSplit { .. }
            | ShellEvent::RemoveSplit { .. }
            | ShellEvent::ResizeSplit { .. } => {
                unreachable!("Split view events are handled by MahoCore::handle_event before dispatch; this branch must never be reached")
            }

            // Tab management
            ShellEvent::CreateTab {
                space_id,
                url,
                parent_id,
                tab_id,
                window_id,
                is_private,
            } => {
                // In the Chromium-facing path every tab must have an explicit URL so the
                // browser knows which page to load. Substitute None with the canonical NTP
                // URL rather than letting about:blank silently create a ghost/url-less tab.
                let resolved_url = Some(url.unwrap_or_else(|| Url::new("chrome://newtab")));
                let tab_id_clone = tab_id.clone();
                let mut is_reannounce = false;
                let tab = if let Some(explicit_id) = tab_id {
                    // Re-announce of an existing tab: if the shell asks for a different
                    // space than the tab currently lives in, treat it as a move.
                    // Otherwise create_tab_with_id silently returns the existing tab and
                    // add_tab_to_space below would leak the same tab_id into two spaces.
                    if let Some(existing) = tab_mgr.get_tab(&explicit_id).cloned() {
                        is_reannounce = true;
                        if existing.space_id != space_id {
                            let source = existing.space_id.clone();
                            tab_mgr.move_tab(&explicit_id, space_id.clone(), 0);
                            space_mgr.remove_tab_from_space(&source, &explicit_id);
                        }
                        // Re-announce always means a new WebContents exists for this tab,
                        // so reset lifecycle to Active. Otherwise a Suspended ghost waking
                        // up via WakeSuspendedTab → Navigate would stay marked Suspended,
                        // making the next close_tab event hit the 2nd-close (delete) path
                        // instead of the 1st-close (suspend) path.
                        if !matches!(existing.state, maho_types::tab::TabLifecycleState::Active) {
                            tab_mgr.restore_to_active(&explicit_id);
                        }
                        existing
                    } else {
                        // On session-restore path this branch should now be dead for
                        // tabs that existed before the restart. Reaching it means the
                        // save-side (Phase 2) or restore-side (Phase 3) hook is broken.
                        // Log so we detect regressions in the field.
                        eprintln!(
                            "[maho] CreateTab with unknown explicit_id={} — session-restore \
                             propagation may have failed",
                            explicit_id
                        );
                        tab_mgr.create_tab_with_id(explicit_id, space_id.clone(), resolved_url, parent_id, window_id, is_private)
                    }
                } else {
                    tab_mgr.create_tab(space_id.clone(), resolved_url, parent_id, window_id, is_private)
                };
                // New tabs (freshly created, even with a C++-assigned id) open at the
                // position the user chose in Settings: TOP (index 0, the historical
                // default) or BOTTOM (append, expressed as None). Re-announce of an
                // already-existing tab (session wake / move) always keeps None so its
                // position is not reshuffled.
                let insert_position = if is_reannounce {
                    None
                } else {
                    match settings_mgr.new_tab_position() {
                        NewTabPosition::Top => Some(0),
                        NewTabPosition::Bottom => None,
                    }
                };
                space_mgr.add_tab_to_space(&space_id, tab.id.clone(), insert_position);
                if let Some(explicit_id) = tab_id_clone {
                    let is_in_folder = space_mgr.is_tab_in_folder(&space_id, &explicit_id);
                    space_mgr.apply_tab_residency(&space_id, &explicit_id, tab.role.is_favorite(), is_in_folder); // L3-EXEMPT: role query
                }
                space_mgr.set_last_active_tab(&space_id, Some(tab.id.clone()));
                vec![CoreUpdate::TabCreated {
                    tab: tab_mgr.to_tab_view_model(&tab),
                }]
            }
            ShellEvent::CloseTab { tab_id, expected_space_id } => {
                if let Some(expected_sid) = expected_space_id {
                    if let Some(tab) = tab_mgr.get_tab(&tab_id) {
                        debug_assert_eq!(
                            tab.space_id, expected_sid,
                            "CloseTab expected_space_id mismatch: tab has space {}, expected {}",
                            tab.space_id, expected_sid
                        );
                        if tab.space_id != expected_sid {
                            eprintln!(
                                "CloseTab ignored due to expected_space_id mismatch (tab has space {}, expected {})",
                                tab.space_id, expected_sid
                            );
                            return vec![];
                        }
                    }
                }
                // Capture descendants and their owning spaces BEFORE close_tab
                // removes them from tab_manager — otherwise we lose the
                // (child_id, space_id) mapping needed to clean their tab_order
                // entries in every space they may live in.
                let descendant_spaces: Vec<(SpaceId, TabId)> = tab_mgr
                    .collect_child_ids(&tab_id)
                    .into_iter()
                    .filter_map(|cid| {
                        tab_mgr.get_tab(&cid).map(|t| (t.space_id.clone(), cid))
                    })
                    .collect();
                if let Some(tab) = tab_mgr.close_tab(&tab_id) {
                    for (sid, cid) in &descendant_spaces {
                        space_mgr.remove_tab_from_space(sid, cid);
                        if space_mgr.get_last_active_tab(sid) == Some(cid) {
                            space_mgr.set_last_active_tab(sid, None);
                        }
                    }
                    space_mgr.remove_tab_from_space(&tab.space_id, &tab.id);
                    if space_mgr.get_last_active_tab(&tab.space_id) == Some(&tab.id) {
                        space_mgr.set_last_active_tab(&tab.space_id, None);
                    }
                    vec![CoreUpdate::TabClosed {
                        tab_id,
                        animated: true,
                    }]
                } else {
                    vec![]
                }
            }
            ShellEvent::ChatRequestModeChanged { .. } => vec![],
            ShellEvent::ActivateTab { tab_id } => {
                if let Some(tab) = tab_mgr.get_tab(&tab_id) {
                    let sid = tab.space_id.clone();
                    space_mgr.set_last_active_tab(&sid, Some(tab_id.clone()));
                }
                tab_mgr.activate_tab(&tab_id);
                vec![]
            }
            ShellEvent::DuplicateTab { tab_id } => {
                if let Some(tab) = tab_mgr.duplicate_tab(&tab_id) {
                    let tab_order_position = space_mgr.get_space(&tab.space_id).and_then(|space| {
                        space
                            .tab_order
                            .iter()
                            .position(|existing_tab_id| existing_tab_id == &tab_id)
                    });
                    space_mgr.add_tab_to_space(&tab.space_id, tab.id.clone(), tab_order_position.map(|pos| pos + 1));

                    let folder_insertion = space_mgr.get_space(&tab.space_id).and_then(|space| {
                        space.folders.iter().find_map(|folder| {
                            folder
                                .tab_ids
                                .iter()
                                .position(|existing_tab_id| existing_tab_id == &tab_id)
                                .map(|position| (folder.id.clone(), position + 1))
                        })
                    });

                    if let Some((folder_id, insert_at)) = folder_insertion {
                        space_mgr.add_tab_to_folder(&tab.space_id, &folder_id, tab.id.clone());
                        space_mgr.reorder_tab_in_folder(
                            &tab.space_id,
                            &folder_id,
                            &tab.id,
                            insert_at,
                        );
                    }

                    vec![CoreUpdate::TabCreated {
                        tab: tab_mgr.to_tab_view_model(&tab),
                    }]
                } else {
                    vec![]
                }
            }
            ShellEvent::PinTab { .. } => {
                unreachable!("PinTab is handled by MahoCore::handle_event before dispatch; this branch must never be reached")
            }
            ShellEvent::UnpinTab { .. } => {
                unreachable!("UnpinTab is handled by MahoCore::handle_event before dispatch; this branch must never be reached")
            }
            ShellEvent::FavoriteTab { .. } => {
                unreachable!("FavoriteTab is handled by MahoCore::handle_event before dispatch; this branch must never be reached")
            }
            ShellEvent::ChangeTabRole { .. } => {
                unreachable!("ChangeTabRole is handled by MahoCore::handle_event before dispatch; this branch must never be reached")
            }
            ShellEvent::MuteTab { tab_id } => {
                tab_mgr.mute_tab(&tab_id);
                vec![CoreUpdate::TabUpdated {
                    tab_id,
                    changes: TabStateUpdate {
                        is_muted: Some(true),
                        ..Default::default()
                    },
                }]
            }
            ShellEvent::UnmuteTab { tab_id } => {
                tab_mgr.unmute_tab(&tab_id);
                vec![CoreUpdate::TabUpdated {
                    tab_id,
                    changes: TabStateUpdate {
                        is_muted: Some(false),
                        ..Default::default()
                    },
                }]
            }
            ShellEvent::FreezeTab { tab_id } => {
                tab_mgr.toggle_freeze(&tab_id);
                vec![]
            }
            ShellEvent::SuspendTab { tab_id } => {
                // The shell dispatches this when the user puts a close-protected
                // tab to sleep (close button / two-stage close). A favorite must
                // sleep as its home page so a later wake reopens the pinned URL
                // instead of the last visited page.
                tab_mgr.reset_favorite_url_to_pinned(&tab_id);
                tab_mgr.suspend_tab(&tab_id);
                vec![]
            }
            ShellEvent::MoveTab {
                tab_id,
                target_space,
                position,
            } => {
                let is_favorite = tab_mgr.get_tab(&tab_id).map(|t| t.role.is_favorite()).unwrap_or(false); // L3-EXEMPT: local query
                if let Some(source_space) =
                    tab_mgr.move_tab(&tab_id, target_space.clone(), position)
                {
                    space_mgr.remove_tab_from_space(&source_space, &tab_id);
                    let is_in_folder = space_mgr.is_tab_in_folder(&target_space, &tab_id);
                    space_mgr.add_tab_to_space(&target_space, tab_id.clone(), Some(position));
                    space_mgr.apply_tab_residency(&target_space, &tab_id, is_favorite, is_in_folder); // L3-EXEMPT: local parameter
                    vec![
                        CoreUpdate::TabOrderChanged {
                            space_id: source_space,
                            order: vec![],
                        },
                        CoreUpdate::TabOrderChanged {
                            space_id: target_space,
                            order: vec![],
                        },
                    ]
                } else {
                    vec![]
                }
            }
            ShellEvent::MoveTabToSpace {
                tab_id,
                target_space_id,
                section: _,
            } => {
                if let Some(source_space) = tab_mgr.get_tab(&tab_id).map(|t| t.space_id.clone()) {
                    let mut is_favorite = tab_mgr.get_tab(&tab_id).map(|t| t.role.is_favorite()).unwrap_or(false); // L3-EXEMPT: local query
                    // Cross-profile favorite reconciliation: the favorite cap is
                    // per-profile, so a favorite entering a different profile that
                    // is already at MAX_FAVORITES is downgraded to Normal rather
                    // than overflowing the target profile's cap.
                    let source_profile = space_mgr.get_space(&source_space).map(|s| s.profile_id.clone());
                    let target_profile = space_mgr.get_space(&target_space_id).map(|s| s.profile_id.clone());
                    if is_favorite && source_profile != target_profile {
                        if let Some(tp) = &target_profile {
                            let target_space_ids = space_mgr.get_space_ids_for_profile(tp);
                            if tab_mgr.count_favorite_tabs_in_spaces(&target_space_ids)
                                >= crate::tab_lifecycle::MAX_FAVORITES
                            {
                                tab_mgr.transition_tab_role(&tab_id, TabRole::Normal);
                                is_favorite = false;
                            }
                        }
                    }
                    tab_mgr.move_tab(&tab_id, target_space_id.clone(), 0);
                    space_mgr.remove_tab_from_space(&source_space, &tab_id);
                    space_mgr.add_tab_to_space(&target_space_id, tab_id.clone(), None);
                    let is_in_folder = space_mgr.is_tab_in_folder(&target_space_id, &tab_id);
                    space_mgr.apply_tab_residency(&target_space_id, &tab_id, is_favorite, is_in_folder); // L3-EXEMPT: local parameter
                    vec![
                        CoreUpdate::TabOrderChanged {
                            space_id: source_space,
                            order: vec![],
                        },
                        CoreUpdate::TabOrderChanged {
                            space_id: target_space_id,
                            order: vec![],
                        },
                    ]
                } else {
                    vec![]
                }
            }
            ShellEvent::SetTabParent {
                tab_id,
                new_parent_id,
            } => {
                tab_mgr.set_tab_parent(&tab_id, new_parent_id);
                vec![]
            }
            ShellEvent::SetTabCustomTitle { tab_id, custom_title } => {
                tab_mgr.update_tab_custom_title(&tab_id, custom_title);
                // Same cache-invalidation rationale as SetTabCustomIcon below:
                // without TabUpdated the sidebar's favorites/tree cache keeps
                // serving the stale title after a Rename save.
                vec![CoreUpdate::TabUpdated {
                    tab_id,
                    changes: TabStateUpdate::default(),
                }]
            }
            ShellEvent::SetTabCustomIcon {
                tab_id,
                custom_icon,
            } => {
                tab_mgr.update_tab_custom_icon(&tab_id, custom_icon);
                // Emit TabUpdated so the sidebar adapter invalidates its
                // favorites/tree cache (tab_updated -> kTreeBundle|kFavorites);
                // returning [] left Change Icon/Rename edits invisible until an
                // unrelated event evicted the cache.
                vec![CoreUpdate::TabUpdated {
                    tab_id,
                    changes: TabStateUpdate::default(),
                }]
            }
            ShellEvent::SetTabPinnedUrl { tab_id, url } => {
                tab_mgr.set_tab_pinned_url(&tab_id, url);
                vec![CoreUpdate::TabUpdated {
                    tab_id,
                    changes: TabStateUpdate::default(),
                }]
            }
            ShellEvent::ReorderTab { tab_id, before_tab_id } => {
                let active_space_id = space_mgr.get_active_space_id().clone();
                space_mgr.reorder_tab(&active_space_id, &tab_id, before_tab_id.as_ref());
                vec![CoreUpdate::TabOrderChanged {
                    space_id: active_space_id,
                    order: vec![],
                }]
            }
            ShellEvent::MoveTabToRoot {
                space_id,
                folder_id,
                tab_id,
                before_tab_id,
            } => {
                space_mgr.move_tab_to_root(
                    &space_id,
                    &folder_id,
                    &tab_id,
                    before_tab_id.as_ref(),
                );
                let mut updates = vec![
                    CoreUpdate::FolderUpdated {
                        folder_id: folder_id.clone(),
                        changes: FolderUpdate {
                            name: None,
                            is_expanded: None,
                            is_pinned: None,
                        },
                    },
                    CoreUpdate::TabOrderChanged {
                        space_id: space_id.clone(),
                        order: vec![],
                    },
                ];
                let is_empty = space_mgr
                    .get_space(&space_id)
                    .and_then(|space| space.folders.iter().find(|f| f.id == folder_id))
                    .map(|f| f.tab_ids.is_empty())
                    .unwrap_or(false);
                if is_empty {
                    space_mgr.delete_folder(&space_id, &folder_id);
                    updates.push(CoreUpdate::FolderDeleted { folder_id });
                }
                updates
            }
            ShellEvent::ReopenLastClosed => {
                if let Some(tab) = tab_mgr.reopen_last_closed() {
                    // The closed tab's space_id was frozen at close time; that
                    // space may have been deleted in the meantime. Fall back to
                    // the active space so the reopened tab is never an orphan.
                    let target_space = if space_mgr.get_space(&tab.space_id).is_some() {
                        tab.space_id.clone()
                    } else {
                        let active = space_mgr.get_active_space_id().clone();
                        tab_mgr.move_tab(&tab.id, active.clone(), 0);
                        active
                    };
                    let is_in_folder = space_mgr.is_tab_in_folder(&target_space, &tab.id);
                    space_mgr.add_tab_to_space(&target_space, tab.id.clone(), None);
                    space_mgr.apply_tab_residency(&target_space, &tab.id, tab.role.is_favorite(), is_in_folder); // L3-EXEMPT: role query
                    let view_model = if target_space == tab.space_id {
                        tab_mgr.to_tab_view_model(&tab)
                    } else {
                        tab_mgr
                            .get_tab(&tab.id)
                            .map(|t| tab_mgr.to_tab_view_model(t))
                            .unwrap_or_else(|| tab_mgr.to_tab_view_model(&tab))
                    };
                    vec![CoreUpdate::TabCreated { tab: view_model }]
                } else {
                    vec![]
                }
            }
            ShellEvent::ArchiveTabById { tab_id } => {
                tab_mgr.suspend_tab(&tab_id);
                tab_mgr.archive_tab(&tab_id);
                vec![]
            }
            ShellEvent::RestoreArchivedTab { tab_id } => {
                tab_mgr.restore_archived_tab(&tab_id);
                vec![]
            }
            ShellEvent::DeleteArchivedTab { tab_id } => {
                let space_id = tab_mgr.get_tab(&tab_id).map(|t| t.space_id.clone());
                if tab_mgr.delete_archived_tab(&tab_id) {
                    if let Some(space_id) = space_id {
                        space_mgr.remove_tab_from_space(&space_id, &tab_id);
                    }
                }
                vec![]
            }
            ShellEvent::ResetPinnedTab { .. } => vec![],
            ShellEvent::CloseOtherTabs { space_id, tab_id } => {
                let closed = tab_mgr.close_other_tabs(&space_id, &tab_id);
                for tid in &closed {
                    space_mgr.remove_tab_from_space(&space_id, tid);
                }
                closed
                    .into_iter()
                    .map(|tab_id| CoreUpdate::TabClosed {
                        tab_id,
                        animated: true,
                    })
                    .collect()
            }
            ShellEvent::CloseTabsToRight { space_id, tab_id } => {
                let closed = tab_mgr.close_tabs_to_right(&space_id, &tab_id);
                for tid in &closed {
                    space_mgr.remove_tab_from_space(&space_id, tid);
                }
                closed
                    .into_iter()
                    .map(|tab_id| CoreUpdate::TabClosed {
                        tab_id,
                        animated: true,
                    })
                    .collect()
            }
            ShellEvent::CloseTabsToLeft { space_id, tab_id } => {
                let closed = tab_mgr.close_tabs_to_left(&space_id, &tab_id);
                for tid in &closed {
                    space_mgr.remove_tab_from_space(&space_id, tid);
                }
                closed
                    .into_iter()
                    .map(|tab_id| CoreUpdate::TabClosed {
                        tab_id,
                        animated: true,
                    })
                    .collect()
            }

            ShellEvent::CreateSpace {
                name,
                color,
                profile_id,
            } => {
                let space = space_mgr.create_space(&name, color, profile_id);
                let space_id = space.id.clone();
                if let Some(space) = space_mgr.get_space_view_model(&space_id) {
                    vec![CoreUpdate::SpaceCreated { space }]
                } else {
                    vec![]
                }
            }
            ShellEvent::ActivateSpace { space_id } => {
                space_mgr.activate_space(&space_id);
                let active_tab_id = space_mgr.get_last_active_tab(&space_id).cloned();
                vec![CoreUpdate::ActiveSpaceChanged { space_id, active_tab_id }]
            }
            ShellEvent::RenameSpace { space_id, name } => {
                match space_mgr.update_space_config(maho_types::space::SpaceConfigUpdate {
                    space_id: space_id.clone(),
                    name: Some(name.clone()),
                    color: None,
                    theme: None,
                    icon: None,
                    profile_id: None,
                }) {
                    Ok(Some(updated)) => vec![CoreUpdate::SpaceConfigUpdated { space_id, changes: updated }],
                    Ok(None) => vec![],
                    Err(err) => vec![CoreUpdate::Error {
                        context: "rename_space".to_string(),
                        error: maho_types::events::core_update::MahoError {
                            code: err.code().to_string(),
                            message: err.to_string(),
                            details: None,
                        },
                    }],
                }
            }
            ShellEvent::RecolorSpace { space_id, color } => {
                match space_mgr.update_space_config(maho_types::space::SpaceConfigUpdate {
                    space_id: space_id.clone(),
                    name: None,
                    color: Some(color),
                    theme: None,
                    icon: None,
                    profile_id: None,
                }) {
                    Ok(Some(updated)) => vec![CoreUpdate::SpaceConfigUpdated { space_id, changes: updated }],
                    Ok(None) => vec![],
                    Err(err) => vec![CoreUpdate::Error {
                        context: "recolor_space".to_string(),
                        error: maho_types::events::core_update::MahoError {
                            code: err.code().to_string(),
                            message: err.to_string(),
                            details: None,
                        },
                    }],
                }
            }
            ShellEvent::ReorderSpace { space_id, from, to } => {
                space_mgr.reorder_space(&space_id, from, to);
                vec![CoreUpdate::SpaceOrderChanged {
                    order: space_mgr.get_space_order().to_vec(),
                }]
            }
            ShellEvent::CreateFolder {
                space_id,
                name,
                is_pinned,
                parent_folder_id,
                provider_type,
                config_json,
            } => {
                if let Some(folder) = space_mgr.create_folder_with_provider(
                    &space_id, &name, is_pinned, parent_folder_id, provider_type, config_json,
                ) {
                    vec![CoreUpdate::FolderCreated { folder }]
                } else {
                    vec![]
                }
            }
            ShellEvent::CreateFolderWithTabs {
                space_id,
                name,
                tab_ids,
            } => {
                if let Some(folder) = space_mgr.create_folder(&space_id, &name, false, None) {
                    let folder_id = folder.id.clone();
                    for tab_id in tab_ids {
                        space_mgr.add_tab_to_folder(&space_id, &folder_id, tab_id);
                    }
                    vec![CoreUpdate::FolderCreated { folder }]
                } else {
                    vec![]
                }
            }
            ShellEvent::RenameFolder {
                space_id,
                folder_id,
                name,
            } => {
                let trimmed_name = name.trim().to_string();
                space_mgr.rename_folder(&space_id, &folder_id, &trimmed_name);
                vec![CoreUpdate::FolderUpdated {
                    folder_id,
                    changes: FolderUpdate {
                        name: Some(trimmed_name),
                        is_expanded: None,
                        is_pinned: None,
                    },
                }]
            }
            ShellEvent::DeleteFolder {
                space_id,
                folder_id,
            } => {
                space_mgr.delete_folder(&space_id, &folder_id);
                vec![CoreUpdate::FolderDeleted { folder_id }]
            }
            ShellEvent::MoveTabToFolder {
                space_id,
                folder_id,
                tab_id,
            } => {
                let previous_parent_folder_id: Option<FolderId> = space_mgr
                    .get_space(&space_id)
                    .and_then(|space| {
                        space.folders.iter().find(|f| f.tab_ids.contains(&tab_id)).map(|f| f.id.clone())
                    });

                space_mgr.add_tab_to_folder(&space_id, &folder_id, tab_id);
                let mut updates = vec![
                    CoreUpdate::FolderUpdated {
                        folder_id: folder_id.clone(),
                        changes: FolderUpdate {
                            name: None,
                            is_expanded: None,
                            is_pinned: None,
                        },
                    },
                    CoreUpdate::TabOrderChanged {
                        space_id: space_id.clone(),
                        order: vec![],
                    },
                ];

                if let Some(prev_fid) = previous_parent_folder_id {
                    let is_empty = space_mgr
                        .get_space(&space_id)
                        .and_then(|space| space.folders.iter().find(|f| f.id == prev_fid))
                        .map(|f| f.tab_ids.is_empty())
                        .unwrap_or(false);
                    if is_empty {
                        space_mgr.delete_folder(&space_id, &prev_fid);
                        updates.push(CoreUpdate::FolderDeleted { folder_id: prev_fid });
                    }
                }
                updates
            }
            ShellEvent::ReorderTabInFolder {
                space_id,
                folder_id,
                tab_id,
                to,
            } => {
                space_mgr.reorder_tab_in_folder(&space_id, &folder_id, &tab_id, to);
                vec![CoreUpdate::FolderUpdated {
                    folder_id,
                    changes: FolderUpdate {
                        name: None,
                        is_expanded: None,
                        is_pinned: None,
                    },
                }]
            }
            ShellEvent::RemoveTabFromFolder {
                space_id,
                folder_id,
                tab_id,
            } => {
                space_mgr.remove_tab_from_folder(&space_id, &folder_id, &tab_id);
                let is_favorite = tab_mgr.get_tab(&tab_id).map(|t| t.role.is_favorite()).unwrap_or(false); // L3-EXEMPT: local query
                space_mgr.apply_tab_residency(&space_id, &tab_id, is_favorite, false); // L3-EXEMPT: local parameter
                let mut updates = vec![CoreUpdate::FolderUpdated {
                    folder_id: folder_id.clone(),
                    changes: FolderUpdate {
                        name: None,
                        is_expanded: None,
                        is_pinned: None,
                    },
                }];
                let is_empty = space_mgr
                    .get_space(&space_id)
                    .and_then(|space| space.folders.iter().find(|f| f.id == folder_id))
                    .map(|f| f.tab_ids.is_empty())
                    .unwrap_or(false);
                if is_empty {
                    space_mgr.delete_folder(&space_id, &folder_id);
                    updates.push(CoreUpdate::FolderDeleted { folder_id });
                }
                updates
            }
            ShellEvent::ToggleFolderExpanded {
                space_id,
                folder_id,
            } => {
                let toggled = space_mgr.toggle_folder_expanded(&space_id, &folder_id);
                let after = space_mgr.find_folder_expanded(&space_id, &folder_id);
                vec![CoreUpdate::FolderUpdated {
                    folder_id,
                    changes: FolderUpdate {
                        name: None,
                        is_expanded: if toggled { after } else { None },
                        is_pinned: None,
                    },
                }]
            }
            ShellEvent::SetFolderPinned {
                space_id,
                folder_id,
                is_pinned,
            } => {
                if space_mgr.set_folder_pinned(&space_id, &folder_id, is_pinned) {
                    vec![CoreUpdate::FolderUpdated {
                        folder_id,
                        changes: FolderUpdate {
                            name: None,
                            is_expanded: None,
                            is_pinned: Some(is_pinned),
                        },
                    }]
                } else {
                    vec![]
                }
            }
            ShellEvent::ConvertFolderToSpace {
                space_id,
                folder_id,
            } => {
                let folder_info = space_mgr.get_space(&space_id)
                    .and_then(|sp| sp.folders.iter().find(|f| f.id == folder_id).map(|f| (f.name.clone(), f.tab_ids.clone(), sp.color.clone(), sp.profile_id.clone())));
                
                if let Some((folder_name, folder_tab_ids, color, profile_id)) = folder_info {
                    let mut descendants_with_tabs = Vec::new();
                    let mut to_visit = vec![folder_id.clone()];
                    while let Some(parent) = to_visit.pop() {
                        if let Some(sp) = space_mgr.get_space(&space_id) {
                            for f in &sp.folders {
                                if f.parent_folder_id == Some(parent.clone()) {
                                    descendants_with_tabs.push((f.id.clone(), f.tab_ids.clone()));
                                    to_visit.push(f.id.clone());
                                }
                            }
                        }
                    }
                    let descendant_ids: Vec<FolderId> = descendants_with_tabs.iter().map(|(id, _)| id.clone()).collect();

                    let new_space = space_mgr.create_space(&folder_name, color, profile_id);
                    let new_space_id = new_space.id.clone();

                    let mut updates = Vec::new();
                    let space_vm = space_mgr.get_space_view_model(&new_space_id).unwrap();
                    updates.push(CoreUpdate::SpaceCreated { space: space_vm });

                    for desc_id in &descendant_ids {
                        let mut parent_in_descendants = false;
                        if let Some(sp) = space_mgr.get_space(&space_id) {
                            if let Some(f) = sp.folders.iter().find(|f| f.id == *desc_id) {
                                if let Some(ref p_id) = f.parent_folder_id {
                                    if descendant_ids.contains(p_id) {
                                        parent_in_descendants = true;
                                    }
                                }
                            }
                        }

                        if !parent_in_descendants {
                            let _ = space_mgr.move_folder_in_space(&space_id, desc_id, None);
                        }

                        space_mgr.move_folder_to_space(desc_id, &space_id, &new_space_id);
                        updates.push(CoreUpdate::FolderUpdated {
                            folder_id: desc_id.clone(),
                            changes: FolderUpdate {
                                name: None,
                                is_expanded: None,
                                is_pinned: None,
                            },
                        });
                    }

                    for tab_id in &folder_tab_ids {
                        let is_favorite = tab_mgr.get_tab(tab_id).map(|t| t.role.is_favorite()).unwrap_or(false); // L3-EXEMPT: local query
                        tab_mgr.move_tab(tab_id, new_space_id.clone(), 0);
                        space_mgr.remove_tab_from_space(&space_id, tab_id);
                        space_mgr.add_tab_to_space(&new_space_id, tab_id.clone(), None);
                        space_mgr.apply_tab_residency(&new_space_id, tab_id, is_favorite, false); // L3-EXEMPT: local parameter
                    }

                    for (_desc_id, desc_tab_ids) in &descendants_with_tabs {
                        for tab_id in desc_tab_ids {
                            let is_favorite = tab_mgr.get_tab(tab_id).map(|t| t.role.is_favorite()).unwrap_or(false); // L3-EXEMPT: local query
                            tab_mgr.move_tab(tab_id, new_space_id.clone(), 0);
                            space_mgr.remove_tab_from_space(&space_id, tab_id);
                            space_mgr.add_tab_to_space(&new_space_id, tab_id.clone(), None);
                            space_mgr.apply_tab_residency(&new_space_id, tab_id, is_favorite, true); // L3-EXEMPT: local parameter
                        }
                    }

                    space_mgr.delete_folder(&space_id, &folder_id);
                    updates.push(CoreUpdate::FolderDeleted { folder_id: folder_id.clone() });
                    updates.push(CoreUpdate::TabOrderChanged {
                        space_id: space_id.clone(),
                        order: vec![],
                    });
                    updates.push(CoreUpdate::TabOrderChanged {
                        space_id: new_space_id,
                        order: vec![],
                    });

                    updates
                } else {
                    vec![]
                }
            }
            ShellEvent::MoveFolderToSpace {
                source_space_id,
                folder_id,
                target_space_id,
            } => {
                let top_tab_ids = if source_space_id == target_space_id {
                    None
                } else {
                    space_mgr.get_space(&source_space_id).and_then(|sp| {
                        sp.folders
                            .iter()
                            .find(|f| f.id == folder_id)
                            .map(|f| f.tab_ids.clone())
                    })
                };

                if let Some(top_tab_ids) = top_tab_ids {
                    let mut descendants_with_tabs: Vec<(FolderId, Vec<TabId>)> = Vec::new();
                    let mut to_visit = vec![folder_id.clone()];
                    while let Some(parent) = to_visit.pop() {
                        if let Some(sp) = space_mgr.get_space(&source_space_id) {
                            for f in &sp.folders {
                                if f.parent_folder_id == Some(parent.clone()) {
                                    descendants_with_tabs
                                        .push((f.id.clone(), f.tab_ids.clone()));
                                    to_visit.push(f.id.clone());
                                }
                            }
                        }
                    }
                    let descendant_ids: Vec<FolderId> = descendants_with_tabs
                        .iter()
                        .map(|(id, _)| id.clone())
                        .collect();

                    // Detach only the top folder to root so move_folder_to_space
                    // re-homes it into the target's root_order; descendants keep
                    // their parent pointers, which stay valid because the whole
                    // subtree moves to the same target space.
                    let _ = space_mgr.move_folder_in_space(&source_space_id, &folder_id, None);

                    let mut updates = Vec::new();

                    space_mgr.move_folder_to_space(&folder_id, &source_space_id, &target_space_id);
                    updates.push(CoreUpdate::FolderUpdated {
                        folder_id: folder_id.clone(),
                        changes: FolderUpdate {
                            name: None,
                            is_expanded: None,
                            is_pinned: None,
                        },
                    });
                    for desc_id in &descendant_ids {
                        space_mgr.move_folder_to_space(
                            desc_id,
                            &source_space_id,
                            &target_space_id,
                        );
                        updates.push(CoreUpdate::FolderUpdated {
                            folder_id: desc_id.clone(),
                            changes: FolderUpdate {
                                name: None,
                                is_expanded: None,
                                is_pinned: None,
                            },
                        });
                    }

                    let source_profile =
                        space_mgr.get_space(&source_space_id).map(|s| s.profile_id.clone());
                    let target_profile =
                        space_mgr.get_space(&target_space_id).map(|s| s.profile_id.clone());
                    let cross_profile = source_profile != target_profile;

                    for tab_id in top_tab_ids.iter().chain(
                        descendants_with_tabs
                            .iter()
                            .flat_map(|(_, tabs)| tabs.iter()),
                    ) {
                        let mut is_favorite = tab_mgr
                            .get_tab(tab_id)
                            .map(|t| t.role.is_favorite())
                            .unwrap_or(false); // L3-EXEMPT: local query
                        // Downgrade favorites that would overflow a different
                        // target profile's per-profile favorite cap (checked in
                        // sequence, so the count grows as earlier tabs land).
                        if is_favorite && cross_profile {
                            if let Some(tp) = &target_profile {
                                let ids = space_mgr.get_space_ids_for_profile(tp);
                                if tab_mgr.count_favorite_tabs_in_spaces(&ids)
                                    >= crate::tab_lifecycle::MAX_FAVORITES
                                {
                                    tab_mgr.transition_tab_role(tab_id, TabRole::Normal);
                                    is_favorite = false;
                                }
                            }
                        }
                        tab_mgr.move_tab(tab_id, target_space_id.clone(), 0);
                        space_mgr.remove_tab_from_space(&source_space_id, tab_id);
                        space_mgr.add_tab_to_space(&target_space_id, tab_id.clone(), None);
                        space_mgr.apply_tab_residency(&target_space_id, tab_id, is_favorite, true); // L3-EXEMPT: local parameter
                    }

                    updates.push(CoreUpdate::TabOrderChanged {
                        space_id: source_space_id,
                        order: vec![],
                    });
                    updates.push(CoreUpdate::TabOrderChanged {
                        space_id: target_space_id,
                        order: vec![],
                    });
                    updates
                } else {
                    vec![]
                }
            }
            ShellEvent::MoveFolderIntoFolder {
                space_id,
                folder_id,
                target_folder_id,
            } => {
                if space_mgr.move_folder_in_space(&space_id, &folder_id, Some(target_folder_id)).is_ok() {
                    vec![CoreUpdate::FolderUpdated {
                        folder_id,
                        changes: FolderUpdate {
                            name: None,
                            is_expanded: None,
                            is_pinned: None,
                        },
                    }]
                } else {
                    vec![]
                }
            }
            ShellEvent::MoveFolderToRoot {
                space_id,
                folder_id,
                before_folder_id,
            } => {
                space_mgr.move_folder_to_root(&space_id, &folder_id, before_folder_id.as_ref());
                vec![CoreUpdate::FolderOrderChanged { space_id }]
            }
            ShellEvent::ReorderFolder {
                space_id,
                folder_id,
                parent_folder_id,
                before_folder_id,
            } => {
                if space_mgr.reorder_folder(&space_id, &folder_id, parent_folder_id.as_ref(), before_folder_id.as_ref()).is_ok() {
                    vec![CoreUpdate::FolderOrderChanged { space_id }]
                } else {
                    vec![]
                }
            }
            ShellEvent::ReorderRootItem {
                space_id,
                item,
                insertion_point,
            } => {
                space_mgr.reorder_root_item(&space_id, &item, &insertion_point);
                vec![CoreUpdate::TabOrderChanged {
                    space_id,
                    order: vec![],
                }]
            }

            // Command bar
            ShellEvent::CommandBarOpened => vec![],
            ShellEvent::CommandBarClosed => vec![],
            ShellEvent::CommandBarQuery { text, mode, is_incognito } => {
                let mut search_ctx = ctx.clone();
                search_ctx.is_incognito = is_incognito;
                let suggestions = cmd_bar.search(&text, mode.as_deref(), &search_ctx);
                vec![CoreUpdate::CommandBarResults { suggestions }]
            }
            ShellEvent::CommandBarSelect { .. } => vec![],
            ShellEvent::CommandBarAction { action } => match action {
                QuickAction::NextSpace => {
                    let space_order = space_mgr.get_space_order().to_vec();
                    if !space_order.is_empty() {
                        let current_id = space_mgr.get_active_space_id().clone();
                        if let Some(pos) = space_order.iter().position(|id| *id == current_id) {
                            let next_pos = (pos + 1) % space_order.len();
                            space_mgr.activate_space(&space_order[next_pos]);
                        }
                    }
                    let new_space_id = space_mgr.get_active_space_id().clone();
                    let active_tab_id = space_mgr.get_last_active_tab(&new_space_id).cloned();
                    vec![CoreUpdate::ActiveSpaceChanged {
                        space_id: new_space_id,
                        active_tab_id,
                    }]
                }
                QuickAction::PrevSpace => {
                    let space_order = space_mgr.get_space_order().to_vec();
                    if !space_order.is_empty() {
                        let current_id = space_mgr.get_active_space_id().clone();
                        if let Some(pos) = space_order.iter().position(|id| *id == current_id) {
                            let prev_pos = (pos + space_order.len() - 1) % space_order.len();
                            space_mgr.activate_space(&space_order[prev_pos]);
                        }
                    }
                    let new_space_id = space_mgr.get_active_space_id().clone();
                    let active_tab_id = space_mgr.get_last_active_tab(&new_space_id).cloned();
                    vec![CoreUpdate::ActiveSpaceChanged {
                        space_id: new_space_id,
                        active_tab_id,
                    }]
                }
                QuickAction::ArchiveTab => vec![],
                QuickAction::SharePage => vec![],
                QuickAction::NewIncognito => vec![],
                QuickAction::ScrollToTop => vec![],
                QuickAction::ScrollToBottom => vec![],
                QuickAction::ClearBrowsingData => vec![],
                // Shell-handled actions (no core state change needed).
                // These return no CoreUpdates — the shell must handle them locally
                // (e.g., ShortcutDispatcher dispatches directly to SpaceContextMenu,
                // AppDelegate, or the relevant shell controller).
                QuickAction::NewTab
                | QuickAction::NewSpace
                | QuickAction::CloseTab
                | QuickAction::CloseOtherTabs
                | QuickAction::CloseAllTabs
                | QuickAction::DuplicateTab
                | QuickAction::PinTab
                | QuickAction::UnpinTab
                | QuickAction::MuteTab
                | QuickAction::UnmuteTab
                | QuickAction::MuteAllTabs
                | QuickAction::FreezeTab
                | QuickAction::UnfreezeTab
                | QuickAction::ReloadTab
                | QuickAction::HardReload
                | QuickAction::CopyUrl
                | QuickAction::ClearHistory
                | QuickAction::ClearCookies
                | QuickAction::OpenSettings
                | QuickAction::OpenDownloads
                | QuickAction::OpenBookmarks
                | QuickAction::OpenHistory
                | QuickAction::ToggleBoost
                | QuickAction::ToggleSidebar
                | QuickAction::ToggleContentBlocker
                | QuickAction::NewFolder
                | QuickAction::RestoreLastClosed
                | QuickAction::ToggleFullScreen
                | QuickAction::ZoomIn
                | QuickAction::ZoomOut
                | QuickAction::ResetZoom
                | QuickAction::FindInPage
                | QuickAction::PrintPage
                | QuickAction::ViewSource
                | QuickAction::ToggleDevTools
                | QuickAction::Custom { .. } => vec![],
            },

            ShellEvent::SaveSearch { .. } => vec![],
            ShellEvent::AddSearchEngine { .. } => vec![],
            ShellEvent::RemoveSearchEngine { .. } => vec![],
            ShellEvent::SetDefaultSearchEngine { .. } => vec![],

            // Boosts — handled in MahoCore::handle_event before dispatch
            ShellEvent::CreateBoost { .. }
            | ShellEvent::UpdateBoost { .. }
            | ShellEvent::SetActiveBoost { .. }
            | ShellEvent::DeleteBoost { .. }
            // CSS Mods
            | ShellEvent::InstallCssMod { .. }
            | ShellEvent::ToggleCssMod { .. }
            | ShellEvent::UninstallCssMod { .. }
            | ShellEvent::UpdateCssMod { .. }
            | ShellEvent::UnfreezeTab { .. }
            | ShellEvent::DeleteSpace { .. }
            // Notes
            | ShellEvent::CreateNote { .. }
            | ShellEvent::UpdateNote { .. }
            | ShellEvent::DeleteNote { .. }
            // Permissions
            | ShellEvent::GrantPermission { .. }
            | ShellEvent::RevokePermission { .. }
            // Air traffic control
            | ShellEvent::CreateTrafficRule { .. }
            | ShellEvent::DeleteTrafficRule { .. }
            | ShellEvent::UpdateTrafficRule { .. }
            | ShellEvent::SetDefaultLinkBehavior { .. }
            => vec![],

            // Settings — handled in MahoCore::handle_event pre-dispatch (needs SQLite persistence)
            ShellEvent::UpdateSettings { .. } => vec![],

            // Window/sidebar
            ShellEvent::SidebarToggled { visible } => {
                self.sidebar_visible = visible;
                vec![]
            }
            ShellEvent::SidebarResized { width } => {
                self.sidebar_width = width;
                vec![]
            }
            ShellEvent::WindowResized { .. } => vec![],
            ShellEvent::WindowFocusChanged { .. } => vec![],

            // Lifecycle
            ShellEvent::AppLaunched => vec![],
            ShellEvent::AppWillTerminate => vec![],
            ShellEvent::MemoryWarning { level } => {
                let action = tab_mgr.handle_memory_pressure(level);
                vec![CoreUpdate::MemoryPressureResponse { action }]
            }

            ShellEvent::TabTitleUpdated { tab_id, title } => {
                tab_mgr.update_tab_title(&tab_id, title.clone());
                vec![CoreUpdate::TabUpdated {
                    tab_id,
                    changes: TabStateUpdate {
                        title: Some(title),
                        ..Default::default()
                    },
                }]
            }
            ShellEvent::UpdateTabScrollPosition { tab_id, x, y } => {
                tab_mgr.update_tab_scroll_position(&tab_id, x, y);
                vec![]
            }
            ShellEvent::TabUrlUpdated { tab_id, url } => {
                tab_mgr.update_tab_url(&tab_id, url.clone());
                vec![CoreUpdate::TabUpdated {
                    tab_id,
                    changes: TabStateUpdate {
                        url: Some(url.0),
                        ..Default::default()
                    },
                }]
            }
            ShellEvent::TabLoadingChanged { tab_id, is_loading } => {
                tab_mgr.update_tab_loading(&tab_id, is_loading);
                vec![CoreUpdate::TabUpdated {
                    tab_id,
                    changes: TabStateUpdate {
                        is_loading: Some(is_loading),
                        ..Default::default()
                    },
                }]
            }
            ShellEvent::TabFaviconUpdated { tab_id, favicon } => {
                tab_mgr.update_tab_favicon(&tab_id, favicon.clone());
                vec![CoreUpdate::TabUpdated {
                    tab_id,
                    changes: TabStateUpdate {
                        favicon: Some(favicon),
                        ..Default::default()
                    },
                }]
            }
            ShellEvent::TabNavigationStateChanged {
                tab_id,
                can_go_back,
                can_go_forward,
            } => {
                let (url, title) = tab_mgr
                    .get_tab(&tab_id)
                    .map(|t| (t.url.clone(), t.title.clone()))
                    .unwrap_or_else(|| (Url::new(""), String::new()));
                let is_loading = tab_mgr.is_tab_loading(&tab_id);
                let is_secure = tab_mgr.is_tab_secure(&tab_id);
                vec![CoreUpdate::NavigationStateChanged {
                    tab_id,
                    url,
                    title,
                    can_go_back,
                    can_go_forward,
                    is_loading,
                    progress: if is_loading { 0.5 } else { 1.0 },
                    is_secure,
                }]
            }
            ShellEvent::TabSecurityChanged {
                tab_id,
                is_secure,
            } => {
                tab_mgr.update_tab_security(&tab_id, is_secure);
                vec![]
            }
            // History
            ShellEvent::SearchHistory { .. } => vec![],
            ShellEvent::ClearHistory => vec![],
            ShellEvent::DeleteHistoryEntry { .. } => vec![],

            // Bookmarks
            ShellEvent::AddBookmark { .. } => vec![],
            ShellEvent::RemoveBookmark { .. } => vec![],
            ShellEvent::MoveBookmark { .. } => vec![],
            ShellEvent::SearchBookmarks { .. } => vec![],

            // Permissions (GrantPermission/RevokePermission handled in MahoCore::handle_event pre-dispatch)
            ShellEvent::QueryPermission { .. } => vec![],

            // Zoom
            ShellEvent::SetZoom { .. } => vec![],
            ShellEvent::ResetZoom { .. } => vec![],

            // Downloads
            ShellEvent::PauseDownload { .. } => vec![],
            ShellEvent::ResumeDownload { .. } => vec![],
            ShellEvent::CancelDownload { .. } => vec![],
            ShellEvent::RemoveDownload { .. } => vec![],
            ShellEvent::RestoreDownloadName { .. } => vec![],

            // Content Blocker (handled in MahoCore::handle_event before dispatch so
            // every mutation converges through the authoritative persistent path)
            ShellEvent::SetContentBlockingMode { .. } => vec![],
            ShellEvent::ToggleContentBlocker { .. } => vec![],
            ShellEvent::TogglePopupBlocking { .. } => vec![],
            ShellEvent::AddFilterList { .. } => vec![],
            ShellEvent::RemoveFilterList { .. } => vec![],
            ShellEvent::ToggleFilterList { .. } => vec![],
            ShellEvent::AddSiteException { .. } => vec![],
            ShellEvent::RemoveSiteException { .. } => vec![],
            ShellEvent::TriggerFilterUpdate { .. } => vec![],

            // Browsing
            ShellEvent::TogglePiP { .. } => vec![],
            ShellEvent::ToggleDevTools { .. } => vec![],
            ShellEvent::PrintPage { .. } => vec![],
            ShellEvent::ViewSource { .. } => vec![],

            // Boosts settings (handled in MahoCore::handle_event before dispatch)


            // Extensions management (handled in MahoCore::handle_event before dispatch)
            ShellEvent::ToggleExtension { .. } => vec![],
            ShellEvent::RemoveExtension { .. } => vec![],

            // Account & Sync (handled in MahoCore::handle_event before dispatch)
            ShellEvent::SignIn { .. } => vec![],
            ShellEvent::SignOut => vec![],
            ShellEvent::ToggleSync => vec![],

            // Air traffic control + space settings + profiles are handled in MahoCore::handle_event
            // before dispatch, so this branch stays a no-op for the consolidated path.
            ShellEvent::UpdateSpaceConfig { .. } => vec![],

            // Profiles (handled in MahoCore::handle_event before dispatch)
            ShellEvent::CreateProfile { .. } => vec![],
            ShellEvent::DeleteProfile { .. } => vec![],
            ShellEvent::UpdateProfile { .. } => vec![],
            ShellEvent::SwitchProfile { .. } => vec![],

            // Passwords (handled in MahoCore::handle_event before dispatch)
            ShellEvent::SearchPasswords { .. } => vec![],
            ShellEvent::DeletePassword { .. } => vec![],
            ShellEvent::AddPassword { .. } => vec![],

            ShellEvent::AddAutofillAddress { .. } => vec![],
            ShellEvent::DeleteAutofillAddress { .. } => vec![],
            ShellEvent::AddAutofillPayment { .. } => vec![],
            ShellEvent::DeleteAutofillPayment { .. } => vec![],
            ShellEvent::ResetSettings => vec![],

            // Reading List - handled in maho_core.rs with persistence
            ShellEvent::AddToReadingList { .. } => vec![],
            ShellEvent::RemoveFromReadingList { .. } => vec![],
            ShellEvent::MarkReadingListItemRead { .. } => vec![],
            ShellEvent::MarkReadingListItemUnread { .. } => vec![],

            // Notifications
            ShellEvent::DismissNotification { notification_id } => {
                notification_mgr.dismiss(&notification_id);
                vec![CoreUpdate::NotificationDismissed { notification_id }]
            }
            ShellEvent::DismissAllNotifications => {
                notification_mgr.dismiss_all();
                vec![CoreUpdate::AllNotificationsDismissed]
            }
            ShellEvent::NotificationAction {
                notification_id,
                action_id,
            } => {
                notification_mgr.handle_action(&notification_id, &action_id);
                vec![CoreUpdate::NotificationDismissed { notification_id }]
            }
            ShellEvent::SetNotificationFilter { origin, allowed } => {
                notification_mgr.set_filter(origin, allowed);
                vec![]
            }
            ShellEvent::ReorderFavorite { tab_id, new_index } => {
                let space_ids = tab_mgr
                    .get_tab(&tab_id)
                    .and_then(|tab| space_mgr.get_space(&tab.space_id))
                    .map(|space| space_mgr.get_space_ids_for_profile(&space.profile_id))
                    .unwrap_or_default();
                tab_mgr.reorder_favorite(&tab_id, new_index, &space_ids);
                let role = tab_mgr.get_tab(&tab_id).map(|t| t.role.clone());
                let favorite_order = role.as_ref().and_then(|r| r.favorite_order()).map(Some);
                vec![CoreUpdate::TabUpdated {
                    tab_id,
                    changes: TabStateUpdate {
                        is_favorite: Some(true), // L3-EXEMPT: legacy compat
                        favorite_order,
                        role,
                        ..Default::default()
                    },
                }]
            }
            ShellEvent::LlmResult { .. } | ShellEvent::LlmError { .. } => vec![],
            ShellEvent::RequestPagePreview { url } => {
                llm_mgr.request_page_preview(&url).into_iter().collect()
            }

            ShellEvent::RequestTidyTabs { .. } | ShellEvent::ApplyTidyTabs { .. } => vec![],
            ShellEvent::ExportSpaceIntoFolder { .. } => vec![],

            ShellEvent::RequestSkillsList | ShellEvent::UseSkill { .. } | ShellEvent::ChatMessage { .. }
            | ShellEvent::ToolPermissionResponse { .. } | ShellEvent::ToolActionResult { .. }
            | ShellEvent::ChatSessionEnded { .. } | ShellEvent::SetMemoryAuth { .. } => vec![],
        }
    }
}

use crate::note_manager::NoteManager;
use crate::settings_manager::SettingsManager;

impl NoteManagerInterface for NoteManager {
    fn create_note(
        &mut self,
        linked_tab: Option<TabId>,
        content: String,
    ) -> maho_types::note::Note {
        self.create_note(linked_tab, content)
    }
    fn update_note(&mut self, note_id: &NoteId, content: String) {
        self.update_note(note_id, content);
    }
    fn delete_note(&mut self, note_id: &NoteId) -> Option<maho_types::note::Note> {
        self.delete_note(note_id)
    }
}

impl SettingsInterface for SettingsManager {
    fn update_settings(&mut self, changes: SettingsUpdate) -> Settings {
        self.update_settings(changes).clone()
    }
    fn new_tab_position(&self) -> NewTabPosition {
        self.get_settings().general.new_tab_position
    }
}

use crate::notification_manager::NotificationManager;

impl NotificationManagerInterface for NotificationManager {
    fn dismiss(&mut self, notification_id: &str) -> bool {
        self.dismiss(notification_id)
    }
    fn dismiss_all(&mut self) {
        self.dismiss_all();
    }
    fn handle_action(&mut self, notification_id: &str, action_id: &str) -> bool {
        self.handle_action(notification_id, action_id)
    }
    fn set_filter(&mut self, origin: String, allowed: bool) {
        self.set_filter(origin, allowed);
    }
    fn get_all(&self) -> Vec<maho_types::traits::shell_renderer::NotificationViewModel> {
        self.get_all()
    }
}

use crate::llm_manager::LLMManager;

impl LLMManagerInterface for LLMManager {
    fn handle_llm_result(&mut self, request_id: &str, result: &str) -> Vec<CoreUpdate> {
        self.handle_result(request_id, result)
    }
    fn handle_llm_error(&mut self, request_id: &str, error: &str) -> Vec<CoreUpdate> {
        self.handle_error(request_id, error)
    }
    fn request_tidy_title(&mut self, tab_id: &TabId, title: &str, url: &str) -> CoreUpdate {
        LLMManager::request_tidy_title(self, tab_id, title, url)
    }
    fn request_tidy_download(
        &mut self,
        download_id: &str,
        filename: &str,
        url: &str,
        page_title: &str,
    ) -> CoreUpdate {
        LLMManager::request_tidy_download(self, download_id, filename, url, page_title)
    }
    fn request_page_preview(&mut self, url: &str) -> Option<CoreUpdate> {
        LLMManager::request_page_preview(self, url)
    }
    fn request_tidy_tabs(
        &mut self,
        space_id: &SpaceId,
        tabs: Vec<(TabId, String, String)>,
    ) -> CoreUpdate {
        LLMManager::request_tidy_tabs(self, space_id, tabs)
    }
    fn take_pending_tidy_tabs(
        &mut self,
    ) -> Option<(SpaceId, Vec<maho_types::events::core_update::TidyTabFolder>)> {
        LLMManager::take_pending_tidy_tabs(self)
    }
    fn request_chat_completion(
        &mut self,
        message: &str,
        context: &ChatRequestContext,
    ) -> CoreUpdate {
        LLMManager::request_chat_completion(self, message, context)
    }
}
