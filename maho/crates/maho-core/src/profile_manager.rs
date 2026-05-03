use maho_types::identifiers::ProfileId;
use maho_types::profile::ProfileConfig;

pub struct ProfileManager {
    profiles: Vec<ProfileConfig>,
    active_profile_id: Option<ProfileId>,
}

impl ProfileManager {
    pub fn new() -> Self {
        let default_profile = ProfileConfig::new_default("Default".to_string());
        let active_id = default_profile.id.clone();
        Self {
            profiles: vec![default_profile],
            active_profile_id: Some(active_id),
        }
    }

    pub fn list_profiles(&self) -> &[ProfileConfig] {
        &self.profiles
    }

    pub fn get_profile(&self, id: &ProfileId) -> Option<&ProfileConfig> {
        self.profiles.iter().find(|p| &p.id == id)
    }

    pub fn get_active_profile_id(&self) -> Option<&ProfileId> {
        self.active_profile_id.as_ref()
    }

    pub fn get_active_profile(&self) -> Option<&ProfileConfig> {
        self.active_profile_id
            .as_ref()
            .and_then(|id| self.profiles.iter().find(|p| &p.id == id))
    }

    pub fn create_profile(&mut self, name: String) -> &ProfileConfig {
        let profile = ProfileConfig::new(name);
        self.profiles.push(profile);
        self.profiles.last().unwrap()
    }

    pub fn delete_profile(&mut self, id: &ProfileId) -> bool {
        if self.profiles.len() <= 1 {
            return false; // Can't delete last profile
        }
        let initial_len = self.profiles.len();
        self.profiles.retain(|p| &p.id != id);
        if self.profiles.len() < initial_len {
            // If active profile was deleted, switch to first
            if self.active_profile_id.as_ref() == Some(id) {
                self.active_profile_id = self.profiles.first().map(|p| p.id.clone());
            }
            true
        } else {
            false
        }
    }

    pub fn update_profile(
        &mut self,
        id: &ProfileId,
        name: Option<String>,
        avatar_color: Option<String>,
        download_path: Option<String>,
        archive_timeout_hours: Option<Option<f64>>,
    ) -> Option<&ProfileConfig> {
        let profile = self.profiles.iter_mut().find(|p| &p.id == id)?;
        if let Some(name) = name {
            profile.name = name;
        }
        if let Some(color) = avatar_color {
            profile.avatar_color = color;
        }
        if let Some(path) = download_path {
            profile.download_path = path;
        }
        if let Some(hours) = archive_timeout_hours {
            profile.archive_timeout_hours = hours;
        }
        let profile = self.profiles.iter().find(|p| &p.id == id)?;
        Some(profile)
    }

    pub fn switch_profile(&mut self, id: &ProfileId) -> bool {
        if self.profiles.iter().any(|p| &p.id == id) {
            self.active_profile_id = Some(id.clone());
            true
        } else {
            false
        }
    }

    pub fn restore_profiles(
        &mut self,
        profiles: Vec<ProfileConfig>,
        active_profile_id: Option<ProfileId>,
    ) {
        if profiles.is_empty() {
            return; // Keep default profile if nothing to restore
        }
        self.profiles = profiles;
        if let Some(ref id) = active_profile_id {
            if self.profiles.iter().any(|p| &p.id == id) {
                self.active_profile_id = active_profile_id;
            } else {
                self.active_profile_id = self.profiles.first().map(|p| p.id.clone());
            }
        } else {
            self.active_profile_id = self.profiles.first().map(|p| p.id.clone());
        }
    }
}

impl Default for ProfileManager {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_new_creates_default_profile() {
        let manager = ProfileManager::new();
        assert_eq!(manager.list_profiles().len(), 1);
        let profile = &manager.list_profiles()[0];
        assert_eq!(profile.name, "Default");
        assert_eq!(manager.get_active_profile_id(), Some(&profile.id));
    }

    #[test]
    fn test_create_profile() {
        let mut manager = ProfileManager::new();
        let created = manager.create_profile("Work".to_string());
        assert_eq!(created.name, "Work");
        assert_eq!(manager.list_profiles().len(), 2);
        assert_eq!(manager.list_profiles()[1].name, "Work");
    }

    #[test]
    fn test_delete_last_profile_blocked() {
        let mut manager = ProfileManager::new();
        let id = manager.list_profiles()[0].id.clone();
        assert!(!manager.delete_profile(&id));
        assert_eq!(manager.list_profiles().len(), 1);
    }

    #[test]
    fn test_delete_active_profile_reassigns() {
        let mut manager = ProfileManager::new();
        let default_id = manager.list_profiles()[0].id.clone();
        manager.create_profile("Work".to_string());
        let work_id = manager.list_profiles()[1].id.clone();
        manager.switch_profile(&work_id);
        assert_eq!(manager.get_active_profile_id(), Some(&work_id));
        assert!(manager.delete_profile(&work_id));
        assert_eq!(manager.list_profiles().len(), 1);
        assert_eq!(manager.get_active_profile_id(), Some(&default_id));
    }

    #[test]
    fn test_delete_nonexistent_profile() {
        let mut manager = ProfileManager::new();
        let fake_id = ProfileId::generate();
        assert!(!manager.delete_profile(&fake_id));
        assert_eq!(manager.list_profiles().len(), 1);
    }

    #[test]
    fn test_switch_profile() {
        let mut manager = ProfileManager::new();
        manager.create_profile("Work".to_string());
        let work_id = manager.list_profiles()[1].id.clone();
        assert!(manager.switch_profile(&work_id));
        assert_eq!(manager.get_active_profile_id(), Some(&work_id));
    }

    #[test]
    fn test_switch_to_invalid_profile() {
        let mut manager = ProfileManager::new();
        let default_id = manager.list_profiles()[0].id.clone();
        let fake_id = ProfileId::generate();
        assert!(!manager.switch_profile(&fake_id));
        assert_eq!(manager.get_active_profile_id(), Some(&default_id));
    }

    #[test]
    fn test_update_profile() {
        let mut manager = ProfileManager::new();
        let id = manager.list_profiles()[0].id.clone();
        let updated = manager
            .update_profile(
                &id,
                Some("Updated".to_string()),
                Some("#FF0000".to_string()),
                Some("/tmp/downloads".to_string()),
                None,
            )
            .unwrap();
        assert_eq!(updated.name, "Updated");
        assert_eq!(updated.avatar_color, "#FF0000");
        assert_eq!(updated.download_path, "/tmp/downloads");
    }

    #[test]
    fn test_restore_profiles() {
        let mut manager = ProfileManager::new();
        let p1 = ProfileConfig::new("One".to_string());
        let p2 = ProfileConfig::new("Two".to_string());
        let active = p2.id.clone();
        manager.restore_profiles(vec![p1.clone(), p2.clone()], Some(active.clone()));
        assert_eq!(manager.list_profiles().len(), 2);
        assert_eq!(manager.get_active_profile_id(), Some(&active));
    }

    #[test]
    fn test_restore_empty_profiles() {
        let mut manager = ProfileManager::new();
        let default_id = manager.list_profiles()[0].id.clone();
        manager.restore_profiles(vec![], None);
        assert_eq!(manager.list_profiles().len(), 1);
        assert_eq!(manager.list_profiles()[0].id, default_id);
        assert_eq!(manager.get_active_profile_id(), Some(&default_id));
    }

    #[test]
    fn test_restore_profiles_invalid_active_id() {
        let mut manager = ProfileManager::new();
        let p1 = ProfileConfig::new("One".to_string());
        let p2 = ProfileConfig::new("Two".to_string());
        let fake_id = ProfileId::generate();
        manager.restore_profiles(vec![p1.clone(), p2.clone()], Some(fake_id));
        assert_eq!(manager.list_profiles().len(), 2);
        assert_eq!(manager.get_active_profile_id(), Some(&p1.id));
    }

    #[test]
    fn test_get_profile() {
        let manager = ProfileManager::new();
        let existing_id = manager.list_profiles()[0].id.clone();
        let fake_id = ProfileId::generate();
        assert!(manager.get_profile(&existing_id).is_some());
        assert!(manager.get_profile(&fake_id).is_none());
    }

    #[test]
    fn test_data_store_id_uniqueness() {
        let mut manager = ProfileManager::new();
        manager.create_profile("One".to_string());
        manager.create_profile("Two".to_string());
        let profiles = manager.list_profiles();
        let id1 = profiles[1].data_store_id.as_ref().unwrap();
        let id2 = profiles[2].data_store_id.as_ref().unwrap();
        assert_ne!(id1, id2);
    }

    #[test]
    fn test_default_profile_has_no_data_store() {
        let manager = ProfileManager::new();
        let default_profile = &manager.list_profiles()[0];
        assert!(default_profile.data_store_id.is_none());
    }
}
