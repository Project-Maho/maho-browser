//! Arc browser sidebar.plist / StorableSidebar.json parser.
//!
//! Ports `maho-chromium/browser/importer/arc_sidebar_parser.cc` to Rust.
//! Reads `<arc_user_data_dir>/StorableSidebar.json` and extracts spaces +
//! tabs + folders into `ParsedWorkspace` structs.
//!
//! Arc stores its sidebar tree as alternating `[uuid, obj, uuid, obj, ...]`
//! arrays. The parser walks two possible roots:
//! - Path B (preferred): `firebaseSyncState.syncData.{spaceModels,items}`
//! - Path A (fallback):  `sidebar.containers[1].{spaces,items}`

use serde_json::Value;
use std::path::Path;

use crate::{ImportError, ImportResult, ParsedFolder, ParsedTab, ParsedWorkspace};

#[derive(Debug, Default)]
pub struct ArcParseResult {
    pub workspaces: Vec<ParsedWorkspace>,
}

pub fn parse_arc_sidebar(arc_user_data_dir: &Path) -> ImportResult<ArcParseResult> {
    let sidebar_path = arc_user_data_dir.join("StorableSidebar.json");

    let content = std::fs::read_to_string(&sidebar_path).map_err(|e| {
        if e.kind() == std::io::ErrorKind::NotFound {
            ImportError::FileNotFound(sidebar_path.display().to_string())
        } else {
            ImportError::Io(format!("{}: {}", sidebar_path.display(), e))
        }
    })?;

    if content.is_empty() {
        return Ok(ArcParseResult::default());
    }

    let root: Value = serde_json::from_str(&content)
        .map_err(|e| ImportError::Parse(format!("StorableSidebar.json: {}", e)))?;

    let root_obj = root
        .as_object()
        .ok_or_else(|| ImportError::Parse("root is not a JSON object".into()))?;

    // Path B (preferred): firebaseSyncState.syncData
    let sync_data = root_obj
        .get("firebaseSyncState")
        .and_then(|v| v.as_object())
        .and_then(|s| s.get("syncData"))
        .and_then(|v| v.as_object());

    let (space_models, items_list) = if let Some(sd) = sync_data {
        (
            sd.get("spaceModels").and_then(|v| v.as_array()),
            sd.get("items").and_then(|v| v.as_array()),
        )
    } else {
        // Path A (fallback): sidebar.containers[1]
        let containers = root_obj
            .get("sidebar")
            .and_then(|v| v.as_object())
            .and_then(|s| s.get("containers"))
            .and_then(|v| v.as_array());

        if let Some(containers) = containers {
            if containers.len() > 1 {
                if let Some(container) = containers[1].as_object() {
                    (
                        container.get("spaces").and_then(|v| v.as_array()),
                        container.get("items").and_then(|v| v.as_array()),
                    )
                } else {
                    (None, None)
                }
            } else {
                (None, None)
            }
        } else {
            (None, None)
        }
    };

    let (Some(space_models), Some(items_list)) = (space_models, items_list) else {
        return Ok(ArcParseResult::default());
    };

    let all_items = parse_alternating_array(items_list);
    let spaces = parse_alternating_array(space_models);

    let mut result = ArcParseResult::default();

    for space_dict in spaces {
        let mut ws = ParsedWorkspace {
            name: space_dict
                .get("title")
                .and_then(|v| v.as_str())
                .unwrap_or("Space")
                .to_string(),
            uuid: result.workspaces.len().to_string(),
            ..Default::default()
        };

        if let Some(custom_info) = space_dict.get("customInfo").and_then(|v| v.as_object()) {
            if let Some(icon_type) = custom_info.get("iconType").and_then(|v| v.as_object()) {
                if let Some(emoji) = icon_type.get("emoji_v2").and_then(|v| v.as_str()) {
                    ws.icon = emoji.to_string();
                } else if let Some(name) = icon_type.get("icon").and_then(|v| v.as_str()) {
                    ws.icon = name.to_string();
                }
            }
        }

        // containerIDs alternate: [type, uuid, type, uuid, ...]
        if let Some(container_ids) = space_dict.get("containerIDs").and_then(|v| v.as_array()) {
            let mut i = 0;
            while i + 1 < container_ids.len() {
                let type_val = container_ids[i].as_str();
                let uuid_val = container_ids[i + 1].as_str();
                i += 2;

                let (Some(container_type), Some(target_uuid)) = (type_val, uuid_val) else {
                    continue;
                };
                let is_pinned = container_type == "pinned";

                if let Some(item) = find_item_by_id(&all_items, target_uuid) {
                    if let Some(children) = find_children_ids(item) {
                        parse_items_into_workspace(&mut ws, &all_items, children, is_pinned);
                    }
                }
            }
        }

        result.workspaces.push(ws);
    }

    Ok(result)
}

/// Strip Arc's `{ "value": <inner> }` wrapper if present, otherwise return as-is.
fn unwrap_value(val: &Value) -> Option<&serde_json::Map<String, Value>> {
    let dict = val.as_object()?;
    if let Some(inner) = dict.get("value").and_then(|v| v.as_object()) {
        Some(inner)
    } else {
        Some(dict)
    }
}

/// Arc alternating arrays: `[uuid, obj, uuid, obj, ...]`. We only want the obj at odd indices.
fn parse_alternating_array(list: &[Value]) -> Vec<&serde_json::Map<String, Value>> {
    let mut result = Vec::with_capacity(list.len() / 2);
    let mut i = 1;
    while i < list.len() {
        if let Some(unwrapped) = unwrap_value(&list[i]) {
            result.push(unwrapped);
        }
        i += 2;
    }
    result
}

fn find_children_ids(item: &serde_json::Map<String, Value>) -> Option<&Vec<Value>> {
    if let Some(direct) = item.get("childrenIds").and_then(|v| v.as_array()) {
        return Some(direct);
    }
    let wrapped = item.get("childrenIds").and_then(|v| v.as_object())?;
    if let Some(inner) = wrapped.get("_0").and_then(|v| v.as_array()) {
        return Some(inner);
    }
    wrapped.get("items").and_then(|v| v.as_array())
}

fn is_tab_item(item: &serde_json::Map<String, Value>) -> bool {
    item.get("data")
        .and_then(|v| v.as_object())
        .and_then(|d| d.get("tab"))
        .and_then(|v| v.as_object())
        .is_some()
}

fn is_folder_item(item: &serde_json::Map<String, Value>) -> bool {
    let Some(data) = item.get("data").and_then(|v| v.as_object()) else {
        return false;
    };
    data.get("list").is_some() || data.get("itemContainer").is_some()
}

fn extract_tab_url(item: &serde_json::Map<String, Value>) -> String {
    item.get("data")
        .and_then(|v| v.as_object())
        .and_then(|d| d.get("tab"))
        .and_then(|v| v.as_object())
        .and_then(|t| t.get("savedURL"))
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .to_string()
}

fn extract_tab_title(item: &serde_json::Map<String, Value>) -> String {
    item.get("data")
        .and_then(|v| v.as_object())
        .and_then(|d| d.get("tab"))
        .and_then(|v| v.as_object())
        .and_then(|t| t.get("savedTitle"))
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .to_string()
}

fn extract_item_title(item: &serde_json::Map<String, Value>) -> String {
    item.get("title")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .to_string()
}

fn find_item_by_id<'a>(
    all_items: &[&'a serde_json::Map<String, Value>],
    target_id: &str,
) -> Option<&'a serde_json::Map<String, Value>> {
    all_items
        .iter()
        .find(|item| {
            item.get("id")
                .and_then(|v| v.as_str())
                .map(|id| id == target_id)
                .unwrap_or(false)
        })
        .copied()
}

fn parse_items_into_workspace(
    ws: &mut ParsedWorkspace,
    all_items: &[&serde_json::Map<String, Value>],
    container_ids: &[Value],
    is_pinned_section: bool,
) {
    let mut i = 1;
    while i < container_ids.len() {
        let Some(item_id) = container_ids[i].as_str() else {
            i += 2;
            continue;
        };

        if let Some(item) = find_item_by_id(all_items, item_id) {
            if is_tab_item(item) {
                let url = extract_tab_url(item);
                if !url.is_empty() {
                    if let Ok(parsed) = url::Url::parse(&url) {
                        let scheme = parsed.scheme();
                        if scheme == "http" || scheme == "https" {
                            let tab = ParsedTab {
                                url,
                                title: extract_tab_title(item),
                                is_pinned: is_pinned_section,
                                is_essential: false,
                            };
                            if is_pinned_section {
                                ws.pinned_tabs.push(tab);
                            } else {
                                ws.regular_tabs.push(tab);
                            }
                        }
                    }
                }
            } else if is_folder_item(item) {
                ws.folders.push(ParsedFolder {
                    id: item_id.to_string(),
                    name: extract_item_title(item),
                    parent_id: String::new(),
                    workspace_id: ws.uuid.clone(),
                    tabs: Vec::new(),
                });
            }
        }

        i += 2;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn missing_file_returns_file_not_found() {
        let tmp = std::env::temp_dir().join("maho-import-test-no-arc");
        let _ = std::fs::remove_dir_all(&tmp);
        std::fs::create_dir_all(&tmp).unwrap();
        let err = parse_arc_sidebar(&tmp).unwrap_err();
        assert!(matches!(err, ImportError::FileNotFound(_)), "got {:?}", err);
    }

    #[test]
    fn empty_file_returns_empty_result() {
        let tmp = std::env::temp_dir().join("maho-import-test-arc-empty");
        let _ = std::fs::remove_dir_all(&tmp);
        std::fs::create_dir_all(&tmp).unwrap();
        std::fs::write(tmp.join("StorableSidebar.json"), "").unwrap();
        let res = parse_arc_sidebar(&tmp).unwrap();
        assert!(res.workspaces.is_empty());
    }

    #[test]
    fn invalid_json_returns_parse_error() {
        let tmp = std::env::temp_dir().join("maho-import-test-arc-bad");
        let _ = std::fs::remove_dir_all(&tmp);
        std::fs::create_dir_all(&tmp).unwrap();
        std::fs::write(tmp.join("StorableSidebar.json"), "{not valid json").unwrap();
        let err = parse_arc_sidebar(&tmp).unwrap_err();
        assert!(matches!(err, ImportError::Parse(_)), "got {:?}", err);
    }

    #[test]
    fn parses_minimal_path_a_fixture() {
        // Minimal sidebar.containers[1] structure with one space + one pinned tab.
        let fixture = serde_json::json!({
            "sidebar": {
                "containers": [
                    {},
                    {
                        "spaces": [
                            "uuid-skip",
                            {
                                "title": "Work",
                                "containerIDs": [
                                    "pinned", "container-1"
                                ],
                                "customInfo": {
                                    "iconType": { "emoji_v2": "🚀" }
                                }
                            }
                        ],
                        "items": [
                            "skip",
                            {
                                "id": "container-1",
                                "data": { "list": {} },
                                "childrenIds": ["skip", "tab-1"]
                            },
                            "skip",
                            {
                                "id": "tab-1",
                                "data": {
                                    "tab": {
                                        "savedURL": "https://example.com",
                                        "savedTitle": "Example"
                                    }
                                }
                            }
                        ]
                    }
                ]
            }
        });
        let tmp = std::env::temp_dir().join("maho-import-test-arc-minimal");
        let _ = std::fs::remove_dir_all(&tmp);
        std::fs::create_dir_all(&tmp).unwrap();
        std::fs::write(
            tmp.join("StorableSidebar.json"),
            serde_json::to_string(&fixture).unwrap(),
        )
        .unwrap();

        let res = parse_arc_sidebar(&tmp).unwrap();
        assert_eq!(res.workspaces.len(), 1);
        let ws = &res.workspaces[0];
        assert_eq!(ws.name, "Work");
        assert_eq!(ws.icon, "🚀");
        assert_eq!(ws.pinned_tabs.len(), 1);
        assert_eq!(ws.pinned_tabs[0].url, "https://example.com");
        assert_eq!(ws.pinned_tabs[0].title, "Example");
        assert!(ws.pinned_tabs[0].is_pinned);
    }

    #[test]
    fn rejects_non_http_scheme() {
        let fixture = serde_json::json!({
            "sidebar": {
                "containers": [
                    {},
                    {
                        "spaces": [
                            "u",
                            {
                                "title": "S",
                                "containerIDs": ["pinned", "c1"]
                            }
                        ],
                        "items": [
                            "u",
                            {
                                "id": "c1",
                                "data": { "list": {} },
                                "childrenIds": ["u", "t1"]
                            },
                            "u",
                            {
                                "id": "t1",
                                "data": {
                                    "tab": { "savedURL": "javascript:alert(1)" }
                                }
                            }
                        ]
                    }
                ]
            }
        });
        let tmp = std::env::temp_dir().join("maho-import-test-arc-bad-scheme");
        let _ = std::fs::remove_dir_all(&tmp);
        std::fs::create_dir_all(&tmp).unwrap();
        std::fs::write(
            tmp.join("StorableSidebar.json"),
            serde_json::to_string(&fixture).unwrap(),
        )
        .unwrap();

        let res = parse_arc_sidebar(&tmp).unwrap();
        assert_eq!(res.workspaces.len(), 1);
        assert!(res.workspaces[0].pinned_tabs.is_empty());
    }
}
