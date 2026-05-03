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
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DefaultLinkBehavior {
    CurrentSpace,
    MostRecentSpace,
    SpecificSpace { space_id: SpaceId },
}
