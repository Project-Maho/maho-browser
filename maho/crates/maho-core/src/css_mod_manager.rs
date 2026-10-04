use std::collections::HashMap;

use maho_types::css_mod::CssMod;

pub struct CssModManager {
    mods: HashMap<String, CssMod>,
}

impl CssModManager {
    pub fn new() -> Self {
        Self {
            mods: HashMap::new(),
        }
    }

    pub fn restore_mods(&mut self, mods: Vec<CssMod>) {
        for m in mods {
            self.mods.insert(m.id.clone(), m);
        }
    }

    pub fn install_mod(&mut self, css_mod: CssMod) -> String {
        let id = css_mod.id.clone();
        self.mods.insert(id.clone(), css_mod);
        id
    }

    pub fn uninstall_mod(&mut self, id: &str) -> Option<CssMod> {
        self.mods.remove(id)
    }

    pub fn toggle_mod(&mut self, id: &str) -> Option<bool> {
        if let Some(m) = self.mods.get_mut(id) {
            m.enabled = !m.enabled;
            Some(m.enabled)
        } else {
            None
        }
    }

    pub fn update_mod_css(&mut self, id: &str, css: String, updated_at: &str) -> Option<&CssMod> {
        if let Some(m) = self.mods.get_mut(id) {
            m.css = css;
            m.updated_at = updated_at.to_string();
            Some(m)
        } else {
            None
        }
    }

    pub fn get_mod(&self, id: &str) -> Option<&CssMod> {
        self.mods.get(id)
    }

    pub fn get_all_mods(&self) -> Vec<&CssMod> {
        let mut mods: Vec<_> = self.mods.values().collect();
        mods.sort_by(|a, b| a.name.cmp(&b.name));
        mods
    }

    pub fn get_enabled_mods(&self) -> Vec<&CssMod> {
        let mut mods: Vec<_> = self.mods.values().filter(|m| m.enabled).collect();
        mods.sort_by(|a, b| a.name.cmp(&b.name));
        mods
    }

    /// Generates combined CSS from all enabled mods.
    pub fn generate_combined_css(&self) -> String {
        self.get_enabled_mods()
            .iter()
            .map(|m| format!("/* Mod: {} */\n{}", m.name, m.css))
            .collect::<Vec<_>>()
            .join("\n\n")
    }
}

impl Default for CssModManager {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_install_and_toggle_mod() {
        let mut mgr = CssModManager::new();
        let m = CssMod {
            id: "m1".into(),
            name: "Test".into(),
            description: None,
            author: None,
            version: None,
            css: "body {}".into(),
            enabled: true,
            homepage: None,
            source_url: None,
            created_at: "2024-01-01".into(),
            updated_at: "2024-01-01".into(),
        };
        mgr.install_mod(m);
        assert_eq!(mgr.get_all_mods().len(), 1);
        assert_eq!(mgr.get_enabled_mods().len(), 1);
        mgr.toggle_mod("m1");
        assert_eq!(mgr.get_enabled_mods().len(), 0);
        assert_eq!(mgr.get_all_mods().len(), 1);
    }

    #[test]
    fn test_generate_combined_css() {
        let mut mgr = CssModManager::new();
        mgr.install_mod(CssMod {
            id: "m1".into(),
            name: "A".into(),
            description: None,
            author: None,
            version: None,
            css: "a { color: red; }".into(),
            enabled: true,
            homepage: None,
            source_url: None,
            created_at: "2024-01-01".into(),
            updated_at: "2024-01-01".into(),
        });
        mgr.install_mod(CssMod {
            id: "m2".into(),
            name: "B".into(),
            description: None,
            author: None,
            version: None,
            css: "b { color: blue; }".into(),
            enabled: false,
            homepage: None,
            source_url: None,
            created_at: "2024-01-01".into(),
            updated_at: "2024-01-01".into(),
        });
        let combined = mgr.generate_combined_css();
        assert!(combined.contains("a { color: red; }"));
        assert!(!combined.contains("b { color: blue; }"));
    }
}
