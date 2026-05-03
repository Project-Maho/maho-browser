use std::collections::HashMap;
use std::collections::VecDeque;

use maho_types::common::Url;
use maho_types::events::core_update::{CoreUpdate, LLMRequestType, TidyTabFolder};
use maho_types::identifiers::{DownloadId, SpaceId, TabId};

#[derive(Debug)]
struct LLMRequest {
    request_type: LLMRequestType,
    context: serde_json::Value,
}

const MAX_CHAT_HISTORY: usize = 20;

pub struct LLMManager {
    pending_requests: HashMap<String, LLMRequest>,
    next_id: u64,
    preview_cache: HashMap<String, (String, String)>,
    pending_tidy_tabs: Option<(SpaceId, Vec<TidyTabFolder>)>,
    chat_history: VecDeque<(String, String)>,
}

impl Default for LLMManager {
    fn default() -> Self {
        Self::new()
    }
}

impl LLMManager {
    pub fn new() -> Self {
        Self {
            pending_requests: HashMap::new(),
            next_id: 0,
            preview_cache: HashMap::new(),
            pending_tidy_tabs: None,
            chat_history: VecDeque::new(),
        }
    }

    fn generate_request_id(&mut self) -> String {
        self.next_id += 1;
        format!("llm_{}", self.next_id)
    }

    pub fn request_tidy_title(&mut self, tab_id: &TabId, title: &str, url: &str) -> CoreUpdate {
        let request_id = self.generate_request_id();
        let prompt = format!(
            "You are a tab title shortener. Given a web page title and URL, return ONLY a short 1-3 word title.\n\n\
             Title: {}\n\
             URL: {}\n\n\
             Rules:\n\
             - Remove site names, separators (-, |, ·), breadcrumbs\n\
             - Keep the most distinctive part\n\
             - Max 3 words\n\
             - Return ONLY the shortened title, nothing else\n\n\
             Examples:\n\
             - \"Settings - Account - GitHub\" → \"Account\"\n\
             - \"Pull requests · user/repo\" → \"Pull Requests\"\n\
             - \"Amazon.com: Wireless Mouse - Electronics\" → \"Wireless Mouse\"",
            title, url
        );
        let context = serde_json::json!({ "tab_id": tab_id.to_string() });
        self.pending_requests.insert(
            request_id.clone(),
            LLMRequest {
                request_type: LLMRequestType::TidyTabTitle,
                context: context.clone(),
            },
        );
        CoreUpdate::RequestLlmCompletion {
            request_id,
            request_type: LLMRequestType::TidyTabTitle,
            prompt,
            context,
        }
    }

    pub fn request_tidy_download(
        &mut self,
        download_id: &str,
        filename: &str,
        url: &str,
        page_title: &str,
    ) -> CoreUpdate {
        let request_id = self.generate_request_id();
        let prompt = format!(
            "Rename this downloaded file to be human-readable. Return ONLY the new filename with extension.\n\n\
             Original: {}\n\
             Source URL: {}\n\
             Page Title: {}\n\n\
             Rules:\n\
             - Keep the file extension\n\
             - Use descriptive words, not codes/hashes\n\
             - Max 50 characters\n\
             - Use spaces, not underscores or dashes\n\
             - Return ONLY the filename, nothing else",
            filename, url, page_title
        );
        let context = serde_json::json!({ "download_id": download_id });
        self.pending_requests.insert(
            request_id.clone(),
            LLMRequest {
                request_type: LLMRequestType::TidyDownload,
                context: context.clone(),
            },
        );
        CoreUpdate::RequestLlmCompletion {
            request_id,
            request_type: LLMRequestType::TidyDownload,
            prompt,
            context,
        }
    }

    pub fn request_page_preview(&mut self, url: &str) -> Option<CoreUpdate> {
        if let Some((title, summary)) = self.preview_cache.get(url) {
            return Some(CoreUpdate::PagePreviewReady {
                url: Url(url.to_string()),
                title: title.clone(),
                summary: summary.clone(),
            });
        }

        let request_id = self.generate_request_id();
        let prompt = format!(
            "Summarize this web page in 2-3 sentences. Return a JSON with \"title\" and \"summary\" fields.\n\nURL: {}",
            url
        );
        let context = serde_json::json!({ "url": url });
        self.pending_requests.insert(
            request_id.clone(),
            LLMRequest {
                request_type: LLMRequestType::PagePreview,
                context: context.clone(),
            },
        );
        Some(CoreUpdate::RequestLlmCompletion {
            request_id,
            request_type: LLMRequestType::PagePreview,
            prompt,
            context,
        })
    }

    pub fn request_tidy_tabs(
        &mut self,
        space_id: &SpaceId,
        tabs: Vec<(TabId, String, String)>,
    ) -> CoreUpdate {
        let request_id = self.generate_request_id();
        let tabs_json: Vec<serde_json::Value> = tabs
            .iter()
            .map(|(id, title, url)| {
                serde_json::json!({
                    "id": id.to_string(),
                    "title": title,
                    "url": url,
                })
            })
            .collect();
        let prompt = serde_json::json!({
            "instruction": "Group these browser tabs into 2-4 folders. Return JSON only.",
            "tabs": tabs_json,
            "response_format": {
                "folders": [
                    {"name": "Example", "tab_ids": ["tab_1"]}
                ]
            }
        })
        .to_string();
        let context = serde_json::json!({ "space_id": space_id.to_string() });
        self.pending_requests.insert(
            request_id.clone(),
            LLMRequest {
                request_type: LLMRequestType::TidyTabs,
                context: context.clone(),
            },
        );
        CoreUpdate::RequestLlmCompletion {
            request_id,
            request_type: LLMRequestType::TidyTabs,
            prompt,
            context,
        }
    }

    pub fn request_chat_completion(
        &mut self,
        message: &str,
        include_page_context: bool,
        page_context: Option<&str>,
        tools_schema: Option<serde_json::Value>,
    ) -> CoreUpdate {
        let request_id = self.generate_request_id();

        self.chat_history
            .push_back(("user".to_string(), message.to_string()));
        while self.chat_history.len() > MAX_CHAT_HISTORY {
            self.chat_history.pop_front();
        }

        let mut messages = Vec::new();
        messages.push(serde_json::json!({
            "role": "system",
            "content": "You are Maho AI, a helpful browser assistant. Be concise and helpful."
        }));

        if include_page_context {
            if let Some(ctx) = page_context.filter(|s| !s.is_empty()) {
                messages.push(serde_json::json!({
                    "role": "system",
                    "content": format!("Current page context:\n{}", ctx)
                }));
            }
        }

        for (role, content) in &self.chat_history {
            messages.push(serde_json::json!({
                "role": role,
                "content": content
            }));
        }

        let prompt = if let Some(tools) = tools_schema {
            serde_json::json!({
                "messages": messages,
                "tools": tools,
            }).to_string()
        } else {
            serde_json::to_string(&messages).unwrap_or_default()
        };
        let context = if include_page_context {
            serde_json::json!({ "page_context": page_context.unwrap_or("") })
        } else {
            serde_json::json!({})
        };

        self.pending_requests.insert(
            request_id.clone(),
            LLMRequest {
                request_type: LLMRequestType::ChatCompletion,
                context: context.clone(),
            },
        );

        CoreUpdate::RequestLlmCompletion {
            request_id,
            request_type: LLMRequestType::ChatCompletion,
            prompt,
            context,
        }
    }

    pub fn take_pending_tidy_tabs(&mut self) -> Option<(SpaceId, Vec<TidyTabFolder>)> {
        self.pending_tidy_tabs.take()
    }

    pub fn handle_result(&mut self, request_id: &str, result: &str) -> Vec<CoreUpdate> {
        let Some(request) = self.pending_requests.remove(request_id) else {
            return vec![];
        };
        match request.request_type {
            LLMRequestType::TidyTabTitle => {
                let tab_id_str = request.context["tab_id"].as_str().unwrap_or_default();
                let tab_id = TabId::new(tab_id_str);
                vec![CoreUpdate::TabUpdated {
                    tab_id,
                    changes: maho_types::traits::shell_renderer::TabStateUpdate {
                        custom_title: Some(result.trim().to_string()),
                        ..Default::default()
                    },
                }]
            }
            LLMRequestType::TidyDownload => {
                let download_id_str = request.context["download_id"].as_str().unwrap_or_default();
                let new_name = result.trim().to_string();
                if new_name.is_empty() {
                    return vec![];
                }
                let download_id = DownloadId::new(download_id_str);
                vec![CoreUpdate::DownloadRenamed {
                    download_id,
                    old_name: String::new(),
                    new_name,
                }]
            }
            LLMRequestType::PagePreview => {
                let url = request.context["url"]
                    .as_str()
                    .unwrap_or_default()
                    .to_string();
                let (title, summary) = parse_page_preview_result(result);
                self.preview_cache
                    .insert(url.clone(), (title.clone(), summary.clone()));
                vec![CoreUpdate::PagePreviewReady {
                    url: Url(url),
                    title,
                    summary,
                }]
            }
            LLMRequestType::TidyTabs => {
                let space_id_str = request.context["space_id"].as_str().unwrap_or_default();
                let space_id = SpaceId::new(space_id_str);
                if let Some(folders) = parse_tidy_tabs_result(result) {
                    self.pending_tidy_tabs = Some((space_id.clone(), folders.clone()));
                    vec![CoreUpdate::TidyTabsReady { space_id, folders }]
                } else {
                    vec![]
                }
            }
            LLMRequestType::ChatCompletion => {
                let response = result.trim().to_string();
                self.chat_history
                    .push_back(("assistant".to_string(), response.clone()));
                while self.chat_history.len() > MAX_CHAT_HISTORY {
                    self.chat_history.pop_front();
                }
                vec![CoreUpdate::ChatCompletionReady {
                    request_id: request_id.to_string(),
                    response,
                }]
            }
        }
    }

    pub fn handle_error(&mut self, request_id: &str, error: &str) -> Vec<CoreUpdate> {
        let Some(request) = self.pending_requests.remove(request_id) else {
            return vec![];
        };
        match request.request_type {
            LLMRequestType::ChatCompletion => {
                let error_response = format!("[Error] {}", error);
                self.chat_history
                    .push_back(("assistant".to_string(), error_response.clone()));
                while self.chat_history.len() > MAX_CHAT_HISTORY {
                    self.chat_history.pop_front();
                }
                vec![CoreUpdate::ChatCompletionReady {
                    request_id: request_id.to_string(),
                    response: error_response,
                }]
            }
            _ => vec![],
        }
    }

    pub fn has_pending(&self) -> bool {
        !self.pending_requests.is_empty()
    }
}

fn parse_page_preview_result(result: &str) -> (String, String) {
    if let Ok(json) = serde_json::from_str::<serde_json::Value>(result.trim()) {
        let title = json["title"].as_str().unwrap_or("").to_string();
        let summary = json["summary"].as_str().unwrap_or("").to_string();
        if !title.is_empty() || !summary.is_empty() {
            return (title, summary);
        }
    }
    (String::new(), result.trim().to_string())
}

fn parse_tidy_tabs_result(result: &str) -> Option<Vec<TidyTabFolder>> {
    let json: serde_json::Value = serde_json::from_str(result.trim()).ok()?;
    let folders_arr = json["folders"].as_array()?;
    let mut folders = Vec::new();
    for f in folders_arr {
        let name = f["name"].as_str()?.to_string();
        let tab_ids: Vec<TabId> = f["tab_ids"]
            .as_array()?
            .iter()
            .filter_map(|v| v.as_str().map(TabId::new))
            .collect();
        if !tab_ids.is_empty() {
            folders.push(TidyTabFolder { name, tab_ids });
        }
    }
    if folders.is_empty() {
        None
    } else {
        Some(folders)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_request_tidy_title() {
        let mut manager = LLMManager::new();
        let tab_id = TabId::generate();
        let update = manager.request_tidy_title(
            &tab_id,
            "Settings - Account - GitHub",
            "https://github.com/settings",
        );
        match &update {
            CoreUpdate::RequestLlmCompletion {
                request_id,
                request_type,
                prompt,
                ..
            } => {
                assert!(request_id.starts_with("llm_"));
                assert!(matches!(request_type, LLMRequestType::TidyTabTitle));
                assert!(prompt.contains("Settings - Account - GitHub"));
            }
            _ => panic!("Expected RequestLlmCompletion"),
        }
        assert!(manager.has_pending());
    }

    #[test]
    fn test_handle_result() {
        let mut manager = LLMManager::new();
        let tab_id = TabId::generate();
        let update = manager.request_tidy_title(&tab_id, "Test", "https://test.com");
        if let CoreUpdate::RequestLlmCompletion { request_id, .. } = &update {
            let results = manager.handle_result(request_id, "Account");
            assert_eq!(results.len(), 1);
            assert!(!manager.has_pending());
        }
    }

    #[test]
    fn test_handle_error_graceful() {
        let mut manager = LLMManager::new();
        let tab_id = TabId::generate();
        let update = manager.request_tidy_title(&tab_id, "Test", "https://test.com");
        if let CoreUpdate::RequestLlmCompletion { request_id, .. } = &update {
            let results = manager.handle_error(request_id, "API key invalid");
            assert!(results.is_empty());
            assert!(!manager.has_pending());
        }
    }

    #[test]
    fn test_unknown_request_id() {
        let mut manager = LLMManager::new();
        let results = manager.handle_result("nonexistent", "data");
        assert!(results.is_empty());
    }

    #[test]
    fn test_request_chat_completion_message_format() {
        let mut manager = LLMManager::new();
        let update = manager.request_chat_completion("Hello", false, None, None);
        if let CoreUpdate::RequestLlmCompletion {
            request_id,
            request_type,
            prompt,
            ..
        } = &update
        {
            assert!(request_id.starts_with("llm_"));
            assert!(matches!(request_type, LLMRequestType::ChatCompletion));
            let messages: Vec<serde_json::Value> = serde_json::from_str(prompt).unwrap();
            assert_eq!(messages.len(), 2);
            assert_eq!(messages[0]["role"], "system");
            assert!(messages[0]["content"]
                .as_str()
                .unwrap()
                .contains("Maho AI"));
            assert_eq!(messages[1]["role"], "user");
            assert_eq!(messages[1]["content"], "Hello");
        } else {
            panic!("Expected RequestLlmCompletion");
        }
    }

    #[test]
    fn test_chat_history_round_trip() {
        let mut manager = LLMManager::new();
        let update = manager.request_chat_completion("Hi there", false, None, None);
        let request_id = match &update {
            CoreUpdate::RequestLlmCompletion { request_id, .. } => request_id.clone(),
            _ => panic!("Expected RequestLlmCompletion"),
        };
        let results = manager.handle_result(&request_id, "Hello! How can I help?");
        assert_eq!(results.len(), 1);
        assert!(matches!(
            &results[0],
            CoreUpdate::ChatCompletionReady { response, .. } if response == "Hello! How can I help?"
        ));
        assert_eq!(manager.chat_history.len(), 2);
        assert_eq!(manager.chat_history[0], ("user".to_string(), "Hi there".to_string()));
        assert_eq!(
            manager.chat_history[1],
            ("assistant".to_string(), "Hello! How can I help?".to_string())
        );
    }

    #[test]
    fn test_chat_history_cap() {
        let mut manager = LLMManager::new();
        for i in 0..12 {
            let update =
                manager.request_chat_completion(&format!("msg {}", i), false, None, None);
            let rid = match update {
                CoreUpdate::RequestLlmCompletion { request_id, .. } => request_id,
                _ => panic!(),
            };
            manager.handle_result(&rid, &format!("reply {}", i));
        }
        assert!(manager.chat_history.len() <= MAX_CHAT_HISTORY);
        assert_eq!(manager.chat_history.len(), 20);
    }

    #[test]
    fn test_chat_completion_error_handling() {
        let mut manager = LLMManager::new();
        let update = manager.request_chat_completion("test", false, None, None);
        let request_id = match &update {
            CoreUpdate::RequestLlmCompletion { request_id, .. } => request_id.clone(),
            _ => panic!("Expected RequestLlmCompletion"),
        };
        assert_eq!(manager.chat_history.len(), 1);
        let results = manager.handle_error(&request_id, "API rate limited");
        assert_eq!(results.len(), 1);
        match &results[0] {
            CoreUpdate::ChatCompletionReady { response, .. } => {
                assert!(response.starts_with("[Error] "));
                assert!(response.contains("API rate limited"));
            }
            _ => panic!("Expected ChatCompletionReady"),
        }
        assert_eq!(manager.chat_history.len(), 2);
        assert_eq!(manager.chat_history[0].0, "user");
        assert_eq!(manager.chat_history[1].0, "assistant");
        assert!(!manager.has_pending());
    }

    #[test]
    fn test_chat_completion_with_page_context() {
        let mut manager = LLMManager::new();
        let update = manager.request_chat_completion(
            "Summarize this page",
            true,
            Some("Title: Rust Docs\nURL: https://doc.rust-lang.org"),
            None,
        );
        if let CoreUpdate::RequestLlmCompletion { prompt, context, .. } = &update {
            let messages: Vec<serde_json::Value> = serde_json::from_str(prompt).unwrap();
            assert_eq!(messages.len(), 3);
            assert_eq!(messages[0]["role"], "system");
            assert!(messages[0]["content"].as_str().unwrap().contains("Maho AI"));
            assert_eq!(messages[1]["role"], "system");
            let ctx_content = messages[1]["content"].as_str().unwrap();
            assert!(ctx_content.contains("Current page context:"));
            assert!(ctx_content.contains("Rust Docs"));
            assert_eq!(messages[2]["role"], "user");
            assert_eq!(messages[2]["content"], "Summarize this page");
            assert!(context["page_context"].as_str().unwrap().contains("Rust Docs"));
        } else {
            panic!("Expected RequestLlmCompletion");
        }
    }
}
