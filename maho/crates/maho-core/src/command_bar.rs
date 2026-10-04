use std::cell::RefCell;
use std::collections::{HashMap, VecDeque};

use base64::Engine;
use maho_types::common::{ImageData, ImageFormat};
use maho_types::search_engine::{SearchEngine, SearchEngineViewModel};
use maho_types::traits::shell_renderer::{SearchContext, SuggestionType, SuggestionViewModel};

const MAX_HISTORY_ENTRIES: usize = 10000;
const MAX_RESULTS: usize = 12;
const MAX_USAGE_KEYS: usize = 500;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct GlobalMailCommand {
    pub id: &'static str,
    pub label: &'static str,
    pub aliases: &'static [&'static str],
}

pub const GLOBAL_MAIL_COMMANDS: &[GlobalMailCommand] = &[
    GlobalMailCommand {
        id: "open_mail",
        label: "Open Mail",
        aliases: &["mail", "email", "mail app"],
    },
    GlobalMailCommand {
        id: "compose_mail",
        label: "Compose Mail",
        aliases: &["compose", "write email", "new message"],
    },
    GlobalMailCommand {
        id: "search_mail",
        label: "Search Mail",
        aliases: &["search email", "find message"],
    },
    GlobalMailCommand {
        id: "mail_inbox",
        label: "Mail Inbox",
        aliases: &["inbox", "inbox messages"],
    },
    GlobalMailCommand {
        id: "mail_sent",
        label: "Mail Sent",
        aliases: &["sent mail", "sent messages"],
    },
    GlobalMailCommand {
        id: "mail_drafts",
        label: "Mail Drafts",
        aliases: &["drafts", "draft messages"],
    },
    GlobalMailCommand {
        id: "mail_starred",
        label: "Mail Starred",
        aliases: &["starred", "starred messages"],
    },
];

fn now_millis() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as u64
}

struct HistoryEntry {
    url: String,
    title: String,
    icon: Option<ImageData>,
    visited_at: u64,
    /// `"{title} {url}"` lowercased, computed once at insert.
    ///
    /// `search` fuzzy-matches every candidate against this on each keystroke;
    /// building it per candidate per keystroke meant two allocations per
    /// candidate, and the sub-3-character path scans the whole history.
    lower_target: String,
}

struct Action {
    id: String,
    label: String,
    category: String,
    aliases: Vec<String>,
}

struct SearchResult {
    suggestion: SuggestionViewModel,
    score: f64,
}

struct FuzzyMatchResult {
    is_match: bool,
    score: f64,
    matched_indices: Vec<usize>,
}

/// Postings are keyed by a monotonic history sequence id, never by position in
/// the history buffer.
///
/// Positions shift on every eviction, which is why the old position-keyed index
/// had to be rebuilt wholesale each time the history hit its cap — an O(N x L)
/// walk on the UI thread for every single navigation. Sequence ids are stable,
/// so an eviction only has to unlink the departing entry's own trigrams.
///
/// Because ids are handed out in increasing order and only the oldest entry is
/// ever evicted, each posting list is sorted ascending and the id being removed
/// is always at its front — hence `VecDeque` and O(1) `pop_front`.
struct TrigramIndex {
    index: HashMap<[u8; 3], VecDeque<u64>>, // trigram -> history sequence ids
    cache: HashMap<String, Vec<u64>>,       // query -> cached sequence ids
    cache_order: Vec<String>,               // LRU order tracking
    max_cache_size: usize,
}

impl TrigramIndex {
    fn new() -> Self {
        Self {
            index: HashMap::new(),
            cache: HashMap::new(),
            cache_order: Vec::new(),
            max_cache_size: 50,
        }
    }

    /// Insert `lower_text`'s trigrams under `seq`.
    ///
    /// `lower_text` must already be lowercased — callers hold a precomputed
    /// lowercase target, so lowercasing again here would be a wasted allocation.
    ///
    /// Does NOT invalidate the query cache; the caller must, once, after its
    /// batch of inserts.
    fn add_entry(&mut self, seq: u64, lower_text: &str) {
        let bytes = lower_text.as_bytes();
        if bytes.len() < 3 {
            return;
        }
        for i in 0..bytes.len() - 2 {
            let trigram = [bytes[i], bytes[i + 1], bytes[i + 2]];
            self.index.entry(trigram).or_default().push_back(seq);
        }
    }

    /// Unlink an evicted entry's trigrams. `lower_text` must be the exact string
    /// that was passed to `add_entry` for `seq`, so the same trigrams are visited.
    ///
    /// `seq` is the oldest live id, so it sits at the front of every posting list
    /// containing it. A trigram repeated within one entry was pushed more than
    /// once, so drain the whole run of leading matches. Emptied posting lists are
    /// dropped to keep the map from growing without bound.
    fn remove_entry(&mut self, seq: u64, lower_text: &str) {
        let bytes = lower_text.as_bytes();
        if bytes.len() < 3 {
            return;
        }
        for i in 0..bytes.len() - 2 {
            let trigram = [bytes[i], bytes[i + 1], bytes[i + 2]];
            if let Some(postings) = self.index.get_mut(&trigram) {
                while postings.front() == Some(&seq) {
                    postings.pop_front();
                }
                if postings.is_empty() {
                    self.index.remove(&trigram);
                }
            }
        }
    }

    fn query_candidates(&mut self, query: &str) -> Option<Vec<u64>> {
        if let Some(cached) = self.cache.get(query) {
            return Some(cached.clone());
        }
        let lower = query.to_lowercase();
        let bytes = lower.as_bytes();
        if bytes.len() < 3 {
            return None; // too short for trigram, fall back to full scan
        }

        let mut candidate_counts: HashMap<u64, usize> = HashMap::new();
        let num_trigrams = bytes.len() - 2;
        for i in 0..num_trigrams {
            let trigram = [bytes[i], bytes[i + 1], bytes[i + 2]];
            if let Some(postings) = self.index.get(&trigram) {
                for &seq in postings {
                    *candidate_counts.entry(seq).or_insert(0) += 1;
                }
            }
        }
        // Keep candidates matching at least 50% of trigrams
        let threshold = num_trigrams.div_ceil(2);
        let candidates: Vec<u64> = candidate_counts
            .into_iter()
            .filter(|&(_, count)| count >= threshold)
            .map(|(seq, _)| seq)
            .collect();

        self.insert_cache(query.to_string(), candidates.clone());
        Some(candidates)
    }

    fn invalidate_cache(&mut self) {
        self.cache.clear();
        self.cache_order.clear();
    }

    fn clear(&mut self) {
        self.index.clear();
        self.cache.clear();
        self.cache_order.clear();
    }

    fn insert_cache(&mut self, key: String, value: Vec<u64>) {
        if self.cache_order.len() >= self.max_cache_size {
            if let Some(oldest) = self.cache_order.first().cloned() {
                self.cache.remove(&oldest);
                self.cache_order.remove(0);
            }
        }
        self.cache.insert(key.clone(), value);
        self.cache_order.push(key);
    }
}

pub struct CommandBarEngine {
    history: VecDeque<HistoryEntry>,
    /// Sequence id of `history.front()`; the entry at position `i` has id
    /// `history_base_seq + i`. Lets a trigram posting resolve to an entry in
    /// O(1) without the index having to track shifting positions.
    history_base_seq: u64,
    actions: Vec<Action>,
    recent_searches: Vec<String>,
    search_engines: Vec<SearchEngine>,
    usage_frequency: HashMap<String, u32>,
    trigram_index: RefCell<TrigramIndex>,
}

impl Default for CommandBarEngine {
    fn default() -> Self {
        Self::new()
    }
}

impl CommandBarEngine {
    pub fn add_global_mail_commands(&mut self) {
        for command in GLOBAL_MAIL_COMMANDS {
            self.add_action_with_aliases(
                command.id.into(),
                command.label.into(),
                "Mail".into(),
                command
                    .aliases
                    .iter()
                    .map(|alias| (*alias).into())
                    .collect(),
            );
        }
    }

    fn icon_for_url(ctx: &SearchContext, url: &str) -> Option<ImageData> {
        ctx.tabs
            .iter()
            .find(|tab| tab.url == url)
            .and_then(|tab| tab.favicon.clone())
            .or_else(|| {
                ctx.archived_tabs
                    .iter()
                    .find(|tab| tab.url == url)
                    .and_then(|tab| tab.favicon.clone())
            })
            .or_else(|| {
                ctx.closed_tabs
                    .iter()
                    .find(|tab| tab.url == url)
                    .and_then(|tab| tab.favicon.clone())
            })
    }

    pub fn bookmark_icon_from_data_url(favicon: &str) -> Option<ImageData> {
        let (prefix, encoded) = favicon.split_once(',')?;
        let format = if prefix.contains("image/png") {
            ImageFormat::Png
        } else if prefix.contains("image/jpeg") || prefix.contains("image/jpg") {
            ImageFormat::Jpeg
        } else if prefix.contains("image/webp") {
            ImageFormat::Webp
        } else {
            return None;
        };

        let engine = base64::engine::general_purpose::STANDARD;
        let data = engine.decode(encoded.trim()).ok()?;
        Some(ImageData {
            data,
            width: 0,
            height: 0,
            format,
        })
    }

    pub fn new() -> Self {
        let mut engine = Self {
            history: VecDeque::new(),
            history_base_seq: 0,
            actions: Vec::new(),
            recent_searches: Vec::new(),
            usage_frequency: HashMap::new(),
            trigram_index: RefCell::new(TrigramIndex::new()),
            search_engines: vec![
                SearchEngine {
                    id: "google".to_string(),
                    name: "Google".to_string(),
                    url_template: "https://www.google.com/search?q={query}".to_string(),
                    shortcut: Some("@g".to_string()),
                    icon_url: None,
                    is_default: true,
                },
                SearchEngine {
                    id: "duckduckgo".to_string(),
                    name: "DuckDuckGo".to_string(),
                    url_template: "https://duckduckgo.com/?q={query}".to_string(),
                    shortcut: Some("@d".to_string()),
                    icon_url: None,
                    is_default: false,
                },
                SearchEngine {
                    id: "bing".to_string(),
                    name: "Bing".to_string(),
                    url_template: "https://www.bing.com/search?q={query}".to_string(),
                    shortcut: Some("@b".to_string()),
                    icon_url: None,
                    is_default: false,
                },
                SearchEngine {
                    id: "brave".to_string(),
                    name: "Brave".to_string(),
                    url_template: "https://search.brave.com/search?q={query}".to_string(),
                    shortcut: Some("@br".to_string()),
                    icon_url: None,
                    is_default: false,
                },
                SearchEngine {
                    id: "ecosia".to_string(),
                    name: "Ecosia".to_string(),
                    url_template: "https://www.ecosia.org/search?q={query}".to_string(),
                    shortcut: Some("@e".to_string()),
                    icon_url: None,
                    is_default: false,
                },
            ],
        };
        engine.register_default_actions();
        engine
    }

    fn register_default_actions(&mut self) {
        self.add_action_with_aliases(
            "toggle_split_orientation".into(),
            "Toggle Split Orientation".into(),
            "Split View".into(),
            vec!["split orientation".into(), "rotate split".into()],
        );
        self.add_action_with_aliases(
            "split_side_by_side".into(),
            "Split Tab: Side-by-Side".into(),
            "Split View".into(),
            vec!["side by side split".into(), "vertical split".into()],
        );
        self.add_action_with_aliases(
            "split_top_bottom".into(),
            "Split Tab: Top/Bottom".into(),
            "Split View".into(),
            vec!["top bottom split".into(), "horizontal split".into()],
        );
    }

    pub fn add_action(&mut self, id: String, label: String, category: String) {
        self.add_action_with_aliases(id, label, category, Vec::new());
    }

    pub fn add_action_with_aliases(
        &mut self,
        id: String,
        label: String,
        category: String,
        aliases: Vec<String>,
    ) {
        self.actions.push(Action {
            id,
            label,
            category,
            aliases,
        });
    }

    pub fn remove_action(&mut self, id: &str) {
        if let Some(index) = self.actions.iter().position(|a| a.id == id) {
            self.actions.remove(index);
        }
    }

    pub fn add_history_entry(&mut self, url: String, title: String, icon: Option<ImageData>) {
        self.push_history_entry(url, title, icon, now_millis());
    }

    /// Rehydrate a persisted visit, keeping its original visit time so
    /// recency ranking survives a restart. Callers restore oldest first.
    pub fn restore_history_entry(&mut self, url: String, title: String, visited_at: u64) {
        self.push_history_entry(url, title, None, visited_at);
    }

    fn push_history_entry(
        &mut self,
        url: String,
        title: String,
        icon: Option<ImageData>,
        visited_at: u64,
    ) {
        let lower_target = format!("{} {}", title, url).to_lowercase();
        let seq = self.history_base_seq + self.history.len() as u64;

        let mut index = self.trigram_index.borrow_mut();
        // Index before the push so `lower_target` can then be moved into the
        // entry instead of cloned.
        index.add_entry(seq, &lower_target);
        self.history.push_back(HistoryEntry {
            url,
            title,
            icon,
            visited_at,
            lower_target,
        });

        // Evict the oldest entry, unlinking only its own trigrams. This used to
        // rebuild the entire index, which at the 10k cap meant an O(N x L) walk
        // on the UI thread for every navigation.
        if self.history.len() > MAX_HISTORY_ENTRIES {
            if let Some(evicted) = self.history.pop_front() {
                index.remove_entry(self.history_base_seq, &evicted.lower_target);
                self.history_base_seq += 1;
            }
        }

        // Cached candidate lists predate this insert (and any eviction).
        index.invalidate_cache();
    }

    /// Resolve a trigram posting to its entry. Returns `None` for ids that have
    /// already been evicted.
    fn history_by_seq(&self, seq: u64) -> Option<&HistoryEntry> {
        let offset = seq.checked_sub(self.history_base_seq)?;
        self.history.get(usize::try_from(offset).ok()?)
    }

    pub fn clear_history(&mut self) {
        self.history.clear();
        // Postings keyed by now-dead ids would otherwise resolve against the
        // refilled buffer. Ids restart from 0 alongside the empty buffer.
        self.history_base_seq = 0;
        self.trigram_index.borrow_mut().clear();
    }

    pub fn search(
        &self,
        query: &str,
        mode: Option<&str>,
        ctx: &SearchContext,
    ) -> Vec<SuggestionViewModel> {
        if ctx.is_incognito {
            return self.search_incognito(query, mode, ctx);
        }
        // ">" prefix → action-only mode
        if let Some(action_query) = query.strip_prefix('>') {
            return self.search_actions_only(action_query.trim(), false);
        }
        // Empty query → surface recently-opened (suspended) tabs as switch-to-tab
        // suggestions, ordered by recency. Does not fuzzy-match an empty needle.
        if query.trim().is_empty() {
            let recent = Self::recent_tab_suggestions(ctx);
            if !recent.is_empty() {
                return recent;
            }
        }
        let mut results: Vec<SearchResult> = Vec::new();
        let lower_query = query.to_lowercase();

        // Calculator detection
        if let Some(calc_result) = evaluate_expression(query) {
            let formatted = if calc_result.fract() == 0.0 && calc_result.abs() < 1e15 {
                format!("{}", calc_result as i64)
            } else {
                format!("{:.6}", calc_result)
                    .trim_end_matches('0')
                    .trim_end_matches('.')
                    .to_string()
            };
            results.push(SearchResult {
                suggestion: SuggestionViewModel {
                    kind: SuggestionType::Calculator,
                    key: format!("calc:{}", query),
                    title: format!("{} = {}", query, formatted),
                    subtitle: Some("Calculator".to_string()),
                    execution_payload: None,
                    icon: None,
                    relevance_score: 3.0,
                    match_ranges: None,
                    shortcut: None,
                    tab_core_id: None,
                    is_suspended: false,
                },
                score: 3.0,
            });
        }

        // Unit conversion detection
        if let Some((result_str, description)) = try_unit_conversion(query) {
            results.push(SearchResult {
                suggestion: SuggestionViewModel {
                    kind: SuggestionType::UnitConversion,
                    key: format!("unit:{}", query),
                    title: result_str,
                    subtitle: Some(description),
                    execution_payload: None,
                    icon: None,
                    relevance_score: 2.8,
                    match_ranges: None,
                    shortcut: None,
                    tab_core_id: None,
                    is_suspended: false,
                },
                score: 2.8,
            });
        }

        // If query is a URL, prepend a navigation suggestion
        if Self::is_url(query) {
            let url = if query.starts_with("http://") || query.starts_with("https://") {
                query.to_string()
            } else {
                format!("https://{}", query)
            };
            results.push(SearchResult {
                suggestion: SuggestionViewModel {
                    kind: SuggestionType::Navigation,
                    key: format!("nav:{}", url),
                    title: format!("Go to {}", url),
                    subtitle: Some("Navigate".to_string()),
                    execution_payload: Some(url.clone()),
                    icon: None,
                    relevance_score: 2.0,
                    match_ranges: None,
                    shortcut: None,
                    tab_core_id: None,
                    is_suspended: false,
                },
                score: 2.0,
            });
        }

        // Always offer a "search the web for X" row when query is non-empty.
        // Score 1.5 < Navigation (2.0) so URL input shows Navigation first.
        // Above History/Bookmark fuzzy matches when query is non-URL.
        if !query.trim().is_empty() {
            let search_url = self.get_search_url(query);
            let engine_name = self
                .get_default_search_engine()
                .map(|e| e.name.clone())
                .unwrap_or_else(|| "Google".to_string());
            results.push(SearchResult {
                suggestion: SuggestionViewModel {
                    kind: SuggestionType::Search,
                    key: format!("search:{}", query),
                    title: format!("{} — Search with {}", query, engine_name),
                    subtitle: None,
                    execution_payload: Some(search_url),
                    icon: None,
                    relevance_score: 1.5,
                    match_ranges: None,
                    shortcut: None,
                    tab_core_id: None,
                    is_suspended: false,
                },
                score: 1.5,
            });

            if !ctx.is_incognito {
                let intent = crate::ai_search_intent::classify_query(query);
                if intent == crate::ai_search_intent::QueryIntent::AiPreferred
                    || intent == crate::ai_search_intent::QueryIntent::Ambiguous
                {
                    let score = if intent == crate::ai_search_intent::QueryIntent::AiPreferred {
                        1.8
                    } else {
                        1.6
                    };
                    results.push(SearchResult {
                        suggestion: SuggestionViewModel {
                            kind: SuggestionType::AiAnswer,
                            key: format!("ai_search:{}", query),
                            title: format!("Ask Maho AI \"{}\"", query),
                            subtitle: None,
                            execution_payload: None,
                            icon: None,
                            relevance_score: score,
                            match_ranges: None,
                            shortcut: None,
                            tab_core_id: None,
                            is_suspended: false,
                        },
                        score,
                    });
                }
            }
        }

        // Search tabs
        for tab in &ctx.tabs {
            if tab.title.trim().is_empty() && tab.url.trim().is_empty() {
                continue;
            }
            let title = if !tab.title.trim().is_empty() {
                tab.title.clone()
            } else {
                tab.url.clone()
            };
            let target = format!("{} {}", title, tab.url).to_lowercase();
            let match_result = Self::fuzzy_match(&lower_query, &target);
            if match_result.is_match {
                results.push(SearchResult {
                    suggestion: SuggestionViewModel {
                        kind: SuggestionType::Tab,
                        key: format!("tab:{}", tab.id),
                        title,
                        subtitle: Some(tab.url.clone()),
                        execution_payload: Some(tab.url.clone()),
                        icon: tab.favicon.clone(),
                        relevance_score: match_result.score,
                        match_ranges: Some(indices_to_ranges(
                            &match_result.matched_indices,
                            tab.title.len(),
                        )),
                        shortcut: None,
                        tab_core_id: None,
                        is_suspended: false,
                    },
                    score: match_result.score,
                });
            }
        }

        // Search spaces
        for space in &ctx.spaces {
            let target = space.name.to_lowercase();
            let match_result = Self::fuzzy_match(&lower_query, &target);
            if match_result.is_match {
                results.push(SearchResult {
                    suggestion: SuggestionViewModel {
                        kind: SuggestionType::Action,
                        key: format!("action:open_space:{}", space.id),
                        title: space.name.clone(),
                        subtitle: Some(format!("Space with {} tabs", space.tab_count)),
                        execution_payload: None,
                        icon: None,
                        relevance_score: match_result.score,
                        match_ranges: Some(indices_to_ranges(
                            &match_result.matched_indices,
                            space.name.len(),
                        )),
                        shortcut: None,
                        tab_core_id: None,
                        is_suspended: false,
                    },
                    score: match_result.score,
                });
            }
        }

        // Search folders
        for folder in &ctx.folders {
            let target = folder.name.to_lowercase();
            let match_result = Self::fuzzy_match(&lower_query, &target);
            if match_result.is_match {
                results.push(SearchResult {
                    suggestion: SuggestionViewModel {
                        kind: SuggestionType::Folder,
                        key: format!("folder:{}", folder.id),
                        title: folder.name.clone(),
                        subtitle: Some(format!("{} tabs", folder.tab_count)),
                        execution_payload: Some(folder.id.to_string()),
                        icon: None,
                        relevance_score: match_result.score,
                        match_ranges: Some(indices_to_ranges(
                            &match_result.matched_indices,
                            folder.name.len(),
                        )),
                        shortcut: None,
                        tab_core_id: None,
                        is_suspended: false,
                    },
                    score: match_result.score,
                });
            }
        }

        // Search archived tabs
        for tab in &ctx.archived_tabs {
            if tab.title.trim().is_empty() && tab.url.trim().is_empty() {
                continue;
            }
            let title = if !tab.title.trim().is_empty() {
                tab.title.clone()
            } else {
                tab.url.clone()
            };
            let target = format!("{} {}", title, tab.url).to_lowercase();
            let match_result = Self::fuzzy_match(&lower_query, &target);
            if match_result.is_match {
                results.push(SearchResult {
                    suggestion: SuggestionViewModel {
                        kind: SuggestionType::ArchivedTab,
                        key: format!("archived:{}", tab.id),
                        title,
                        subtitle: Some(tab.url.clone()),
                        execution_payload: Some(tab.url.clone()),
                        icon: tab.favicon.clone(),
                        relevance_score: match_result.score - 0.2,
                        match_ranges: Some(indices_to_ranges(
                            &match_result.matched_indices,
                            tab.title.len(),
                        )),
                        shortcut: None,
                        tab_core_id: None,
                        is_suspended: false,
                    },
                    score: match_result.score - 0.2,
                });
            }
        }

        if !ctx.is_incognito {
            // Search bookmarks
            for (id, title, url) in &ctx.bookmarks {
                let target = format!("{} {}", title, url).to_lowercase();
                let match_result = Self::fuzzy_match(&lower_query, &target);
                if match_result.is_match {
                    let icon = ctx
                        .bookmark_favicons
                        .iter()
                        .find(|(bookmark_id, _)| bookmark_id == id)
                        .map(|(_, icon)| icon.clone());
                    results.push(SearchResult {
                        suggestion: SuggestionViewModel {
                            kind: SuggestionType::Bookmark,
                            key: format!("bookmark:{}", id),
                            title: title.clone(),
                            subtitle: Some(url.clone()),
                            execution_payload: Some(url.clone()),
                            icon,
                            relevance_score: match_result.score,
                            match_ranges: Some(indices_to_ranges(
                                &match_result.matched_indices,
                                title.len(),
                            )),
                            shortcut: None,
                            tab_core_id: None,
                            is_suspended: false,
                        },
                        score: match_result.score,
                    });
                }
            }
        }

        if !ctx.is_incognito {
            // Search recently closed tabs
            let mut closed_count = 0;
            for tab in &ctx.closed_tabs {
                if closed_count >= 3 {
                    break;
                }
                if tab.title.trim().is_empty() && tab.url.trim().is_empty() {
                    continue;
                }
                let title = if !tab.title.trim().is_empty() {
                    tab.title.clone()
                } else {
                    tab.url.clone()
                };
                let target = format!("{} {}", title, tab.url).to_lowercase();
                let match_result = Self::fuzzy_match(&lower_query, &target);
                if match_result.is_match {
                    results.push(SearchResult {
                        suggestion: SuggestionViewModel {
                            kind: SuggestionType::ClosedTab,
                            key: format!("closed:{}", tab.url),
                            title,
                            subtitle: Some(tab.url.clone()),
                            execution_payload: Some(tab.url.clone()),
                            icon: tab.favicon.clone(),
                            relevance_score: match_result.score - 0.1,
                            match_ranges: Some(indices_to_ranges(
                                &match_result.matched_indices,
                                tab.title.len(),
                            )),
                            shortcut: None,
                            tab_core_id: None,
                            is_suspended: false,
                        },
                        score: match_result.score - 0.1,
                    });
                    closed_count += 1;
                }
            }
        }

        // Search extensions
        for (id, name) in &ctx.extensions {
            let target = name.to_lowercase();
            let match_result = Self::fuzzy_match(&lower_query, &target);
            if match_result.is_match {
                results.push(SearchResult {
                    suggestion: SuggestionViewModel {
                        kind: SuggestionType::Action,
                        key: format!("extension:{}", id),
                        title: name.clone(),
                        subtitle: Some("Extension".to_string()),
                        execution_payload: None,
                        icon: None,
                        relevance_score: match_result.score - 0.3,
                        match_ranges: Some(indices_to_ranges(
                            &match_result.matched_indices,
                            name.len(),
                        )),
                        shortcut: None,
                        tab_core_id: None,
                        is_suspended: false,
                    },
                    score: match_result.score - 0.3,
                });
            }
        }

        // Search actions
        if !lower_query.is_empty() {
            let has_exact_alias = self.actions.iter().any(|action| {
                action
                    .aliases
                    .iter()
                    .any(|alias| alias.eq_ignore_ascii_case(query.trim()))
            });
            for action in &self.actions {
                let exact_alias = action
                    .aliases
                    .iter()
                    .any(|alias| alias.eq_ignore_ascii_case(query.trim()));
                if has_exact_alias && !exact_alias {
                    continue;
                }
                let target = format!(
                    "{} {} {}",
                    action.label,
                    action.category,
                    action.aliases.join(" ")
                )
                .to_lowercase();
                let match_result = Self::fuzzy_match(&lower_query, &target);
                if match_result.is_match && match_result.score > 0.3 {
                    results.push(SearchResult {
                        suggestion: SuggestionViewModel {
                            kind: SuggestionType::Action,
                            key: format!("action:{}", action.id),
                            title: action.label.clone(),
                            subtitle: Some(action.category.clone()),
                            execution_payload: None,
                            icon: None,
                            relevance_score: match_result.score,
                            match_ranges: Some(indices_to_ranges(
                                &match_result.matched_indices,
                                action.label.len(),
                            )),
                            shortcut: None,
                            tab_core_id: None,
                            is_suspended: false,
                        },
                        score: match_result.score,
                    });
                }
            }
        }

        // Search history with trigram index optimization
        if !ctx.is_incognito {
            let candidates = self.trigram_index.borrow_mut().query_candidates(query);
            if let Some(seqs) = candidates {
                for seq in seqs {
                    if let Some(entry) = self.history_by_seq(seq) {
                        let match_result = Self::fuzzy_match(&lower_query, &entry.lower_target);
                        if match_result.is_match {
                            results.push(SearchResult {
                                suggestion: SuggestionViewModel {
                                    kind: SuggestionType::History,
                                    key: format!("history:{}", entry.url),
                                    title: entry.title.clone(),
                                    subtitle: Some(entry.url.clone()),
                                    execution_payload: Some(entry.url.clone()),
                                    icon: entry
                                        .icon
                                        .clone()
                                        .or_else(|| Self::icon_for_url(ctx, &entry.url)),
                                    relevance_score: match_result.score,
                                    match_ranges: Some(indices_to_ranges(
                                        &match_result.matched_indices,
                                        entry.title.len(),
                                    )),
                                    shortcut: None,
                                    tab_core_id: None,
                                    is_suspended: false,
                                },
                                score: match_result.score + Self::recency_boost(entry.visited_at),
                            });
                        }
                    }
                }
            } else {
                // Fall back to full scan for queries shorter than 3 characters
                for entry in &self.history {
                    let match_result = Self::fuzzy_match(&lower_query, &entry.lower_target);
                    if match_result.is_match {
                        results.push(SearchResult {
                            suggestion: SuggestionViewModel {
                                kind: SuggestionType::History,
                                key: format!("history:{}", entry.url),
                                title: entry.title.clone(),
                                subtitle: Some(entry.url.clone()),
                                execution_payload: Some(entry.url.clone()),
                                icon: entry
                                    .icon
                                    .clone()
                                    .or_else(|| Self::icon_for_url(ctx, &entry.url)),
                                relevance_score: match_result.score,
                                match_ranges: Some(indices_to_ranges(
                                    &match_result.matched_indices,
                                    entry.title.len(),
                                )),
                                shortcut: None,
                                tab_core_id: None,
                                is_suspended: false,
                            },
                            score: match_result.score + Self::recency_boost(entry.visited_at),
                        });
                    }
                }
            }
        }

        // Pass 1: key dedup (same type, same URL)
        {
            let mut seen = std::collections::HashSet::new();
            results.retain(|r| seen.insert(r.suggestion.key.clone()));
        }

        // Apply frequency boost before sorting
        for result in &mut results {
            let key = &result.suggestion.key;
            let boost = self.frequency_boost(key);
            result.score += boost;
            result.suggestion.relevance_score += boost;
        }

        // Apply mode-specific score boosts
        if let Some(mode) = mode {
            for result in &mut results {
                let kind_boost = match mode {
                    "normal" => match result.suggestion.kind {
                        SuggestionType::Action => 0.5,
                        SuggestionType::Tab => 0.3,
                        SuggestionType::Search => 0.2, // ← NEW
                        SuggestionType::Folder => 0.2,
                        _ => 0.0,
                    },
                    "addressBar" => match result.suggestion.kind {
                        SuggestionType::Navigation => 0.5,
                        SuggestionType::Search => 0.3, // ← NEW
                        SuggestionType::History => 0.3,
                        _ => 0.0,
                    },
                    "newTab" => match result.suggestion.kind {
                        SuggestionType::Navigation => 0.5,
                        SuggestionType::Tab => 0.4,
                        SuggestionType::Search => 0.3,
                        SuggestionType::History => 0.3,
                        SuggestionType::Bookmark => 0.3,
                        SuggestionType::Action => 0.2,
                        _ => 0.0,
                    },
                    _ => 0.0,
                };
                result.score += kind_boost;
                result.suggestion.relevance_score += kind_boost;
            }
        }

        // Sort descending by score; type priority breaks ties deterministically.
        results.sort_by(|a, b| {
            b.score
                .partial_cmp(&a.score)
                .unwrap_or(std::cmp::Ordering::Equal)
                .then_with(|| {
                    Self::type_priority(&a.suggestion.kind)
                        .cmp(&Self::type_priority(&b.suggestion.kind))
                })
        });

        // Pass 2: cross-type URL dedup — collapse same URL across Tab/Bookmark/History/ClosedTab/ArchivedTab,
        // keeping the highest-ranked entry (already sorted above).
        {
            let mut seen_urls: std::collections::HashSet<String> = std::collections::HashSet::new();
            results.retain(|r| {
                if Self::is_url_bearing_type(&r.suggestion.kind) {
                    if let Some(ref url) = r.suggestion.execution_payload {
                        seen_urls.insert(url.trim_end_matches('/').to_lowercase())
                    } else {
                        true
                    }
                } else {
                    true
                }
            });
        }

        results
            .into_iter()
            .take(MAX_RESULTS)
            .map(|r| r.suggestion)
            .collect()
    }

    fn is_ineligible_tab_url(url: &str) -> bool {
        let trimmed = url.trim();
        if trimmed.is_empty() {
            return true;
        }
        let lower = trimmed.to_lowercase();
        lower.starts_with("about:")
            || lower.starts_with("chrome-search://")
            || lower.starts_with("chrome://newtab")
            || lower.starts_with("chrome://new-tab-page")
            || lower.starts_with("chrome://downloads")
            || lower.starts_with("chrome://history")
    }

    fn recent_tab_suggestions(ctx: &SearchContext) -> Vec<SuggestionViewModel> {
        let live_ids: std::collections::HashSet<String> =
            ctx.tabs.iter().map(|tab| tab.id.to_string()).collect();
        let live_urls: std::collections::HashSet<String> =
            ctx.tabs.iter().map(|tab| normalize_url(&tab.url)).collect();

        let mut suggestions: Vec<SuggestionViewModel> = Vec::new();
        for (index, tab) in ctx.recent_tabs.iter().enumerate() {
            let trimmed_url = tab.url.trim();
            if Self::is_ineligible_tab_url(trimmed_url) {
                continue;
            }
            if tab.title.trim().is_empty() && trimmed_url.is_empty() {
                continue;
            }
            if live_ids.contains(&tab.id.to_string())
                || live_urls.contains(&normalize_url(&tab.url))
            {
                continue;
            }
            let title = if !tab.title.trim().is_empty() {
                tab.title.clone()
            } else {
                trimmed_url.to_string()
            };
            suggestions.push(SuggestionViewModel {
                kind: SuggestionType::Tab,
                key: format!("tab:{}", tab.id),
                title,
                subtitle: Some(tab.url.clone()),
                execution_payload: Some(tab.url.clone()),
                icon: tab.favicon.clone(),
                relevance_score: MAX_RESULTS as f64 - index as f64,
                match_ranges: None,
                shortcut: None,
                tab_core_id: Some(tab.id.to_string()),
                is_suspended: true,
            });
            if suggestions.len() >= MAX_RESULTS {
                break;
            }
        }
        suggestions
    }

    fn search_actions_only(&self, query: &str, is_incognito: bool) -> Vec<SuggestionViewModel> {
        let lower_query = query.to_lowercase();
        let mut results: Vec<SearchResult> = Vec::new();

        let ephemeral_actions = vec![
            ("close_tab", "Close Tab", "Tabs"),
            ("reload_tab", "Reload Tab", "Tabs"),
            ("hard_reload", "Hard Reload", "Tabs"),
            ("copy_url", "Copy URL", "Navigation"),
            ("toggle_sidebar", "Toggle Sidebar", "Navigation"),
            ("toggle_split_view", "Toggle Split View", "Split View"),
            ("zoom_in", "Zoom In", "View"),
            ("zoom_out", "Zoom Out", "View"),
            ("reset_zoom", "Reset Zoom", "View"),
            ("find_in_page", "Find in Page", "View"),
            ("view_source", "View Source", "View"),
            ("toggle_dev_tools", "Toggle Developer Tools", "View"),
            ("print_page", "Print Page", "View"),
        ];

        if is_incognito {
            for (id, label, category) in ephemeral_actions {
                let target = format!("{} {}", label, category).to_lowercase();
                let match_result = if query.is_empty() {
                    FuzzyMatchResult {
                        is_match: true,
                        score: 0.5,
                        matched_indices: vec![],
                    }
                } else {
                    Self::fuzzy_match(&lower_query, &target)
                };
                if match_result.is_match {
                    results.push(SearchResult {
                        suggestion: SuggestionViewModel {
                            kind: SuggestionType::Action,
                            key: format!("action:{}", id),
                            title: label.to_string(),
                            subtitle: Some(category.to_string()),
                            execution_payload: None,
                            icon: None,
                            relevance_score: match_result.score,
                            match_ranges: Some(indices_to_ranges(
                                &match_result.matched_indices,
                                label.len(),
                            )),
                            shortcut: None,
                            tab_core_id: None,
                            is_suspended: false,
                        },
                        score: match_result.score,
                    });
                }
            }
        } else {
            let has_exact_alias = self.actions.iter().any(|action| {
                action
                    .aliases
                    .iter()
                    .any(|alias| alias.eq_ignore_ascii_case(query.trim()))
            });
            for action in &self.actions {
                let exact_alias = action
                    .aliases
                    .iter()
                    .any(|alias| alias.eq_ignore_ascii_case(query.trim()));
                if has_exact_alias && !exact_alias {
                    continue;
                }
                let target = format!(
                    "{} {} {}",
                    action.label,
                    action.category,
                    action.aliases.join(" ")
                )
                .to_lowercase();
                let match_result = if query.is_empty() {
                    FuzzyMatchResult {
                        is_match: true,
                        score: 0.5,
                        matched_indices: vec![],
                    }
                } else {
                    Self::fuzzy_match(&lower_query, &target)
                };
                if match_result.is_match {
                    results.push(SearchResult {
                        suggestion: SuggestionViewModel {
                            kind: SuggestionType::Action,
                            key: format!("action:{}", action.id),
                            title: action.label.clone(),
                            subtitle: Some(action.category.clone()),
                            execution_payload: None,
                            icon: None,
                            relevance_score: match_result.score,
                            match_ranges: Some(indices_to_ranges(
                                &match_result.matched_indices,
                                action.label.len(),
                            )),
                            shortcut: None,
                            tab_core_id: None,
                            is_suspended: false,
                        },
                        score: match_result.score,
                    });
                }
            }
        }

        // Apply frequency boost before sorting (only for non-incognito)
        if !is_incognito {
            for result in &mut results {
                let key = &result.suggestion.key;
                let boost = self.frequency_boost(key);
                result.score += boost;
                result.suggestion.relevance_score += boost;
            }
        }

        results.sort_by(|a, b| {
            b.score
                .partial_cmp(&a.score)
                .unwrap_or(std::cmp::Ordering::Equal)
        });
        let limit = if is_incognito { 20 } else { MAX_RESULTS };
        results
            .into_iter()
            .take(limit)
            .map(|r| r.suggestion)
            .collect()
    }

    fn search_incognito(
        &self,
        query: &str,
        mode: Option<&str>,
        ctx: &SearchContext,
    ) -> Vec<SuggestionViewModel> {
        // ">" prefix → action-only mode
        if let Some(action_query) = query.strip_prefix('>') {
            return self.search_actions_only(action_query.trim(), true);
        }

        let mut results: Vec<SearchResult> = Vec::new();
        let lower_query = query.to_lowercase();

        // 1. Navigation / Direct URL
        if Self::is_url(query) {
            let url = if query.starts_with("http://") || query.starts_with("https://") {
                query.to_string()
            } else {
                format!("https://{}", query)
            };
            results.push(SearchResult {
                suggestion: SuggestionViewModel {
                    kind: SuggestionType::Navigation,
                    key: format!("nav:{}", url),
                    title: format!("Go to {}", url),
                    subtitle: Some("Navigate".to_string()),
                    execution_payload: Some(url.clone()),
                    icon: None,
                    relevance_score: 2.0,
                    match_ranges: None,
                    shortcut: None,
                    tab_core_id: None,
                    is_suspended: false,
                },
                score: 2.0,
            });
        }

        // 2. Search row
        if !query.trim().is_empty() {
            let search_url = self.get_search_url(query);
            let engine_name = self
                .get_default_search_engine()
                .map(|e| e.name.clone())
                .unwrap_or_else(|| "Google".to_string());
            results.push(SearchResult {
                suggestion: SuggestionViewModel {
                    kind: SuggestionType::Search,
                    key: format!("search:{}", query),
                    title: format!("{} — Search with {}", query, engine_name),
                    subtitle: None,
                    execution_payload: Some(search_url),
                    icon: None,
                    relevance_score: 1.5,
                    match_ranges: None,
                    shortcut: None,
                    tab_core_id: None,
                    is_suspended: false,
                },
                score: 1.5,
            });
        }

        // 3. Same-window tabs
        for tab in &ctx.tabs {
            if tab.title.trim().is_empty() && tab.url.trim().is_empty() {
                continue;
            }
            let title = if !tab.title.trim().is_empty() {
                tab.title.clone()
            } else {
                tab.url.clone()
            };
            let target = format!("{} {}", title, tab.url).to_lowercase();
            let match_result = Self::fuzzy_match(&lower_query, &target);
            if match_result.is_match {
                results.push(SearchResult {
                    suggestion: SuggestionViewModel {
                        kind: SuggestionType::Tab,
                        key: format!("tab:{}", tab.id),
                        title,
                        subtitle: Some(tab.url.clone()),
                        execution_payload: Some(tab.url.clone()),
                        icon: tab.favicon.clone(),
                        relevance_score: match_result.score,
                        match_ranges: Some(indices_to_ranges(
                            &match_result.matched_indices,
                            tab.title.len(),
                        )),
                        shortcut: None,
                        tab_core_id: None,
                        is_suspended: false,
                    },
                    score: match_result.score,
                });
            }
        }

        // 4. Ephemeral actions
        if !lower_query.is_empty() {
            let ephemeral_actions = vec![
                ("close_tab", "Close Tab", "Tabs"),
                ("reload_tab", "Reload Tab", "Tabs"),
                ("hard_reload", "Hard Reload", "Tabs"),
                ("copy_url", "Copy URL", "Navigation"),
                ("toggle_sidebar", "Toggle Sidebar", "Navigation"),
                ("toggle_split_view", "Toggle Split View", "Split View"),
                ("zoom_in", "Zoom In", "View"),
                ("zoom_out", "Zoom Out", "View"),
                ("reset_zoom", "Reset Zoom", "View"),
                ("find_in_page", "Find in Page", "View"),
                ("view_source", "View Source", "View"),
                ("toggle_dev_tools", "Toggle Developer Tools", "View"),
                ("print_page", "Print Page", "View"),
            ];

            for (id, label, category) in ephemeral_actions {
                let target = format!("{} {}", label, category).to_lowercase();
                let match_result = Self::fuzzy_match(&lower_query, &target);
                if match_result.is_match && match_result.score > 0.3 {
                    results.push(SearchResult {
                        suggestion: SuggestionViewModel {
                            kind: SuggestionType::Action,
                            key: format!("action:{}", id),
                            title: label.to_string(),
                            subtitle: Some(category.to_string()),
                            execution_payload: None,
                            icon: None,
                            relevance_score: match_result.score,
                            match_ranges: Some(indices_to_ranges(
                                &match_result.matched_indices,
                                label.len(),
                            )),
                            shortcut: None,
                            tab_core_id: None,
                            is_suspended: false,
                        },
                        score: match_result.score,
                    });
                }
            }
        }

        // Deduplicate keys
        {
            let mut seen = std::collections::HashSet::new();
            results.retain(|r| seen.insert(r.suggestion.key.clone()));
        }

        // Apply mode boosts
        if let Some(mode) = mode {
            for result in &mut results {
                let kind_boost = match mode {
                    "normal" => match result.suggestion.kind {
                        SuggestionType::Action => 0.5,
                        SuggestionType::Tab => 0.3,
                        SuggestionType::Search => 0.2,
                        _ => 0.0,
                    },
                    "addressBar" => match result.suggestion.kind {
                        SuggestionType::Navigation => 0.5,
                        SuggestionType::Search => 0.3,
                        _ => 0.0,
                    },
                    "newTab" => match result.suggestion.kind {
                        SuggestionType::Navigation => 0.5,
                        SuggestionType::Tab => 0.4,
                        SuggestionType::Search => 0.3,
                        _ => 0.0,
                    },
                    _ => 0.0,
                };
                result.score += kind_boost;
                result.suggestion.relevance_score += kind_boost;
            }
        }

        // Sort descending by score; type priority breaks ties deterministically.
        results.sort_by(|a, b| {
            b.score
                .partial_cmp(&a.score)
                .unwrap_or(std::cmp::Ordering::Equal)
                .then_with(|| {
                    Self::type_priority(&a.suggestion.kind)
                        .cmp(&Self::type_priority(&b.suggestion.kind))
                })
        });

        // Cross-type URL deduplication
        {
            let mut seen_urls: std::collections::HashSet<String> = std::collections::HashSet::new();
            results.retain(|r| {
                if Self::is_url_bearing_type(&r.suggestion.kind) {
                    if let Some(ref url) = r.suggestion.execution_payload {
                        seen_urls.insert(url.trim_end_matches('/').to_lowercase())
                    } else {
                        true
                    }
                } else {
                    true
                }
            });
        }

        results
            .into_iter()
            .take(MAX_RESULTS)
            .map(|r| r.suggestion)
            .collect()
    }

    fn type_priority(kind: &SuggestionType) -> u8 {
        match kind {
            SuggestionType::Tab => 0,
            SuggestionType::Bookmark => 1,
            SuggestionType::ClosedTab => 2,
            SuggestionType::ArchivedTab => 3,
            SuggestionType::History => 4,
            SuggestionType::Navigation => 5,
            SuggestionType::AiAnswer => 6,
            SuggestionType::Search => 7,
            SuggestionType::Action => 8,
            SuggestionType::Folder => 9,
            SuggestionType::Calculator => 10,
            SuggestionType::UnitConversion => 11,
            _ => 12,
        }
    }

    fn is_url_bearing_type(kind: &SuggestionType) -> bool {
        matches!(
            kind,
            SuggestionType::Tab
                | SuggestionType::Bookmark
                | SuggestionType::History
                | SuggestionType::ClosedTab
                | SuggestionType::ArchivedTab
        )
    }

    fn is_url(text: &str) -> bool {
        let trimmed = text.trim();
        if trimmed.starts_with("http://") || trimmed.starts_with("https://") {
            return true;
        }
        // Check if it contains a dot and no spaces (likely a domain)
        trimmed.contains('.') && !trimmed.contains(' ')
    }

    fn fuzzy_match(query: &str, target: &str) -> FuzzyMatchResult {
        if query.is_empty() {
            return FuzzyMatchResult {
                is_match: true,
                score: 0.5,
                matched_indices: vec![],
            };
        }

        // Exact prefix match (highest score)
        if target.starts_with(query) {
            return FuzzyMatchResult {
                is_match: true,
                score: 1.0,
                matched_indices: (0..query.len()).collect(),
            };
        }

        // Substring match (high score)
        if let Some(pos) = target.find(query) {
            return FuzzyMatchResult {
                is_match: true,
                score: 0.8,
                matched_indices: (pos..pos + query.len()).collect(),
            };
        }

        // Fuzzy match
        let query_chars: Vec<char> = query.chars().collect();
        let target_chars: Vec<char> = target.chars().collect();
        let mut query_index = 0;
        let mut target_index = 0;
        let mut consecutive_bonus: f64 = 0.0;
        let mut word_start_bonus: f64 = 0.0;
        let mut last_match_index: Option<usize> = None;
        let mut matched_count: usize = 0;
        let mut matched_indices: Vec<usize> = Vec::new();

        while query_index < query_chars.len() && target_index < target_chars.len() {
            if query_chars[query_index] == target_chars[target_index] {
                matched_count += 1;
                matched_indices.push(target_index);
                query_index += 1;

                // Consecutive match bonus
                if let Some(last) = last_match_index {
                    if target_index == last + 1 {
                        consecutive_bonus += 0.1;
                    }
                }

                // Word start bonus
                if target_index == 0 || target_chars[target_index - 1] == ' ' {
                    word_start_bonus += 0.05;
                }

                last_match_index = Some(target_index);
            }
            target_index += 1;
        }

        // All query chars must be found
        if query_index < query_chars.len() {
            return FuzzyMatchResult {
                is_match: false,
                score: 0.0,
                matched_indices: vec![],
            };
        }

        let base_score = matched_count as f64 / target_chars.len() as f64;
        let bonus = consecutive_bonus + word_start_bonus;
        let final_score = (base_score + bonus).min(0.7);

        FuzzyMatchResult {
            is_match: true,
            score: final_score,
            matched_indices,
        }
    }

    pub fn save_search(&mut self, query: String) {
        let trimmed = query.trim().to_string();
        if trimmed.is_empty() {
            return;
        }
        self.recent_searches.retain(|s| s != &trimmed);
        self.recent_searches.insert(0, trimmed);
        if self.recent_searches.len() > 20 {
            self.recent_searches.truncate(20);
        }
    }

    pub fn get_recent_searches(&self) -> &[String] {
        &self.recent_searches
    }

    pub fn clear_recent_searches(&mut self) {
        self.recent_searches.clear();
    }

    pub fn record_usage(&mut self, item_key: &str) {
        let count = self
            .usage_frequency
            .entry(item_key.to_string())
            .or_insert(0);
        *count += 1;

        if self.usage_frequency.len() > MAX_USAGE_KEYS {
            if let Some(min_key) = self
                .usage_frequency
                .iter()
                .min_by_key(|(_, &count)| count)
                .map(|(k, _)| k.clone())
            {
                self.usage_frequency.remove(&min_key);
            }
        }
    }

    pub fn load_usage_frequencies(&mut self, frequencies: Vec<(String, u32)>) {
        for (key, count) in frequencies {
            self.usage_frequency.insert(key, count);
        }
        if self.usage_frequency.len() > MAX_USAGE_KEYS {
            let mut entries: Vec<(String, u32)> = self
                .usage_frequency
                .iter()
                .map(|(k, &v)| (k.clone(), v))
                .collect();
            entries.sort_by_key(|(_, count)| *count);
            let excess = self.usage_frequency.len() - MAX_USAGE_KEYS;
            for (key, _) in entries.into_iter().take(excess) {
                self.usage_frequency.remove(&key);
            }
        }
    }

    pub fn get_usage_frequency(&self, item_key: &str) -> u32 {
        self.usage_frequency.get(item_key).copied().unwrap_or(0)
    }

    fn frequency_boost(&self, item_key: &str) -> f64 {
        let count = self.get_usage_frequency(item_key);
        if count == 0 {
            0.0
        } else {
            (count as f64).ln() * 0.1
        }
    }

    fn recency_boost(visited_at: u64) -> f64 {
        let now = now_millis();
        if visited_at == 0 || now < visited_at {
            return 0.0;
        }
        let age_hours = (now - visited_at) as f64 / (1000.0 * 60.0 * 60.0);
        if age_hours < 1.0 {
            0.3
        } else if age_hours < 24.0 {
            0.2
        } else if age_hours < 168.0 {
            0.1
        } else {
            0.0
        }
    }

    pub fn add_search_engine(&mut self, engine: SearchEngine) {
        if engine.is_default {
            for e in &mut self.search_engines {
                e.is_default = false;
            }
        }
        self.search_engines.push(engine);
    }

    pub fn remove_search_engine(&mut self, id: &str) {
        self.search_engines.retain(|e| e.id != id);
    }

    pub fn set_default_search_engine(&mut self, id: &str) -> bool {
        let mut found = false;
        for engine in &mut self.search_engines {
            if engine.id == id {
                engine.is_default = true;
                found = true;
            } else {
                engine.is_default = false;
            }
        }
        found
    }

    pub fn get_search_engines(&self) -> Vec<SearchEngineViewModel> {
        self.search_engines
            .iter()
            .map(|e| SearchEngineViewModel {
                id: e.id.clone(),
                name: e.name.clone(),
                shortcut: e.shortcut.clone(),
                icon_url: e.icon_url.clone(),
                is_default: e.is_default,
            })
            .collect()
    }

    pub fn get_default_search_engine(&self) -> Option<&SearchEngine> {
        self.search_engines
            .iter()
            .find(|e| e.is_default)
            .or_else(|| self.search_engines.first())
    }

    pub fn get_search_url(&self, query: &str) -> String {
        // Parse @shortcut prefix (e.g. "@g actual query") and route to the matching engine.
        if let Some(rest) = query.strip_prefix('@') {
            if let Some(space_idx) = rest.find(' ') {
                let shortcut_part = &rest[..space_idx];
                let actual_query = rest[space_idx + 1..].trim();
                if !shortcut_part.is_empty() && !actual_query.is_empty() {
                    let at_shortcut = format!("@{}", shortcut_part);
                    if let Some(engine) = self.search_engines.iter().find(|e| {
                        if let Some(ref sc) = e.shortcut {
                            sc.eq_ignore_ascii_case(&at_shortcut)
                                || sc
                                    .trim_start_matches('@')
                                    .eq_ignore_ascii_case(shortcut_part)
                        } else {
                            false
                        }
                    }) {
                        return engine
                            .url_template
                            .replace("{query}", &urlencoding_encode(actual_query));
                    }
                }
            }
        }
        if let Some(engine) = self.get_default_search_engine() {
            engine
                .url_template
                .replace("{query}", &urlencoding_encode(query))
        } else {
            format!(
                "https://www.google.com/search?q={}",
                urlencoding_encode(query)
            )
        }
    }
}

fn normalize_url(url: &str) -> String {
    url.trim_end_matches('/').to_lowercase()
}

fn indices_to_ranges(indices: &[usize], max_len: usize) -> Vec<(usize, usize)> {
    let filtered: Vec<usize> = indices.iter().copied().filter(|&i| i < max_len).collect();
    if filtered.is_empty() {
        return vec![];
    }
    let mut ranges = Vec::new();
    let mut start = filtered[0];
    let mut end = filtered[0];
    for &idx in &filtered[1..] {
        if idx == end + 1 {
            end = idx;
        } else {
            ranges.push((start, end + 1));
            start = idx;
            end = idx;
        }
    }
    ranges.push((start, end + 1));
    ranges
}

fn urlencoding_encode(input: &str) -> String {
    let mut encoded = String::new();
    for byte in input.bytes() {
        match byte {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => {
                encoded.push(byte as char);
            }
            b' ' => encoded.push('+'),
            _ => {
                encoded.push_str(&format!("%{:02X}", byte));
            }
        }
    }
    encoded
}

fn evaluate_expression(expr: &str) -> Option<f64> {
    let tokens = tokenize_expr(expr)?;
    let mut pos = 0;
    let result = parse_add_sub(&tokens, &mut pos)?;
    if pos == tokens.len() {
        Some(result)
    } else {
        None
    }
}

#[derive(Debug, Clone)]
enum Token {
    Number(f64),
    Plus,
    Minus,
    Mul,
    Div,
    Mod,
    LParen,
    RParen,
}

fn tokenize_expr(expr: &str) -> Option<Vec<Token>> {
    let mut tokens = Vec::new();
    let chars: Vec<char> = expr.chars().collect();
    let mut i = 0;
    while i < chars.len() {
        let ch = chars[i];
        if ch.is_whitespace() {
            i += 1;
            continue;
        }
        match ch {
            '+' => {
                tokens.push(Token::Plus);
                i += 1;
            }
            '-' => {
                tokens.push(Token::Minus);
                i += 1;
            }
            '*' => {
                tokens.push(Token::Mul);
                i += 1;
            }
            '/' => {
                tokens.push(Token::Div);
                i += 1;
            }
            '%' => {
                tokens.push(Token::Mod);
                i += 1;
            }
            '(' => {
                tokens.push(Token::LParen);
                i += 1;
            }
            ')' => {
                tokens.push(Token::RParen);
                i += 1;
            }
            '0'..='9' | '.' => {
                let start = i;
                let mut has_dot = false;
                while i < chars.len() && (chars[i].is_ascii_digit() || chars[i] == '.') {
                    if chars[i] == '.' {
                        if has_dot {
                            return None;
                        }
                        has_dot = true;
                    }
                    i += 1;
                }
                let num_str: String = chars[start..i].iter().collect();
                if num_str == "." || num_str.is_empty() {
                    return None;
                }
                let value = num_str.parse::<f64>().ok()?;
                tokens.push(Token::Number(value));
            }
            _ => return None,
        }
    }
    Some(tokens)
}

fn parse_add_sub(tokens: &[Token], pos: &mut usize) -> Option<f64> {
    let mut left = parse_mul_div(tokens, pos)?;
    while *pos < tokens.len() {
        match &tokens[*pos] {
            Token::Plus => {
                *pos += 1;
                let right = parse_mul_div(tokens, pos)?;
                left += right;
            }
            Token::Minus => {
                *pos += 1;
                let right = parse_mul_div(tokens, pos)?;
                left -= right;
            }
            _ => break,
        }
    }
    Some(left)
}

fn parse_mul_div(tokens: &[Token], pos: &mut usize) -> Option<f64> {
    let mut left = parse_unary(tokens, pos)?;
    while *pos < tokens.len() {
        match &tokens[*pos] {
            Token::Mul => {
                *pos += 1;
                let right = parse_unary(tokens, pos)?;
                left *= right;
            }
            Token::Div => {
                *pos += 1;
                let right = parse_unary(tokens, pos)?;
                if right == 0.0 {
                    return None;
                }
                left /= right;
            }
            Token::Mod => {
                *pos += 1;
                let right = parse_unary(tokens, pos)?;
                if right == 0.0 {
                    return None;
                }
                left %= right;
            }
            _ => break,
        }
    }
    Some(left)
}

fn parse_unary(tokens: &[Token], pos: &mut usize) -> Option<f64> {
    if *pos < tokens.len() {
        if let Token::Minus = &tokens[*pos] {
            *pos += 1;
            let value = parse_unary(tokens, pos)?;
            return Some(-value);
        }
    }
    parse_primary(tokens, pos)
}

fn parse_primary(tokens: &[Token], pos: &mut usize) -> Option<f64> {
    if *pos >= tokens.len() {
        return None;
    }
    match &tokens[*pos] {
        Token::Number(n) => {
            let value = *n;
            *pos += 1;
            Some(value)
        }
        Token::LParen => {
            *pos += 1;
            let value = parse_add_sub(tokens, pos)?;
            if *pos < tokens.len() {
                if let Token::RParen = &tokens[*pos] {
                    *pos += 1;
                    return Some(value);
                }
            }
            None
        }
        _ => None,
    }
}

fn try_unit_conversion(query: &str) -> Option<(String, String)> {
    let normalized = query.to_lowercase();
    let parts: Vec<&str> = if normalized.contains(" to ") {
        normalized.splitn(2, " to ").collect()
    } else if normalized.contains(" in ") {
        normalized.splitn(2, " in ").collect()
    } else {
        return None;
    };
    if parts.len() != 2 {
        return None;
    }
    let source = parts[0].trim();
    let target_unit_raw = parts[1].trim();
    if source.is_empty() || target_unit_raw.is_empty() {
        return None;
    }
    let (value, source_unit) = parse_value_and_unit(source)?;
    let target_unit = normalize_unit(target_unit_raw);
    let source_unit_norm = normalize_unit(&source_unit);
    let result = convert(value, &source_unit_norm, &target_unit)?;
    let result_str = format_value(result);
    let title = format!(
        "{} {} = {} {}",
        value, source_unit_norm, result_str, target_unit
    );
    Some((title, "Unit Conversion".to_string()))
}

fn parse_value_and_unit(input: &str) -> Option<(f64, String)> {
    let chars: Vec<char> = input.chars().collect();
    let mut num_end = 0;
    while num_end < chars.len() && (chars[num_end].is_ascii_digit() || chars[num_end] == '.') {
        num_end += 1;
    }
    if num_end == 0 {
        return None;
    }
    let value_str: String = chars[0..num_end].iter().collect();
    let value = value_str.parse::<f64>().ok()?;
    let unit: String = chars[num_end..]
        .iter()
        .collect::<String>()
        .trim()
        .to_string();
    if unit.is_empty() {
        return None;
    }
    Some((value, unit))
}

fn normalize_unit(unit: &str) -> String {
    let u = unit.trim().to_lowercase();
    match u.as_str() {
        "kg" | "kilogram" | "kilograms" => "kg",
        "lb" | "lbs" | "pound" | "pounds" => "lb",
        "g" | "gram" | "grams" => "g",
        "oz" | "ounce" | "ounces" => "oz",
        "cm" | "centimeter" | "centimeters" => "cm",
        "m" | "meter" | "meters" => "m",
        "ft" | "foot" | "feet" => "ft",
        "km" | "kilometer" | "kilometers" => "km",
        "mile" | "miles" | "mi" => "mile",
        "in" | "inch" | "inches" => "in",
        "c" | "celsius" => "°C",
        "f" | "fahrenheit" => "°F",
        _ => &u,
    }
    .to_string()
}

fn convert(value: f64, from: &str, to: &str) -> Option<f64> {
    if from == to {
        return Some(value);
    }
    match (from, to) {
        ("kg", "lb") => Some(value * 2.20462),
        ("lb", "kg") => Some(value / 2.20462),
        ("g", "oz") => Some(value * 0.035274),
        ("oz", "g") => Some(value / 0.035274),
        ("cm", "in") => Some(value * 0.393701),
        ("in", "cm") => Some(value / 0.393701),
        ("m", "ft") => Some(value * 3.28084),
        ("ft", "m") => Some(value / 3.28084),
        ("km", "mile") => Some(value * 0.621371),
        ("mile", "km") => Some(value / 0.621371),
        ("°C", "°F") => Some(value * 9.0 / 5.0 + 32.0),
        ("°F", "°C") => Some((value - 32.0) * 5.0 / 9.0),
        _ => None,
    }
}

fn format_value(val: f64) -> String {
    if val.fract().abs() < 1e-9 && val.abs() < i64::MAX as f64 {
        format!("{}", val as i64)
    } else {
        let s = format!("{:.4}", val);
        s.trim_end_matches('0').trim_end_matches('.').to_string()
    }
}

#[cfg(test)]
mod history_restore_tests {
    use super::*;

    fn ctx() -> SearchContext {
        SearchContext {
            tabs: Vec::new(),
            spaces: Vec::new(),
            folders: Vec::new(),
            archived_tabs: Vec::new(),
            bookmarks: Vec::new(),
            bookmark_favicons: Vec::new(),
            closed_tabs: Vec::new(),
            extensions: Vec::new(),
            is_incognito: false,
            recent_tabs: Vec::new(),
        }
    }

    #[test]
    fn restored_history_is_searchable_and_keeps_visit_time() {
        let mut engine = CommandBarEngine::new();
        engine.restore_history_entry(
            "https://github.com/".into(),
            "GitHub".into(),
            1_000,
        );
        let urls: Vec<String> = engine
            .search("github", None, &ctx())
            .into_iter()
            .filter_map(|s| s.execution_payload)
            .collect();
        assert!(urls.iter().any(|u| u == "https://github.com/"), "{urls:?}");
        assert_eq!(engine.history.back().map(|e| e.visited_at), Some(1_000));
    }

    #[test]
    fn loaded_usage_frequency_is_visible() {
        let mut engine = CommandBarEngine::new();
        engine.load_usage_frequencies(vec![("tab:1".into(), 7)]);
        assert_eq!(engine.get_usage_frequency("tab:1"), 7);
    }
}

#[cfg(test)]
mod trigram_eviction_tests {
    use super::*;

    // Built by hand: SearchContext derives neither Default nor PartialEq, and a
    // shared contract crate should not grow derives just to serve a test here.
    fn ctx() -> SearchContext {
        SearchContext {
            tabs: Vec::new(),
            spaces: Vec::new(),
            folders: Vec::new(),
            archived_tabs: Vec::new(),
            bookmarks: Vec::new(),
            bookmark_favicons: Vec::new(),
            closed_tabs: Vec::new(),
            extensions: Vec::new(),
            is_incognito: false,
            recent_tabs: Vec::new(),
        }
    }

    fn titles_for(engine: &CommandBarEngine, query: &str) -> Vec<String> {
        engine
            .search(query, None, &ctx())
            .into_iter()
            .filter(|s| matches!(s.kind, SuggestionType::History))
            .map(|s| s.title)
            .collect()
    }

    /// The sequence-id index must survive eviction: postings for the entry that
    /// was dropped are unlinked, and every surviving entry still resolves to
    /// itself (not to whatever slid into its old position).
    #[test]
    fn eviction_unlinks_only_the_dropped_entry() {
        let mut engine = CommandBarEngine::new();
        for i in 0..MAX_HISTORY_ENTRIES {
            engine.add_history_entry(
                format!("https://example.com/zzz{i}"),
                format!("zzzentry{i}"),
                None,
            );
        }
        assert_eq!(engine.history.len(), MAX_HISTORY_ENTRIES);
        assert_eq!(engine.history_base_seq, 0);

        // Overflow by one: the oldest entry must go, nothing else.
        engine.add_history_entry(
            "https://example.com/zzzfresh".to_string(),
            "zzzentryfresh".to_string(),
            None,
        );
        assert_eq!(engine.history.len(), MAX_HISTORY_ENTRIES);
        assert_eq!(engine.history_base_seq, 1);

        // Resolve ids directly: fuzzy search is subsequence-based, so "zzzentry0"
        // also matches "zzzentry1000" and friends. What matters here is the id
        // -> entry mapping, not ranking.
        assert!(
            engine.history_by_seq(0).is_none(),
            "evicted id still resolves"
        );
        // Survivors keep their own records. Position-keyed postings would have
        // shifted every one of these by one after the eviction.
        assert_eq!(engine.history_by_seq(1).unwrap().title, "zzzentry1");
        assert_eq!(engine.history_by_seq(5000).unwrap().title, "zzzentry5000");
        assert_eq!(
            engine
                .history_by_seq(MAX_HISTORY_ENTRIES as u64)
                .unwrap()
                .title,
            "zzzentryfresh"
        );

        // And the evicted title is genuinely unreachable through search.
        assert!(
            !titles_for(&engine, "zzzentry0")
                .iter()
                .any(|t| t == "zzzentry0"),
            "evicted entry still surfaced by search"
        );
    }

    /// Every posting for an evicted id must be dropped, including the repeats
    /// produced when one trigram occurs several times in the same entry.
    #[test]
    fn eviction_drains_repeated_trigrams() {
        let mut engine = CommandBarEngine::new();
        // "aba" recurs many times within this single title.
        engine.add_history_entry(
            "https://example.com/a".to_string(),
            "abababababab".to_string(),
            None,
        );
        for i in 0..MAX_HISTORY_ENTRIES {
            engine.add_history_entry(
                format!("https://example.com/f{i}"),
                format!("filler{i}"),
                None,
            );
        }
        assert_eq!(engine.history_base_seq, 1);

        let index = engine.trigram_index.borrow();
        let leftover = index
            .index
            .values()
            .flat_map(|postings| postings.iter())
            .filter(|&&seq| seq == 0)
            .count();
        assert_eq!(leftover, 0, "evicted id still referenced by the index");
    }

    /// clear_history must reset ids alongside the buffer, or stale postings
    /// would resolve against freshly added entries.
    #[test]
    fn clear_history_resets_sequence_space() {
        let mut engine = CommandBarEngine::new();
        engine.add_history_entry(
            "https://example.com/one".to_string(),
            "zzzalpha".to_string(),
            None,
        );
        engine.clear_history();
        assert_eq!(engine.history_base_seq, 0);
        assert!(titles_for(&engine, "zzzalpha").is_empty());

        engine.add_history_entry(
            "https://example.com/two".to_string(),
            "zzzbeta".to_string(),
            None,
        );
        assert!(titles_for(&engine, "zzzbeta")
            .iter()
            .any(|t| t == "zzzbeta"));
        assert!(titles_for(&engine, "zzzalpha").is_empty());
    }
}

#[cfg(test)]
mod mail_action_tests {
    use super::*;
    use maho_types::common::DateTime;
    use maho_types::identifiers::{SpaceId, TabId};
    use maho_types::tab::TabRole;
    use maho_types::traits::shell_renderer::TabViewModel;

    fn ctx() -> SearchContext {
        SearchContext {
            tabs: Vec::new(),
            spaces: Vec::new(),
            folders: Vec::new(),
            archived_tabs: Vec::new(),
            bookmarks: Vec::new(),
            bookmark_favicons: Vec::new(),
            closed_tabs: Vec::new(),
            extensions: Vec::new(),
            is_incognito: false,
            recent_tabs: Vec::new(),
        }
    }

    #[test]
    fn exact_mail_alias_resolves_to_one_command() {
        let mut engine = CommandBarEngine::new();
        engine.add_global_mail_commands();

        let actions: Vec<_> = engine
            .search(">mail", None, &ctx())
            .into_iter()
            .filter(|suggestion| matches!(suggestion.kind, SuggestionType::Action))
            .collect();

        assert_eq!(actions.len(), 1);
        assert_eq!(actions[0].key, "action:open_mail");
    }

    #[test]
    fn global_mail_commands_have_distinct_discoverable_aliases() {
        let mut engine = CommandBarEngine::new();
        engine.add_global_mail_commands();

        for command in GLOBAL_MAIL_COMMANDS {
            for alias in command.aliases {
                let actions: Vec<_> = engine
                    .search(&format!(">{alias}"), None, &ctx())
                    .into_iter()
                    .filter(|suggestion| matches!(suggestion.kind, SuggestionType::Action))
                    .collect();
                assert_eq!(actions.len(), 1, "alias {alias:?} was ambiguous");
                assert_eq!(actions[0].key, format!("action:{}", command.id));
            }
        }
    }

    #[test]
    fn recent_tab_suggestions_filters_empty_tabs() {
        let mut context = ctx();
        let make_tab = |title: &str, url: &str| TabViewModel {
            id: TabId::generate(),
            space_id: SpaceId::generate(),
            title: title.to_string(),
            custom_title: None,
            custom_icon: None,
            pinned_url: None,
            url: url.to_string(),
            favicon: None,
            is_loading: false,
            is_pinned: false,
            is_favorite: false,
            favorite_order: None,
            is_muted: false,
            is_playing_audio: false,
            lifecycle_state: "active".to_string(),
            children: Vec::new(),
            created_at: DateTime::now(),
            last_active_at: DateTime::now(),
            role: TabRole::Normal,
            is_private: false,
        };

        context.recent_tabs = vec![
            make_tab("", ""),
            make_tab("", "about:blank"),
            make_tab("", "chrome://new-tab-page"),
            make_tab("", "chrome://newtab/suffix?foo=bar"),
            make_tab("", "chrome-search://local-ntp/ntp.html"),
            make_tab("Google", "https://www.google.com"),
        ];

        let suggestions = CommandBarEngine::recent_tab_suggestions(&context);
        assert_eq!(suggestions.len(), 1);
        assert_eq!(suggestions[0].title, "Google");
        assert_eq!(suggestions[0].subtitle, Some("https://www.google.com".to_string()));
    }
}
