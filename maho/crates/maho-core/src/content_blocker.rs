use std::collections::HashMap;

use serde::{Deserialize, Serialize};

use adblock::cosmetic_filter_cache::UrlSpecificResources;
use adblock::lists::{parse_filter, FilterSet, ParseOptions};
use adblock::request::Request;
use adblock::Engine;

use maho_types::content_blocking::{
    CanonicalSiteException, ContentBlockerStateDto, ContentBlockingError, ContentBlockingMode,
    FilterHealthStatus, FilterListHealth, FilterListMetadata, FilterListUpdateResponse,
};

pub const MAX_LIST_BODY_BYTES: usize = 16 * 1024 * 1024; // 16 MiB
pub const MAX_FILTER_LISTS: usize = 64;
pub const MAX_ENABLED_FILTER_BYTES: usize = 32 * 1024 * 1024;
// Aggregate rule budget across every enabled list. The default seeded set
// enables EasyList and EasyPrivacy, measured at ~81.7k and ~56.2k rules on
// 2026-09-27 (~138k combined), so the budget must clear that with headroom for
// list growth. At the previous 100k value the two defaults could never be
// active at once: `validate_enabled_candidate_aggregate` rejected whichever
// list lost the fetch race on every attempt, leaving it permanently at zero
// rules. Profiles where EasyPrivacy won that race held no ad rules at all, so
// ad blocking silently did nothing while the pane only reported a health
// error.
pub const MAX_ENABLED_FILTER_RULES: usize = 200_000;

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CosmeticResourcesResponse {
    pub hide_selectors: Vec<String>,
    pub injected_script: Option<String>,
    pub generichide: bool,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BlockResult {
    pub blocked: bool,
    pub redirect: Option<String>,
    pub rewritten_url: Option<String>,
}

#[derive(Clone)]
pub struct CompileInputSnapshot {
    pub raw_contents: Vec<String>,
    pub popup_blocking_enabled: bool,
    pub generation: u64,
    pub content_hash: String,
    pub candidate_ids: Vec<String>,
}

pub struct OpaqueCompiledEngine {
    pub engine: Engine,
    pub generation: u64,
    pub content_hash: String,
    pub candidate_ids: Vec<String>,
}

pub struct ContentBlockerPromotion {
    pub lists: Vec<FilterListMetadata>,
    pub contents: Vec<(String, String)>,
    pub exceptions: Vec<CanonicalSiteException>,
    pub generation: u64,
    pub content_hash: String,
    compiled_hash: String,
    candidate_ids: Vec<String>,
}

/// Outcome of applying a filter-list update response. A 200 with new content is
/// held pending and never becomes observable until a compiled engine installs.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum FilterUpdateOutcome {
    NotModified,
    Unchanged,
    CandidatePending,
    Failed { status_code: u16 },
}

/// A validated candidate body not yet promoted into the active engine/content.
/// It is promoted only when a compiled engine for the same generation and
/// aggregate content hash installs; any failure/stale generation discards it and
/// leaves last-known-good untouched.
#[derive(Clone)]
struct PendingCandidate {
    body: String,
    rule_count: usize,
    etag: Option<String>,
    last_modified: Option<String>,
    sha256: Option<String>,
    generation: u64,
}

struct ValidatedCandidateBody {
    body: String,
    rule_count: usize,
    sha256: String,
}

fn candidate_body_error(reason: impl Into<String>) -> ContentBlockingError {
    ContentBlockingError::InvalidUpdateBody {
        reason: reason.into(),
    }
}

fn validate_candidate_body(
    body: String,
    expected_sha256: Option<&str>,
) -> Result<ValidatedCandidateBody, ContentBlockingError> {
    let body = body.strip_prefix('\u{feff}').unwrap_or(&body).to_string();
    if body.len() > MAX_LIST_BODY_BYTES {
        return Err(ContentBlockingError::FilterBodyTooLarge {
            max_bytes: u64::try_from(MAX_LIST_BODY_BYTES).unwrap_or(u64::MAX),
            actual_bytes: u64::try_from(body.len()).unwrap_or(u64::MAX),
        });
    }

    let trimmed = body.trim();
    if trimmed.is_empty() {
        return Err(candidate_body_error("empty body"));
    }
    let lower = trimmed.to_ascii_lowercase();
    if ["<!doctype", "<html", "<head", "<body", "<!--", "<?xml"]
        .iter()
        .any(|prefix| lower.starts_with(prefix))
    {
        return Err(candidate_body_error("error document"));
    }

    let rule_count = body
        .lines()
        .filter(|line| parse_filter(line, false, ParseOptions::default()).is_ok())
        .count();
    if rule_count == 0 {
        return Err(candidate_body_error("no usable filter rules"));
    }

    use sha2::{Digest, Sha256};
    let sha256 = format!("{:x}", Sha256::digest(body.as_bytes()));
    if let Some(expected) = expected_sha256 {
        if !expected.trim().eq_ignore_ascii_case(&sha256) {
            return Err(candidate_body_error("sha256 mismatch"));
        }
    }

    Ok(ValidatedCandidateBody {
        body,
        rule_count,
        sha256,
    })
}

fn is_secure_https_url(url: &str) -> bool {
    match reqwest::Url::parse(url) {
        Ok(parsed) => parsed.scheme() == "https" && parsed.has_host(),
        Err(_) => false,
    }
}

pub fn normalize_site_exception_key(raw_input: &str) -> String {
    let trimmed = raw_input.trim();
    if trimmed.is_empty() {
        return String::new();
    }
    if trimmed.eq_ignore_ascii_case("null") {
        return String::new();
    }

    let without_scheme = trimmed
        .split_once("://")
        .map(|(_, rest)| rest)
        .unwrap_or(trimmed);
    let without_path = without_scheme
        .split(['/', '?', '#'])
        .next()
        .unwrap_or(without_scheme);
    let without_userinfo = without_path
        .rsplit_once('@')
        .map(|(_, host)| host)
        .unwrap_or(without_path);
    let host = if without_userinfo.starts_with('[') {
        without_userinfo
            .split_once(']')
            .map(|(ipv6, _)| format!("{ipv6}]"))
            .unwrap_or_else(|| without_userinfo.to_string())
    } else if let Some((host, port)) = without_userinfo.split_once(':') {
        if !port.is_empty() && port.chars().all(|c| c.is_ascii_digit()) {
            host.to_string()
        } else {
            return String::new();
        }
    } else {
        without_userinfo.to_string()
    };

    let lower_host = host.to_lowercase();
    let ascii_host = idna::domain_to_ascii(&lower_host).unwrap_or(lower_host);

    if ascii_host.is_empty() {
        return String::new();
    }

    if ascii_host.parse::<std::net::IpAddr>().is_ok()
        || (ascii_host.starts_with('[') && ascii_host.ends_with(']'))
        || !ascii_host.contains('.')
    {
        return ascii_host;
    }

    if let Some(domain) = psl::domain_str(&ascii_host) {
        domain.to_string()
    } else {
        ascii_host
    }
}

pub fn compute_content_hash(contents: &[&str], popup_enabled: bool) -> String {
    use sha2::{Digest, Sha256};
    let mut hasher = Sha256::new();
    hasher.update(if popup_enabled {
        b"popup:1\n"
    } else {
        b"popup:0\n"
    });
    for c in contents {
        hasher.update(c.as_bytes());
        hasher.update(b"\n---\n");
    }
    format!("{:x}", hasher.finalize())
}

fn update_content_hash_component(hasher: &mut sha2::Sha256, value: &[u8]) {
    use sha2::Digest;
    let length = u64::try_from(value.len()).unwrap_or(u64::MAX);
    hasher.update(length.to_le_bytes());
    hasher.update(value);
}

pub fn compile_engine_snapshot(snapshot: CompileInputSnapshot) -> OpaqueCompiledEngine {
    let mut filter_set = FilterSet::new(false);
    for content in &snapshot.raw_contents {
        filter_set.add_filter_list(content, ParseOptions::default());
    }
    if snapshot.popup_blocking_enabled {
        filter_set.add_filter_list("||*$popup,third-party", ParseOptions::default());
    }
    let engine = Engine::from_filter_set(filter_set, true);
    OpaqueCompiledEngine {
        engine,
        generation: snapshot.generation,
        content_hash: snapshot.content_hash,
        candidate_ids: snapshot.candidate_ids,
    }
}

pub fn default_seeded_filter_lists() -> Vec<FilterListMetadata> {
    vec![
        FilterListMetadata {
            id: "easylist".to_string(),
            name: "EasyList".to_string(),
            url: "https://easylist.to/easylist/easylist.txt".to_string(),
            enabled: true,
            rule_count: 0,
            etag: None,
            last_modified: None,
            sha256: None,
            last_attempt_timestamp: None,
            last_success_timestamp: None,
            failure_count: 0,
            last_status: None,
            last_error: None,
        },
        FilterListMetadata {
            id: "easyprivacy".to_string(),
            name: "EasyPrivacy".to_string(),
            url: "https://easylist.to/easylist/easyprivacy.txt".to_string(),
            enabled: true,
            rule_count: 0,
            etag: None,
            last_modified: None,
            sha256: None,
            last_attempt_timestamp: None,
            last_success_timestamp: None,
            failure_count: 0,
            last_status: None,
            last_error: None,
        },
        FilterListMetadata {
            id: "fanboy-annoyance".to_string(),
            name: "Fanboy's Annoyance List".to_string(),
            url: "https://easylist.to/easylist/fanboy-annoyance.txt".to_string(),
            enabled: false,
            rule_count: 0,
            etag: None,
            last_modified: None,
            sha256: None,
            last_attempt_timestamp: None,
            last_success_timestamp: None,
            failure_count: 0,
            last_status: None,
            last_error: None,
        },
    ]
}

pub struct ContentBlocker {
    mode: ContentBlockingMode,
    engine: Engine,
    filter_lists: Vec<FilterListMetadata>,
    filter_list_contents: HashMap<String, String>,
    site_exceptions: HashMap<String, i64>,
    pending_candidates: HashMap<String, PendingCandidate>,
    popup_blocking_enabled: bool,
    generation: u64,
    active_content_hash: Option<String>,
    health: FilterListHealth,
}

impl Default for ContentBlocker {
    fn default() -> Self {
        Self::new()
    }
}

impl ContentBlocker {
    pub fn new() -> Self {
        let filter_set = FilterSet::new(false);
        let lists = default_seeded_filter_lists();
        let engine = Engine::from_filter_set(filter_set, true);
        let mut blocker = Self {
            mode: ContentBlockingMode::Native,
            engine,
            filter_lists: lists,
            filter_list_contents: HashMap::new(),
            site_exceptions: HashMap::new(),
            pending_candidates: HashMap::new(),
            popup_blocking_enabled: true,
            generation: 1,
            active_content_hash: None,
            health: FilterListHealth::default(),
        };
        blocker.update_health();
        blocker
    }

    pub fn mode(&self) -> ContentBlockingMode {
        self.mode
    }

    pub fn set_mode(&mut self, mode: ContentBlockingMode) {
        if self.mode != mode {
            self.mode = mode;
            self.advance_generation();
        }
    }

    pub fn is_native(&self) -> bool {
        self.mode == ContentBlockingMode::Native
    }

    pub fn is_enabled(&self) -> bool {
        self.is_native()
    }

    pub fn set_enabled(&mut self, enabled: bool) {
        let new_mode = if enabled {
            ContentBlockingMode::Native
        } else {
            ContentBlockingMode::Disabled
        };
        self.set_mode(new_mode);
    }

    pub fn set_popup_blocking(&mut self, enabled: bool) {
        if self.popup_blocking_enabled != enabled {
            self.popup_blocking_enabled = enabled;
            self.advance_generation();
        }
    }

    pub fn is_popup_blocking_enabled(&self) -> bool {
        self.popup_blocking_enabled
    }

    pub fn generation(&self) -> u64 {
        self.generation
    }

    pub fn active_content_hash(&self) -> Option<&str> {
        self.active_content_hash.as_deref()
    }

    fn advance_generation(&mut self) {
        self.generation = self.generation.wrapping_add(1);
        self.rebase_pending_candidates();
    }

    fn rebase_pending_candidates(&mut self) {
        for candidate in self.pending_candidates.values_mut() {
            candidate.generation = self.generation;
        }
    }

    pub fn add_site_exception(&mut self, raw_input: &str) -> String {
        let key = normalize_site_exception_key(raw_input);
        if !key.is_empty() && !self.site_exceptions.contains_key(&key) {
            self.site_exceptions
                .insert(key.clone(), chrono::Utc::now().timestamp());
            self.advance_generation();
        }
        key
    }

    /// Restore a persisted exception with its original `created_at` (unlike
    /// `add_site_exception`, which stamps the current time for a new entry).
    pub fn restore_site_exception(&mut self, key: &str, created_at: i64) {
        if !key.is_empty() {
            self.site_exceptions.insert(key.to_string(), created_at);
        }
    }

    pub fn remove_site_exception(&mut self, raw_input: &str) -> bool {
        let key = normalize_site_exception_key(raw_input);
        if !key.is_empty() {
            let removed = self.site_exceptions.remove(&key).is_some();
            if removed {
                self.advance_generation();
            }
            removed
        } else {
            false
        }
    }

    pub fn get_site_exceptions(&self) -> Vec<CanonicalSiteException> {
        self.site_exceptions
            .iter()
            .map(|(key, created_at)| CanonicalSiteException {
                key: key.clone(),
                created_at: *created_at,
            })
            .collect()
    }

    pub fn is_site_excepted(&self, source_url: &str) -> bool {
        let key = normalize_site_exception_key(source_url);
        if key.is_empty() {
            return false;
        }
        self.site_exceptions.contains_key(&key)
    }

    pub fn should_block_request(&self, url: &str, source_url: &str, request_type: &str) -> bool {
        if !self.is_native() {
            return false;
        }

        if self.is_site_excepted(source_url) {
            return false;
        }

        let request = match Request::new(url, source_url, request_type) {
            Ok(r) => r,
            Err(_) => return false,
        };

        let result = self.engine.check_network_request(&request);
        result.matched
    }

    pub fn check_request(&self, url: &str, source_url: &str, request_type: &str) -> BlockResult {
        if !self.is_native() {
            return BlockResult::default();
        }

        if self.is_site_excepted(source_url) {
            return BlockResult::default();
        }

        let request = match Request::new(url, source_url, request_type) {
            Ok(r) => r,
            Err(_) => return BlockResult::default(),
        };

        let result = self.engine.check_network_request(&request);
        BlockResult {
            blocked: result.matched,
            redirect: result.redirect,
            rewritten_url: result.rewritten_url,
        }
    }

    pub fn add_filter_list(
        &mut self,
        id: String,
        name: String,
        url: String,
    ) -> Result<(), ContentBlockingError> {
        if id.trim().is_empty() {
            return Err(ContentBlockingError::InvalidSiteException { input: id });
        }
        if !is_secure_https_url(&url) {
            return Err(ContentBlockingError::InsecureFilterListUrl { url });
        }
        if self.filter_lists.len() >= MAX_FILTER_LISTS {
            return Err(ContentBlockingError::TooManyFilterLists {
                max: MAX_FILTER_LISTS as u32,
                actual: self.filter_lists.len() as u32,
            });
        }
        if self.filter_lists.iter().any(|f| f.id == id) {
            return Err(ContentBlockingError::DuplicateFilterListId { id });
        }

        self.filter_lists.push(FilterListMetadata {
            id,
            name,
            url,
            enabled: true,
            rule_count: 0,
            etag: None,
            last_modified: None,
            sha256: None,
            last_attempt_timestamp: None,
            last_success_timestamp: None,
            failure_count: 0,
            last_status: None,
            last_error: None,
        });
        self.advance_generation();
        self.update_health();
        Ok(())
    }

    pub fn get_filter_lists(&self) -> &[FilterListMetadata] {
        &self.filter_lists
    }

    pub fn toggle_filter_list(&mut self, id: &str, enabled: bool) -> bool {
        if let Some(fl) = self.filter_lists.iter_mut().find(|f| f.id == id) {
            if fl.enabled != enabled {
                fl.enabled = enabled;
                self.advance_generation();
                self.update_health();
                return true;
            }
        }
        false
    }

    pub fn remove_filter_list(&mut self, id: &str) -> bool {
        let initial_len = self.filter_lists.len();
        self.filter_lists.retain(|f| f.id != id);
        self.filter_list_contents.remove(id);
        if self.filter_lists.len() != initial_len {
            self.pending_candidates.remove(id);
            self.advance_generation();
            self.update_health();
            true
        } else {
            false
        }
    }

    pub fn update_filter_list_content(
        &mut self,
        id: &str,
        content: String,
    ) -> Result<(), ContentBlockingError> {
        let candidate = validate_candidate_body(content, None)?;

        if let Some(fl) = self.filter_lists.iter_mut().find(|f| f.id == id) {
            fl.rule_count = candidate.rule_count;
            fl.sha256 = Some(candidate.sha256);
            fl.last_success_timestamp = Some(chrono::Utc::now().timestamp());
            fl.failure_count = 0;
            fl.last_error = None;
        }

        self.pending_candidates.remove(id);
        self.filter_list_contents
            .insert(id.to_string(), candidate.body);
        self.advance_generation();
        self.update_health();
        Ok(())
    }

    pub fn apply_update_response(
        &mut self,
        resp: FilterListUpdateResponse,
    ) -> Result<FilterUpdateOutcome, ContentBlockingError> {
        if !self.filter_lists.iter().any(|list| list.id == resp.list_id) {
            return Err(ContentBlockingError::InvalidUpdateBody {
                reason: format!("unknown filter list id: {}", resp.list_id),
            });
        }

        let now = chrono::Utc::now().timestamp();

        if resp.status_code == 304 {
            // A 304 only confirms content we already hold. Without a stored body
            // the validators are stale; drop them so the next fetch is
            // unconditional instead of reporting a healthy list with 0 rules.
            if !self.filter_list_contents.contains_key(&resp.list_id) {
                if let Some(fl) = self.filter_lists.iter_mut().find(|f| f.id == resp.list_id) {
                    fl.etag = None;
                    fl.last_modified = None;
                }
                self.record_update_failure(
                    &resp.list_id,
                    304,
                    "304 Not Modified without stored list content",
                );
                return Ok(FilterUpdateOutcome::Failed { status_code: 304 });
            }
            if let Some(fl) = self.filter_lists.iter_mut().find(|f| f.id == resp.list_id) {
                fl.last_attempt_timestamp = Some(now);
                fl.last_success_timestamp = Some(now);
                fl.failure_count = 0;
                fl.last_error = None;
                if let Some(e) = resp.etag {
                    fl.etag = Some(e);
                }
                if let Some(lm) = resp.last_modified {
                    fl.last_modified = Some(lm);
                }
            }
            self.update_health();
            return Ok(FilterUpdateOutcome::NotModified);
        }

        if resp.status_code == 200 {
            let Some(body) = resp.body else {
                self.record_update_failure(&resp.list_id, 200, "200 response missing body");
                return Ok(FilterUpdateOutcome::Failed { status_code: 200 });
            };
            let candidate = match validate_candidate_body(body, resp.sha256.as_deref()) {
                Ok(candidate) => candidate,
                Err(error) => {
                    self.record_update_failure(&resp.list_id, 200, &error.to_string());
                    return Err(error);
                }
            };

            let identical = self
                .filter_list_contents
                .get(&resp.list_id)
                .is_some_and(|active| active == &candidate.body);
            if identical {
                if let Some(fl) = self.filter_lists.iter_mut().find(|f| f.id == resp.list_id) {
                    fl.last_attempt_timestamp = Some(now);
                    fl.last_success_timestamp = Some(now);
                    fl.failure_count = 0;
                    fl.last_error = None;
                    if let Some(e) = resp.etag {
                        fl.etag = Some(e);
                    }
                    if let Some(lm) = resp.last_modified {
                        fl.last_modified = Some(lm);
                    }
                    fl.sha256 = Some(candidate.sha256);
                }
                self.update_health();
                return Ok(FilterUpdateOutcome::Unchanged);
            }

            if let Err(error) = self.validate_enabled_candidate_aggregate(
                &resp.list_id,
                &candidate.body,
                candidate.rule_count,
            ) {
                self.record_update_failure(&resp.list_id, 200, &error.to_string());
                return Err(error);
            }
            self.pending_candidates.insert(
                resp.list_id.clone(),
                PendingCandidate {
                    body: candidate.body,
                    rule_count: candidate.rule_count,
                    etag: resp.etag,
                    last_modified: resp.last_modified,
                    sha256: Some(candidate.sha256),
                    generation: self.generation,
                },
            );
            return Ok(FilterUpdateOutcome::CandidatePending);
        }

        self.record_update_failure(
            &resp.list_id,
            resp.status_code,
            &format!("HTTP status {}", resp.status_code),
        );
        Ok(FilterUpdateOutcome::Failed {
            status_code: resp.status_code,
        })
    }

    pub fn has_pending_candidate(&self, list_id: &str) -> bool {
        self.pending_candidates.contains_key(list_id)
    }

    fn validate_enabled_candidate_aggregate(
        &self,
        candidate_id: &str,
        candidate_body: &str,
        candidate_rule_count: usize,
    ) -> Result<(), ContentBlockingError> {
        let mut total_bytes = 0usize;
        let mut total_rules = 0usize;
        for list in self.filter_lists.iter().filter(|list| list.enabled) {
            let candidate = if list.id == candidate_id {
                Some((candidate_body, candidate_rule_count))
            } else {
                self.pending_candidates
                    .get(&list.id)
                    .filter(|candidate| candidate.generation == self.generation)
                    .map(|candidate| (candidate.body.as_str(), candidate.rule_count))
            };
            let active = self
                .filter_list_contents
                .get(&list.id)
                .map(|body| (body.as_str(), list.rule_count));
            if let Some((body, rule_count)) = candidate.or(active) {
                total_bytes = total_bytes.saturating_add(body.len());
                total_rules = total_rules.saturating_add(rule_count);
            }
        }
        if total_bytes > MAX_ENABLED_FILTER_BYTES {
            return Err(ContentBlockingError::FilterBodyTooLarge {
                max_bytes: u64::try_from(MAX_ENABLED_FILTER_BYTES).unwrap_or(u64::MAX),
                actual_bytes: u64::try_from(total_bytes).unwrap_or(u64::MAX),
            });
        }
        if total_rules > MAX_ENABLED_FILTER_RULES {
            return Err(candidate_body_error(
                "aggregate enabled rule limit exceeded",
            ));
        }
        Ok(())
    }

    pub fn discard_pending_candidate(&mut self, list_id: &str) -> bool {
        self.pending_candidates.remove(list_id).is_some()
    }

    pub fn filter_list_content(&self, id: &str) -> Option<&str> {
        self.filter_list_contents.get(id).map(String::as_str)
    }

    pub fn record_update_failure(&mut self, list_id: &str, status_code: u16, error: &str) {
        if let Some(fl) = self.filter_lists.iter_mut().find(|f| f.id == list_id) {
            fl.last_attempt_timestamp = Some(chrono::Utc::now().timestamp());
            fl.failure_count = fl.failure_count.saturating_add(1);
            fl.last_status = Some(status_code);
            fl.last_error = Some(error.to_string());
        }
        self.update_health();
    }

    fn current_candidate_ids(&self) -> Vec<String> {
        self.filter_lists
            .iter()
            .filter(|list| {
                self.pending_candidates
                    .get(&list.id)
                    .is_some_and(|candidate| candidate.generation == self.generation)
            })
            .map(|list| list.id.clone())
            .collect()
    }

    fn create_compile_snapshot_for_candidates(
        &self,
        candidate_ids: Vec<String>,
    ) -> CompileInputSnapshot {
        let candidates = candidate_ids
            .iter()
            .filter_map(|id| {
                self.pending_candidates
                    .get(id)
                    .filter(|candidate| candidate.generation == self.generation)
                    .map(|candidate| (id.as_str(), candidate))
            })
            .collect::<Vec<_>>();

        let mut raw_contents = Vec::new();
        let mut hasher = sha2::Sha256::new();
        use sha2::Digest;
        hasher.update(if self.popup_blocking_enabled {
            b"popup:1\n"
        } else {
            b"popup:0\n"
        });

        for list in self.filter_lists.iter().filter(|list| list.enabled) {
            let body = candidates
                .iter()
                .find(|(id, _)| *id == list.id)
                .map(|(_, candidate)| candidate.body.as_str())
                .or_else(|| self.filter_list_contents.get(&list.id).map(String::as_str));
            if let Some(body) = body {
                update_content_hash_component(&mut hasher, list.id.as_bytes());
                update_content_hash_component(&mut hasher, body.as_bytes());
                raw_contents.push(body.to_string());
            }
        }

        for (id, candidate) in candidates {
            update_content_hash_component(&mut hasher, b"candidate");
            update_content_hash_component(&mut hasher, id.as_bytes());
            update_content_hash_component(&mut hasher, candidate.body.as_bytes());
        }

        CompileInputSnapshot {
            raw_contents,
            popup_blocking_enabled: self.popup_blocking_enabled,
            generation: self.generation,
            content_hash: format!("{:x}", hasher.finalize()),
            candidate_ids,
        }
    }

    pub fn create_compile_snapshot(&self) -> CompileInputSnapshot {
        self.create_compile_snapshot_for_candidates(Vec::new())
    }

    /// Compile input for a pending candidate: the active enabled contents with
    /// the candidate list's body substituted, tagged with the candidate's frozen
    /// generation and prospective content hash so a stale build is rejected.
    pub fn create_candidate_compile_snapshot(&self, list_id: &str) -> Option<CompileInputSnapshot> {
        self.pending_candidates
            .get(list_id)
            .filter(|candidate| candidate.generation == self.generation)
            .map(|_| self.create_compile_snapshot_for_candidates(vec![list_id.to_string()]))
    }

    /// Compile input for the off-thread build step: prefers a pending candidate
    /// (so the resulting engine can be promoted on install) and otherwise
    /// returns the active configuration.
    pub fn select_compile_snapshot(&self) -> CompileInputSnapshot {
        let candidate_ids = self.current_candidate_ids();
        if !candidate_ids.is_empty() {
            return self.create_compile_snapshot_for_candidates(candidate_ids);
        }
        self.create_compile_snapshot()
    }

    pub fn prepare_compiled_engine_install(
        &self,
        compiled: &OpaqueCompiledEngine,
    ) -> Option<ContentBlockerPromotion> {
        if !self.is_native() {
            return None;
        }
        if compiled.generation != self.generation {
            return None;
        }

        if compiled.candidate_ids.is_empty() {
            let active_hash = self.create_compile_snapshot().content_hash;
            if compiled.content_hash != active_hash {
                return None;
            }
            return Some(ContentBlockerPromotion {
                lists: self.filter_lists.clone(),
                contents: self
                    .filter_list_contents
                    .iter()
                    .map(|(id, content)| (id.clone(), content.clone()))
                    .collect(),
                exceptions: self.get_site_exceptions(),
                generation: self.generation,
                content_hash: compiled.content_hash.clone(),
                compiled_hash: compiled.content_hash.clone(),
                candidate_ids: Vec::new(),
            });
        }

        let all_candidates_match_generation = compiled.candidate_ids.iter().all(|id| {
            self.pending_candidates
                .get(id)
                .is_some_and(|candidate| candidate.generation == self.generation)
        });
        if !all_candidates_match_generation {
            return None;
        }
        let matching_snapshot =
            self.create_compile_snapshot_for_candidates(compiled.candidate_ids.clone());
        if matching_snapshot.content_hash != compiled.content_hash {
            return None;
        }

        let frozen_candidates = compiled
            .candidate_ids
            .iter()
            .filter_map(|id| {
                self.pending_candidates
                    .get(id)
                    .cloned()
                    .map(|candidate| (id.clone(), candidate))
            })
            .collect::<Vec<_>>();
        if frozen_candidates.len() != compiled.candidate_ids.len() {
            return None;
        }

        let now = chrono::Utc::now().timestamp();
        let mut lists = self.filter_lists.clone();
        let mut contents = self.filter_list_contents.clone();
        for (id, candidate) in frozen_candidates {
            contents.insert(id.clone(), candidate.body);
            if let Some(list) = lists.iter_mut().find(|list| list.id == id) {
                list.rule_count = candidate.rule_count;
                list.etag = candidate.etag;
                list.last_modified = candidate.last_modified;
                list.sha256 = candidate.sha256;
                list.last_attempt_timestamp = Some(now);
                list.last_success_timestamp = Some(now);
                list.failure_count = 0;
                list.last_error = None;
            }
        }
        Some(ContentBlockerPromotion {
            content_hash: self.active_content_hash_for(&lists, &contents),
            lists,
            contents: contents.into_iter().collect(),
            exceptions: self.get_site_exceptions(),
            generation: self.generation,
            compiled_hash: compiled.content_hash.clone(),
            candidate_ids: compiled.candidate_ids.clone(),
        })
    }

    pub fn commit_compiled_engine(
        &mut self,
        compiled: OpaqueCompiledEngine,
        promotion: ContentBlockerPromotion,
    ) -> bool {
        if compiled.generation != promotion.generation
            || compiled.content_hash != promotion.compiled_hash
        {
            return false;
        }
        self.filter_lists = promotion.lists;
        self.filter_list_contents = promotion.contents.into_iter().collect();
        for id in promotion.candidate_ids {
            self.pending_candidates.remove(&id);
        }
        self.engine = compiled.engine;
        self.active_content_hash = Some(promotion.content_hash);
        self.update_health();
        true
    }

    fn active_content_hash_for(
        &self,
        lists: &[FilterListMetadata],
        contents: &HashMap<String, String>,
    ) -> String {
        use sha2::Digest;
        let mut hasher = sha2::Sha256::new();
        hasher.update(if self.popup_blocking_enabled {
            b"popup:1\n"
        } else {
            b"popup:0\n"
        });
        for list in lists.iter().filter(|list| list.enabled) {
            if let Some(body) = contents.get(&list.id) {
                update_content_hash_component(&mut hasher, list.id.as_bytes());
                update_content_hash_component(&mut hasher, body.as_bytes());
            }
        }
        format!("{:x}", hasher.finalize())
    }

    pub fn install_compiled_engine(&mut self, compiled: OpaqueCompiledEngine) -> bool {
        let Some(promotion) = self.prepare_compiled_engine_install(&compiled) else {
            return false;
        };
        self.commit_compiled_engine(compiled, promotion)
    }

    pub fn deserialize_engine_with_gen(
        &mut self,
        bytes: &[u8],
        generation: u64,
        content_hash: Option<&str>,
    ) -> Result<(), String> {
        self.engine
            .deserialize(bytes)
            .map_err(|e| format!("{e:?}"))?;
        self.set_generation(generation);
        if let Some(hash) = content_hash {
            self.active_content_hash = Some(hash.to_string());
        }
        Ok(())
    }

    pub fn get_cosmetic_resources(&self, url: &str) -> CosmeticResourcesResponse {
        if !self.is_native() {
            return CosmeticResourcesResponse::default();
        }
        let resources: UrlSpecificResources = self.engine.url_cosmetic_resources(url);
        CosmeticResourcesResponse {
            hide_selectors: resources.hide_selectors.into_iter().collect(),
            injected_script: if resources.injected_script.is_empty() {
                None
            } else {
                Some(resources.injected_script)
            },
            generichide: resources.generichide,
        }
    }

    pub fn get_rule_count(&self) -> usize {
        self.filter_lists
            .iter()
            .filter(|fl| fl.enabled)
            .map(|fl| fl.rule_count)
            .sum()
    }

    pub fn serialize_engine(&self) -> Vec<u8> {
        self.engine.serialize()
    }

    pub fn deserialize_engine(&mut self, data: &[u8]) -> Result<(), String> {
        self.engine
            .deserialize(data)
            .map_err(|e| format!("{e:?}"))?;
        let snapshot = self.create_compile_snapshot();
        self.active_content_hash = Some(snapshot.content_hash);
        Ok(())
    }

    fn update_health(&mut self) {
        let total_lists = self.filter_lists.len();
        let active_lists = self.filter_lists.iter().filter(|f| f.enabled).count();
        let total_rules: usize = self
            .filter_lists
            .iter()
            .filter(|f| f.enabled)
            .map(|f| f.rule_count)
            .sum();
        let last_update_timestamp = self
            .filter_lists
            .iter()
            .filter_map(|f| f.last_success_timestamp)
            .max();
        let failure_count: u32 = self.filter_lists.iter().map(|f| f.failure_count).sum();

        let (status, overall_error) = if failure_count == 0 {
            (FilterHealthStatus::Ok, None)
        } else if failure_count < 3 {
            (
                FilterHealthStatus::Warning,
                Some(format!("{failure_count} filter update failures recorded")),
            )
        } else {
            (
                FilterHealthStatus::Error,
                Some(format!(
                    "Multiple filter list updates failed ({failure_count})"
                )),
            )
        };

        self.health = FilterListHealth {
            total_lists,
            active_lists,
            total_rules,
            last_update_timestamp,
            health_status: status,
            overall_error,
        };
    }

    pub fn health(&self) -> &FilterListHealth {
        &self.health
    }

    pub fn get_state_dto(&self) -> ContentBlockerStateDto {
        ContentBlockerStateDto {
            mode: self.mode,
            lists: self.filter_lists.clone(),
            exceptions: self.get_site_exceptions(),
            health: self.health.clone(),
            engine_generation: self.generation,
            engine_hash: self.active_content_hash.clone(),
        }
    }

    pub fn get_content_rules(&self) -> String {
        if !self.is_native() {
            return "[]".to_string();
        }

        let mut rules_arr: Vec<serde_json::Value> = Vec::new();

        for origin in self.site_exceptions.keys() {
            let escaped = regex::escape(origin);
            rules_arr.push(serde_json::json!({
                "trigger": {
                    "url-filter": format!("^https?://([^/]+\\.)?{}(/|$)", escaped)
                },
                "action": {
                    "type": "ignore-previous-rules"
                }
            }));
        }

        for fl in &self.filter_lists {
            if !fl.enabled {
                continue;
            }
            if let Some(content) = self.filter_list_contents.get(&fl.id) {
                for line in content.lines() {
                    let line = line.trim();
                    if line.is_empty() || line.starts_with('!') || line.starts_with('[') {
                        continue;
                    }

                    if let Some(pos) = line.find("##") {
                        let (domains_part, selector_part) = line.split_at(pos);
                        let selector = &selector_part[2..].trim();
                        if selector.is_empty() {
                            continue;
                        }

                        let mut trigger = serde_json::json!({
                            "url-filter": ".*"
                        });

                        if !domains_part.is_empty() {
                            let mut if_domains = Vec::new();
                            let mut unless_domains = Vec::new();
                            for dom in domains_part.split(',') {
                                let dom = dom.trim();
                                if dom.starts_with('~') {
                                    unless_domains.push(dom[1..].to_string());
                                } else {
                                    if_domains.push(dom.to_string());
                                }
                            }
                            if !if_domains.is_empty() {
                                trigger["if-domain"] = serde_json::json!(if_domains);
                            }
                            if !unless_domains.is_empty() {
                                trigger["unless-domain"] = serde_json::json!(unless_domains);
                            }
                        }

                        rules_arr.push(serde_json::json!({
                            "trigger": trigger,
                            "action": {
                                "type": "css-display-none",
                                "selector": selector
                            }
                        }));
                        continue;
                    }

                    let is_exception = line.starts_with("@@");
                    let rest = if is_exception { &line[2..] } else { line };

                    let (rule_part, options_part) = if let Some(idx) = rest.find('$') {
                        let (r, o) = rest.split_at(idx);
                        (r, Some(&o[1..]))
                    } else {
                        (rest, None)
                    };

                    if rule_part.is_empty() {
                        continue;
                    }

                    let mut is_domain_anchor = false;
                    let mut r = rule_part;

                    if r.starts_with("||") {
                        is_domain_anchor = true;
                        r = &r[2..];
                    }

                    let mut has_wildcard_end = false;
                    if r.ends_with('^') {
                        has_wildcard_end = true;
                        r = &r[..r.len() - 1];
                    }

                    let escaped_r = regex::escape(r);

                    let url_filter = if is_domain_anchor {
                        if has_wildcard_end {
                            format!("^https?://([^/]+\\.)?{}(/|$)", escaped_r)
                        } else {
                            format!("^https?://([^/]+\\.)?{}", escaped_r)
                        }
                    } else {
                        escaped_r.replace("\\*", ".*")
                    };

                    let mut trigger = serde_json::json!({
                        "url-filter": url_filter
                    });

                    if let Some(opts) = options_part {
                        let mut load_types = Vec::new();
                        let mut resource_types = Vec::new();
                        for opt in opts.split(',') {
                            let opt = opt.trim();
                            match opt {
                                "third-party" => load_types.push("third-party"),
                                "first-party" => load_types.push("first-party"),
                                "script" => resource_types.push("script"),
                                "stylesheet" | "css" => resource_types.push("style-sheet"),
                                "image" => resource_types.push("image"),
                                "subdocument" => resource_types.push("document"),
                                "xmlhttprequest" => resource_types.push("raw"),
                                _ => {}
                            }
                        }
                        if !load_types.is_empty() {
                            trigger["load-type"] = serde_json::json!(load_types);
                        }
                        if !resource_types.is_empty() {
                            trigger["resource-type"] = serde_json::json!(resource_types);
                        }
                    }

                    let action_type = if is_exception {
                        "ignore-previous-rules"
                    } else {
                        "block"
                    };

                    rules_arr.push(serde_json::json!({
                        "trigger": trigger,
                        "action": {
                            "type": action_type
                        }
                    }));
                }
            }
        }

        if rules_arr.len() > 20000 {
            rules_arr.truncate(20000);
        }

        serde_json::to_string(&serde_json::Value::Array(rules_arr))
            .unwrap_or_else(|_| "[]".to_string())
    }

    /// Adopts persisted lists as the authoritative set, dropping any seeded
    /// defaults so a deleted seed cannot reappear after a restart.
    pub fn hydrate_lists(&mut self, lists: Vec<(FilterListMetadata, String)>) {
        self.filter_lists = lists.iter().map(|(meta, _)| meta.clone()).collect();
        self.filter_list_contents = lists
            .into_iter()
            .filter(|(_, content)| !content.is_empty())
            .map(|(meta, content)| (meta.id, content))
            .collect();
        self.pending_candidates.clear();
        self.update_health();
    }

    pub fn set_generation(&mut self, generation: u64) {
        self.generation = generation;
        self.rebase_pending_candidates();
    }

    pub fn rebuild_engine_sync(&mut self) {
        let snapshot = self.create_compile_snapshot();
        let compiled = compile_engine_snapshot(snapshot);
        self.install_compiled_engine(compiled);
    }
}

impl crate::event_dispatcher::ContentBlockerInterface for ContentBlocker {
    fn set_enabled(&mut self, enabled: bool) {
        let mode = if enabled {
            ContentBlockingMode::Native
        } else {
            ContentBlockingMode::Disabled
        };
        self.set_mode(mode);
    }

    fn is_enabled(&self) -> bool {
        self.is_enabled()
    }

    fn mode(&self) -> ContentBlockingMode {
        self.mode()
    }

    fn set_mode(&mut self, mode: ContentBlockingMode) {
        self.set_mode(mode);
    }

    fn set_popup_blocking(&mut self, enabled: bool) {
        self.set_popup_blocking(enabled);
    }

    fn is_popup_blocking_enabled(&self) -> bool {
        self.is_popup_blocking_enabled()
    }

    fn add_filter_list(&mut self, id: String, name: String, url: String) {
        let _ = self.add_filter_list(id, name, url);
    }

    fn remove_filter_list(&mut self, id: &str) {
        let _ = self.remove_filter_list(id);
    }

    fn toggle_filter_list(&mut self, id: &str, enabled: bool) {
        let _ = self.toggle_filter_list(id, enabled);
    }
}

#[cfg(test)]
mod tests {
    use super::normalize_site_exception_key;

    #[test]
    fn normalize_site_exception_key_handles_t7_site_exception_cases() {
        // Given: site-exception inputs that C++ callers must canonicalize exactly
        // like the Rust content blocker.
        let cases = [
            ("https://sub.example.com/x", "example.com"),
            ("http://example.com:8443", "example.com"),
            ("http://[::1]:3000", "[::1]"),
            ("localhost", "localhost"),
            ("about:blank", ""),
            ("null", ""),
            ("", ""),
        ];

        for (input, expected) in cases {
            // When: the raw origin-like value is normalized.
            let normalized = normalize_site_exception_key(input);

            // Then: the key matches the canonical storage/matching contract.
            assert_eq!(normalized, expected, "input={input}");
        }
    }
}
