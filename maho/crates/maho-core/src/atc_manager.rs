use maho_types::air_traffic::{DefaultLinkBehavior, LinkDestination, MatchType, TrafficRule};
use maho_types::identifiers::{SpaceId, TabId};
use std::collections::HashMap;

pub struct TabInfo {
    pub id: TabId,
    pub space_id: SpaceId,
    pub url: String,
    pub last_active_hours: f64,
}

#[derive(Clone, Debug, serde::Serialize, serde::Deserialize)]
pub struct ATCRule {
    pub id: String,
    pub name: String,
    pub space_id: Option<SpaceId>,
    pub url_pattern: Option<String>,
    pub max_age_hours: Option<u32>,
    pub max_tabs: Option<usize>,
    pub enabled: bool,
}

pub struct ATCManager {
    rules: Vec<ATCRule>,
    traffic_rules: Vec<TrafficRule>,
    default_link_behavior: DefaultLinkBehavior,
    open_external_links_in_maho_mini: bool,
    regex_cache: HashMap<String, Option<regex::Regex>>,
}

fn glob_body_to_regex(glob: &str) -> String {
    let mut pattern_regex = String::new();
    for ch in glob.chars() {
        if ch == '*' {
            pattern_regex.push_str(".*");
            continue;
        }
        pattern_regex.push_str(&regex::escape(&ch.to_string()));
    }
    pattern_regex
}

/// Translate a user glob pattern into an unanchored regex tested with
/// `Regex::is_match` over the full URL.
///
/// A leading `*.` is treated specially: it matches the apex domain AND any
/// subdomain depth, guarded by a host boundary. The `*.` is stripped and the
/// remaining domain is anchored to a preceding scheme/host boundary
/// (`^`, `.`, `/`, or `@`) and followed by a URL delimiter (`:`, `/`, `?`,
/// `#`, or end-of-string). So `*.github.com` becomes
/// `(?:^|[./@])github\.com(?:[:/?#]|$)`, which matches `https://github.com`,
/// `https://github.com/signup`, `https://api.github.com`, and
/// `https://a.b.github.com/x`, but NOT `https://evilgithub.com` or
/// `https://github.com.evil.com`.
///
/// All other `*` wildcards translate to `.*` (contains-style, unanchored), so a
/// pattern without wildcards behaves like `Contains`. A trailing path wildcard
/// such as `example.com/*` requires the slash, so it matches
/// `https://example.com/path` but NOT bare `https://example.com`.
fn glob_to_regex(glob: &str) -> String {
    if let Some(rest) = glob.strip_prefix("*.") {
        let body = glob_body_to_regex(rest);
        format!("(?:^|[./@]){body}(?:[:/?#]|$)")
    } else {
        glob_body_to_regex(glob)
    }
}

fn infer_legacy_traffic_match_type(pattern: &str) -> MatchType {
    if pattern.contains('*') {
        MatchType::Glob
    } else {
        MatchType::Contains
    }
}

impl ATCManager {
    pub fn new() -> Self {
        Self {
            rules: Vec::new(),
            traffic_rules: Vec::new(),
            default_link_behavior: DefaultLinkBehavior::CurrentSpace,
            open_external_links_in_maho_mini: false,
            regex_cache: HashMap::new(),
        }
    }

    /// Cache key for compiled patterns. Includes the match type: the same
    /// pattern text compiles differently as Glob vs Regex, and a Contains rule
    /// must not shadow (poison) a later Glob rule for the same pattern.
    fn cache_key(match_type: &MatchType, pattern: &str) -> String {
        format!("{}:{pattern}", match_type.as_str())
    }

    fn cache_pattern(&mut self, pattern: &str, match_type: &MatchType) {
        let key = Self::cache_key(match_type, pattern);
        self.regex_cache
            .entry(key)
            .or_insert_with(|| match match_type {
                MatchType::Glob => regex::Regex::new(&glob_to_regex(pattern)).ok(),
                MatchType::Regex => regex::Regex::new(pattern).ok(),
                MatchType::Contains | MatchType::Equals => None,
            });
    }

    fn rebuild_regex_cache(&mut self) {
        self.regex_cache.clear();
        let traffic_rules = self.traffic_rules.clone();
        for rule in traffic_rules {
            self.cache_pattern(&rule.url_pattern, &rule.match_type);
        }
    }

    pub fn add_rule(&mut self, rule: ATCRule) -> String {
        let id = rule.id.clone();
        if let Some(space_id) = &rule.space_id {
            if rule.max_age_hours.is_none() && rule.max_tabs.is_none() && rule.url_pattern.is_none()
            {
                let match_type = infer_legacy_traffic_match_type(&rule.name);
                self.cache_pattern(&rule.name, &match_type);
                self.traffic_rules.push(TrafficRule {
                    id: rule.id.clone(),
                    url_pattern: rule.name.clone(),
                    match_type,
                    target_space_id: space_id.clone(),
                    enabled: rule.enabled,
                });
            }
        }
        self.rules.push(rule);
        id
    }

    pub fn remove_rule(&mut self, rule_id: &str) -> Option<ATCRule> {
        if let Some(pos) = self.rules.iter().position(|r| r.id == rule_id) {
            let removed = self.rules.remove(pos);
            if let Some(t_pos) = self.traffic_rules.iter().position(|r| r.id == rule_id) {
                self.traffic_rules.remove(t_pos);
                self.rebuild_regex_cache();
            }
            Some(removed)
        } else {
            None
        }
    }

    pub fn toggle_rule(&mut self, rule_id: &str, enabled: bool) {
        if let Some(rule) = self.rules.iter_mut().find(|r| r.id == rule_id) {
            rule.enabled = enabled;
            if let Some(t_rule) = self.traffic_rules.iter_mut().find(|r| r.id == rule_id) {
                t_rule.enabled = enabled;
            }
        }
    }

    /// Restore a rule loaded from storage. `traffic_match_type` is the
    /// persisted match type of the traffic-rule mirror; rows persisted before
    /// the match_type column existed pass `None` and the type is inferred:
    /// wildcard patterns were only ever produced by glob rules, plain text by
    /// legacy Contains mirrors (glob without `*` behaves like Contains, so the
    /// inference is semantics-preserving either way).
    pub fn restore_rule(&mut self, rule: ATCRule, traffic_match_type: Option<MatchType>) {
        self.rules.push(rule.clone());
        // If it looks like a traffic rule, sync it to traffic_rules
        if let Some(space_id) = &rule.space_id {
            if rule.max_age_hours.is_none() && rule.max_tabs.is_none() && rule.url_pattern.is_none()
            {
                let match_type = traffic_match_type
                    .unwrap_or_else(|| infer_legacy_traffic_match_type(&rule.name));
                self.cache_pattern(&rule.name, &match_type);
                self.traffic_rules.push(TrafficRule {
                    id: rule.id.clone(),
                    url_pattern: rule.name.clone(),
                    match_type,
                    target_space_id: space_id.clone(),
                    enabled: rule.enabled,
                });
            }
        }
    }

    pub fn get_rules(&self) -> &[ATCRule] {
        &self.rules
    }

    pub fn evaluate(&self, tabs: &[TabInfo]) -> Vec<TabId> {
        let mut to_close = Vec::new();

        for rule in &self.rules {
            if !rule.enabled {
                continue;
            }

            let matching_tabs: Vec<&TabInfo> = tabs
                .iter()
                .filter(|tab| {
                    if let Some(ref space_id) = rule.space_id {
                        if tab.space_id != *space_id {
                            return false;
                        }
                    }
                    if let Some(ref pattern) = rule.url_pattern {
                        if !tab.url.contains(pattern) {
                            return false;
                        }
                    }
                    true
                })
                .collect();

            if let Some(max_age) = rule.max_age_hours {
                for tab in &matching_tabs {
                    if tab.last_active_hours > f64::from(max_age) && !to_close.contains(&tab.id) {
                        to_close.push(tab.id.clone());
                    }
                }
            }

            if let Some(max_tabs) = rule.max_tabs {
                if matching_tabs.len() > max_tabs {
                    let mut sorted: Vec<&TabInfo> = matching_tabs.clone();
                    sorted.sort_by(|a, b| {
                        b.last_active_hours
                            .partial_cmp(&a.last_active_hours)
                            .unwrap_or(std::cmp::Ordering::Equal)
                    });

                    let excess = matching_tabs.len() - max_tabs;
                    for tab in sorted.iter().take(excess) {
                        if !to_close.contains(&tab.id) {
                            to_close.push(tab.id.clone());
                        }
                    }
                }
            }
        }

        to_close
    }

    // === Traffic Rule CRUD ===

    pub fn create_traffic_rule(&mut self, rule: TrafficRule) -> TrafficRule {
        self.cache_pattern(&rule.url_pattern, &rule.match_type);
        self.traffic_rules.push(rule.clone());
        if !self.rules.iter().any(|r| r.id == rule.id) {
            self.rules.push(ATCRule {
                id: rule.id.clone(),
                name: rule.url_pattern.clone(),
                space_id: Some(rule.target_space_id.clone()),
                url_pattern: None,
                max_age_hours: None,
                max_tabs: None,
                enabled: rule.enabled,
            });
        }
        rule
    }

    pub fn update_traffic_rule(&mut self, rule: TrafficRule) -> bool {
        self.cache_pattern(&rule.url_pattern, &rule.match_type);
        if let Some(existing) = self.traffic_rules.iter_mut().find(|r| r.id == rule.id) {
            *existing = rule.clone();
            if let Some(existing_rule) = self.rules.iter_mut().find(|r| r.id == rule.id) {
                existing_rule.name = rule.url_pattern;
                existing_rule.space_id = Some(rule.target_space_id);
                existing_rule.enabled = rule.enabled;
            }
            self.rebuild_regex_cache();
            true
        } else {
            false
        }
    }

    pub fn delete_traffic_rule(&mut self, rule_id: &str) -> Option<TrafficRule> {
        if let Some(pos) = self.traffic_rules.iter().position(|r| r.id == rule_id) {
            let removed = self.traffic_rules.remove(pos);
            if let Some(pos_rule) = self.rules.iter().position(|r| r.id == rule_id) {
                self.rules.remove(pos_rule);
            }
            self.rebuild_regex_cache();
            Some(removed)
        } else {
            None
        }
    }

    pub fn get_traffic_rules(&self) -> &[TrafficRule] {
        &self.traffic_rules
    }

    pub fn set_default_link_behavior(&mut self, behavior: DefaultLinkBehavior) {
        self.default_link_behavior = behavior;
    }

    pub fn get_default_link_behavior(&self) -> &DefaultLinkBehavior {
        &self.default_link_behavior
    }

    pub fn set_open_external_links_in_maho_mini(&mut self, enabled: bool) {
        self.open_external_links_in_maho_mini = enabled;
    }

    pub fn get_open_external_links_in_maho_mini(&self) -> bool {
        self.open_external_links_in_maho_mini
    }

    pub fn decide_link_destination(
        &self,
        url: &str,
        is_external: bool,
        space_rules: &[(SpaceId, Vec<maho_types::space::ATCRule>)],
    ) -> LinkDestination {
        if let Some(space_id) = self.route_url_by_space_rules(url, space_rules) {
            LinkDestination::Space(space_id)
        } else if let Some(space_id) = self.route_url(url) {
            LinkDestination::Space(space_id)
        } else if is_external && self.open_external_links_in_maho_mini {
            LinkDestination::MahoMini
        } else {
            match &self.default_link_behavior {
                DefaultLinkBehavior::CurrentSpace | DefaultLinkBehavior::MostRecentSpace => {
                    LinkDestination::Normal
                }
                DefaultLinkBehavior::SpecificSpace { space_id } => {
                    LinkDestination::Space(space_id.clone())
                }
            }
        }
    }

    pub fn route_url(&self, url: &str) -> Option<SpaceId> {
        for rule in &self.traffic_rules {
            if !rule.enabled {
                continue;
            }
            let matched = match rule.match_type {
                MatchType::Contains => url.contains(&rule.url_pattern),
                MatchType::Equals => url == rule.url_pattern,
                MatchType::Regex | MatchType::Glob => {
                    let key = Self::cache_key(&rule.match_type, &rule.url_pattern);
                    if let Some(Some(ref re)) = self.regex_cache.get(&key) {
                        re.is_match(url)
                    } else {
                        false
                    }
                }
            };
            if matched {
                return Some(rule.target_space_id.clone());
            }
        }
        None
    }

    pub fn route_url_by_space_rules(
        &self,
        url: &str,
        space_rules: &[(SpaceId, Vec<maho_types::space::ATCRule>)],
    ) -> Option<SpaceId> {
        for (space_id, rules) in space_rules {
            for rule in rules {
                if !rule.enabled {
                    continue;
                }
                let matched = match &rule.condition {
                    maho_types::space::ATCCondition::UrlPattern { pattern } => {
                        url.contains(pattern)
                    }
                    maho_types::space::ATCCondition::UrlContains { text } => {
                        url.contains(text.as_str())
                    }
                    maho_types::space::ATCCondition::UrlEquals { url: rule_url } => {
                        url == rule_url.as_str()
                    }
                    _ => false,
                };
                if matched {
                    if let maho_types::space::ATCAction::Route { space_id: target } = &rule.action {
                        return Some(target.clone());
                    }
                    return Some(space_id.clone());
                }
            }
        }
        None
    }
}

impl Default for ATCManager {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use maho_types::air_traffic::LinkDestination;

    #[test]
    fn test_decide_link_destination() {
        let mut manager = ATCManager::new();
        let space_1 = SpaceId::new("space-1");
        let space_2 = SpaceId::new("space-2");

        // Create a traffic rule mapping "google.com" to space-1
        manager.create_traffic_rule(TrafficRule {
            id: "rule-1".to_string(),
            url_pattern: "google.com".to_string(),
            match_type: MatchType::Contains,
            target_space_id: space_1.clone(),
            enabled: true,
        });

        let space_rules = vec![(
            space_2.clone(),
            vec![maho_types::space::ATCRule {
                id: "space-rule-1".to_string(),
                condition: maho_types::space::ATCCondition::UrlContains {
                    text: "github.com".to_string(),
                },
                action: maho_types::space::ATCAction::Route {
                    space_id: space_2.clone(),
                },
                enabled: true,
            }],
        )];

        // 1. Matched space rule (global rule)
        let dest = manager.decide_link_destination("https://google.com/search", true, &space_rules);
        assert_eq!(dest, LinkDestination::Space(space_1.clone()));

        // 2. Matched space rule (space-specific rule)
        let dest =
            manager.decide_link_destination("https://github.com/rust-lang", true, &space_rules);
        assert_eq!(dest, LinkDestination::Space(space_2.clone()));

        // 3. Unmatched external + flag off -> Normal
        manager.set_open_external_links_in_maho_mini(false);
        let dest = manager.decide_link_destination("https://example.com", true, &space_rules);
        assert_eq!(dest, LinkDestination::Normal);

        // 4. Unmatched external + flag on -> MahoMini
        manager.set_open_external_links_in_maho_mini(true);
        let dest = manager.decide_link_destination("https://example.com", true, &space_rules);
        assert_eq!(dest, LinkDestination::MahoMini);

        // 5. In-app link + flag on -> Normal
        let dest = manager.decide_link_destination("https://example.com", false, &space_rules);
        assert_eq!(dest, LinkDestination::Normal);

        // 6. Precedence tie: matched space rule (global) wins over MahoMini fallback
        let dest = manager.decide_link_destination("https://google.com/search", true, &space_rules);
        assert_eq!(dest, LinkDestination::Space(space_1.clone()));

        // 7. Precedence tie: matched space rule (space-specific) wins over MahoMini fallback
        let dest =
            manager.decide_link_destination("https://github.com/rust-lang", true, &space_rules);
        assert_eq!(dest, LinkDestination::Space(space_2.clone()));

        // 8. Newly added rule matches immediately without restart (Task 1.3)
        let space_3 = SpaceId::new("space-3");
        manager.add_rule(ATCRule {
            id: "rule-added-immediately".to_string(),
            name: "added-test.com".to_string(),
            space_id: Some(space_3.clone()),
            url_pattern: None,
            max_age_hours: None,
            max_tabs: None,
            enabled: true,
        });
        let dest =
            manager.decide_link_destination("https://added-test.com/path", true, &space_rules);
        assert_eq!(dest, LinkDestination::Space(space_3));
    }

    fn glob_rule(id: &str, pattern: &str, space: &SpaceId) -> TrafficRule {
        TrafficRule {
            id: id.to_string(),
            url_pattern: pattern.to_string(),
            match_type: MatchType::Glob,
            target_space_id: space.clone(),
            enabled: true,
        }
    }

    #[test]
    fn test_glob_subdomain_wildcard() {
        let mut manager = ATCManager::new();
        let space = SpaceId::new("space-glob");
        manager.create_traffic_rule(glob_rule("g1", "*.example.com", &space));

        assert_eq!(
            manager.route_url("https://sub.example.com/path"),
            Some(space.clone())
        );
        assert_eq!(
            manager.route_url("https://example.com"),
            Some(space.clone())
        );
        assert_eq!(manager.route_url("https://sub.other.org"), None);
    }

    #[test]
    fn test_glob_leading_wildcard_apex_and_boundaries() {
        let mut manager = ATCManager::new();
        let space = SpaceId::new("space-gh");
        manager.create_traffic_rule(glob_rule("gh", "*.github.com", &space));

        for url in [
            "https://github.com/signup",
            "https://github.com",
            "https://api.github.com",
            "https://a.b.github.com/x",
        ] {
            assert_eq!(
                manager.route_url(url),
                Some(space.clone()),
                "should match {url}"
            );
        }

        for url in [
            "https://evilgithub.com",
            "https://github.com.evil.com",
            "https://notgithub.com",
        ] {
            assert_eq!(manager.route_url(url), None, "should NOT match {url}");
        }
    }

    #[test]
    fn test_glob_path_wildcard() {
        let mut manager = ATCManager::new();
        let space = SpaceId::new("space-path");
        manager.create_traffic_rule(glob_rule("g2", "example.com/*", &space));

        assert_eq!(
            manager.route_url("https://example.com/anything"),
            Some(space.clone())
        );
        assert_eq!(manager.route_url("https://example.com"), None);
    }

    fn restored_rule(id: &str, name: &str, space: &SpaceId) -> ATCRule {
        ATCRule {
            id: id.to_string(),
            name: name.to_string(),
            space_id: Some(space.clone()),
            url_pattern: None,
            max_age_hours: None,
            max_tabs: None,
            enabled: true,
        }
    }

    // Regression: persisted glob rules must still match after a restart
    // (match_type used to be dropped on persist and restored as Contains,
    // which never matches a wildcard pattern literally).
    #[test]
    fn test_restored_glob_rule_routes_after_restart() {
        let mut manager = ATCManager::new();
        let space = SpaceId::new("space-restored");
        manager.restore_rule(
            restored_rule("r1", "*.github.com", &space),
            Some(MatchType::Glob),
        );

        assert_eq!(
            manager.route_url("https://github.com/signup"),
            Some(space.clone())
        );
        assert_eq!(manager.route_url("https://api.github.com"), Some(space));
        assert_eq!(manager.route_url("https://evilgithub.com"), None);
    }

    // Rows persisted before the match_type column existed: wildcard patterns
    // are inferred as Glob, plain text stays Contains.
    #[test]
    fn test_restored_legacy_rule_match_type_inference() {
        let mut manager = ATCManager::new();
        let glob_space = SpaceId::new("space-glob-legacy");
        let plain_space = SpaceId::new("space-plain-legacy");
        manager.restore_rule(restored_rule("rg", "*.github.com", &glob_space), None);
        manager.restore_rule(restored_rule("rp", "news.site", &plain_space), None);

        assert_eq!(
            manager.route_url("https://github.com/signup"),
            Some(glob_space)
        );
        assert_eq!(
            manager.route_url("https://news.site/article"),
            Some(plain_space)
        );
    }

    // Regression: the regex cache is keyed by (match_type, pattern). A
    // Contains rule for a pattern must not poison the cache entry for a Glob
    // rule with the same pattern text added later.
    #[test]
    fn test_regex_cache_not_poisoned_across_match_types() {
        let mut manager = ATCManager::new();
        let contains_space = SpaceId::new("space-contains");
        let glob_space = SpaceId::new("space-glob");

        manager.create_traffic_rule(TrafficRule {
            id: "c1".to_string(),
            url_pattern: "*.github.com".to_string(),
            match_type: MatchType::Contains,
            target_space_id: contains_space,
            enabled: true,
        });
        manager.create_traffic_rule(glob_rule("g1", "*.github.com", &glob_space));

        // The Contains rule never matches (no literal "*." in real URLs); the
        // Glob rule must still compile and match despite the shared pattern.
        assert_eq!(
            manager.route_url("https://github.com/signup"),
            Some(glob_space)
        );
    }

    #[test]
    fn test_glob_without_wildcard_behaves_like_contains() {
        let mut manager = ATCManager::new();
        let space = SpaceId::new("space-plain");
        manager.create_traffic_rule(glob_rule("g3", "github.com", &space));

        assert_eq!(
            manager.route_url("https://github.com/x"),
            Some(space.clone())
        );
        assert_eq!(manager.route_url("https://gitlab.com/x"), None);
    }

    #[test]
    fn test_add_rule_wildcard_matches_like_restored_rule() {
        let mut added_manager = ATCManager::new();
        let mut restored_manager = ATCManager::new();
        let space = SpaceId::new("space-add-restore");

        added_manager.add_rule(restored_rule("wildcard", "*.github.com", &space));
        restored_manager.restore_rule(restored_rule("wildcard", "*.github.com", &space), None);

        for url in ["https://github.com/signup", "https://api.github.com"] {
            assert_eq!(added_manager.route_url(url), Some(space.clone()));
            assert_eq!(restored_manager.route_url(url), Some(space.clone()));
        }

        assert_eq!(added_manager.route_url("https://evilgithub.com"), None);
        assert_eq!(restored_manager.route_url("https://evilgithub.com"), None);
    }

    #[test]
    fn test_default_link_behavior_current_space_keeps_unmatched_links_normal() {
        let mut manager = ATCManager::new();
        manager.set_default_link_behavior(DefaultLinkBehavior::CurrentSpace);

        assert_eq!(
            manager.decide_link_destination("https://example.com", false, &[]),
            LinkDestination::Normal
        );
    }

    #[test]
    fn test_default_link_behavior_specific_space_routes_unmatched_links() {
        let mut manager = ATCManager::new();
        let space = SpaceId::new("space-default");
        manager.set_default_link_behavior(DefaultLinkBehavior::SpecificSpace {
            space_id: space.clone(),
        });

        assert_eq!(
            manager.decide_link_destination("https://example.com", false, &[]),
            LinkDestination::Space(space)
        );
    }

    #[test]
    fn test_external_maho_mini_preference_wins_over_default_space() {
        let mut manager = ATCManager::new();
        manager.set_open_external_links_in_maho_mini(true);
        manager.set_default_link_behavior(DefaultLinkBehavior::SpecificSpace {
            space_id: SpaceId::new("space-default"),
        });

        assert_eq!(
            manager.decide_link_destination("https://example.com", true, &[]),
            LinkDestination::MahoMini
        );
    }

    #[test]
    fn test_default_link_behavior_most_recent_is_noop_without_recent_space_input() {
        let mut manager = ATCManager::new();
        manager.set_default_link_behavior(DefaultLinkBehavior::MostRecentSpace);

        assert_eq!(
            manager.decide_link_destination("https://example.com", false, &[]),
            LinkDestination::Normal
        );
    }
}
