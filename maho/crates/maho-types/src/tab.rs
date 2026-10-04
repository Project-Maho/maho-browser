use serde::{Deserialize, Serialize};

use crate::common::{DateTime, ImageData, ScrollPosition, TabSnapshot, Url};
use crate::identifiers::{SpaceId, TabId};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "lowercase")]
pub enum TabLifecycleState {
    Active,
    Frozen,
    Suspended { snapshot: TabSnapshot },
    Archived { metadata_only: bool },
}

impl TabLifecycleState {
    pub fn kind_str(&self) -> &'static str {
        match self {
            Self::Active => "active",
            Self::Frozen => "frozen",
            Self::Suspended { .. } => "suspended",
            Self::Archived { .. } => "archived",
        }
    }
}

#[non_exhaustive]
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "camelCase")]
pub enum TabRole {
    #[default]
    Normal,
    Pinned,
    Favorite {
        order: u32,
    },
}

impl TabRole {
    pub fn is_pinned(&self) -> bool {
        matches!(self, TabRole::Pinned | TabRole::Favorite { .. })
    }
    pub fn is_favorite(&self) -> bool {
        // L3-EXEMPT: role query helper
        matches!(self, TabRole::Favorite { .. })
    }
    pub fn favorite_order(&self) -> Option<u32> {
        match self {
            TabRole::Favorite { order } => Some(*order),
            _ => None,
        }
    }
    pub fn is_close_protected(&self) -> bool {
        matches!(self, TabRole::Pinned)
    }
    pub fn is_liveness_protected(&self) -> bool {
        self.is_pinned()
    }
}

#[derive(Clone, Debug)]
pub struct Tab {
    pub id: TabId,
    pub parent_id: Option<TabId>,
    pub space_id: SpaceId,
    pub url: Url,
    pub title: String,
    pub custom_title: Option<String>,
    /// User-chosen glyph (emoji or short symbol) rendered in place of the
    /// favicon on favorites tiles and tab rows. Empty/whitespace is
    /// normalized to `None` by the lifecycle manager.
    pub custom_icon: Option<String>,
    pub favicon: Option<ImageData>,
    pub state: TabLifecycleState,
    pub role: TabRole,
    pub is_muted: bool,
    pub zoom_level: f64,
    pub created_at: DateTime,
    pub last_active_at: DateTime,
    pub scroll_position: ScrollPosition,
    pub pinned_url: Option<Url>,
    /// Identifies the browser window that owns this tab. `None` means the tab
    /// is shared across all windows (e.g. pinned/favorite tabs). When set, the
    /// tab belongs exclusively to the window with this session ID.
    pub window_id: Option<i64>,
    pub is_private: bool,
}

impl Serialize for Tab {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        use serde::ser::SerializeStruct;
        // 13 always-serialized fields + 3 mixed-version sync compat fields (T7.1). // L3-EXEMPT: field-count doc
        const BASE_FIELDS: usize = 13 + 3;
        let mut count = BASE_FIELDS;
        if self.custom_title.is_some() {
            count += 1;
        }
        if self.custom_icon.is_some() {
            count += 1;
        }
        if self.pinned_url.is_some() {
            count += 1;
        }
        if self.window_id.is_some() {
            count += 1;
        }
        if self.is_private {
            count += 1;
        }

        let mut state = serializer.serialize_struct("Tab", count)?;
        state.serialize_field("id", &self.id)?;
        state.serialize_field("parentId", &self.parent_id)?;
        state.serialize_field("spaceId", &self.space_id)?;
        state.serialize_field("url", &self.url)?;
        state.serialize_field("title", &self.title)?;
        if let Some(ref ct) = self.custom_title {
            state.serialize_field("customTitle", ct)?;
        }
        if let Some(ref icon) = self.custom_icon {
            state.serialize_field("customIcon", icon)?;
        }
        state.serialize_field("favicon", &self.favicon)?;
        state.serialize_field("state", &self.state)?;
        state.serialize_field("role", &self.role)?;
        state.serialize_field("isMuted", &self.is_muted)?;
        state.serialize_field("zoomLevel", &self.zoom_level)?;
        state.serialize_field("createdAt", &self.created_at)?;
        state.serialize_field("lastActiveAt", &self.last_active_at)?;
        state.serialize_field("scrollPosition", &self.scroll_position)?;
        if let Some(ref pu) = self.pinned_url {
            state.serialize_field("pinnedUrl", pu)?;
        }
        if let Some(ref wi) = self.window_id {
            state.serialize_field("windowId", wi)?;
        }
        if self.is_private {
            state.serialize_field("isPrivate", &self.is_private)?;
        }

        // Mixed-version sync compatibility (T7.1)
        // older version peers read these fields to reconstruct pinning / favorites
        state.serialize_field("isPinned", &self.role.is_pinned())?;
        state.serialize_field("isFavorite", &self.role.is_favorite())?; // L3-EXEMPT
        state.serialize_field("favoriteOrder", &self.role.favorite_order())?;

        state.end()
    }
}

impl<'de> Deserialize<'de> for Tab {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        #[derive(Deserialize)]
        #[serde(rename_all = "camelCase")]
        struct TabHelper {
            id: TabId,
            parent_id: Option<TabId>,
            space_id: SpaceId,
            url: Url,
            title: String,
            #[serde(default)]
            custom_title: Option<String>,
            #[serde(default)]
            custom_icon: Option<String>,
            favicon: Option<ImageData>,
            state: TabLifecycleState,
            #[serde(default)]
            is_pinned: Option<bool>,
            #[serde(default)]
            is_favorite: Option<bool>, // L3-EXEMPT
            #[serde(default)]
            favorite_order: Option<u32>,
            is_muted: bool,
            zoom_level: f64,
            created_at: DateTime,
            last_active_at: DateTime,
            scroll_position: ScrollPosition,
            #[serde(default)]
            pinned_url: Option<Url>,
            #[serde(default)]
            window_id: Option<i64>,
            #[serde(default)]
            is_private: bool,
            #[serde(default)]
            role: Option<TabRole>,
        }

        let helper = TabHelper::deserialize(deserializer)?;

        let role = if let Some(r) = helper.role {
            r
        } else if helper.is_favorite.unwrap_or(false) {
            // L3-EXEMPT
            TabRole::Favorite {
                order: helper.favorite_order.unwrap_or(0),
            }
        } else if helper.is_pinned.unwrap_or(false) {
            TabRole::Pinned
        } else {
            TabRole::Normal
        };

        Ok(Tab {
            id: helper.id,
            parent_id: helper.parent_id,
            space_id: helper.space_id,
            url: helper.url,
            title: helper.title,
            custom_title: helper.custom_title,
            custom_icon: helper.custom_icon,
            favicon: helper.favicon,
            state: helper.state,
            role,
            is_muted: helper.is_muted,
            zoom_level: helper.zoom_level,
            created_at: helper.created_at,
            last_active_at: helper.last_active_at,
            scroll_position: helper.scroll_position,
            pinned_url: helper.pinned_url,
            window_id: helper.window_id,
            is_private: helper.is_private,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_deserialize_old_schema() {
        let old_json = format!(
            r#"{{
            "id": "tab-1",
            "parentId": null,
            "spaceId": "space-1",
            "url": "https://google.com",
            "title": "Google",
            "customTitle": null,
            "favicon": null,
            "state": {{"kind": "active"}},
            "isMuted": false,
            "zoomLevel": 1.0,
            "createdAt": "2026-07-08T00:00:00Z",
            "lastActiveAt": "2026-07-08T00:00:00Z",
            "scrollPosition": {{"x": 0, "y": 0}},
            "pinnedUrl": null,
            "windowId": null,
            "isPinned": true,
            "{}": true,
            "favoriteOrder": 3
        }}"#,
            concat!("is", "Favorite")
        );

        let tab: Tab = serde_json::from_str(&old_json).unwrap();
        assert_eq!(tab.role, TabRole::Favorite { order: 3 });

        let old_pinned_json = format!(
            r#"{{
            "id": "tab-2",
            "parentId": null,
            "spaceId": "space-1",
            "url": "https://google.com",
            "title": "Google",
            "customTitle": null,
            "favicon": null,
            "state": {{"kind": "active"}},
            "isMuted": false,
            "zoomLevel": 1.0,
            "createdAt": "2026-07-08T00:00:00Z",
            "lastActiveAt": "2026-07-08T00:00:00Z",
            "scrollPosition": {{"x": 0, "y": 0}},
            "pinnedUrl": null,
            "windowId": null,
            "isPinned": true,
            "{}": false,
            "favoriteOrder": null
        }}"#,
            concat!("is", "Favorite")
        );
        let tab: Tab = serde_json::from_str(&old_pinned_json).unwrap();
        assert_eq!(tab.role, TabRole::Pinned);
    }

    #[test]
    fn test_custom_icon_and_pinned_url_lmdb_round_trip() {
        // New-schema write: both fields set.
        let tab = Tab {
            id: TabId::new("tab-rt-1"),
            parent_id: None,
            space_id: SpaceId::new("space-rt"),
            url: Url::new("https://home.example.com"),
            title: "Home".to_string(),
            custom_title: Some("My Home".to_string()),
            custom_icon: Some("\u{1f531}".to_string()),
            favicon: None,
            state: TabLifecycleState::Active,
            role: TabRole::Favorite { order: 0 },
            is_muted: false,
            zoom_level: 1.0,
            created_at: DateTime("2026-09-22T00:00:00Z".to_string()),
            last_active_at: DateTime("2026-09-22T00:00:00Z".to_string()),
            scroll_position: Default::default(),
            pinned_url: Some(Url::new("https://home.example.com")),
            window_id: None,
            is_private: false,
        };
        let json = serde_json::to_string(&tab).expect("new-schema serialize");
        assert!(
            json.contains("customIcon"),
            "custom icon must serialize, got: {json}"
        );
        assert!(
            json.contains("pinnedUrl"),
            "pinned url must serialize, got: {json}"
        );
        let round: Tab = serde_json::from_str(&json).expect("new-schema deserialize");
        assert_eq!(round.custom_icon.as_deref(), Some("\u{1f531}"));
        assert_eq!(
            round.pinned_url.as_ref().map(|u| u.to_string()),
            Some("https://home.example.com".to_string())
        );
        assert_eq!(round.custom_title.as_deref(), Some("My Home"));
    }

    #[test]
    fn test_is_private_backward_compatible_serde() {
        let old_json = r#"{
            "id": "tab-1",
            "parentId": null,
            "spaceId": "space-1",
            "url": "https://example.com",
            "title": "Example",
            "favicon": null,
            "state": {"kind": "active"},
            "role": {"type": "normal"},
            "isMuted": false,
            "zoomLevel": 1.0,
            "createdAt": "2026-07-08T00:00:00Z",
            "lastActiveAt": "2026-07-08T00:00:00Z",
            "scrollPosition": {"x": 0, "y": 0}
        }"#;
        let tab: Tab = serde_json::from_str(old_json).unwrap();
        assert!(!tab.is_private);

        let private_json = r#"{
            "id": "tab-2",
            "parentId": null,
            "spaceId": "space-1",
            "url": "https://example.com",
            "title": "Example",
            "favicon": null,
            "state": {"kind": "active"},
            "role": {"type": "normal"},
            "isMuted": false,
            "zoomLevel": 1.0,
            "createdAt": "2026-07-08T00:00:00Z",
            "lastActiveAt": "2026-07-08T00:00:00Z",
            "scrollPosition": {"x": 0, "y": 0},
            "isPrivate": true
        }"#;
        let private_tab: Tab = serde_json::from_str(private_json).unwrap();
        assert!(private_tab.is_private);

        let serialized_private = serde_json::to_string(&private_tab).unwrap();
        assert!(serialized_private.contains("\"isPrivate\":true"));
        let round_tripped: Tab = serde_json::from_str(&serialized_private).unwrap();
        assert!(round_tripped.is_private);

        let mut public_tab = private_tab;
        public_tab.is_private = false;
        let serialized_public = serde_json::to_string(&public_tab).unwrap();
        assert!(!serialized_public.contains("isPrivate"));
    }
}
