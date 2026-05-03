use std::collections::HashMap;

use maho_types::common::DateTime;
use maho_types::folder::Folder;
use maho_types::identifiers::{FolderId, ProfileId, SpaceId, TabId};
use maho_types::space::{Space, SpaceColor, SpaceConfigUpdate};
use maho_types::traits::shell_renderer::{FolderViewModel, SpaceViewModel};

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum SpaceUpdateError {
    EmptyName,
    DuplicateName,
}

#[cfg(test)]
#[allow(clippy::items_after_test_module)]
mod tests {
    use super::*;
    use maho_types::folder::Folder;

    #[test]
    fn add_tab_to_space_ignores_duplicate_tab_ids() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let tab_id = TabId::generate();

        manager.add_tab_to_space(&space_id, tab_id.clone(), None);
        manager.add_tab_to_space(&space_id, tab_id.clone(), Some(0));

        let tab_order = manager.get_tab_order(&space_id);
        assert_eq!(tab_order.iter().filter(|id| *id == &tab_id).count(), 1);
    }

    #[test]
    fn move_tab_to_root_removes_from_folder_and_reorders_before_target() {
        let mut manager = SpaceManager::new();
        let space_id = manager.get_active_space_id().clone();
        let folder_id = FolderId::generate();
        let moved_tab = TabId::generate();
        let before_tab = TabId::generate();

        manager.add_tab_to_space(&space_id, moved_tab.clone(), None);
        manager.add_tab_to_space(&space_id, before_tab.clone(), None);

        if let Some(space) = manager.spaces.get_mut(&space_id) {
            space.folders.push(Folder {
                id: folder_id.clone(),
                name: "Folder".to_string(),
                tab_ids: vec![moved_tab.clone()],
                is_expanded: true,
                is_pinned: false,
                parent_folder_id: None,
            });
            space.tab_order = vec![before_tab.clone(), moved_tab.clone()];
        }

        manager.move_tab_to_root(&space_id, &folder_id, &moved_tab, Some(&before_tab));

        let space = manager.spaces.get(&space_id).unwrap();
        let folder = space.folders.iter().find(|folder| folder.id == folder_id).unwrap();
        assert!(!folder.tab_ids.contains(&moved_tab));
        assert_eq!(space.tab_order, vec![moved_tab, before_tab]);
    }
}

impl std::fmt::Display for SpaceUpdateError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::EmptyName => write!(f, "Space name cannot be empty"),
            Self::DuplicateName => write!(f, "Space name must be unique"),
        }
    }
}

impl SpaceUpdateError {
    pub fn code(&self) -> &'static str {
        match self {
            Self::EmptyName => "INVALID_NAME",
            Self::DuplicateName => "DUPLICATE_NAME",
        }
    }
}

pub struct SpaceManager {
    spaces: HashMap<SpaceId, Space>,
    space_order: Vec<SpaceId>,
    active_space_id: SpaceId,
}

impl Default for SpaceManager {
    fn default() -> Self {
        Self::new()
    }
}

impl SpaceManager {
    pub fn new() -> Self {
        let mut mgr = Self {
            spaces: HashMap::new(),
            space_order: vec![],
            active_space_id: SpaceId::new(""),
        };
        let default_space = mgr.create_space_internal(
            "Personal",
            SpaceColor {
                hue: 220.0,
                saturation: 0.7,
                brightness: 0.9,
                grain: 0.0,
            },
            ProfileId::new(""),
        );
        mgr.active_space_id = default_space.id;
        mgr
    }

    fn create_space_internal(
        &mut self,
        name: &str,
        color: SpaceColor,
        profile_id: ProfileId,
    ) -> Space {
        let now = DateTime::now();
        let space = Space {
            id: SpaceId::generate(),
            profile_id,
            name: name.to_string(),
            color,
            theme: None,
            icon: None,
            tab_order: vec![],
            folders: vec![],
            atc_rules: vec![],
            is_active: true,
            created_at: now,
            last_active_tab_id: None,
        };
        self.spaces.insert(space.id.clone(), space.clone());
        self.space_order.push(space.id.clone());
        space
    }

    pub fn create_space(&mut self, name: &str, color: SpaceColor, profile_id: ProfileId) -> Space {
        let space = self.create_space_internal(name, color, profile_id);
        if let Some(s) = self.spaces.get_mut(&space.id) {
            s.is_active = false;
        }
        space
    }

    pub fn delete_space(&mut self, space_id: &SpaceId) -> Option<SpaceId> {
        if self.spaces.len() <= 1 {
            return None;
        }
        let was_active = self.active_space_id == *space_id;
        self.spaces.remove(space_id);
        self.space_order.retain(|id| id != space_id);
        if was_active {
            if let Some(first_id) = self.space_order.first().cloned() {
                self.active_space_id = first_id.clone();
                for (id, space) in self.spaces.iter_mut() {
                    space.is_active = *id == first_id;
                }
                return Some(first_id);
            }
        }
        None
    }

    pub fn activate_space(&mut self, space_id: &SpaceId) {
        if !self.spaces.contains_key(space_id) {
            return;
        }
        if let Some(prev) = self.spaces.get_mut(&self.active_space_id) {
            prev.is_active = false;
        }
        self.active_space_id = space_id.clone();
        if let Some(space) = self.spaces.get_mut(space_id) {
            space.is_active = true;
        }
    }

    /// Record the currently visible tab for a space so it can be restored later.
    pub fn set_last_active_tab(&mut self, space_id: &SpaceId, tab_id: Option<TabId>) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            space.last_active_tab_id = tab_id;
        }
    }

    /// Get the last active tab for a space.
    pub fn get_last_active_tab(&self, space_id: &SpaceId) -> Option<&TabId> {
        self.spaces
            .get(space_id)
            .and_then(|s| s.last_active_tab_id.as_ref())
    }

    pub fn rename_space(&mut self, space_id: &SpaceId, name: &str) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            space.name = name.to_string();
        }
    }

    pub fn recolor_space(&mut self, space_id: &SpaceId, color: SpaceColor) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            space.color = color.clone();
        }
    }

    pub fn get_space(&self, space_id: &SpaceId) -> Option<&Space> {
        self.spaces.get(space_id)
    }

    pub fn get_all_spaces(&self) -> Vec<&Space> {
        self.space_order
            .iter()
            .filter_map(|id| self.spaces.get(id))
            .collect()
    }

    pub fn restore_spaces(&mut self, spaces: Vec<Space>, active_space_id: SpaceId) {
        self.spaces.clear();
        self.space_order.clear();
        for space in &spaces {
            self.space_order.push(space.id.clone());
        }
        for space in spaces {
            self.spaces.insert(space.id.clone(), space);
        }
        if self.spaces.contains_key(&active_space_id) {
            self.active_space_id = active_space_id.clone();
            for (id, space) in self.spaces.iter_mut() {
                space.is_active = *id == active_space_id;
            }
        } else if let Some(first_id) = self.space_order.first().cloned() {
            self.active_space_id = first_id.clone();
            if let Some(active_space) = self.spaces.get_mut(&first_id) {
                active_space.is_active = true;
            }
        }
    }

    pub fn get_active_space(&self) -> &Space {
        self.spaces
            .get(&self.active_space_id)
            .expect("No active space")
    }

    pub fn get_active_space_id(&self) -> &SpaceId {
        &self.active_space_id
    }

    pub fn reorder_space(&mut self, space_id: &SpaceId, from: usize, to: usize) {
        if from >= self.space_order.len() || to >= self.space_order.len() {
            return;
        }
        if self.space_order.get(from) != Some(space_id) {
            return;
        }
        self.space_order.remove(from);
        self.space_order.insert(to, space_id.clone());
    }

    pub fn get_space_order(&self) -> &[SpaceId] {
        &self.space_order
    }

    pub fn add_tab_to_space(&mut self, space_id: &SpaceId, tab_id: TabId, position: Option<usize>) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            if space.tab_order.iter().any(|existing| existing == &tab_id) {
                return;
            }
            match position {
                Some(pos) if pos <= space.tab_order.len() => {
                    space.tab_order.insert(pos, tab_id);
                }
                _ => {
                    space.tab_order.push(tab_id);
                }
            }
        }
    }

    pub fn remove_tab_from_space(&mut self, space_id: &SpaceId, tab_id: &TabId) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            if let Some(idx) = space.tab_order.iter().position(|id| id == tab_id) {
                space.tab_order.remove(idx);
            }
        }
    }

    pub fn reorder_tab(&mut self, space_id: &SpaceId, tab_id: &TabId, before_tab_id: Option<&TabId>) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            if let Some(current_pos) = space.tab_order.iter().position(|id| id == tab_id) {
                space.tab_order.remove(current_pos);
                let insert_at = match before_tab_id {
                    Some(before_id) => space
                        .tab_order
                        .iter()
                        .position(|id| id == before_id)
                        .unwrap_or(space.tab_order.len()),
                    None => space.tab_order.len(),
                };
                space.tab_order.insert(insert_at, tab_id.clone());
            }
        }
    }

    pub fn move_tab_to_root(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        tab_id: &TabId,
        before_tab_id: Option<&TabId>,
    ) {
        self.remove_tab_from_folder(space_id, folder_id, tab_id);
        self.reorder_tab(space_id, tab_id, before_tab_id);
    }

    pub fn reorder_folder(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        parent_folder_id: Option<&FolderId>,
        before_folder_id: Option<&FolderId>,
    ) {
        let space = match self.spaces.get_mut(space_id) {
            Some(s) => s,
            None => return,
        };

        if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
            folder.parent_folder_id = parent_folder_id.cloned();
        }

        let folder_pos = match space.folders.iter().position(|f| f.id == *folder_id) {
            Some(p) => p,
            None => return,
        };
        let folder = space.folders.remove(folder_pos);

        let insert_at = match before_folder_id {
            Some(before_id) => space
                .folders
                .iter()
                .position(|f| f.id == *before_id)
                .unwrap_or(space.folders.len()),
            None => {
                let last_sibling = space
                    .folders
                    .iter()
                    .rposition(|f| f.parent_folder_id.as_ref() == parent_folder_id);
                match last_sibling {
                    Some(pos) => pos + 1,
                    None => space.folders.len(),
                }
            }
        };

        space.folders.insert(insert_at, folder);
    }

    pub fn move_folder_to_root(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        before_folder_id: Option<&FolderId>,
    ) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                folder.parent_folder_id = None;
            }
        }

        let space = match self.spaces.get_mut(space_id) {
            Some(s) => s,
            None => return,
        };

        let folder_pos = match space.folders.iter().position(|f| f.id == *folder_id) {
            Some(p) => p,
            None => return,
        };
        let folder = space.folders.remove(folder_pos);

        let insert_at = match before_folder_id {
            Some(before_id) => space
                .folders
                .iter()
                .position(|f| f.id == *before_id)
                .unwrap_or(space.folders.len()),
            None => {
                let last_root = space
                    .folders
                    .iter()
                    .rposition(|f| f.parent_folder_id.is_none());
                match last_root {
                    Some(pos) => pos + 1,
                    None => space.folders.len(),
                }
            }
        };

        space.folders.insert(insert_at, folder);
    }

    pub fn to_view_model(&self, space: &Space) -> SpaceViewModel {
        SpaceViewModel {
            id: space.id.clone(),
            name: space.name.clone(),
            color: space.color.clone(),
            theme: space.theme.clone(),
            tab_count: space.tab_order.len(),
            is_active: space.is_active,
            icon: space.icon.clone(),
            order_index: self.space_order.iter().position(|id| id == &space.id),
            profile_id: Some(space.profile_id.clone()),
            profile_name: None,
        }
    }

    pub fn create_folder(
        &mut self,
        space_id: &SpaceId,
        name: &str,
        is_pinned: bool,
    ) -> Option<FolderViewModel> {
        let space = self.spaces.get_mut(space_id)?;
        let folder = Folder {
            id: FolderId::generate(),
            name: name.to_string(),
            tab_ids: vec![],
            is_expanded: true,
            is_pinned,
            parent_folder_id: None,
        };
        let vm = FolderViewModel {
            id: folder.id.clone(),
            name: folder.name.clone(),
            tab_count: folder.tab_ids.len(),
            is_expanded: folder.is_expanded,
            is_pinned: folder.is_pinned,
            tab_ids: folder.tab_ids.clone(),
            parent_folder_id: folder.parent_folder_id.clone(),
        };
        space.folders.push(folder);
        Some(vm)
    }

    pub fn rename_folder(&mut self, space_id: &SpaceId, folder_id: &FolderId, name: &str) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                folder.name = name.to_string();
            }
        }
    }

    pub fn delete_folder(&mut self, space_id: &SpaceId, folder_id: &FolderId) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            space.folders.retain(|f| f.id != *folder_id);
        }
    }

    pub fn add_tab_to_folder(&mut self, space_id: &SpaceId, folder_id: &FolderId, tab_id: TabId) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                if !folder.tab_ids.contains(&tab_id) {
                    folder.tab_ids.push(tab_id);
                }
            }
        }
    }

    pub fn reorder_tab_in_folder(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        tab_id: &TabId,
        to: usize,
    ) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                if let Some(from) = folder.tab_ids.iter().position(|t| t == tab_id) {
                    let tab = folder.tab_ids.remove(from);
                    let insert_at = to.min(folder.tab_ids.len());
                    folder.tab_ids.insert(insert_at, tab);
                }
            }
        }
    }

    pub fn remove_tab_from_folder(
        &mut self,
        space_id: &SpaceId,
        folder_id: &FolderId,
        tab_id: &TabId,
    ) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                folder.tab_ids.retain(|id| id != tab_id);
            }
        }
    }

    pub fn toggle_folder_expanded(&mut self, space_id: &SpaceId, folder_id: &FolderId) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                folder.is_expanded = !folder.is_expanded;
            }
        }
    }

    pub fn pin_folder(&mut self, space_id: &SpaceId, folder_id: &FolderId) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                folder.is_pinned = true;
            }
        }
    }

    pub fn unpin_folder(&mut self, space_id: &SpaceId, folder_id: &FolderId) {
        if let Some(space) = self.spaces.get_mut(space_id) {
            if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                folder.is_pinned = false;
            }
        }
    }

    pub fn get_folder_view_models(&self, space_id: &SpaceId) -> Vec<FolderViewModel> {
        self.spaces
            .get(space_id)
            .map(|space| {
                space
                    .folders
                    .iter()
                    .map(|f| self.folder_to_view_model(f))
                    .collect()
            })
            .unwrap_or_default()
    }

    pub fn move_folder(&mut self, folder_id: &FolderId, new_parent: Option<FolderId>) {
        for space in self.spaces.values_mut() {
            if let Some(folder) = space.folders.iter_mut().find(|f| f.id == *folder_id) {
                folder.parent_folder_id = new_parent;
                return;
            }
        }
    }

    pub fn get_subfolders(&self, space_id: &SpaceId, parent_id: Option<&FolderId>) -> Vec<&Folder> {
        self.spaces
            .get(space_id)
            .map(|space| {
                space
                    .folders
                    .iter()
                    .filter(|f| f.parent_folder_id.as_ref() == parent_id)
                    .collect()
            })
            .unwrap_or_default()
    }

    fn folder_to_view_model(&self, folder: &Folder) -> FolderViewModel {
        FolderViewModel {
            id: folder.id.clone(),
            name: folder.name.clone(),
            tab_count: folder.tab_ids.len(),
            is_expanded: folder.is_expanded,
            is_pinned: folder.is_pinned,
            tab_ids: folder.tab_ids.clone(),
            parent_folder_id: folder.parent_folder_id.clone(),
        }
    }

    pub fn update_space_config(
        &mut self,
        changes: SpaceConfigUpdate,
    ) -> Result<Option<SpaceConfigUpdate>, SpaceUpdateError> {
        if let Some(ref name) = changes.name {
            let normalized = name.trim();
            if normalized.is_empty() {
                return Err(SpaceUpdateError::EmptyName);
            }
            if self
                .spaces
                .values()
                .any(|space| space.id != changes.space_id && space.name == normalized)
            {
                return Err(SpaceUpdateError::DuplicateName);
            }
        }

        if let Some(space) = self.spaces.get_mut(&changes.space_id) {
            let mut applied = SpaceConfigUpdate {
                space_id: changes.space_id.clone(),
                name: None,
                color: None,
                theme: None,
                icon: None,
                profile_id: None,
            };
            if let Some(ref n) = changes.name {
                let normalized = n.trim().to_string();
                space.name = normalized.clone();
                applied.name = Some(normalized);
            }
            if let Some(ref c) = changes.color {
                space.color = c.clone();
                applied.color = Some(c.clone());
            }
            if let Some(ref theme) = changes.theme {
                space.theme = Some(theme.clone());
                applied.theme = Some(theme.clone());
            }
            if let Some(ref i) = changes.icon {
                space.icon = i.clone();
                applied.icon = Some(i.clone());
            }
            if let Some(ref profile_id) = changes.profile_id {
                space.profile_id = profile_id.clone();
                applied.profile_id = Some(profile_id.clone());
            }
            Ok(Some(applied))
        } else {
            Ok(None)
        }
    }

    pub fn get_space_configs(&self) -> Vec<(SpaceId, String, SpaceColor)> {
        self.space_order
            .iter()
            .filter_map(|id| {
                self.spaces
                    .get(id)
                    .map(|s| (s.id.clone(), s.name.clone(), s.color.clone()))
            })
            .collect()
    }

    pub fn get_space_ids_for_profile(
        &self,
        profile_id: &ProfileId,
    ) -> std::collections::HashSet<SpaceId> {
        self.spaces
            .values()
            .filter(|s| s.profile_id == *profile_id)
            .map(|s| s.id.clone())
            .collect()
    }

    pub fn get_tab_order(&self, space_id: &SpaceId) -> Vec<TabId> {
        self.spaces
            .get(space_id)
            .map(|s| s.tab_order.clone())
            .unwrap_or_default()
    }

    pub fn get_all_atc_rules(&self) -> Vec<(SpaceId, Vec<maho_types::space::ATCRule>)> {
        self.spaces
            .values()
            .map(|s| (s.id.clone(), s.atc_rules.clone()))
            .collect()
    }
}
