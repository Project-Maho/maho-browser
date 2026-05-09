use maho_types::boost::BoostUpdate;
use maho_types::common::{MemoryPressureLevel, Url, UrlPattern};
use maho_types::chat::ChatRequestContext;
use maho_types::events::core_update::{CoreUpdate, FolderUpdate, MemoryAction};
use maho_types::events::shell_event::{QuickAction, ShellEvent};
use maho_types::identifiers::{BoostId, FolderId, NoteId, ProfileId, SpaceId, TabId};
use maho_types::settings::{Settings, SettingsUpdate};
use maho_types::space::{Space, SpaceColor};
use maho_types::tab::Tab;
use maho_types::traits::shell_renderer::{
    SearchContext, SpaceViewModel, SuggestionViewModel, TabStateUpdate, TabViewModel,
};

pub trait TabManagerInterface {
    fn create_tab(&mut self, space_id: SpaceId, url: Option<Url>, parent_id: Option<TabId>) -> Tab;
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
    fn unfavorite_tab(&mut self, tab_id: &TabId);
    fn reorder_favorite(&mut self, tab_id: &TabId, new_index: usize);
    fn mute_tab(&mut self, tab_id: &TabId);
    fn unmute_tab(&mut self, tab_id: &TabId);
    fn freeze_tab(&mut self, tab_id: &TabId);
    fn suspend_tab(&mut self, tab_id: &TabId);
    fn archive_tab(&mut self, tab_id: &TabId);
    fn reopen_last_closed(&mut self) -> Option<Tab>;
    fn handle_memory_pressure(&mut self, level: MemoryPressureLevel) -> MemoryAction;
    fn update_tab_title(&mut self, tab_id: &TabId, title: String);
    fn update_tab_custom_title(&mut self, tab_id: &TabId, custom_title: Option<String>);
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
    fn is_tab_secure(&self, tab_id: &TabId) -> bool;
    fn close_other_tabs(&mut self, space_id: &SpaceId, tab_id: &TabId) -> Vec<TabId>;
    fn close_tabs_to_right(&mut self, space_id: &SpaceId, tab_id: &TabId) -> Vec<TabId>;
    fn close_tabs_to_left(&mut self, space_id: &SpaceId, tab_id: &TabId) -> Vec<TabId>;
    fn toggle_freeze(&mut self, tab_id: &TabId);
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
    fn reorder_folder(&mut self, space_id: &SpaceId, folder_id: &FolderId, parent_folder_id: Option<&FolderId>, before_folder_id: Option<&FolderId>);
    fn move_folder_to_root(&mut self, space_id: &SpaceId, folder_id: &FolderId, before_folder_id: Option<&FolderId>);
    fn get_active_space_id(&self) -> &SpaceId;
    fn get_space_order(&self) -> &[SpaceId];
    fn get_space(&self, space_id: &SpaceId) -> Option<&maho_types::space::Space>;
    fn get_space_view_model(&self, space_id: &SpaceId) -> Option<SpaceViewModel>;
    fn create_folder(
        &mut self,
        space_id: &SpaceId,
        name: &str,
        is_pinned: bool,
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
    fn toggle_folder_expanded(&mut self, space_id: &SpaceId, folder_id: &FolderId);
    fn pin_folder(&mut self, space_id: &SpaceId, folder_id: &FolderId);
    fn unpin_folder(&mut self, space_id: &SpaceId, folder_id: &FolderId);
    fn move_folder(&mut self, folder_id: &FolderId, new_parent: Option<FolderId>);
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
}

pub trait CommandBarInterface {
    fn search(
        &self,
        query: &str,
        mode: Option<&str>,
        ctx: &SearchContext,
    ) -> Vec<SuggestionViewModel>;
}

pub trait BoostManagerInterface {
    fn create_boost(&mut self, url_pattern: UrlPattern) -> maho_types::boost::Boost;
    fn update_boost(&mut self, boost_id: &BoostId, changes: BoostUpdate);
    fn toggle_boost(&mut self, boost_id: &BoostId, enabled: bool);
    fn delete_boost(&mut self, boost_id: &BoostId) -> Option<maho_types::boost::Boost>;
}

pub trait NoteManagerInterface {
    fn create_note(&mut self, linked_tab: Option<TabId>, content: String)
        -> maho_types::note::Note;
    fn update_note(&mut self, note_id: &NoteId, content: String);
    fn delete_note(&mut self, note_id: &NoteId) -> Option<maho_types::note::Note>;
}

pub trait SettingsInterface {
    fn update_settings(&mut self, changes: SettingsUpdate) -> Settings;
}

pub trait ContentBlockerInterface {
    fn set_enabled(&mut self, enabled: bool);
    fn is_enabled(&self) -> bool;
    fn set_popup_blocking(&mut self, enabled: bool);
    fn is_popup_blocking_enabled(&self) -> bool;
    fn add_filter_list(&mut self, id: String, name: String, url: String);
    fn remove_filter_list(&mut self, id: &str);
    fn toggle_filter_list(&mut self, id: &str, enabled: bool);
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

    #[allow(clippy::too_many_arguments)]
    pub fn dispatch(
        &mut self,
        event: ShellEvent,
        tab_mgr: &mut dyn TabManagerInterface,
        space_mgr: &mut dyn SpaceManagerInterface,
        cmd_bar: &dyn CommandBarInterface,
        _boost_mgr: &mut dyn BoostManagerInterface,
        _note_mgr: &mut dyn NoteManagerInterface,
        _settings_mgr: &mut dyn SettingsInterface,
        content_blocker: &mut dyn ContentBlockerInterface,
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
            ShellEvent::ClearSplitView => vec![],
            ShellEvent::CreateSplit { .. } => vec![],

            // Tab management
            ShellEvent::CreateTab {
                space_id,
                url,
                parent_id,
            } => {
                let tab = tab_mgr.create_tab(space_id.clone(), url, parent_id);
                space_mgr.add_tab_to_space(&space_id, tab.id.clone(), None);
                space_mgr.set_last_active_tab(&space_id, Some(tab.id.clone()));
                vec![CoreUpdate::TabCreated {
                    tab: tab_mgr.to_tab_view_model(&tab),
                }]
            }
            ShellEvent::CloseTab { tab_id } => {
                if let Some(tab) = tab_mgr.close_tab(&tab_id) {
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
                vec![CoreUpdate::TabUpdated {
                    tab_id,
                    changes: TabStateUpdate::default(),
                }]
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
            ShellEvent::PinTab { tab_id } => {
                tab_mgr.pin_tab(&tab_id);
                vec![CoreUpdate::TabUpdated {
                    tab_id,
                    changes: TabStateUpdate {
                        custom_title: None,
                        is_pinned: Some(true),
                        ..Default::default()
                    },
                }]
            }
            ShellEvent::UnpinTab { tab_id } => {
                tab_mgr.unpin_tab(&tab_id);
                vec![CoreUpdate::TabUpdated {
                    tab_id,
                    changes: TabStateUpdate {
                        custom_title: None,
                        is_pinned: Some(false),
                        ..Default::default()
                    },
                }]
            }
            ShellEvent::FavoriteTab { tab_id } => {
                tab_mgr.favorite_tab(&tab_id);
                vec![CoreUpdate::TabUpdated {
                    tab_id,
                    changes: TabStateUpdate {
                        custom_title: None,
                        is_favorite: Some(true),
                        ..Default::default()
                    },
                }]
            }
            ShellEvent::UnfavoriteTab { tab_id } => {
                tab_mgr.unfavorite_tab(&tab_id);
                vec![CoreUpdate::TabUpdated {
                    tab_id,
                    changes: TabStateUpdate {
                        custom_title: None,
                        is_favorite: Some(false),
                        ..Default::default()
                    },
                }]
            }
            ShellEvent::MuteTab { tab_id } => {
                tab_mgr.mute_tab(&tab_id);
                vec![]
            }
            ShellEvent::UnmuteTab { tab_id } => {
                tab_mgr.unmute_tab(&tab_id);
                vec![]
            }
            ShellEvent::FreezeTab { tab_id } => {
                tab_mgr.toggle_freeze(&tab_id);
                vec![]
            }
            ShellEvent::MoveTab {
                tab_id,
                target_space,
                position,
            } => {
                if let Some(source_space) =
                    tab_mgr.move_tab(&tab_id, target_space.clone(), position)
                {
                    space_mgr.remove_tab_from_space(&source_space, &tab_id);
                    space_mgr.add_tab_to_space(&target_space, tab_id, Some(position));
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
            ShellEvent::SetTabParent {
                tab_id,
                new_parent_id,
            } => {
                tab_mgr.set_tab_parent(&tab_id, new_parent_id);
                vec![]
            }
            ShellEvent::SetTabCustomTitle { tab_id, custom_title } => {
                tab_mgr.update_tab_custom_title(&tab_id, custom_title);
                vec![]
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
                vec![
                    CoreUpdate::FolderUpdated {
                        folder_id,
                        changes: FolderUpdate {
                            name: None,
                            is_expanded: None,
                            is_pinned: None,
                        },
                    },
                    CoreUpdate::TabOrderChanged {
                        space_id,
                        order: vec![],
                    },
                ]
            }
            ShellEvent::ReopenLastClosed => {
                if let Some(tab) = tab_mgr.reopen_last_closed() {
                    space_mgr.add_tab_to_space(&tab.space_id, tab.id.clone(), None);
                    vec![CoreUpdate::TabCreated {
                        tab: tab_mgr.to_tab_view_model(&tab),
                    }]
                } else {
                    vec![]
                }
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
            } => {
                if let Some(folder) = space_mgr.create_folder(&space_id, &name, is_pinned) {
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
                space_mgr.add_tab_to_folder(&space_id, &folder_id, tab_id);
                vec![CoreUpdate::FolderUpdated {
                    folder_id,
                    changes: FolderUpdate {
                        name: None,
                        is_expanded: None,
                        is_pinned: None,
                    },
                }]
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
                vec![CoreUpdate::FolderUpdated {
                    folder_id,
                    changes: FolderUpdate {
                        name: None,
                        is_expanded: None,
                        is_pinned: None,
                    },
                }]
            }
            ShellEvent::ToggleFolderExpanded {
                space_id,
                folder_id,
            } => {
                space_mgr.toggle_folder_expanded(&space_id, &folder_id);
                vec![CoreUpdate::FolderUpdated {
                    folder_id,
                    changes: FolderUpdate {
                        name: None,
                        is_expanded: None,
                        is_pinned: None,
                    },
                }]
            }
            ShellEvent::PinFolder {
                space_id,
                folder_id,
            } => {
                space_mgr.pin_folder(&space_id, &folder_id);
                vec![CoreUpdate::FolderUpdated {
                    folder_id,
                    changes: FolderUpdate {
                        name: None,
                        is_expanded: None,
                        is_pinned: Some(true),
                    },
                }]
            }
            ShellEvent::UnpinFolder {
                space_id,
                folder_id,
            } => {
                space_mgr.unpin_folder(&space_id, &folder_id);
                vec![CoreUpdate::FolderUpdated {
                    folder_id,
                    changes: FolderUpdate {
                        name: None,
                        is_expanded: None,
                        is_pinned: Some(false),
                    },
                }]
            }
            ShellEvent::MoveFolderIntoFolder {
                space_id: _,
                folder_id,
                target_folder_id,
            } => {
                space_mgr.move_folder(&folder_id, Some(target_folder_id));
                vec![CoreUpdate::FolderUpdated {
                    folder_id,
                    changes: FolderUpdate {
                        name: None,
                        is_expanded: None,
                        is_pinned: None,
                    },
                }]
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
                space_mgr.reorder_folder(&space_id, &folder_id, parent_folder_id.as_ref(), before_folder_id.as_ref());
                vec![CoreUpdate::FolderOrderChanged { space_id }]
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
            ShellEvent::CommandBarQuery { text, mode } => {
                let suggestions = cmd_bar.search(&text, mode.as_deref(), ctx);
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
                QuickAction::ArchiveTab => {
                    if let Some(tab_id) = space_mgr
                        .get_last_active_tab(space_mgr.get_active_space_id())
                        .cloned()
                    {
                        tab_mgr.suspend_tab(&tab_id);
                        tab_mgr.archive_tab(&tab_id);
                    }
                    vec![]
                }
                QuickAction::SharePage => vec![],
                QuickAction::NewIncognito => vec![],
                QuickAction::ScrollToTop => vec![],
                QuickAction::ScrollToBottom => vec![],
                QuickAction::ClearBrowsingData => vec![],
                // Shell-handled actions (no core state change needed)
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

            // Boosts
            ShellEvent::CreateBoost { .. }
            | ShellEvent::UpdateBoost { .. }
            | ShellEvent::ToggleBoost { .. }
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

            // Content Blocker
            ShellEvent::ToggleContentBlocker { enabled } => {
                content_blocker.set_enabled(enabled);
                vec![CoreUpdate::ContentBlockerStateChanged {
                    enabled,
                    popup_blocking: content_blocker.is_popup_blocking_enabled(),
                }]
            }
            ShellEvent::TogglePopupBlocking { enabled } => {
                content_blocker.set_popup_blocking(enabled);
                vec![CoreUpdate::ContentBlockerStateChanged {
                    enabled: content_blocker.is_enabled(),
                    popup_blocking: enabled,
                }]
            }
            ShellEvent::AddFilterList { id, name, url } => {
                content_blocker.add_filter_list(id, name, url);
                vec![]
            }
            ShellEvent::RemoveFilterList { id } => {
                content_blocker.remove_filter_list(&id);
                vec![]
            }
            ShellEvent::ToggleFilterList { id, enabled } => {
                content_blocker.toggle_filter_list(&id, enabled);
                vec![]
            }

            // Browsing
            ShellEvent::TogglePiP { .. } => vec![],
            ShellEvent::ToggleDevTools { .. } => vec![],
            ShellEvent::PrintPage { .. } => vec![],
            ShellEvent::ViewSource { .. } => vec![],

            // Boosts settings (handled in MahoCore::handle_event before dispatch)
            ShellEvent::ToggleGlobalBoosts => vec![],
            ShellEvent::ToggleJsBoosts => vec![],

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
                tab_mgr.reorder_favorite(&tab_id, new_index);
                vec![CoreUpdate::TabUpdated {
                    tab_id,
                    changes: TabStateUpdate {
                        is_favorite: Some(true),
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
            | ShellEvent::ToolPermissionResponse { .. } | ShellEvent::ToolActionResult { .. } => vec![],
        }
    }
}

use crate::boost_manager::BoostManager;
use crate::note_manager::NoteManager;
use crate::settings_manager::SettingsManager;

impl BoostManagerInterface for BoostManager {
    fn create_boost(&mut self, url_pattern: UrlPattern) -> maho_types::boost::Boost {
        self.create_boost(url_pattern)
    }
    fn update_boost(&mut self, boost_id: &BoostId, changes: BoostUpdate) {
        self.update_boost(boost_id, changes);
    }
    fn toggle_boost(&mut self, boost_id: &BoostId, enabled: bool) {
        self.toggle_boost(boost_id, enabled);
    }
    fn delete_boost(&mut self, boost_id: &BoostId) -> Option<maho_types::boost::Boost> {
        self.delete_boost(boost_id)
    }
}

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
}

use crate::content_blocker::ContentBlocker;

impl ContentBlockerInterface for ContentBlocker {
    fn set_enabled(&mut self, enabled: bool) {
        self.set_enabled(enabled);
    }
    fn is_enabled(&self) -> bool {
        self.is_enabled()
    }
    fn set_popup_blocking(&mut self, enabled: bool) {
        self.set_popup_blocking(enabled);
    }
    fn is_popup_blocking_enabled(&self) -> bool {
        self.is_popup_blocking_enabled()
    }
    fn add_filter_list(&mut self, id: String, name: String, url: String) {
        self.add_filter_list(id, name, url);
    }
    fn remove_filter_list(&mut self, id: &str) {
        self.remove_filter_list(id);
    }
    fn toggle_filter_list(&mut self, id: &str, enabled: bool) {
        self.toggle_filter_list(id, enabled);
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
