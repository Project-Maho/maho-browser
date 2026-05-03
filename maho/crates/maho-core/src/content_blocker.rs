use serde::{Deserialize, Serialize};

use adblock::cosmetic_filter_cache::UrlSpecificResources;
use adblock::lists::{FilterSet, ParseOptions};
use adblock::request::Request;
use adblock::Engine;

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FilterList {
    pub id: String,
    pub name: String,
    pub url: String,
    pub enabled: bool,
    pub rule_count: usize,
    pub last_updated: Option<String>,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CosmeticResourcesResponse {
    pub hide_selectors: Vec<String>,
    pub injected_script: Option<String>,
    pub generichide: bool,
}

pub struct ContentBlocker {
    engine: Engine,
    filter_lists: Vec<FilterList>,
    filter_list_contents: std::collections::HashMap<String, String>,
    enabled: bool,
    popup_blocking_enabled: bool,
}

impl Default for ContentBlocker {
    fn default() -> Self {
        Self::new()
    }
}

impl ContentBlocker {
    pub fn new() -> Self {
        let filter_set = FilterSet::new(false);
        Self {
            engine: Engine::from_filter_set(filter_set, true),
            filter_lists: Vec::new(),
            filter_list_contents: std::collections::HashMap::new(),
            enabled: true,
            popup_blocking_enabled: true,
        }
    }

    pub fn set_enabled(&mut self, enabled: bool) {
        self.enabled = enabled;
    }

    pub fn is_enabled(&self) -> bool {
        self.enabled
    }

    pub fn set_popup_blocking(&mut self, enabled: bool) {
        self.popup_blocking_enabled = enabled;
    }

    pub fn is_popup_blocking_enabled(&self) -> bool {
        self.popup_blocking_enabled
    }

    pub fn should_block_request(&self, url: &str, source_url: &str, request_type: &str) -> bool {
        if !self.enabled {
            return false;
        }

        let request = match Request::new(url, source_url, request_type) {
            Ok(r) => r,
            Err(_) => return false,
        };

        let result = self.engine.check_network_request(&request);
        result.matched
    }

    pub fn add_filter_list(&mut self, id: String, name: String, url: String) -> &FilterList {
        self.filter_lists.push(FilterList {
            id,
            name,
            url,
            enabled: true,
            rule_count: 0,
            last_updated: None,
        });
        // SAFETY: we just pushed, so last() is always Some
        self.filter_lists.last().expect("just pushed")
    }

    pub fn get_filter_lists(&self) -> &[FilterList] {
        &self.filter_lists
    }

    pub fn toggle_filter_list(&mut self, id: &str, enabled: bool) {
        if let Some(fl) = self.filter_lists.iter_mut().find(|f| f.id == id) {
            fl.enabled = enabled;
        }
    }

    pub fn remove_filter_list(&mut self, id: &str) {
        self.filter_lists.retain(|f| f.id != id);
        self.filter_list_contents.remove(id);
    }

    pub fn update_filter_list_content(&mut self, id: &str, content: String) {
        let count = content
            .lines()
            .filter(|line| {
                let trimmed = line.trim();
                !trimmed.is_empty() && !trimmed.starts_with('!')
            })
            .count();

        if let Some(fl) = self.filter_lists.iter_mut().find(|f| f.id == id) {
            fl.rule_count = count;
            fl.last_updated =
                Some(chrono::Utc::now().to_rfc3339_opts(chrono::SecondsFormat::Secs, true));
        }

        self.filter_list_contents.insert(id.to_string(), content);
    }

    pub fn rebuild_engine(&mut self) {
        let mut filter_set = FilterSet::new(false);

        for fl in &self.filter_lists {
            if !fl.enabled {
                continue;
            }
            if let Some(content) = self.filter_list_contents.get(&fl.id) {
                filter_set.add_filter_list(content, ParseOptions::default());
            }
        }

        if self.popup_blocking_enabled {
            filter_set.add_filter_list(
                "||*$popup,third-party",
                ParseOptions::default(),
            );
        }

        self.engine = Engine::from_filter_set(filter_set, true);
    }

    pub fn get_cosmetic_resources(&self, url: &str) -> CosmeticResourcesResponse {
        if !self.enabled {
            return CosmeticResourcesResponse::default();
        }
        let resources: UrlSpecificResources = self.engine.url_cosmetic_resources(url);
        CosmeticResourcesResponse {
            hide_selectors: resources.hide_selectors.into_iter().collect(),
            injected_script: if resources.injected_script.is_empty() {
                None
            } else {
                Some(resources.injected_script)
            },
            generichide: resources.generichide,
        }
    }

    pub fn get_rule_count(&self) -> usize {
        self.filter_lists
            .iter()
            .filter(|fl| fl.enabled)
            .map(|fl| fl.rule_count)
            .sum()
    }

    pub fn serialize_engine(&self) -> Vec<u8> {
        self.engine.serialize()
    }

    pub fn deserialize_engine(&mut self, data: &[u8]) -> Result<(), String> {
        self.engine
            .deserialize(data)
            .map_err(|e| format!("{e:?}"))
    }
}

pub fn default_filter_lists() -> Vec<(String, String, String)> {
    vec![
        (
            "easylist".to_string(),
            "EasyList".to_string(),
            "https://easylist.to/easylist/easylist.txt".to_string(),
        ),
        (
            "easyprivacy".to_string(),
            "EasyPrivacy".to_string(),
            "https://easylist.to/easylist/easyprivacy.txt".to_string(),
        ),
        (
            "fanboy-annoyance".to_string(),
            "Fanboy's Annoyance List".to_string(),
            "https://easylist.to/easylist/fanboy-annoyance.txt".to_string(),
        ),
    ]
}
