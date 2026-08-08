use serde::ser::SerializeStruct;
use serde::{Deserialize, Serialize};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum ContentBlockingMode {
    Native,
    Extension,
    Disabled,
    /// Forward-compatible catch-all for a mode string this build does not
    /// recognise. It deserializes from any unknown value and is treated as
    /// non-native so a future or corrupt mode can never silently turn native
    /// blocking on.
    #[serde(other)]
    Unknown,
}

impl Default for ContentBlockingMode {
    fn default() -> Self {
        Self::Native
    }
}

impl ContentBlockingMode {
    pub fn is_native(&self) -> bool {
        matches!(self, Self::Native)
    }

    pub fn to_legacy_bool(&self) -> bool {
        matches!(self, Self::Native)
    }

    pub fn from_legacy_bool(enabled: bool) -> Self {
        if enabled {
            Self::Native
        } else {
            Self::Disabled
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum ContentBlockingError {
    DuplicateFilterListId { id: String },
    InsecureFilterListUrl { url: String },
    TooManyFilterLists { max: u32, actual: u32 },
    FilterBodyTooLarge { max_bytes: u64, actual_bytes: u64 },
    InvalidSiteException { input: String },
    UnknownMode { raw: String },
    InvalidUpdateBody { reason: String },
}

impl ContentBlockingError {
    pub fn code(&self) -> &'static str {
        match self {
            Self::DuplicateFilterListId { .. } => "duplicate_filter_list_id",
            Self::InsecureFilterListUrl { .. } => "insecure_filter_list_url",
            Self::TooManyFilterLists { .. } => "too_many_filter_lists",
            Self::FilterBodyTooLarge { .. } => "filter_body_too_large",
            Self::InvalidSiteException { .. } => "invalid_site_exception",
            Self::UnknownMode { .. } => "unknown_mode",
            Self::InvalidUpdateBody { .. } => "invalid_update_body",
        }
    }
}

impl std::fmt::Display for ContentBlockingError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::DuplicateFilterListId { id } => write!(f, "duplicate filter list id: {id}"),
            Self::InsecureFilterListUrl { url } => write!(f, "filter list url is not https: {url}"),
            Self::TooManyFilterLists { max, actual } => {
                write!(f, "too many filter lists: {actual} exceeds max {max}")
            }
            Self::FilterBodyTooLarge {
                max_bytes,
                actual_bytes,
            } => write!(
                f,
                "filter body too large: {actual_bytes} bytes exceeds max {max_bytes}"
            ),
            Self::InvalidSiteException { input } => write!(f, "invalid site exception: {input}"),
            Self::UnknownMode { raw } => write!(f, "unknown content blocking mode: {raw}"),
            Self::InvalidUpdateBody { reason } => write!(f, "invalid update body: {reason}"),
        }
    }
}

impl std::error::Error for ContentBlockingError {}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ContentBlockerStateChange {
    pub mode: ContentBlockingMode,
    pub popup_blocking: bool,
    pub last_error: Option<ContentBlockingError>,
}

impl ContentBlockerStateChange {
    pub fn legacy_enabled(&self) -> bool {
        self.mode.to_legacy_bool()
    }
}

impl Serialize for ContentBlockerStateChange {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        use serde::ser::SerializeStruct;
        let field_count = 3 + usize::from(self.last_error.is_some());
        let mut state = serializer.serialize_struct("ContentBlockerStateChange", field_count)?;
        state.serialize_field("mode", &self.mode)?;
        state.serialize_field("enabled", &self.mode.to_legacy_bool())?;
        state.serialize_field("popup_blocking", &self.popup_blocking)?;
        if let Some(ref err) = self.last_error {
            state.serialize_field("last_error", err)?;
        }
        state.end()
    }
}

impl<'de> Deserialize<'de> for ContentBlockerStateChange {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        #[derive(Deserialize)]
        struct Raw {
            #[serde(default)]
            mode: Option<ContentBlockingMode>,
            #[serde(default)]
            enabled: Option<bool>,
            #[serde(default)]
            popup_blocking: bool,
            #[serde(default)]
            last_error: Option<ContentBlockingError>,
        }
        let raw = Raw::deserialize(deserializer)?;
        let mode = raw.mode.unwrap_or_else(|| {
            raw.enabled.map_or(
                ContentBlockingMode::Native,
                ContentBlockingMode::from_legacy_bool,
            )
        });
        Ok(ContentBlockerStateChange {
            mode,
            popup_blocking: raw.popup_blocking,
            last_error: raw.last_error,
        })
    }
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct FilterListMetadata {
    pub id: String,
    pub name: String,
    pub url: String,
    pub enabled: bool,
    pub rule_count: usize,
    #[serde(default)]
    pub etag: Option<String>,
    #[serde(default)]
    pub last_modified: Option<String>,
    #[serde(default)]
    pub sha256: Option<String>,
    #[serde(default)]
    pub last_attempt_timestamp: Option<i64>,
    #[serde(default)]
    pub last_success_timestamp: Option<i64>,
    #[serde(default)]
    pub failure_count: u32,
    #[serde(default)]
    pub last_status: Option<u16>,
    #[serde(default)]
    pub last_error: Option<String>,
}

impl FilterListMetadata {
    pub const fn retry_delay_seconds(failure_count: u32) -> Option<i64> {
        match failure_count {
            0 => None,
            1 => Some(15 * 60),
            2 => Some(60 * 60),
            3 => Some(6 * 60 * 60),
            _ => Some(24 * 60 * 60),
        }
    }

    pub fn next_retry_timestamp(&self) -> Option<i64> {
        let delay = Self::retry_delay_seconds(self.failure_count)?;
        self.last_attempt_timestamp
            .map(|last_attempt| last_attempt.saturating_add(delay))
    }
}

impl Serialize for FilterListMetadata {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        let mut state = serializer.serialize_struct("FilterListMetadata", 14)?;
        state.serialize_field("id", &self.id)?;
        state.serialize_field("name", &self.name)?;
        state.serialize_field("url", &self.url)?;
        state.serialize_field("enabled", &self.enabled)?;
        state.serialize_field("ruleCount", &self.rule_count)?;
        state.serialize_field("etag", &self.etag)?;
        state.serialize_field("lastModified", &self.last_modified)?;
        state.serialize_field("sha256", &self.sha256)?;
        state.serialize_field("lastAttemptTimestamp", &self.last_attempt_timestamp)?;
        state.serialize_field("lastSuccessTimestamp", &self.last_success_timestamp)?;
        state.serialize_field("failureCount", &self.failure_count)?;
        state.serialize_field("lastStatus", &self.last_status)?;
        state.serialize_field("lastError", &self.last_error)?;
        state.serialize_field("nextRetryTimestamp", &self.next_retry_timestamp())?;
        state.end()
    }
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum FilterHealthStatus {
    Ok,
    Warning,
    Error,
}

impl Default for FilterHealthStatus {
    fn default() -> Self {
        Self::Ok
    }
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq, Default)]
#[serde(rename_all = "camelCase")]
pub struct FilterListHealth {
    pub total_lists: usize,
    pub active_lists: usize,
    pub total_rules: usize,
    #[serde(default)]
    pub last_update_timestamp: Option<i64>,
    pub health_status: FilterHealthStatus,
    #[serde(default)]
    pub overall_error: Option<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct FilterListUpdateResponse {
    pub list_id: String,
    pub status_code: u16,
    pub body: Option<String>,
    pub etag: Option<String>,
    pub last_modified: Option<String>,
    pub sha256: Option<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct CanonicalSiteException {
    pub key: String,
    pub created_at: i64,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct ContentBlockerStateDto {
    pub mode: ContentBlockingMode,
    pub lists: Vec<FilterListMetadata>,
    pub exceptions: Vec<CanonicalSiteException>,
    pub health: FilterListHealth,
    pub engine_generation: u64,
    #[serde(default)]
    pub engine_hash: Option<String>,
}
