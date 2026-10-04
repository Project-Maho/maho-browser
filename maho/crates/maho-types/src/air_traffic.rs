use serde::{Deserialize, Serialize};

use crate::identifiers::SpaceId;

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TrafficRule {
    pub id: String,
    pub url_pattern: String,
    pub match_type: MatchType,
    pub target_space_id: SpaceId,
    pub enabled: bool,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum MatchType {
    Contains,
    Equals,
    Regex,
    Glob,
}

impl MatchType {
    /// Stable storage identifier; must round-trip through [`MatchType::parse`].
    pub fn as_str(&self) -> &'static str {
        match self {
            MatchType::Contains => "contains",
            MatchType::Equals => "equals",
            MatchType::Regex => "regex",
            MatchType::Glob => "glob",
        }
    }

    pub fn parse(s: &str) -> Option<Self> {
        match s {
            "contains" => Some(MatchType::Contains),
            "equals" => Some(MatchType::Equals),
            "regex" => Some(MatchType::Regex),
            "glob" => Some(MatchType::Glob),
            _ => None,
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DefaultLinkBehavior {
    CurrentSpace,
    MostRecentSpace,
    SpecificSpace { space_id: SpaceId },
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case", tag = "type", content = "space_id")]
pub enum LinkDestination {
    Normal,
    MahoMini,
    Space(SpaceId),
}
