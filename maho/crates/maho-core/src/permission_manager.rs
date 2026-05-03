use std::collections::HashMap;

use maho_types::settings::PermissionPolicy;

pub struct PermissionManager {
    per_site: HashMap<String, HashMap<String, PermissionPolicy>>,
}

impl Default for PermissionManager {
    fn default() -> Self {
        Self::new()
    }
}

impl PermissionManager {
    pub fn new() -> Self {
        Self {
            per_site: HashMap::new(),
        }
    }

    pub fn set_permission(&mut self, origin: &str, permission: &str, policy: PermissionPolicy) {
        self.per_site
            .entry(origin.to_string())
            .or_default()
            .insert(permission.to_string(), policy);
    }

    pub fn get_permission(&self, origin: &str, permission: &str) -> PermissionPolicy {
        self.per_site
            .get(origin)
            .and_then(|perms| perms.get(permission))
            .cloned()
            .unwrap_or(PermissionPolicy::Ask)
    }

    pub fn get_permissions_for_origin(&self, origin: &str) -> HashMap<String, PermissionPolicy> {
        self.per_site.get(origin).cloned().unwrap_or_default()
    }

    pub fn get_all_permissions(&self) -> &HashMap<String, HashMap<String, PermissionPolicy>> {
        &self.per_site
    }

    pub fn reset_permissions(&mut self, origin: &str) {
        self.per_site.remove(origin);
    }

    pub fn reset_all(&mut self) {
        self.per_site.clear();
    }
}
