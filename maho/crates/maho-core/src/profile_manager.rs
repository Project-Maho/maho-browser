use maho_types::identifiers::ProfileId;
use maho_types::profile::{
    is_allowed_profile_archive_timeout, normalize_profile_avatar_color, ProfileConfig,
    ProfileDeleteOutcome,
};

#[derive(Debug, thiserror::Error, serde::Serialize, PartialEq, Eq, Clone)]
pub enum ProfileError {
    #[error("Profile name cannot be empty")]
    EmptyName,
    #[error("Profile with name '{0}' already exists")]
    DuplicateName(String),
    #[error("Maximum profile limit reached ({0})")]
    LimitReached(usize),
    #[error("Profile not found")]
    NotFound,
    #[error("Unsupported avatar color '{0}'")]
    InvalidAvatarColor(String),
    #[error("Unsupported archive timeout")]
    InvalidArchiveTimeout,
    #[error("Profile persistence failed: {0}")]
    PersistenceFailed(String),
    #[error("Internal error")]
    InternalError,
}

#[derive(Clone)]
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

    pub fn create_profile(&mut self, name: String) -> Result<&ProfileConfig, ProfileError> {
        let trimmed_name = name.trim().to_string();
        if trimmed_name.is_empty() {
            return Err(ProfileError::EmptyName);
        }

        // Limit check: max 20 profiles
        if self.profiles.len() >= 20 {
            return Err(ProfileError::LimitReached(20));
        }

        // Case-insensitive duplicate check
        let lower_name = trimmed_name.to_lowercase();
        if self
            .profiles
            .iter()
            .any(|p| p.name.trim().to_lowercase() == lower_name)
        {
            return Err(ProfileError::DuplicateName(trimmed_name));
        }

        let profile = ProfileConfig::new(trimmed_name);
        self.profiles.push(profile);
        self.profiles.last().ok_or(ProfileError::InternalError)
    }

    pub fn delete_profile(&mut self, id: &ProfileId) -> ProfileDeleteOutcome {
        let Some(index) = self.profiles.iter().position(|profile| &profile.id == id) else {
            return ProfileDeleteOutcome::NotFound;
        };
        if self.profiles[index].data_store_id.is_none() {
            return ProfileDeleteOutcome::Protected;
        }
        if self.profiles.len() <= 1 {
            return ProfileDeleteOutcome::FinalProfile;
        }

        self.profiles.remove(index);
        if self.active_profile_id.as_ref() == Some(id) {
            self.active_profile_id = self.profiles.first().map(|profile| profile.id.clone());
        }
        ProfileDeleteOutcome::Deleted
    }

    pub fn update_profile(
        &mut self,
        id: &ProfileId,
        name: Option<String>,
        avatar_color: Option<String>,
        download_path: Option<String>,
        archive_timeout_hours: Option<Option<f64>>,
    ) -> Result<&ProfileConfig, ProfileError> {
        if !self.profiles.iter().any(|profile| &profile.id == id) {
            return Err(ProfileError::NotFound);
        }

        let normalized_name = match name {
            Some(name) => {
                let trimmed = name.trim().to_string();
                if trimmed.is_empty() {
                    return Err(ProfileError::EmptyName);
                }
                let lower_name = trimmed.to_lowercase();
                if self.profiles.iter().any(|profile| {
                    &profile.id != id && profile.name.trim().to_lowercase() == lower_name
                }) {
                    return Err(ProfileError::DuplicateName(trimmed));
                }
                Some(trimmed)
            }
            None => None,
        };
        let normalized_color = match avatar_color {
            Some(color) => Some(
                normalize_profile_avatar_color(&color)
                    .ok_or(ProfileError::InvalidAvatarColor(color))?,
            ),
            None => None,
        };
        if archive_timeout_hours
            .as_ref()
            .is_some_and(|hours| !is_allowed_profile_archive_timeout(*hours))
        {
            return Err(ProfileError::InvalidArchiveTimeout);
        }

        let profile = self
            .profiles
            .iter_mut()
            .find(|profile| &profile.id == id)
            .ok_or(ProfileError::NotFound)?;
        if let Some(name) = normalized_name {
            profile.name = name;
        }
        if let Some(color) = normalized_color {
            profile.avatar_color = color;
        }
        // Legacy compatibility only: profile download_path remains round-trippable,
        // but platform-owned download settings are authoritative.
        if let Some(path) = download_path {
            profile.download_path = path;
        }
        if let Some(hours) = archive_timeout_hours {
            profile.archive_timeout_hours = hours;
        }
        Ok(profile)
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
        let created = manager.create_profile("Work".to_string()).unwrap();
        assert_eq!(created.name, "Work");
        assert_eq!(manager.list_profiles().len(), 2);
        assert_eq!(manager.list_profiles()[1].name, "Work");
    }

    #[test]
    fn test_delete_last_profile_blocked() {
        let mut manager = ProfileManager::new();
        let id = manager.list_profiles()[0].id.clone();
        assert_eq!(manager.delete_profile(&id), ProfileDeleteOutcome::Protected);
        assert_eq!(manager.list_profiles().len(), 1);
    }

    #[test]
    fn test_delete_active_profile_reassigns() {
        let mut manager = ProfileManager::new();
        let default_id = manager.list_profiles()[0].id.clone();
        manager.create_profile("Work".to_string()).unwrap();
        let work_id = manager.list_profiles()[1].id.clone();
        manager.switch_profile(&work_id);
        assert_eq!(manager.get_active_profile_id(), Some(&work_id));
        assert_eq!(
            manager.delete_profile(&work_id),
            ProfileDeleteOutcome::Deleted
        );
        assert_eq!(manager.list_profiles().len(), 1);
        assert_eq!(manager.get_active_profile_id(), Some(&default_id));
    }

    #[test]
    fn test_delete_nonexistent_profile() {
        let mut manager = ProfileManager::new();
        let fake_id = ProfileId::generate();
        assert_eq!(
            manager.delete_profile(&fake_id),
            ProfileDeleteOutcome::NotFound
        );
        assert_eq!(manager.list_profiles().len(), 1);
    }

    #[test]
    fn test_delete_default_profile_is_protected_even_with_other_profiles() {
        let mut manager = ProfileManager::new();
        let default_id = manager.list_profiles()[0].id.clone();
        manager.create_profile("Work".to_string()).unwrap();

        assert_eq!(
            manager.delete_profile(&default_id),
            ProfileDeleteOutcome::Protected
        );
        assert_eq!(manager.list_profiles().len(), 2);
    }

    #[test]
    fn test_delete_final_non_default_profile_is_distinguishable() {
        let mut manager = ProfileManager::new();
        let profile = ProfileConfig::new("Only".to_string());
        let profile_id = profile.id.clone();
        manager.restore_profiles(vec![profile], Some(profile_id.clone()));

        assert_eq!(
            manager.delete_profile(&profile_id),
            ProfileDeleteOutcome::FinalProfile
        );
        assert_eq!(manager.list_profiles().len(), 1);
    }

    #[test]
    fn test_switch_profile() {
        let mut manager = ProfileManager::new();
        manager.create_profile("Work".to_string()).unwrap();
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
    fn test_update_profile_validates_metadata_atomically() {
        let mut manager = ProfileManager::new();
        let default_id = manager.list_profiles()[0].id.clone();
        let work_id = manager
            .create_profile("Work".to_string())
            .unwrap()
            .id
            .clone();

        let updated = manager
            .update_profile(
                &work_id,
                Some("  Focus  ".to_string()),
                Some("#4a90d9".to_string()),
                None,
                Some(None),
            )
            .unwrap();
        assert_eq!(updated.name, "Focus");
        assert_eq!(updated.avatar_color, "#4A90D9");
        assert_eq!(updated.archive_timeout_hours, None);

        assert!(matches!(
            manager.update_profile(&work_id, Some("   ".to_string()), None, None, None),
            Err(ProfileError::EmptyName)
        ));
        assert!(matches!(
            manager.update_profile(&work_id, Some(" default ".to_string()), None, None, None),
            Err(ProfileError::DuplicateName(_))
        ));
        assert!(matches!(
            manager.update_profile(&work_id, None, Some("blue".to_string()), None, None),
            Err(ProfileError::InvalidAvatarColor(_))
        ));
        assert!(matches!(
            manager.update_profile(&work_id, None, None, None, Some(Some(48.0))),
            Err(ProfileError::InvalidArchiveTimeout)
        ));

        let unchanged = manager.get_profile(&work_id).unwrap();
        assert_eq!(unchanged.name, "Focus");
        assert_eq!(unchanged.avatar_color, "#4A90D9");
        assert_eq!(manager.get_active_profile_id(), Some(&default_id));
    }

    #[test]
    fn test_update_profile_accepts_all_supported_archive_timeouts() {
        let mut manager = ProfileManager::new();
        let id = manager.list_profiles()[0].id.clone();

        for hours in maho_types::profile::ALLOWED_PROFILE_ARCHIVE_TIMEOUT_HOURS {
            let updated = manager
                .update_profile(&id, None, None, None, Some(Some(*hours)))
                .unwrap();
            assert_eq!(updated.archive_timeout_hours, Some(*hours));
        }
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
        manager.create_profile("One".to_string()).unwrap();
        manager.create_profile("Two".to_string()).unwrap();
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

    #[test]
    fn test_create_profile_validation() {
        let mut manager = ProfileManager::new();

        // Empty/whitespace name rejections
        assert!(matches!(
            manager.create_profile("".to_string()),
            Err(ProfileError::EmptyName)
        ));
        assert!(matches!(
            manager.create_profile("   ".to_string()),
            Err(ProfileError::EmptyName)
        ));

        // Duplicate case-insensitive name rejections
        manager.create_profile("Work".to_string()).unwrap();
        assert!(matches!(
            manager.create_profile("work".to_string()),
            Err(ProfileError::DuplicateName(_))
        ));
        assert!(matches!(
            manager.create_profile(" WORK ".to_string()),
            Err(ProfileError::DuplicateName(_))
        ));

        // Limit rejection (max 20 profiles)
        // Currently 2 profiles exist (Default, Work). Let's create 18 more.
        for i in 1..=18 {
            manager.create_profile(format!("Profile {}", i)).unwrap();
        }
        assert_eq!(manager.list_profiles().len(), 20);

        // 21st profile must fail
        assert!(matches!(
            manager.create_profile("Extra Profile".to_string()),
            Err(ProfileError::LimitReached(20))
        ));
    }
}
