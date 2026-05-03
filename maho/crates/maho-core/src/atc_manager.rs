use maho_types::air_traffic::{DefaultLinkBehavior, MatchType, TrafficRule};
use maho_types::identifiers::{SpaceId, TabId};

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
}

impl ATCManager {
    pub fn new() -> Self {
        Self {
            rules: Vec::new(),
            traffic_rules: Vec::new(),
            default_link_behavior: DefaultLinkBehavior::CurrentSpace,
        }
    }

    pub fn add_rule(&mut self, rule: ATCRule) -> String {
        let id = rule.id.clone();
        self.rules.push(rule);
        id
    }

    pub fn remove_rule(&mut self, rule_id: &str) -> Option<ATCRule> {
        if let Some(pos) = self.rules.iter().position(|r| r.id == rule_id) {
            Some(self.rules.remove(pos))
        } else {
            None
        }
    }

    pub fn toggle_rule(&mut self, rule_id: &str, enabled: bool) {
        if let Some(rule) = self.rules.iter_mut().find(|r| r.id == rule_id) {
            rule.enabled = enabled;
        }
    }

    pub fn restore_rule(&mut self, rule: ATCRule) {
        self.rules.push(rule);
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
        self.traffic_rules.push(rule.clone());
        rule
    }

    pub fn update_traffic_rule(&mut self, rule: TrafficRule) -> bool {
        if let Some(existing) = self.traffic_rules.iter_mut().find(|r| r.id == rule.id) {
            *existing = rule;
            true
        } else {
            false
        }
    }

    pub fn delete_traffic_rule(&mut self, rule_id: &str) -> Option<TrafficRule> {
        if let Some(pos) = self.traffic_rules.iter().position(|r| r.id == rule_id) {
            Some(self.traffic_rules.remove(pos))
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

    pub fn route_url(&self, url: &str) -> Option<SpaceId> {
        for rule in &self.traffic_rules {
            if !rule.enabled {
                continue;
            }
            let matched = match rule.match_type {
                MatchType::Contains => url.contains(&rule.url_pattern),
                MatchType::Equals => url == rule.url_pattern,
                MatchType::Regex => regex::Regex::new(&rule.url_pattern)
                    .map(|re| re.is_match(url))
                    .unwrap_or(false),
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
