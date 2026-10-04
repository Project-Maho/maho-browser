use std::collections::HashMap;
use std::collections::VecDeque;
use std::time::{Duration, Instant};

use maho_types::chat::{ChatRequestContext, ChatRequestMode};
use maho_types::common::Url;
use maho_types::events::core_update::{
    CoreUpdate, LLMRequestContext, LLMRequestType, TidyTabFolder,
};
use maho_types::identifiers::{DownloadId, SpaceId, TabId};

#[derive(Debug)]
struct LLMRequest {
    request_type: LLMRequestType,
    context: LLMRequestContext,
    session_id: Option<String>,
    created_at: Instant,
}

const MAX_CHAT_HISTORY: usize = 20;
const STALE_PENDING_REQUEST_TIMEOUT: Duration = Duration::from_secs(600);
const MAX_PREVIEW_CACHE_ENTRIES: usize = 128;

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
        self.sweep_stale_pending_requests();
        self.next_id += 1;
        format!("llm_{}", self.next_id)
    }

    pub fn sweep_stale_pending_requests(&mut self) {
        let now = Instant::now();
        self.pending_requests
            .retain(|_, req| now.duration_since(req.created_at) < STALE_PENDING_REQUEST_TIMEOUT);
    }

    pub fn trigger_completion(&mut self) {
        self.sweep_stale_pending_requests();
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
        let context = LLMRequestContext::TidyTabTitle {
            tab_id: tab_id.clone(),
        };
        self.pending_requests.insert(
            request_id.clone(),
            LLMRequest {
                request_type: LLMRequestType::TidyTabTitle,
                context: context.clone(),
                session_id: None,
                created_at: Instant::now(),
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
        let context = LLMRequestContext::TidyDownload {
            download_id: DownloadId::new(download_id),
        };
        self.pending_requests.insert(
            request_id.clone(),
            LLMRequest {
                request_type: LLMRequestType::TidyDownload,
                context: context.clone(),
                session_id: None,
                created_at: Instant::now(),
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
        let context = LLMRequestContext::PagePreview {
            url: Url(url.to_string()),
        };
        self.pending_requests.insert(
            request_id.clone(),
            LLMRequest {
                request_type: LLMRequestType::PagePreview,
                context: context.clone(),
                session_id: None,
                created_at: Instant::now(),
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
        let tabs_lines: Vec<String> = tabs
            .iter()
            .map(|(id, title, url)| format!("- id={} title={} url={}", id.as_ref(), title, url))
            .collect();
        let user_content = format!(
            "Group these {} browser tabs into 2-4 topical folders. \
Respond with JSON only, matching this schema: \
{{\"folders\":[{{\"name\":\"<folder name>\",\"tab_ids\":[\"<tab_id>\",...]}}]}}. \
Use the exact tab ids provided. Do not include prose.\n\nTabs:\n{}",
            tabs.len(),
            tabs_lines.join("\n"),
        );
        let messages = serde_json::json!([
            {
                "role": "system",
                "content": "You are Maho AI, a browser tab organizer. Return valid JSON only."
            },
            {
                "role": "user",
                "content": user_content
            }
        ]);
        let prompt = serde_json::to_string(&messages).unwrap_or_default();
        let context = LLMRequestContext::TidyTabs {
            space_id: space_id.clone(),
        };
        self.pending_requests.insert(
            request_id.clone(),
            LLMRequest {
                request_type: LLMRequestType::TidyTabs,
                context: context.clone(),
                session_id: None,
                created_at: Instant::now(),
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
        context: &ChatRequestContext,
    ) -> CoreUpdate {
        // Support-layer boundary: the browser now owns live chat orchestration and
        // page-context assembly. Rust keeps the typed request payload, history, and
        // compatibility path for LLM persistence/round-tripping.
        let request_id = self.generate_request_id();

        self.chat_history
            .push_back(("user".to_string(), message.to_string()));
        while self.chat_history.len() > MAX_CHAT_HISTORY {
            self.chat_history.pop_front();
        }

        let request_context = context.clone();
        let mut messages = Vec::new();
        let system_prompt = match request_context.request_mode {
            ChatRequestMode::PageQuestion => {
                "You are Maho AI, a helpful browser assistant. Analyze the current page deeply and be concise but specific."
            }
            ChatRequestMode::PageTransformation => {
                "You are Maho AI, a helpful browser assistant. Explain the current page clearly in plain language."
            }
            ChatRequestMode::ToolAssistedTask => {
                "You are Maho AI, a helpful browser assistant. Summarize the current page clearly and concisely."
            }
            _ => "You are Maho AI, a helpful browser assistant. Be concise and helpful.",
        };
        messages.push(serde_json::json!({
            "role": "system",
            "content": system_prompt
        }));

        if let Some(ctx) = request_context.page_context.as_ref() {
            messages.push(serde_json::json!({
                "role": "system",
                "content": format!(
                    "Current page context:\n{}",
                    serde_json::to_string_pretty(ctx).unwrap_or_default()
                )
            }));
        }

        for (role, content) in &self.chat_history {
            messages.push(serde_json::json!({
                "role": role,
                "content": content
            }));
        }

        let tools_schema = request_context.tools.as_ref().map(|tools| {
            serde_json::Value::Array(tools.iter().map(|tool| tool.to_openai_function()).collect())
        });
        let prompt = if let Some(tools) = tools_schema {
            serde_json::json!({
                "messages": messages,
                "tools": tools,
            })
            .to_string()
        } else {
            serde_json::to_string(&messages).unwrap_or_default()
        };
        let session_id = request_context.session_id.clone();
        let context = LLMRequestContext::Chat {
            chat: Box::new(request_context),
        };
        self.pending_requests.insert(
            request_id.clone(),
            LLMRequest {
                request_type: LLMRequestType::ChatCompletion,
                context: context.clone(),
                session_id,
                created_at: Instant::now(),
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
                let tab_id = match request.context {
                    LLMRequestContext::TidyTabTitle { tab_id } => tab_id,
                    _ => TabId::new(""),
                };
                vec![CoreUpdate::TabUpdated {
                    tab_id,
                    changes: maho_types::traits::shell_renderer::TabStateUpdate {
                        custom_title: Some(result.trim().to_string()),
                        ..Default::default()
                    },
                }]
            }
            LLMRequestType::TidyDownload => {
                let download_id = match request.context {
                    LLMRequestContext::TidyDownload { download_id } => download_id,
                    _ => DownloadId::new(""),
                };
                let new_name = result.trim().to_string();
                if new_name.is_empty() {
                    return vec![];
                }
                vec![CoreUpdate::DownloadRenamed {
                    download_id,
                    old_name: String::new(),
                    new_name,
                }]
            }
            LLMRequestType::PagePreview => {
                let url = match request.context {
                    LLMRequestContext::PagePreview { url } => url,
                    _ => Url(String::new()),
                };
                let (title, summary) = parse_page_preview_result(result);
                self.preview_cache
                    .insert(url.0.clone(), (title.clone(), summary.clone()));
                if self.preview_cache.len() > MAX_PREVIEW_CACHE_ENTRIES {
                    if let Some(key) = self.preview_cache.keys().next().cloned() {
                        self.preview_cache.remove(&key);
                    }
                }
                vec![CoreUpdate::PagePreviewReady {
                    url,
                    title,
                    summary,
                }]
            }
            LLMRequestType::TidyTabs => {
                let space_id = match request.context {
                    LLMRequestContext::TidyTabs { space_id } => space_id,
                    _ => SpaceId::new(""),
                };
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

    /// Returns the session_id associated with a pending LLM request (for memory persistence).
    pub fn lookup_session_for_request(&self, request_id: &str) -> Option<String> {
        self.pending_requests
            .get(request_id)
            .and_then(|r| r.session_id.clone())
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
    fn test_sweep_stale_pending_requests() {
        let mut manager = LLMManager::new();
        let tab_id = TabId::generate();
        let _ = manager.request_tidy_title(&tab_id, "Test", "https://test.com");
        assert!(manager.has_pending());

        for req in manager.pending_requests.values_mut() {
            req.created_at = Instant::now() - Duration::from_secs(601);
        }

        manager.sweep_stale_pending_requests();
        assert!(!manager.has_pending());
    }

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
        let update = manager.request_chat_completion("Hello", &ChatRequestContext::default());
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
            assert!(messages[0]["content"].as_str().unwrap().contains("Maho AI"));
            assert_eq!(messages[1]["role"], "user");
            assert_eq!(messages[1]["content"], "Hello");
        } else {
            panic!("Expected RequestLlmCompletion");
        }
    }

    #[test]
    fn test_chat_history_round_trip() {
        let mut manager = LLMManager::new();
        let update = manager.request_chat_completion("Hi there", &ChatRequestContext::default());
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
        assert_eq!(
            manager.chat_history[0],
            ("user".to_string(), "Hi there".to_string())
        );
        assert_eq!(
            manager.chat_history[1],
            (
                "assistant".to_string(),
                "Hello! How can I help?".to_string()
            )
        );
    }

    #[test]
    fn test_chat_history_cap() {
        let mut manager = LLMManager::new();
        for i in 0..12 {
            let update = manager
                .request_chat_completion(&format!("msg {}", i), &ChatRequestContext::default());
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
        let update = manager.request_chat_completion("test", &ChatRequestContext::default());
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
        let page_context = maho_types::chat::PageContext {
            title: "Rust Docs".into(),
            url: Url("https://doc.rust-lang.org".into()),
            selected_text: None,
            main_text: Some("Rust documentation".into()),
            headings: vec!["Overview".into()],
            meta_description: None,
            links: vec![],
            extraction_status: maho_types::chat::PageExtractionStatus::Complete,
            extraction_warnings: vec![],
        };
        let update = manager.request_chat_completion(
            "Summarize this page",
            &ChatRequestContext::new(ChatRequestMode::PageQuestion).with_page_context(page_context),
        );
        if let CoreUpdate::RequestLlmCompletion {
            prompt, context, ..
        } = &update
        {
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
            assert!(matches!(context, LLMRequestContext::Chat { .. }));
        } else {
            panic!("Expected RequestLlmCompletion");
        }
    }

    // -----------------------------------------------------------------------
    // Tab Tidy integration tests (T3.1)
    // -----------------------------------------------------------------------

    #[test]
    fn test_tidy_tabs_12_tabs_prompt_and_result() {
        let mut manager = LLMManager::new();
        let space_id = SpaceId::new("space-test");

        // 12 tabs: github, news, docs, social mix
        let tabs: Vec<(TabId, String, String)> = vec![
            (
                TabId::new("t1"),
                "rust-lang/rust PR #1234".into(),
                "https://github.com/rust-lang/rust/pull/1234".into(),
            ),
            (
                TabId::new("t2"),
                "tokio issues".into(),
                "https://github.com/tokio-rs/tokio/issues".into(),
            ),
            (
                TabId::new("t3"),
                "serde docs".into(),
                "https://docs.rs/serde/latest".into(),
            ),
            (
                TabId::new("t4"),
                "Hacker News".into(),
                "https://news.ycombinator.com".into(),
            ),
            (
                TabId::new("t5"),
                "TechCrunch AI".into(),
                "https://techcrunch.com/ai".into(),
            ),
            (
                TabId::new("t6"),
                "Rust Book".into(),
                "https://doc.rust-lang.org/book".into(),
            ),
            (
                TabId::new("t7"),
                "Twitter".into(),
                "https://twitter.com/home".into(),
            ),
            (
                TabId::new("t8"),
                "Reddit r/rust".into(),
                "https://reddit.com/r/rust".into(),
            ),
            (
                TabId::new("t9"),
                "MDN Web Docs".into(),
                "https://developer.mozilla.org".into(),
            ),
            (
                TabId::new("t10"),
                "AWS Console".into(),
                "https://console.aws.amazon.com".into(),
            ),
            (
                TabId::new("t11"),
                "Figma Project".into(),
                "https://figma.com/file/xyz".into(),
            ),
            (
                TabId::new("t12"),
                "YouTube".into(),
                "https://youtube.com".into(),
            ),
        ];

        let update = manager.request_tidy_tabs(&space_id, tabs);

        // Verify the prompt contains all 12 tab titles and URLs
        let (request_id, prompt) = match &update {
            CoreUpdate::RequestLlmCompletion {
                request_id,
                prompt,
                request_type,
                ..
            } => {
                assert!(matches!(request_type, LLMRequestType::TidyTabs));
                (request_id.clone(), prompt.clone())
            }
            _ => panic!("Expected RequestLlmCompletion"),
        };

        let prompt_json: serde_json::Value = serde_json::from_str(&prompt).unwrap();
        let messages = prompt_json
            .as_array()
            .expect("prompt must be a messages array");
        assert_eq!(messages.len(), 2, "prompt must have system + user message");
        assert_eq!(messages[0]["role"], "system");
        assert_eq!(messages[1]["role"], "user");
        let user_content = messages[1]["content"].as_str().unwrap();
        assert!(user_content.contains("rust-lang/rust PR #1234"));
        assert!(user_content.contains("https://youtube.com"));
        assert!(user_content.contains("Group these"));

        // Simulate LLM returning a folder grouping
        let mock_result = serde_json::json!({
            "folders": [
                {"name": "Development", "tab_ids": ["t1", "t2", "t3", "t6"]},
                {"name": "News & Social", "tab_ids": ["t4", "t5", "t7", "t8", "t12"]},
                {"name": "Work Tools", "tab_ids": ["t9", "t10", "t11"]}
            ]
        });

        let updates = manager.handle_result(&request_id, &mock_result.to_string());
        assert_eq!(updates.len(), 1);

        match &updates[0] {
            CoreUpdate::TidyTabsReady {
                space_id: sid,
                folders,
            } => {
                assert_eq!(sid, &space_id);
                assert_eq!(folders.len(), 3);
                let dev = folders.iter().find(|f| f.name == "Development").unwrap();
                assert_eq!(dev.tab_ids.len(), 4);
                assert!(dev.tab_ids.contains(&TabId::new("t1")));
                assert!(dev.tab_ids.contains(&TabId::new("t6")));
            }
            _ => panic!("Expected TidyTabsReady"),
        }

        // Verify pending_tidy_tabs is set
        let pending = manager.take_pending_tidy_tabs();
        assert!(pending.is_some());
        let (ps, pf) = pending.unwrap();
        assert_eq!(ps, space_id);
        assert_eq!(pf.len(), 3);
    }

    #[test]
    fn test_tidy_tabs_empty_list_returns_empty() {
        let mut manager = LLMManager::new();
        let space_id = SpaceId::new("space-empty");

        // Empty tab list
        let tabs: Vec<(TabId, String, String)> = vec![];
        let update = manager.request_tidy_tabs(&space_id, tabs);

        let (request_id, prompt) = match &update {
            CoreUpdate::RequestLlmCompletion {
                request_id, prompt, ..
            } => (request_id.clone(), prompt.clone()),
            _ => panic!("Expected RequestLlmCompletion"),
        };

        let prompt_json: serde_json::Value = serde_json::from_str(&prompt).unwrap();
        let messages = prompt_json
            .as_array()
            .expect("prompt must be a messages array");
        assert_eq!(messages.len(), 2);
        let user_content = messages[1]["content"].as_str().unwrap();
        assert!(user_content.contains("Group these 0"));

        // Simulate LLM returning empty folders (realistic for empty input)
        let empty_result = r#"{"folders": []}"#;
        let updates = manager.handle_result(&request_id, empty_result);
        // parse_tidy_tabs_result returns None for empty folders → no updates
        assert!(
            updates.is_empty(),
            "Empty folder list should produce no updates"
        );
        assert!(manager.take_pending_tidy_tabs().is_none());
    }

    #[test]
    fn test_tidy_tabs_malformed_json_no_panic() {
        let mut manager = LLMManager::new();
        let space_id = SpaceId::new("space-bad");
        let tabs = vec![(
            TabId::new("t1"),
            "Tab One".into(),
            "https://example.com".into(),
        )];

        let update = manager.request_tidy_tabs(&space_id, tabs);
        let request_id = match &update {
            CoreUpdate::RequestLlmCompletion { request_id, .. } => request_id.clone(),
            _ => panic!("Expected RequestLlmCompletion"),
        };

        // Test various malformed responses
        let malformed_cases = [
            "not json at all",
            "{\"folders\": \"not an array\"}",
            "{\"wrong_key\": []}",
            "",
            "null",
            "{\"folders\": [{\"name\": 123, \"tab_ids\": []}]}", // name not string
        ];

        for (i, bad_json) in malformed_cases.iter().enumerate() {
            if i > 0 {
                // Re-issue a request for subsequent iterations since the first one was consumed
                let tabs2 = vec![(
                    TabId::new("t1"),
                    "Tab One".into(),
                    "https://example.com".into(),
                )];
                let u = manager.request_tidy_tabs(&space_id, tabs2);
                let rid = match &u {
                    CoreUpdate::RequestLlmCompletion { request_id, .. } => request_id.clone(),
                    _ => panic!("Expected RequestLlmCompletion"),
                };
                let updates = manager.handle_result(&rid, bad_json);
                assert!(
                    updates.is_empty(),
                    "Malformed JSON case {i} ({bad_json:?}) should return empty updates"
                );
            } else {
                let updates = manager.handle_result(&request_id, bad_json);
                assert!(
                    updates.is_empty(),
                    "Malformed JSON case {i} ({bad_json:?}) should return empty updates"
                );
            }
            assert!(manager.take_pending_tidy_tabs().is_none());
        }
    }
}
