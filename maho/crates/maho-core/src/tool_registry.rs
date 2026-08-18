use std::collections::HashMap;
use std::time::Duration;

use maho_types::events::core_update::{CoreUpdate, LLMRequestContext};
use maho_types::mcp::{ToolCall, ToolDescriptor, ToolPermission, ToolProvenance, ToolResult};
pub type ToolDefinition = ToolDescriptor;

fn is_internal_tool(name: &str) -> bool {
    matches!(
        name,
        "search_bookmarks" | "list_tabs" | "get_page_info" | "search_history"
    )
}

fn validate_namespace(name: &str) -> bool {
    if name.starts_with("mcp:") {
        let rest = &name[4..];
        if let Some((server, tool)) = rest.split_once('/') {
            !server.is_empty()
                && !tool.is_empty()
                && server
                    .chars()
                    .all(|c| c.is_alphanumeric() || c == '_' || c == '-')
                && tool
                    .chars()
                    .all(|c| c.is_alphanumeric() || c == '_' || c == '-')
        } else {
            false
        }
    } else if name.starts_with("sh:") {
        let tool = &name[3..];
        !tool.is_empty()
            && tool
                .chars()
                .all(|c| c.is_alphanumeric() || c == '_' || c == '-')
    } else {
        // Bare name
        !name.is_empty() && name.chars().all(|c| c.is_alphanumeric() || c == '_')
    }
}

fn llm_facing_name(name: &str) -> String {
    if let Some(rest) = name.strip_prefix("mcp:") {
        if let Some((_server, tool)) = rest.split_once('/') {
            return format!("mcp_{}", tool);
        }
    }
    if let Some(tool) = name.strip_prefix("sh:") {
        return format!("sh_{}", tool);
    }
    name.to_string()
}

const MAX_TOOL_ROUNDS: u8 = 5;
type TabSummary = (String, String, String);
type BookmarkSearchResult = (String, String, String, Option<String>, String);
type HistorySearchFn<'a> = dyn Fn(&str, usize) -> Vec<(String, String, String)> + 'a;
type BookmarkSearchFn<'a> = dyn Fn(&str) -> Vec<BookmarkSearchResult> + 'a;

struct PendingToolLoop {
    original_request_id: String,
    _tool_calls: Vec<ToolCall>,
    completed_results: Vec<ToolResult>,
    remaining_external: Vec<ToolCall>,
    tool_round: u8,
    _conversation_messages: serde_json::Value,
    created_at: std::time::Instant,
}

pub struct ToolRegistry {
    tool_registry: Vec<ToolDefinition>,
    permission_cache: HashMap<String, bool>,
    pending_tool_loops: HashMap<String, PendingToolLoop>,
    active_tool_rounds: HashMap<String, u8>,
}

impl Default for ToolRegistry {
    fn default() -> Self {
        Self::new()
    }
}

impl ToolRegistry {
    pub fn new() -> Self {
        let mut mgr = Self {
            tool_registry: Vec::new(),
            permission_cache: HashMap::new(),
            pending_tool_loops: HashMap::new(),
            active_tool_rounds: HashMap::new(),
        };
        mgr.register_starter_tools();
        mgr
    }

    fn register_starter_tools(&mut self) {
        self.tool_registry.push(ToolDescriptor {
            name: "search_bookmarks".into(),
            description: "Search the user's bookmarks by keyword".into(),
            parameters_schema: serde_json::json!({
                "type": "object",
                "properties": {
                    "query": { "type": "string", "description": "Search query" }
                },
                "required": ["query"]
            }),
            provenance: ToolProvenance::BuiltinBrowser,
            sensitive: false,
            permission: ToolPermission::AutoApprove,
        });

        self.tool_registry.push(ToolDescriptor {
            name: "list_tabs".into(),
            description: "List all open tabs with their titles and URLs".into(),
            parameters_schema: serde_json::json!({
                "type": "object",
                "properties": {}
            }),
            provenance: ToolProvenance::BuiltinBrowser,
            sensitive: false,
            permission: ToolPermission::AutoApprove,
        });

        self.tool_registry.push(ToolDescriptor {
            name: "get_page_info".into(),
            description: "Get the title and URL of the currently active tab".into(),
            parameters_schema: serde_json::json!({
                "type": "object",
                "properties": {}
            }),
            provenance: ToolProvenance::BuiltinBrowser,
            sensitive: false,
            permission: ToolPermission::AutoApprove,
        });

        self.tool_registry.push(ToolDescriptor {
            name: "search_history".into(),
            description: "Search browsing history by keyword".into(),
            parameters_schema: serde_json::json!({
                "type": "object",
                "properties": {
                    "query": { "type": "string", "description": "Search query" }
                },
                "required": ["query"]
            }),
            provenance: ToolProvenance::BuiltinBrowser,
            sensitive: false,
            permission: ToolPermission::AutoApprove,
        });

        self.tool_registry.push(ToolDescriptor {
            name: "open_tab".into(),
            description: "Open a new tab with the given URL".into(),
            parameters_schema: serde_json::json!({
                "type": "object",
                "properties": {
                    "url": { "type": "string", "description": "URL to open" }
                },
                "required": ["url"]
            }),
            provenance: ToolProvenance::BuiltinBrowser,
            sensitive: false,
            permission: ToolPermission::SessionApprove,
        });

        self.tool_registry.push(ToolDescriptor {
            name: "navigate".into(),
            description: "Navigate the current tab to a URL".into(),
            parameters_schema: serde_json::json!({
                "type": "object",
                "properties": {
                    "url": { "type": "string", "description": "URL to navigate to" }
                },
                "required": ["url"]
            }),
            provenance: ToolProvenance::BuiltinBrowser,
            sensitive: false,
            permission: ToolPermission::SessionApprove,
        });

        self.tool_registry.push(ToolDescriptor {
            name: "close_tab".into(),
            description: "Close a tab by its ID".into(),
            parameters_schema: serde_json::json!({
                "type": "object",
                "properties": {
                    "tab_id": { "type": "string", "description": "Tab ID to close" }
                },
                "required": ["tab_id"]
            }),
            provenance: ToolProvenance::BuiltinBrowser,
            sensitive: true,
            permission: ToolPermission::AlwaysAsk,
        });

        self.tool_registry.push(ToolDescriptor {
            name: "create_bookmark".into(),
            description: "Create a bookmark for a URL".into(),
            parameters_schema: serde_json::json!({
                "type": "object",
                "properties": {
                    "url": { "type": "string", "description": "URL to bookmark" },
                    "title": { "type": "string", "description": "Bookmark title" }
                },
                "required": ["url", "title"]
            }),
            provenance: ToolProvenance::BuiltinBrowser,
            sensitive: false,
            permission: ToolPermission::SessionApprove,
        });
    }

    pub fn register_tool(&mut self, def: ToolDefinition) -> Result<(), String> {
        if !validate_namespace(&def.name) {
            return Err(format!("Invalid tool name format: {}", def.name));
        }

        let new_llm_name = llm_facing_name(&def.name);

        for existing in &self.tool_registry {
            if llm_facing_name(&existing.name) == new_llm_name {
                return Err(format!(
                    "Collision detected: tool '{}' and existing tool '{}' both resolve to LLM name '{}'",
                    def.name, existing.name, new_llm_name
                ));
            }
        }

        self.tool_registry.push(def);
        Ok(())
    }

    pub fn get_tool(&self, name: &str) -> Option<&ToolDescriptor> {
        if let Some(t) = self.tool_registry.iter().find(|t| t.name == name) {
            return Some(t);
        }
        let mut candidates: Vec<&ToolDescriptor> = self
            .tool_registry
            .iter()
            .filter(|t| {
                t.name == name
                    || t.name.split('/').last().unwrap_or("") == name
                    || t.name.split(':').last().unwrap_or("") == name
            })
            .collect();

        candidates.sort_by_key(|t| match &t.provenance {
            ToolProvenance::BuiltinBrowser => 0,
            _ => 1,
        });

        candidates.into_iter().next()
    }

    pub fn unregister_tool(&mut self, name: &str) {
        self.tool_registry.retain(|t| t.name != name);
    }

    pub fn list_tool_names(&self) -> Vec<String> {
        self.tool_registry.iter().map(|t| t.name.clone()).collect()
    }

    pub fn get_tools_schema(&self) -> serde_json::Value {
        let tools: Vec<serde_json::Value> = self
            .tool_registry
            .iter()
            .map(|t| t.to_openai_function())
            .collect();
        serde_json::Value::Array(tools)
    }

    pub fn sweep_stale_tool_loops(&mut self) {
        let now = std::time::Instant::now();
        self.pending_tool_loops
            .retain(|_, entry| now.duration_since(entry.created_at) <= Duration::from_secs(3600));
    }

    #[allow(clippy::too_many_arguments)]
    pub fn handle_llm_response_with_tools(
        &mut self,
        request_id: &str,
        tool_calls_json: &[serde_json::Value],
        conversation_messages: serde_json::Value,
        tab_summaries: &[TabSummary],
        active_tab_info: Option<(&str, &str)>,
        history_search_fn: &HistorySearchFn<'_>,
        bookmark_search_fn: &BookmarkSearchFn<'_>,
    ) -> Vec<CoreUpdate> {
        self.sweep_stale_tool_loops();

        let current_round = self.active_tool_rounds.remove(request_id).unwrap_or(0);
        if current_round >= MAX_TOOL_ROUNDS {
            return vec![CoreUpdate::ChatCompletionReady {
                request_id: request_id.to_string(),
                response: "Tool execution limit reached. Please rephrase your request.".into(),
            }];
        }

        let tool_calls: Vec<ToolCall> = tool_calls_json
            .iter()
            .filter_map(|tc| {
                let id = tc["id"].as_str()?.to_string();
                let name = tc["function"]["name"].as_str()?.to_string();
                let args_str = tc["function"]["arguments"].as_str().unwrap_or("{}");
                let arguments = serde_json::from_str(args_str).unwrap_or(serde_json::json!({}));
                Some(ToolCall {
                    id,
                    name,
                    arguments,
                })
            })
            .collect();

        if tool_calls.is_empty() {
            return vec![];
        }

        let mut completed = Vec::new();
        let mut needs_permission = Vec::new();
        let mut needs_external = Vec::new();
        let mut updates = Vec::new();

        for tc in &tool_calls {
            let def = self.tool_registry.iter().find(|d| d.name == tc.name);
            let Some(def) = def else {
                completed.push(ToolResult {
                    call_id: tc.id.clone(),
                    success: false,
                    content: format!("Unknown tool: {}", tc.name),
                });
                continue;
            };

            let needs_perm = if def.sensitive {
                true
            } else {
                match def.permission {
                    ToolPermission::AutoApprove => false,
                    ToolPermission::SessionApprove => !self
                        .permission_cache
                        .get(&def.name)
                        .copied()
                        .unwrap_or(false),
                    ToolPermission::AlwaysAsk => true,
                }
            };

            if needs_perm {
                needs_permission.push(tc.clone());
                continue;
            }

            if is_internal_tool(&def.name) {
                let result = self.execute_internal_tool(
                    tc,
                    tab_summaries,
                    active_tab_info,
                    history_search_fn,
                    bookmark_search_fn,
                );
                completed.push(result);
            } else {
                needs_external.push(tc.clone());
            }
        }

        for tc in &needs_permission {
            let def = self.tool_registry.iter().find(|d| d.name == tc.name);
            let desc = def.map(|d| d.description.clone()).unwrap_or_default();
            let args_summary = serde_json::to_string(&tc.arguments).unwrap_or_default();
            updates.push(CoreUpdate::RequestToolPermission {
                call_id: tc.id.clone(),
                tool_name: tc.name.clone(),
                description: desc,
                args_summary,
            });
        }

        for tc in &needs_external {
            let args = serde_json::to_string(&tc.arguments).unwrap_or_default();
            updates.push(CoreUpdate::ExecuteToolAction {
                call_id: tc.id.clone(),
                tool_name: tc.name.clone(),
                args,
            });
        }

        let has_pending = !needs_permission.is_empty() || !needs_external.is_empty();
        if has_pending {
            let mut remaining = needs_permission;
            remaining.extend(needs_external);
            self.pending_tool_loops.insert(
                request_id.to_string(),
                PendingToolLoop {
                    original_request_id: request_id.to_string(),
                    _tool_calls: tool_calls.clone(),
                    completed_results: completed,
                    remaining_external: remaining,
                    tool_round: current_round + 1,
                    _conversation_messages: conversation_messages,
                    created_at: std::time::Instant::now(),
                },
            );
        } else if !completed.is_empty() {
            let messages_json = build_tool_results_message(&completed);
            updates.push(CoreUpdate::RequestLlmCompletion {
                request_id: request_id.to_string(),
                request_type: maho_types::events::core_update::LLMRequestType::ChatCompletion,
                prompt: messages_json,
                context: LLMRequestContext::Chat {
                    chat: Box::new(maho_types::chat::ChatRequestContext {
                        request_mode: maho_types::chat::ChatRequestMode::ToolAssistedTask,
                        page_context: None,
                        tools: None,
                        session_id: None,
                        page_url: None,
                    }),
                },
            });
        }

        updates
    }

    pub fn handle_tool_action_result(
        &mut self,
        call_id: &str,
        success: bool,
        result_content: &str,
    ) -> Vec<CoreUpdate> {
        let request_id = self.find_request_for_call(call_id);
        let Some(request_id) = request_id else {
            return vec![];
        };

        let pending = match self.pending_tool_loops.get_mut(&request_id) {
            Some(p) => p,
            None => return vec![],
        };

        let tool_result = ToolResult {
            call_id: call_id.to_string(),
            success,
            content: result_content.to_string(),
        };
        pending.completed_results.push(tool_result);
        pending.remaining_external.retain(|tc| tc.id != call_id);

        if !pending.remaining_external.is_empty() {
            return vec![];
        }

        let pending = self.pending_tool_loops.remove(&request_id);
        let Some(pending) = pending else {
            return vec![];
        };

        self.active_tool_rounds
            .insert(pending.original_request_id.clone(), pending.tool_round);
        let messages_json = build_tool_results_message(&pending.completed_results);
        vec![CoreUpdate::RequestLlmCompletion {
            request_id: pending.original_request_id,
            request_type: maho_types::events::core_update::LLMRequestType::ChatCompletion,
            prompt: messages_json,
            context: LLMRequestContext::Chat {
                chat: Box::new(maho_types::chat::ChatRequestContext {
                    request_mode: maho_types::chat::ChatRequestMode::ToolAssistedTask,
                    page_context: None,
                    tools: None,
                    session_id: None,
                    page_url: None,
                }),
            },
        }]
    }

    pub fn handle_tool_permission_response(
        &mut self,
        call_id: &str,
        granted: bool,
    ) -> Vec<CoreUpdate> {
        let request_id = self.find_request_for_call(call_id);
        let Some(request_id) = request_id else {
            return vec![];
        };

        if granted {
            let tool_name = self.pending_tool_loops.get(&request_id).and_then(|p| {
                p.remaining_external
                    .iter()
                    .find(|tc| tc.id == call_id)
                    .map(|tc| tc.name.clone())
            });
            if let Some(name) = tool_name {
                let def = self.tool_registry.iter().find(|d| d.name == name);
                if let Some(def) = def {
                    if matches!(def.permission, ToolPermission::SessionApprove) {
                        self.permission_cache.insert(name.clone(), true);
                    }
                }
            }
        }

        let pending = match self.pending_tool_loops.get_mut(&request_id) {
            Some(p) => p,
            None => return vec![],
        };

        let tc = pending
            .remaining_external
            .iter()
            .find(|tc| tc.id == call_id)
            .cloned();

        if !granted {
            if let Some(tc) = &tc {
                pending.completed_results.push(ToolResult {
                    call_id: call_id.to_string(),
                    success: false,
                    content: format!("Permission denied for tool: {}", tc.name),
                });
                pending.remaining_external.retain(|t| t.id != call_id);
            }
        } else if let Some(tc) = &tc {
            let def = self.tool_registry.iter().find(|d| d.name == tc.name);
            if let Some(def) = def {
                if !is_internal_tool(&def.name) {
                    let args = serde_json::to_string(&tc.arguments).unwrap_or_default();
                    return vec![CoreUpdate::ExecuteToolAction {
                        call_id: call_id.to_string(),
                        tool_name: tc.name.clone(),
                        args,
                    }];
                } else {
                    pending.remaining_external.retain(|t| t.id != call_id);
                    pending.completed_results.push(ToolResult {
                        call_id: call_id.to_string(),
                        success: true,
                        content: "{\"result\": \"Not yet implemented\"}".into(),
                    });
                    return vec![];
                }
            }
        }

        if pending.remaining_external.is_empty() {
            let pending = self.pending_tool_loops.remove(&request_id);
            if let Some(pending) = pending {
                self.active_tool_rounds
                    .insert(pending.original_request_id.clone(), pending.tool_round);
                let messages_json = build_tool_results_message(&pending.completed_results);
                return vec![CoreUpdate::RequestLlmCompletion {
                    request_id: pending.original_request_id,
                    request_type: maho_types::events::core_update::LLMRequestType::ChatCompletion,
                    prompt: messages_json,
                    context: LLMRequestContext::Chat {
                        chat: Box::new(maho_types::chat::ChatRequestContext {
                            request_mode: maho_types::chat::ChatRequestMode::ToolAssistedTask,
                            page_context: None,
                            tools: None,
                            session_id: None,
                            page_url: None,
                        }),
                    },
                }];
            }
        }

        vec![]
    }

    fn execute_internal_tool(
        &self,
        tc: &ToolCall,
        tab_summaries: &[TabSummary],
        active_tab_info: Option<(&str, &str)>,
        history_search_fn: &HistorySearchFn<'_>,
        bookmark_search_fn: &BookmarkSearchFn<'_>,
    ) -> ToolResult {
        let content = match tc.name.as_str() {
            "search_bookmarks" => {
                let query = tc.arguments["query"].as_str().unwrap_or("");
                let results = bookmark_search_fn(query);
                let items: Vec<serde_json::Value> = results
                    .iter()
                    .map(|(id, url, title, _folder, _created)| {
                        serde_json::json!({"id": id, "url": url, "title": title})
                    })
                    .collect();
                serde_json::to_string(&items).unwrap_or_else(|_| "[]".into())
            }
            "list_tabs" => {
                let items: Vec<serde_json::Value> = tab_summaries
                    .iter()
                    .map(|(id, title, url)| {
                        serde_json::json!({"id": id, "title": title, "url": url})
                    })
                    .collect();
                serde_json::to_string(&items).unwrap_or_else(|_| "[]".into())
            }
            "get_page_info" => match active_tab_info {
                Some((title, url)) => serde_json::json!({"title": title, "url": url}).to_string(),
                None => "{\"title\": \"\", \"url\": \"\"}".into(),
            },
            "search_history" => {
                let query = tc.arguments["query"].as_str().unwrap_or("");
                let results = history_search_fn(query, 20);
                let items: Vec<serde_json::Value> = results
                    .iter()
                    .map(|(url, title, visited_at)| {
                        serde_json::json!({"url": url, "title": title, "visited_at": visited_at})
                    })
                    .collect();
                serde_json::to_string(&items).unwrap_or_else(|_| "[]".into())
            }
            _ => "{\"result\": \"Not yet implemented\"}".into(),
        };

        ToolResult {
            call_id: tc.id.clone(),
            success: true,
            content,
        }
    }

    fn find_request_for_call(&self, call_id: &str) -> Option<String> {
        for (request_id, pending) in &self.pending_tool_loops {
            if pending.remaining_external.iter().any(|tc| tc.id == call_id) {
                return Some(request_id.clone());
            }
        }
        None
    }
}

fn build_tool_results_message(results: &[ToolResult]) -> String {
    let messages: Vec<serde_json::Value> = results
        .iter()
        .map(|r| {
            serde_json::json!({
                "role": "tool",
                "tool_call_id": r.call_id,
                "content": r.content,
            })
        })
        .collect();
    serde_json::to_string(&messages).unwrap_or_else(|_| "[]".into())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_new_registers_starter_tools() {
        let mgr = ToolRegistry::new();
        assert_eq!(mgr.tool_registry.len(), 8);
        let names: Vec<&str> = mgr.tool_registry.iter().map(|t| t.name.as_str()).collect();
        assert!(names.contains(&"search_bookmarks"));
        assert!(names.contains(&"list_tabs"));
        assert!(names.contains(&"open_tab"));
        assert!(names.contains(&"close_tab"));
    }

    #[test]
    fn test_get_tools_schema_format() {
        let mgr = ToolRegistry::new();
        let schema = mgr.get_tools_schema();
        let arr = schema.as_array().expect("should be array");
        assert_eq!(arr.len(), 8);
        for tool in arr {
            assert_eq!(tool["type"], "function");
            assert!(tool["function"]["name"].is_string());
            assert!(tool["function"]["description"].is_string());
            assert!(tool["function"]["parameters"].is_object());
        }
    }

    #[test]
    fn test_execute_internal_list_tabs() {
        let mgr = ToolRegistry::new();
        let tc = ToolCall {
            id: "call_1".into(),
            name: "list_tabs".into(),
            arguments: serde_json::json!({}),
        };
        let tabs = vec![("tab1".into(), "Google".into(), "https://google.com".into())];
        let result = mgr.execute_internal_tool(&tc, &tabs, None, &|_, _| vec![], &|_| vec![]);
        assert!(result.success);
        assert!(result.content.contains("Google"));
    }

    #[test]
    fn test_execute_internal_get_page_info() {
        let mgr = ToolRegistry::new();
        let tc = ToolCall {
            id: "call_2".into(),
            name: "get_page_info".into(),
            arguments: serde_json::json!({}),
        };
        let result = mgr.execute_internal_tool(
            &tc,
            &[],
            Some(("Rust Docs", "https://doc.rust-lang.org")),
            &|_, _| vec![],
            &|_| vec![],
        );
        assert!(result.success);
        assert!(result.content.contains("Rust Docs"));
    }

    #[test]
    fn test_handle_tool_action_result_unknown_call() {
        let mut mgr = ToolRegistry::new();
        let updates = mgr.handle_tool_action_result("unknown", true, "ok");
        assert!(updates.is_empty());
    }

    #[test]
    fn test_handle_tool_permission_denied() {
        let mut mgr = ToolRegistry::new();
        let updates = mgr.handle_tool_permission_response("unknown", false);
        assert!(updates.is_empty());
    }

    #[test]
    fn test_auto_approve_internal_tool() {
        let mut mgr = ToolRegistry::new();
        let tc_json = vec![serde_json::json!({
            "id": "call_1",
            "function": {
                "name": "list_tabs",
                "arguments": "{}"
            }
        })];
        let tabs = vec![("t1".into(), "Tab 1".into(), "https://example.com".into())];
        let updates = mgr.handle_llm_response_with_tools(
            "req_1",
            &tc_json,
            serde_json::json!([]),
            &tabs,
            None,
            &|_, _| vec![],
            &|_| vec![],
        );
        assert_eq!(updates.len(), 1);
        assert!(matches!(
            &updates[0],
            CoreUpdate::RequestLlmCompletion { .. }
        ));
    }

    #[test]
    fn test_session_approve_needs_permission() {
        let mut mgr = ToolRegistry::new();
        let tc_json = vec![serde_json::json!({
            "id": "call_1",
            "function": {
                "name": "open_tab",
                "arguments": "{\"url\": \"https://example.com\"}"
            }
        })];
        let updates = mgr.handle_llm_response_with_tools(
            "req_1",
            &tc_json,
            serde_json::json!([]),
            &[],
            None,
            &|_, _| vec![],
            &|_| vec![],
        );
        assert_eq!(updates.len(), 1);
        assert!(matches!(
            &updates[0],
            CoreUpdate::RequestToolPermission { tool_name, .. } if tool_name == "open_tab"
        ));
    }

    #[test]
    fn test_round_counter_increments_across_continuations() {
        let mut mgr = ToolRegistry::new();
        let tc_json = vec![serde_json::json!({
            "id": "call_1",
            "function": { "name": "list_tabs", "arguments": "{}" }
        })];
        let tabs = vec![("t1".into(), "Tab".into(), "https://x.com".into())];

        let updates = mgr.handle_llm_response_with_tools(
            "req_1",
            &tc_json,
            serde_json::json!([]),
            &tabs,
            None,
            &|_, _| vec![],
            &|_| vec![],
        );
        assert_eq!(updates.len(), 1);
        assert!(matches!(
            &updates[0],
            CoreUpdate::RequestLlmCompletion { .. }
        ));
    }

    #[test]
    fn test_round_counter_caps_at_max() {
        let mut mgr = ToolRegistry::new();
        let tc_json = vec![serde_json::json!({
            "id": "call_1",
            "function": { "name": "list_tabs", "arguments": "{}" }
        })];
        let tabs = vec![("t1".into(), "Tab".into(), "https://x.com".into())];

        mgr.active_tool_rounds
            .insert("req_capped".to_string(), MAX_TOOL_ROUNDS);
        let updates = mgr.handle_llm_response_with_tools(
            "req_capped",
            &tc_json,
            serde_json::json!([]),
            &tabs,
            None,
            &|_, _| vec![],
            &|_| vec![],
        );
        assert_eq!(updates.len(), 1);
        assert!(
            matches!(&updates[0], CoreUpdate::ChatCompletionReady { response, .. } if response.contains("limit reached"))
        );
    }

    #[test]
    fn test_round_counter_tracks_through_action_result() {
        let mut mgr = ToolRegistry::new();
        mgr.permission_cache.insert("open_tab".to_string(), true);
        let tc_json = vec![serde_json::json!({
            "id": "call_ext_1",
            "function": { "name": "open_tab", "arguments": "{\"url\": \"https://example.com\"}" }
        })];

        let updates = mgr.handle_llm_response_with_tools(
            "req_ext",
            &tc_json,
            serde_json::json!([]),
            &[],
            None,
            &|_, _| vec![],
            &|_| vec![],
        );
        assert_eq!(updates.len(), 1);
        assert!(matches!(&updates[0], CoreUpdate::ExecuteToolAction { .. }));

        let updates = mgr.handle_tool_action_result("call_ext_1", true, "{\"ok\":true}");
        assert_eq!(updates.len(), 1);
        assert!(matches!(
            &updates[0],
            CoreUpdate::RequestLlmCompletion { .. }
        ));

        assert_eq!(*mgr.active_tool_rounds.get("req_ext").unwrap(), 1);
    }

    #[test]
    fn test_register_tool_adds_to_registry() {
        let mut mgr = ToolRegistry::new();
        let initial_len = mgr.tool_registry.len();
        mgr.register_tool(ToolDescriptor {
            name: "custom_tool".into(),
            description: "A custom tool".into(),
            parameters_schema: serde_json::json!({"type": "object", "properties": {}}),
            provenance: ToolProvenance::BuiltinBrowser,
            sensitive: false,
            permission: ToolPermission::AutoApprove,
        })
        .unwrap();
        assert_eq!(mgr.tool_registry.len(), initial_len + 1);
        assert!(mgr.list_tool_names().contains(&"custom_tool".to_string()));
    }

    #[test]
    fn test_register_tool_dedups() {
        let mut mgr = ToolRegistry::new();
        let def = ToolDescriptor {
            name: "dedup_tool".into(),
            description: "Dedup test".into(),
            parameters_schema: serde_json::json!({"type": "object", "properties": {}}),
            provenance: ToolProvenance::BuiltinBrowser,
            sensitive: false,
            permission: ToolPermission::AutoApprove,
        };
        mgr.register_tool(def.clone()).unwrap();
        // Registering exact same name should fail due to collision checking
        assert!(mgr.register_tool(def.clone()).is_err());
        let count = mgr
            .tool_registry
            .iter()
            .filter(|t| t.name == "dedup_tool")
            .count();
        assert_eq!(count, 1);
    }

    #[test]
    fn test_unregister_tool() {
        let mut mgr = ToolRegistry::new();
        mgr.register_tool(ToolDescriptor {
            name: "removable_tool".into(),
            description: "Will be removed".into(),
            parameters_schema: serde_json::json!({"type": "object", "properties": {}}),
            provenance: ToolProvenance::BuiltinBrowser,
            sensitive: false,
            permission: ToolPermission::AutoApprove,
        })
        .unwrap();
        assert!(mgr
            .list_tool_names()
            .contains(&"removable_tool".to_string()));
        mgr.unregister_tool("removable_tool");
        assert!(!mgr
            .list_tool_names()
            .contains(&"removable_tool".to_string()));
    }

    #[test]
    fn test_tool_registration_namespace_validation() {
        let mut mgr = ToolRegistry::new();

        // Valid bare name
        assert!(mgr
            .register_tool(ToolDescriptor {
                name: "valid_name".into(),
                description: "".into(),
                parameters_schema: serde_json::json!({}),
                provenance: ToolProvenance::BuiltinBrowser,
                sensitive: false,
                permission: ToolPermission::AutoApprove,
            })
            .is_ok());

        // Invalid bare name (contains spaces)
        assert!(mgr
            .register_tool(ToolDescriptor {
                name: "invalid name".into(),
                description: "".into(),
                parameters_schema: serde_json::json!({}),
                provenance: ToolProvenance::BuiltinBrowser,
                sensitive: false,
                permission: ToolPermission::AutoApprove,
            })
            .is_err());

        // Valid mcp format
        assert!(mgr
            .register_tool(ToolDescriptor {
                name: "mcp:server-1/valid-tool".into(),
                description: "".into(),
                parameters_schema: serde_json::json!({}),
                provenance: ToolProvenance::External("server-1".into()),
                sensitive: false,
                permission: ToolPermission::AutoApprove,
            })
            .is_ok());

        // Invalid mcp format (missing slash)
        assert!(mgr
            .register_tool(ToolDescriptor {
                name: "mcp:server-1".into(),
                description: "".into(),
                parameters_schema: serde_json::json!({}),
                provenance: ToolProvenance::External("server-1".into()),
                sensitive: false,
                permission: ToolPermission::AutoApprove,
            })
            .is_err());

        // Valid sh format
        assert!(mgr
            .register_tool(ToolDescriptor {
                name: "sh:ripgrep".into(),
                description: "".into(),
                parameters_schema: serde_json::json!({}),
                provenance: ToolProvenance::AgentLocal,
                sensitive: false,
                permission: ToolPermission::AutoApprove,
            })
            .is_ok());
    }

    #[test]
    fn test_tool_registration_collision_detection() {
        let mut mgr = ToolRegistry::new();

        // Register mcp:server1/search (LLM name: mcp_search) and bare search (LLM name: search) -> should succeed
        mgr.register_tool(ToolDescriptor {
            name: "mcp:server1/search".into(),
            description: "".into(),
            parameters_schema: serde_json::json!({}),
            provenance: ToolProvenance::External("server1".into()),
            sensitive: false,
            permission: ToolPermission::AutoApprove,
        })
        .unwrap();

        mgr.register_tool(ToolDescriptor {
            name: "search".into(),
            description: "".into(),
            parameters_schema: serde_json::json!({}),
            provenance: ToolProvenance::BuiltinBrowser,
            sensitive: false,
            permission: ToolPermission::AutoApprove,
        })
        .unwrap();

        // Registering mcp:server2/search (LLM name: mcp_search) should collide with mcp:server1/search
        let res = mgr.register_tool(ToolDescriptor {
            name: "mcp:server2/search".into(),
            description: "".into(),
            parameters_schema: serde_json::json!({}),
            provenance: ToolProvenance::External("server2".into()),
            sensitive: false,
            permission: ToolPermission::AutoApprove,
        });
        assert!(res.is_err());
        assert!(res.err().unwrap().contains("Collision detected"));
    }

    #[test]
    fn test_tool_registration_priority_resolution() {
        let mut mgr = ToolRegistry::new();

        // Register a namespaced tool and a bare builtin tool with the same short name
        mgr.register_tool(ToolDescriptor {
            name: "mcp:my-server/lookup".into(),
            description: "namespaced".into(),
            parameters_schema: serde_json::json!({}),
            provenance: ToolProvenance::External("my-server".into()),
            sensitive: false,
            permission: ToolPermission::AutoApprove,
        })
        .unwrap();

        mgr.register_tool(ToolDescriptor {
            name: "lookup".into(),
            description: "builtin".into(),
            parameters_schema: serde_json::json!({}),
            provenance: ToolProvenance::BuiltinBrowser,
            sensitive: false,
            permission: ToolPermission::AutoApprove,
        })
        .unwrap();

        // lookup should resolve to the bare builtin
        let tool = mgr.get_tool("lookup").unwrap();
        assert_eq!(tool.description, "builtin");
    }

    #[test]
    fn test_sweep_stale_tool_loops() {
        let mut mgr = ToolRegistry::new();
        mgr.pending_tool_loops.insert(
            "stale_req".to_string(),
            PendingToolLoop {
                original_request_id: "stale_req".into(),
                _tool_calls: vec![],
                completed_results: vec![],
                remaining_external: vec![],
                tool_round: 1,
                _conversation_messages: serde_json::json!([]),
                created_at: std::time::Instant::now() - Duration::from_secs(3601),
            },
        );
        mgr.pending_tool_loops.insert(
            "fresh_req".to_string(),
            PendingToolLoop {
                original_request_id: "fresh_req".into(),
                _tool_calls: vec![],
                completed_results: vec![],
                remaining_external: vec![],
                tool_round: 1,
                _conversation_messages: serde_json::json!([]),
                created_at: std::time::Instant::now(),
            },
        );
        mgr.sweep_stale_tool_loops();
        assert!(!mgr.pending_tool_loops.contains_key("stale_req"));
        assert!(mgr.pending_tool_loops.contains_key("fresh_req"));
    }
}
