use std::collections::HashSet;

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

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BlockResult {
    pub blocked: bool,
    pub redirect: Option<String>,
    pub rewritten_url: Option<String>,
}

pub struct ContentBlocker {
    engine: Engine,
    filter_lists: Vec<FilterList>,
    filter_list_contents: std::collections::HashMap<String, String>,
    site_exceptions: HashSet<String>,
    enabled: bool,
    popup_blocking_enabled: bool,
}

impl Default for ContentBlocker {
    fn default() -> Self {
        Self::new()
    }
}

fn extract_origin_from_source_url(source_url: &str) -> Option<&str> {
    let without_scheme = source_url
        .split_once("://")
        .map(|(_, rest)| rest)
        .unwrap_or(source_url);
    let without_path = without_scheme
        .split(['/', '?', '#'])
        .next()
        .unwrap_or(without_scheme);
    let without_userinfo = without_path.rsplit_once('@').map(|(_, host)| host).unwrap_or(without_path);
    let origin = if without_userinfo.starts_with('[') {
        // IPv6 bracket notation — don't strip port via naive rsplit
        without_userinfo
    } else {
        without_userinfo
            .rsplit_once(':')
            .map(|(host, _)| host)
            .unwrap_or(without_userinfo)
    };
    if origin.is_empty() {
        None
    } else {
        Some(origin)
    }
}

impl ContentBlocker {
    pub fn new() -> Self {
        let filter_set = FilterSet::new(false);
        Self {
            engine: Engine::from_filter_set(filter_set, true),
            filter_lists: Vec::new(),
            filter_list_contents: std::collections::HashMap::new(),
            site_exceptions: HashSet::new(),
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

    pub fn add_site_exception(&mut self, origin: String) {
        self.site_exceptions.insert(origin);
    }

    pub fn remove_site_exception(&mut self, origin: &str) {
        self.site_exceptions.remove(origin);
    }

    pub fn get_site_exceptions(&self) -> Vec<&str> {
        self.site_exceptions.iter().map(|s| s.as_str()).collect()
    }

    pub fn is_site_excepted(&self, origin: &str) -> bool {
        self.site_exceptions.contains(origin)
    }

    pub fn should_block_request(&self, url: &str, source_url: &str, request_type: &str) -> bool {
        if !self.enabled {
            return false;
        }

        if let Some(origin) = extract_origin_from_source_url(source_url) {
            if self.site_exceptions.contains(origin) {
                return false;
            }
        }

        let request = match Request::new(url, source_url, request_type) {
            Ok(r) => r,
            Err(_) => return false,
        };

        let result = self.engine.check_network_request(&request);
        result.matched
    }

    pub fn check_request(&self, url: &str, source_url: &str, request_type: &str) -> BlockResult {
        if !self.enabled {
            return BlockResult::default();
        }

        if let Some(origin) = extract_origin_from_source_url(source_url) {
            if self.site_exceptions.contains(origin) {
                return BlockResult::default();
            }
        }

        let request = match Request::new(url, source_url, request_type) {
            Ok(r) => r,
            Err(_) => return BlockResult::default(),
        };

        let result = self.engine.check_network_request(&request);
        BlockResult {
            blocked: result.matched,
            redirect: result.redirect,
            rewritten_url: result.rewritten_url,
        }
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
            filter_set.add_filter_list("||*$popup,third-party", ParseOptions::default());
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

    pub fn get_content_rules(&self) -> String {
        if !self.enabled {
            return "[]".to_string();
        }

        let mut rules: serde_json::Value = serde_json::Value::Array(Vec::new());
        let rules_arr = rules.as_array_mut().unwrap();

        // 1. Add site exceptions first (ignore-previous-rules)
        for origin in &self.site_exceptions {
            let escaped = regex::escape(origin);
            rules_arr.push(serde_json::json!({
                "trigger": {
                    "url-filter": format!("^https?://([^/]+\\.)?{}(/|$)", escaped)
                },
                "action": {
                    "type": "ignore-previous-rules"
                }
            }));
        }

        // 2. Translate filter lists
        for fl in &self.filter_lists {
            if !fl.enabled {
                continue;
            }
            if let Some(content) = self.filter_list_contents.get(&fl.id) {
                for line in content.lines() {
                    let line = line.trim();
                    if line.is_empty() || line.starts_with('!') || line.starts_with('[') {
                        continue;
                    }

                    // Cosmetic display-none rules
                    if let Some(pos) = line.find("##") {
                        let (domains_part, selector_part) = line.split_at(pos);
                        let selector = &selector_part[2..].trim(); // skip "##"
                        if selector.is_empty() {
                            continue;
                        }

                        let mut trigger = serde_json::json!({
                            "url-filter": ".*"
                        });

                        if !domains_part.is_empty() {
                            let mut if_domains = Vec::new();
                            let mut unless_domains = Vec::new();
                            for dom in domains_part.split(',') {
                                let dom = dom.trim();
                                if dom.starts_with('~') {
                                    unless_domains.push(dom[1..].to_string());
                                } else {
                                    if_domains.push(dom.to_string());
                                }
                            }
                            if !if_domains.is_empty() {
                                trigger["if-domain"] = serde_json::json!(if_domains);
                            }
                            if !unless_domains.is_empty() {
                                trigger["unless-domain"] = serde_json::json!(unless_domains);
                            }
                        }

                        rules_arr.push(serde_json::json!({
                            "trigger": trigger,
                            "action": {
                                "type": "css-display-none",
                                "selector": selector
                            }
                        }));
                        continue;
                    }

                    // Exception rules starting with "@@"
                    let is_exception = line.starts_with("@@");
                    let rest = if is_exception { &line[2..] } else { line };

                    // Parse options at the end (e.g. $third-party)
                    let (rule_part, options_part) = if let Some(idx) = rest.find('$') {
                        let (r, o) = rest.split_at(idx);
                        (r, Some(&o[1..]))
                    } else {
                        (rest, None)
                    };

                    if rule_part.is_empty() {
                        continue;
                    }

                    let mut is_domain_anchor = false;
                    let mut r = rule_part;

                    if r.starts_with("||") {
                        is_domain_anchor = true;
                        r = &r[2..];
                    }

                    let mut has_wildcard_end = false;
                    if r.ends_with('^') {
                        has_wildcard_end = true;
                        r = &r[..r.len() - 1];
                    }

                    let escaped_r = regex::escape(r);

                    let url_filter = if is_domain_anchor {
                        if has_wildcard_end {
                            format!("^https?://([^/]+\\.)?{}(/|$)", escaped_r)
                        } else {
                            format!("^https?://([^/]+\\.)?{}", escaped_r)
                        }
                    } else {
                        escaped_r.replace("\\*", ".*")
                    };

                    let mut trigger = serde_json::json!({
                        "url-filter": url_filter
                    });

                    if let Some(opts) = options_part {
                        let mut load_types = Vec::new();
                        let mut resource_types = Vec::new();
                        for opt in opts.split(',') {
                            let opt = opt.trim();
                            match opt {
                                "third-party" => load_types.push("third-party"),
                                "first-party" => load_types.push("first-party"),
                                "script" => resource_types.push("script"),
                                "stylesheet" | "css" => resource_types.push("style-sheet"),
                                "image" => resource_types.push("image"),
                                "subdocument" => resource_types.push("document"),
                                "xmlhttprequest" => resource_types.push("raw"),
                                _ => {}
                            }
                        }
                        if !load_types.is_empty() {
                            trigger["load-type"] = serde_json::json!(load_types);
                        }
                        if !resource_types.is_empty() {
                            trigger["resource-type"] = serde_json::json!(resource_types);
                        }
                    }

                    let action_type = if is_exception {
                        "ignore-previous-rules"
                    } else {
                        "block"
                    };

                    rules_arr.push(serde_json::json!({
                        "trigger": trigger,
                        "action": {
                            "type": action_type
                        }
                    }));
                }
            }
        }

        // Limit rule count to prevent WKWebView compile performance issues/crash
        if rules_arr.len() > 20000 {
            rules_arr.truncate(20000);
        }

        serde_json::to_string(&rules).unwrap_or_else(|_| "[]".to_string())
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
