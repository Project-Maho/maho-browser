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

fn resolve_sidebar_path(start: &Path) -> ImportResult<std::path::PathBuf> {
    let mut candidate = Some(start.to_path_buf());
    let mut tried: Vec<String> = Vec::new();
    for _ in 0..4 {
        let Some(dir) = candidate else { break };
        let file = dir.join("StorableSidebar.json");
        if file.exists() {
            return Ok(file);
        }
        tried.push(file.display().to_string());
        candidate = dir.parent().map(|p| p.to_path_buf());
    }
    Err(ImportError::FileNotFound(tried.join(" | ")))
}

pub fn parse_arc_sidebar(arc_user_data_dir: &Path) -> ImportResult<ArcParseResult> {
    let sidebar_path = resolve_sidebar_path(arc_user_data_dir)?;

    let content = std::fs::read_to_string(&sidebar_path)
        .map_err(|e| ImportError::Io(format!("{}: {}", sidebar_path.display(), e)))?;

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
                        parse_items_into_workspace(
                            &mut ws,
                            &all_items,
                            children,
                            is_pinned,
                            None,
                            &mut std::collections::HashSet::new(),
                        );
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

/// A "real" user-visible folder has `data.list`. Items with `data.itemContainer`
/// are space-level root containers (pinned/unpinned roots), not user folders —
/// they have null titles and exist as structural roots only. Treating them as
/// folders pollutes LMDB with empty-named placeholder folders.
fn is_folder_item(item: &serde_json::Map<String, Value>) -> bool {
    item.get("data")
        .and_then(|v| v.as_object())
        .and_then(|d| d.get("list"))
        .is_some()
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
    let raw = item
        .get("title")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim();
    if raw.is_empty() {
        "Untitled".to_string()
    } else {
        raw.to_string()
    }
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
    current_folder_id: Option<&str>,
    visited: &mut std::collections::HashSet<String>,
) {
    let mut i = 1;
    while i < container_ids.len() {
        let Some(item_id) = container_ids[i].as_str() else {
            i += 2;
            continue;
        };
        i += 2;

        if !visited.insert(item_id.to_string()) {
            continue;
        }

        let Some(item) = find_item_by_id(all_items, item_id) else {
            continue;
        };

        if is_tab_item(item) {
            let url = extract_tab_url(item);
            if url.is_empty() {
                continue;
            }
            let Ok(parsed) = url::Url::parse(&url) else {
                continue;
            };
            let scheme = parsed.scheme();
            if scheme != "http" && scheme != "https" {
                continue;
            }
            let tab = ParsedTab {
                url,
                title: extract_tab_title(item),
                is_pinned: is_pinned_section,
                is_essential: false,
            };
            match current_folder_id {
                Some(fid) => {
                    if let Some(folder) = ws.folders.iter_mut().find(|f| f.id == fid) {
                        folder.tabs.push(tab);
                    }
                }
                None => {
                    if is_pinned_section {
                        ws.pinned_tabs.push(tab);
                    } else {
                        ws.regular_tabs.push(tab);
                    }
                }
            }
        } else if is_folder_item(item) {
            let folder_id = item_id.to_string();
            ws.folders.push(ParsedFolder {
                id: folder_id.clone(),
                name: extract_item_title(item),
                parent_id: current_folder_id.map(String::from).unwrap_or_default(),
                workspace_id: ws.uuid.clone(),
                tabs: Vec::new(),
            });

            if let Some(children) = find_children_ids(item) {
                parse_items_into_workspace(
                    ws,
                    all_items,
                    children,
                    is_pinned_section,
                    Some(&folder_id),
                    visited,
                );
            }
        }
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

    fn write_fixture(name: &str, fixture: serde_json::Value) -> std::path::PathBuf {
        let tmp = std::env::temp_dir().join(name);
        let _ = std::fs::remove_dir_all(&tmp);
        std::fs::create_dir_all(&tmp).unwrap();
        std::fs::write(
            tmp.join("StorableSidebar.json"),
            serde_json::to_string(&fixture).unwrap(),
        )
        .unwrap();
        tmp
    }

    #[test]
    fn tabs_inside_folder_are_nested_not_flat() {
        let fixture = serde_json::json!({
            "sidebar": {
                "containers": [
                    {},
                    {
                        "spaces": [
                            "u",
                            { "title": "S", "containerIDs": ["pinned", "root"] }
                        ],
                        "items": [
                            "u",
                            { "id": "root", "data": { "list": {} }, "childrenIds": ["u", "folder-a", "u", "loose-tab"] },
                            "u",
                            { "id": "folder-a", "title": "Bookmarks",
                              "data": { "list": {} },
                              "childrenIds": ["u", "tab-in-folder"] },
                            "u",
                            { "id": "tab-in-folder",
                              "data": { "tab": { "savedURL": "https://nested.example.com", "savedTitle": "Nested" } } },
                            "u",
                            { "id": "loose-tab",
                              "data": { "tab": { "savedURL": "https://loose.example.com", "savedTitle": "Loose" } } }
                        ]
                    }
                ]
            }
        });
        let tmp = write_fixture("maho-import-test-arc-nested-tab", fixture);
        let res = parse_arc_sidebar(&tmp).unwrap();
        let ws = &res.workspaces[0];
        assert_eq!(ws.folders.len(), 1);
        assert_eq!(ws.folders[0].name, "Bookmarks");
        assert_eq!(ws.folders[0].tabs.len(), 1);
        assert_eq!(ws.folders[0].tabs[0].url, "https://nested.example.com");
        assert_eq!(ws.pinned_tabs.len(), 1);
        assert_eq!(ws.pinned_tabs[0].url, "https://loose.example.com");
    }

    #[test]
    fn nested_folder_has_parent_id() {
        let fixture = serde_json::json!({
            "sidebar": {
                "containers": [
                    {},
                    {
                        "spaces": [
                            "u",
                            { "title": "S", "containerIDs": ["pinned", "root"] }
                        ],
                        "items": [
                            "u", { "id": "root", "data": { "list": {} }, "childrenIds": ["u", "outer"] },
                            "u", { "id": "outer", "title": "Outer",
                                   "data": { "list": {} },
                                   "childrenIds": ["u", "inner"] },
                            "u", { "id": "inner", "title": "Inner",
                                   "data": { "list": {} },
                                   "childrenIds": [] }
                        ]
                    }
                ]
            }
        });
        let tmp = write_fixture("maho-import-test-arc-nested-folder", fixture);
        let res = parse_arc_sidebar(&tmp).unwrap();
        let ws = &res.workspaces[0];
        assert_eq!(ws.folders.len(), 2);
        let outer = ws.folders.iter().find(|f| f.name == "Outer").unwrap();
        let inner = ws.folders.iter().find(|f| f.name == "Inner").unwrap();
        assert!(outer.parent_id.is_empty(), "outer should be root-level");
        assert_eq!(inner.parent_id, outer.id, "inner parent_id should be outer's id");
    }

    #[test]
    fn item_container_is_not_classified_as_folder() {
        let fixture = serde_json::json!({
            "sidebar": {
                "containers": [
                    {},
                    {
                        "spaces": [
                            "u",
                            { "title": "S", "containerIDs": ["pinned", "root"] }
                        ],
                        "items": [
                            "u",
                            { "id": "root", "data": { "list": {} }, "childrenIds": ["u", "structural"] },
                            "u",
                            { "id": "structural", "title": null,
                              "data": { "itemContainer": { "containerType": { "spaceItems": { "_0": "space-xyz" } } } },
                              "childrenIds": [] }
                        ]
                    }
                ]
            }
        });
        let tmp = write_fixture("maho-import-test-arc-itemcontainer", fixture);
        let res = parse_arc_sidebar(&tmp).unwrap();
        let ws = &res.workspaces[0];
        assert!(ws.folders.is_empty(), "itemContainer must not become a folder");
    }

    #[test]
    fn folder_with_empty_title_falls_back_to_untitled() {
        let fixture = serde_json::json!({
            "sidebar": {
                "containers": [
                    {},
                    {
                        "spaces": [
                            "u",
                            { "title": "S", "containerIDs": ["pinned", "root"] }
                        ],
                        "items": [
                            "u", { "id": "root", "data": { "list": {} }, "childrenIds": ["u", "blank"] },
                            "u", { "id": "blank", "title": "   ", "data": { "list": {} }, "childrenIds": [] }
                        ]
                    }
                ]
            }
        });
        let tmp = write_fixture("maho-import-test-arc-blank-title", fixture);
        let res = parse_arc_sidebar(&tmp).unwrap();
        assert_eq!(res.workspaces[0].folders[0].name, "Untitled");
    }

    #[test]
    fn cycle_in_children_does_not_infinite_loop() {
        let fixture = serde_json::json!({
            "sidebar": {
                "containers": [
                    {},
                    {
                        "spaces": [
                            "u",
                            { "title": "S", "containerIDs": ["pinned", "root"] }
                        ],
                        "items": [
                            "u", { "id": "root", "data": { "list": {} }, "childrenIds": ["u", "a"] },
                            "u", { "id": "a", "title": "A",
                                   "data": { "list": {} },
                                   "childrenIds": ["u", "b"] },
                            "u", { "id": "b", "title": "B",
                                   "data": { "list": {} },
                                   "childrenIds": ["u", "a"] }
                        ]
                    }
                ]
            }
        });
        let tmp = write_fixture("maho-import-test-arc-cycle", fixture);
        let res = parse_arc_sidebar(&tmp).unwrap();
        assert_eq!(res.workspaces[0].folders.len(), 2);
    }
}
