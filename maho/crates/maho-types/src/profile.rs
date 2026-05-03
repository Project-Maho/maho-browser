use serde::{Deserialize, Serialize};

use crate::boost::Boost;
use crate::easel::Easel;
use crate::extension::Extension;
use crate::identifiers::ProfileId;
use crate::note::Note;
use crate::settings::{SearchEngine, Settings};
use crate::space::Space;
use crate::split_view::SplitViewConfig;

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Profile {
    pub id: ProfileId,
    pub name: String,
    pub spaces: Vec<Space>,
    pub boosts: Vec<Boost>,
    pub notes: Vec<Note>,
    pub easels: Vec<Easel>,
    pub split_view_configs: Vec<SplitViewConfig>,
    pub search_engines: Vec<SearchEngine>,
    pub extensions: Vec<Extension>,
    pub settings: Settings,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ProfileConfig {
    pub id: ProfileId,
    pub name: String,
    pub avatar_color: String,
    pub default_search_engine: SearchEngine,
    pub download_path: String,
    pub archive_timeout_hours: Option<f64>,
    pub data_store_id: Option<String>,
}

impl ProfileConfig {
    pub fn new(name: String) -> Self {
        Self {
            id: ProfileId::generate(),
            name,
            avatar_color: "#007AFF".to_string(),
            default_search_engine: SearchEngine {
                name: "Google".to_string(),
                url_template: "https://www.google.com/search?q={query}".to_string(),
                is_default: true,
            },
            download_path: "~/Downloads".to_string(),
            archive_timeout_hours: Some(12.0),
            data_store_id: Some(uuid::Uuid::new_v4().to_string()),
        }
    }

    pub fn new_default(name: String) -> Self {
        Self {
            id: ProfileId::generate(),
            name,
            avatar_color: "#007AFF".to_string(),
            default_search_engine: SearchEngine {
                name: "Google".to_string(),
                url_template: "https://www.google.com/search?q={query}".to_string(),
                is_default: true,
            },
            download_path: "~/Downloads".to_string(),
            archive_timeout_hours: Some(12.0),
            data_store_id: None,
        }
    }
}
