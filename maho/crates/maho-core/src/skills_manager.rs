use ed25519_dalek::Verifier;
use maho_types::events::core_update::SkillInfo;
use serde::{Deserialize, Serialize};
use std::collections::{HashMap, HashSet};
use std::path::Path;

const SKILL_PUBLIC_KEY: [u8; 32] = [
    0x8b, 0x07, 0x6b, 0x75, 0xcb, 0x78, 0x96, 0x81, 0x09, 0x97, 0xc5, 0x9b, 0x1e, 0xc7, 0xe1, 0x84,
    0x73, 0x0d, 0x71, 0x4c, 0xa0, 0x67, 0x9e, 0x0d, 0xea, 0xe2, 0x2f, 0x7a, 0xba, 0x0d, 0x4d, 0xbc,
];

/// Compiled brain capability version understood by this Rust core.
///
/// Mirrors the C++ `kCompiledBrainCapabilityVersion = 1` HC4 gate for
/// defense-in-depth because public FFI/offline-LKG callers can re-apply a config
/// without passing through the C++ capability check.
const COMPILED_BRAIN_CAPABILITY_VERSION: i64 = 1;

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct Skill {
    pub id: String,
    pub name: String,
    pub slash_command: String,
    pub description: String,
    pub system_prompt: String,
    pub icon: String,
    pub is_builtin: bool,
    pub is_first_party: bool,
    pub allowed_tools: Vec<String>,
    #[serde(default)]
    pub url_patterns: Vec<String>,
    #[serde(default)]
    pub auto_inject: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub required_capability_id: Option<String>,
    #[serde(default)]
    pub user_handoff: bool,
}

impl Skill {
    /// Determines if this skill is discoverable given the currently available capabilities.
    ///
    /// A skill is discoverable IFF:
    /// 1. It declares a `required_capability_id` and that capability is present in `available_capabilities`, OR
    /// 2. It requires no capability (`required_capability_id: None`) and declares `user_handoff: true`.
    pub fn is_discoverable(&self, available_capabilities: &[&str]) -> bool {
        if let Some(ref req) = self.required_capability_id {
            available_capabilities
                .iter()
                .any(|cap| *cap == req.as_str())
        } else {
            self.user_handoff
        }
    }
}

impl From<&Skill> for SkillInfo {
    fn from(skill: &Skill) -> Self {
        Self {
            id: skill.id.clone(),
            name: skill.name.clone(),
            slash_command: skill.slash_command.clone(),
            description: skill.description.clone(),
            icon: Some(skill.icon.clone()),
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct SkillManifestEntry {
    pub id: String,
    pub name: String,
    #[serde(default)]
    pub slash_command: Option<String>,
    #[serde(default)]
    pub description: String,
    #[serde(default)]
    pub system_prompt: String,
    #[serde(default)]
    pub icon: Option<String>,
    #[serde(default)]
    pub is_builtin: Option<bool>,
    #[serde(default)]
    pub is_first_party: Option<bool>,
    #[serde(default)]
    pub allowed_tools: Vec<String>,
    #[serde(default)]
    pub url_patterns: Vec<String>,
    #[serde(default)]
    pub auto_inject: bool,
    #[serde(default)]
    pub required_capability_id: Option<String>,
    #[serde(default)]
    pub user_handoff: bool,
    #[serde(flatten)]
    pub extra_fields: HashMap<String, serde_json::Value>,
}

impl SkillManifestEntry {
    pub fn into_skill(self) -> Skill {
        let slash_command = self
            .slash_command
            .unwrap_or_else(|| format!("/{}", self.id));
        let icon = self.icon.unwrap_or_else(|| "doc.text".to_string());
        Skill {
            id: self.id,
            name: self.name,
            slash_command,
            description: self.description,
            system_prompt: self.system_prompt,
            icon,
            is_builtin: self.is_builtin.unwrap_or(true),
            is_first_party: self.is_first_party.unwrap_or(true),
            allowed_tools: self.allowed_tools,
            url_patterns: self.url_patterns,
            auto_inject: self.auto_inject,
            required_capability_id: self.required_capability_id,
            user_handoff: self.user_handoff,
        }
    }
}

/// Parses a skill manifest in JSON array or line-by-line JSON format.
///
/// Unknown fields are ignored without errors. Malformed lines are skipped
/// and counted in the returned warnings count.
pub fn parse_skill_manifest(content: &str) -> (Vec<Skill>, usize) {
    let trimmed = content.trim();
    if trimmed.is_empty() {
        return (Vec::new(), 0);
    }

    if trimmed.starts_with('[') && trimmed.ends_with(']') {
        if let Ok(entries) = serde_json::from_str::<Vec<SkillManifestEntry>>(trimmed) {
            let skills = entries.into_iter().map(|e| e.into_skill()).collect();
            return (skills, 0);
        }
        if let Ok(raw_items) = serde_json::from_str::<Vec<serde_json::Value>>(trimmed) {
            let mut skills = Vec::new();
            let mut warnings = 0;
            for item in raw_items {
                match serde_json::from_value::<SkillManifestEntry>(item) {
                    Ok(entry) => skills.push(entry.into_skill()),
                    Err(_) => warnings += 1,
                }
            }
            return (skills, warnings);
        }
    }

    let mut skills = Vec::new();
    let mut warnings = 0;
    for line in content.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') || line.starts_with("//") {
            continue;
        }
        match serde_json::from_str::<SkillManifestEntry>(line) {
            Ok(entry) => skills.push(entry.into_skill()),
            Err(_) => {
                warnings += 1;
            }
        }
    }

    (skills, warnings)
}

/// Returns the 31 launch target builtin skills for the agent catalog.
///
/// Each skill declares either a concrete `required_capability_id` or an explicit `user_handoff: true`
/// policy to prevent unbacked capability claims.
pub fn builtin_manifest() -> Vec<Skill> {
    vec![
        // 1. Credentials & Password Managers
        Skill {
            id: "password-manager".into(),
            name: "Password Manager".into(),
            slash_command: "/password-manager".into(),
            description: "Manage and autofill saved passwords and credentials".into(),
            system_prompt: "Manage and autofill credentials securely using the Maho native vault. Never expose plaintext credentials in reasoning or output.".into(),
            icon: "key.fill".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec![],
            auto_inject: false,
            required_capability_id: Some("vault_management".into()),
            user_handoff: false,
        },
        Skill {
            id: "apple-passwords".into(),
            name: "Apple Passwords".into(),
            slash_command: "/apple-passwords".into(),
            description: "Bridge to Apple Passwords and iCloud Keychain".into(),
            system_prompt: "Interface with Apple Passwords and iCloud Keychain. Always notify the user prior to biometric authentication prompts.".into(),
            icon: "lock.shield".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec![],
            auto_inject: false,
            required_capability_id: Some("apple_passwords_bridge".into()),
            user_handoff: false,
        },
        Skill {
            id: "1password".into(),
            name: "1Password".into(),
            slash_command: "/1password".into(),
            description: "Guidance for 1Password extension unlock and autofill".into(),
            system_prompt: "Provide guidance for unlocking the 1Password extension and managing autofill. Request user assistance to complete vault unlock.".into(),
            icon: "lock.circle".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec![],
            auto_inject: false,
            required_capability_id: None,
            user_handoff: true,
        },
        Skill {
            id: "bitwarden".into(),
            name: "Bitwarden".into(),
            slash_command: "/bitwarden".into(),
            description: "Guidance for Bitwarden inline autofill and extension unlock".into(),
            system_prompt: "Provide guidance for unlocking Bitwarden and managing autofill forms. Request user assistance for master password verification.".into(),
            icon: "shield.lefthalf.filled".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec![],
            auto_inject: false,
            required_capability_id: None,
            user_handoff: true,
        },
        Skill {
            id: "dashlane".into(),
            name: "Dashlane".into(),
            slash_command: "/dashlane".into(),
            description: "Guidance for Dashlane extension unlock and autofill".into(),
            system_prompt: "Provide guidance for Dashlane extension operations. Request user assistance to authenticate.".into(),
            icon: "shield.slash".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec![],
            auto_inject: false,
            required_capability_id: None,
            user_handoff: true,
        },
        Skill {
            id: "lastpass".into(),
            name: "LastPass".into(),
            slash_command: "/lastpass".into(),
            description: "Guidance for LastPass extension unlock and autofill".into(),
            system_prompt: "Provide guidance for LastPass extension operations. Request user assistance to authenticate.".into(),
            icon: "asterisk.circle".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec![],
            auto_inject: false,
            required_capability_id: None,
            user_handoff: true,
        },

        // 2. Google Workspace Suite
        Skill {
            id: "google-accounts".into(),
            name: "Google Accounts".into(),
            slash_command: "/google-accounts".into(),
            description: "Discover and route signed-in Google profiles".into(),
            system_prompt: "Discover signed-in Google account profiles and construct multi-account URL paths.".into(),
            icon: "person.crop.circle".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec!["accounts.google.com/*".into()],
            auto_inject: false,
            required_capability_id: Some("google_accounts_read".into()),
            user_handoff: false,
        },
        Skill {
            id: "google-gmail".into(),
            name: "Gmail".into(),
            slash_command: "/gmail".into(),
            description: "Zero-tab direct Gmail search, read, and draft composer".into(),
            system_prompt: "Access Gmail messages, search threads, and compose email drafts directly without opening tabs.".into(),
            icon: "envelope.fill".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec!["mail.google.com/*".into()],
            auto_inject: false,
            required_capability_id: Some("gmail_direct_api".into()),
            user_handoff: false,
        },
        Skill {
            id: "google-docs".into(),
            name: "Google Docs".into(),
            slash_command: "/google-docs".into(),
            description: "Read and manipulate Google Docs content".into(),
            system_prompt: "Read and edit Google Docs documents with tab-attached or zero-tab access.".into(),
            icon: "doc.richtext".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec!["docs.google.com/document/*".into()],
            auto_inject: false,
            required_capability_id: Some("google_docs_adapter".into()),
            user_handoff: false,
        },
        Skill {
            id: "google-sheets".into(),
            name: "Google Sheets".into(),
            slash_command: "/google-sheets".into(),
            description: "Read grid matrix and manipulate Google Sheets spreadsheets".into(),
            system_prompt: "Inspect metadata, read sheet matrices, and edit Google Sheets spreadsheets.".into(),
            icon: "tablecells".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec!["docs.google.com/spreadsheets/*".into()],
            auto_inject: false,
            required_capability_id: Some("google_sheets_adapter".into()),
            user_handoff: false,
        },
        Skill {
            id: "google-calendar".into(),
            name: "Google Calendar".into(),
            slash_command: "/calendar".into(),
            description: "Guidance for Google Calendar event management and scheduling".into(),
            system_prompt: "Help create and review Google Calendar events. Request user confirmation before finalizing schedule modifications.".into(),
            icon: "calendar".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec!["calendar.google.com/*".into()],
            auto_inject: false,
            required_capability_id: None,
            user_handoff: true,
        },
        Skill {
            id: "google-drive".into(),
            name: "Google Drive".into(),
            slash_command: "/drive".into(),
            description: "Guidance for Google Drive navigation and file management".into(),
            system_prompt: "Navigate and organize files in Google Drive. Hand off bulk deletions or permission sharing to user confirmation.".into(),
            icon: "externaldrive.fill".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec!["drive.google.com/*".into()],
            auto_inject: false,
            required_capability_id: None,
            user_handoff: true,
        },
        Skill {
            id: "google-search".into(),
            name: "Google Search".into(),
            slash_command: "/google-search".into(),
            description: "Search the web using authenticated Google Search queries".into(),
            system_prompt: "Perform authenticated Google Search queries and extract structured search results with provenance citations.".into(),
            icon: "magnifyingglass".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec![],
            auto_inject: false,
            required_capability_id: Some("browser_history_search".into()),
            user_handoff: false,
        },
        Skill {
            id: "image-search".into(),
            name: "Image Search".into(),
            slash_command: "/image-search".into(),
            description: "Search for images with parameter filtering".into(),
            system_prompt: "Search for images using structured query filters. Return image URLs and metadata without injecting raw image binaries.".into(),
            icon: "photo.on.rectangle".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec![],
            auto_inject: false,
            required_capability_id: Some("image_search_query".into()),
            user_handoff: false,
        },

        // 3. Collaboration & Communication
        Skill {
            id: "slack".into(),
            name: "Slack".into(),
            slash_command: "/slack".into(),
            description: "Slack workspace message reading, channel history, and drafting".into(),
            system_prompt: "Access Slack conversations, list channels, and draft messages. Always require action confirmation before sending.".into(),
            icon: "bubble.left.and.bubble.right".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec!["app.slack.com/*".into()],
            auto_inject: false,
            required_capability_id: Some("slack_client_api".into()),
            user_handoff: false,
        },
        Skill {
            id: "x-twitter".into(),
            name: "X / Twitter".into(),
            slash_command: "/twitter".into(),
            description: "X/Twitter timeline, search, and tweet drafting".into(),
            system_prompt: "Interact with X/Twitter for reading timelines, searching tweets, and preparing drafts. Require confirmation for posts.".into(),
            icon: "bird".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec!["x.com/*".into(), "twitter.com/*".into()],
            auto_inject: false,
            required_capability_id: Some("twitter_direct_api".into()),
            user_handoff: false,
        },
        Skill {
            id: "notion".into(),
            name: "Notion".into(),
            slash_command: "/notion".into(),
            description: "Notion workspace search and block-level page manipulation".into(),
            system_prompt: "Search Notion workspace and manipulate block-level documents directly.".into(),
            icon: "note.text".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec!["*.notion.so/*".into(), "*.notion.site/*".into()],
            auto_inject: false,
            required_capability_id: Some("notion_direct_api".into()),
            user_handoff: false,
        },
        Skill {
            id: "channel".into(),
            name: "Channel Gateway".into(),
            slash_command: "/channel".into(),
            description: "Inbound and outbound channel bridge for messaging platforms".into(),
            system_prompt: "Bridge agent interactions across messaging channels with cursor deduplication and action confirmation.".into(),
            icon: "antenna.radiowaves.left.and.right".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec![],
            auto_inject: false,
            required_capability_id: Some("channel_gateway".into()),
            user_handoff: false,
        },

        // 4. Document & Office Processing
        Skill {
            id: "docx".into(),
            name: "Word Document (DOCX)".into(),
            slash_command: "/docx".into(),
            description: "Parse, create, and modify DOCX word documents".into(),
            system_prompt: "Programmatically inspect, parse, and edit Word (.docx) documents, preserving formatting and styles.".into(),
            icon: "doc.text.fill".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec![],
            auto_inject: false,
            required_capability_id: Some("docx_processor".into()),
            user_handoff: false,
        },
        Skill {
            id: "pptx".into(),
            name: "PowerPoint Presentation (PPTX)".into(),
            slash_command: "/pptx".into(),
            description: "Extract slides, create, and edit PPTX presentations".into(),
            system_prompt: "Parse and manipulate PowerPoint (.pptx) presentation slides and shape structures.".into(),
            icon: "play.rectangle".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec![],
            auto_inject: false,
            required_capability_id: Some("pptx_processor".into()),
            user_handoff: false,
        },
        Skill {
            id: "xlsx".into(),
            name: "Excel Spreadsheet (XLSX)".into(),
            slash_command: "/xlsx".into(),
            description: "Parse, analyze, and format Excel (.xlsx) spreadsheets".into(),
            system_prompt: "Read, validate formulas, and construct Excel (.xlsx) spreadsheet workbooks.".into(),
            icon: "tablecells.badge.ellipsis".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec![],
            auto_inject: false,
            required_capability_id: Some("xlsx_processor".into()),
            user_handoff: false,
        },
        Skill {
            id: "pdf".into(),
            name: "PDF Document".into(),
            slash_command: "/pdf".into(),
            description: "Render, extract text, merge, and fill PDF documents".into(),
            system_prompt: "Inspect, extract text, render pages, and fill interactive AcroForms in PDF documents.".into(),
            icon: "doc.viewfinder".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec![],
            auto_inject: false,
            required_capability_id: Some("pdf_processor".into()),
            user_handoff: false,
        },

        // 5. Browser Automation, Fallback Controls & Runtime Features
        Skill {
            id: "chrome".into(),
            name: "Chrome Extension API".into(),
            slash_command: "/chrome".into(),
            description: "Access browser extension APIs for tabs, history, and bookmarks".into(),
            system_prompt: "Interact with browser MV3 extension APIs for tabs, bookmarks, and history management.".into(),
            icon: "puzzlepiece.extension".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec![],
            auto_inject: false,
            required_capability_id: Some("browser_extension_api".into()),
            user_handoff: false,
        },
        Skill {
            id: "visual-browse".into(),
            name: "Visual Browse".into(),
            slash_command: "/visual-browse".into(),
            description: "Coordinate-based visual browsing and proof screenshot capture".into(),
            system_prompt: "Perform coordinate-based visual browsing fallback and capture screenshot proof for verification.".into(),
            icon: "eye".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec![],
            auto_inject: false,
            required_capability_id: Some("screenshot_proof".into()),
            user_handoff: false,
        },
        Skill {
            id: "captcha-solver".into(),
            name: "Challenge Detection & Handoff".into(),
            slash_command: "/captcha-solver".into(),
            description: "Detect anti-bot challenges and safely hand off to the user".into(),
            system_prompt: "Detect CAPTCHA or anti-bot challenge screens, suspend agent execution, hand off resolution to the user, and resume upon completion.".into(),
            icon: "shield.lefthalf.filled.badge.checkmark".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec![],
            auto_inject: false,
            required_capability_id: None,
            user_handoff: true,
        },
        Skill {
            id: "notification-activation".into(),
            name: "Notification Activation".into(),
            slash_command: "/notification-activation".into(),
            description: "Activate browser notifications and register event-wait triggers".into(),
            system_prompt: "Check and request notification permissions, guide site notification toggles, and register event waits.".into(),
            icon: "bell.badge".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec![],
            auto_inject: false,
            required_capability_id: Some("notification_activation".into()),
            user_handoff: false,
        },
        Skill {
            id: "aside".into(),
            name: "Maho System Management".into(),
            slash_command: "/aside".into(),
            description: "Manage routines, sessions, and daemon configuration".into(),
            system_prompt: "Inspect and manage agent routines, sessions, settings, and introspection logs safely.".into(),
            icon: "slider.horizontal.3".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec![],
            auto_inject: false,
            required_capability_id: Some("routine_management".into()),
            user_handoff: false,
        },
        Skill {
            id: "draft-preview".into(),
            name: "Draft Preview".into(),
            slash_command: "/draft-preview".into(),
            description: "Render structured interactive draft cards in chat".into(),
            system_prompt: "Format email, message, tweet, and calendar drafts using structured JSON code blocks for chat UI preview.".into(),
            icon: "rectangle.and.pencil.and.ellipsis".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec![],
            auto_inject: false,
            required_capability_id: Some("draft_preview_render".into()),
            user_handoff: false,
        },
        Skill {
            id: "skill-creator".into(),
            name: "Skill Creator".into(),
            slash_command: "/skill-creator".into(),
            description: "Author and validate new user-defined skills".into(),
            system_prompt: "Draft, validate frontmatter schemas, and install new user-defined skills with user confirmation.".into(),
            icon: "hammer".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec![],
            auto_inject: false,
            required_capability_id: Some("skill_authoring".into()),
            user_handoff: false,
        },
        Skill {
            id: "youtube".into(),
            name: "YouTube".into(),
            slash_command: "/youtube".into(),
            description: "Search videos, retrieve transcripts, and inspect comments".into(),
            system_prompt: "Search YouTube videos, retrieve timed transcripts, and summarize video content.".into(),
            icon: "play.tv".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec!["youtube.com/*".into(), "youtu.be/*".into()],
            auto_inject: false,
            required_capability_id: Some("youtube_direct_api".into()),
            user_handoff: false,
        },
        Skill {
            id: "github".into(),
            name: "GitHub".into(),
            slash_command: "/github".into(),
            description: "Guidance for GitHub repositories, pull requests, and issues".into(),
            system_prompt: "Navigate GitHub repositories, review PR files, and inspect issues. Require user confirmation for mutations.".into(),
            icon: "chevron.left.forwardslash.chevron.right".into(),
            is_builtin: true,
            is_first_party: true,
            allowed_tools: vec![],
            url_patterns: vec!["github.com/*".into()],
            auto_inject: false,
            required_capability_id: None,
            user_handoff: true,
        },
    ]
}

pub struct SkillsManager {
    skills: HashMap<String, Skill>,
    available_capabilities: std::collections::HashSet<String>,
    auto_inject_enabled: bool,
}

#[derive(Debug, Deserialize)]
struct SkillsConfigBlob {
    version: u64,
    min_brain_version: i64,
    skills: Vec<ConfigSkill>,
}

#[derive(Debug, Deserialize)]
struct ConfigSkill {
    id: String,
    name: String,
    slash_command: Option<String>,
    description: String,
    icon: String,
    first_party: bool,
    signature: Option<String>,
    #[serde(default)]
    url_patterns: Vec<String>,
    #[serde(default)]
    auto_inject: bool,
    #[serde(default)]
    allowed_tools: Vec<String>,
    #[serde(default)]
    required_capability_id: Option<String>,
    #[serde(default)]
    user_handoff: bool,
    system_prompt: String,
}

impl ConfigSkill {
    fn defaulted_slash_command(&self) -> String {
        self.slash_command
            .clone()
            .unwrap_or_else(|| format!("/{}", self.id))
    }

    fn into_skill(self, slash_command: String) -> Skill {
        Skill {
            id: self.id,
            name: self.name,
            slash_command,
            description: self.description,
            system_prompt: self.system_prompt,
            icon: self.icon,
            is_builtin: false,
            is_first_party: self.first_party,
            allowed_tools: self.allowed_tools,
            url_patterns: self.url_patterns,
            auto_inject: self.auto_inject,
            required_capability_id: self.required_capability_id,
            user_handoff: self.user_handoff,
        }
    }
}

struct OriginMatchCandidates {
    full_url: Option<String>,
    host_path: String,
    host: String,
}

impl Default for SkillsManager {
    fn default() -> Self {
        Self::new()
    }
}

impl SkillsManager {
    pub fn new() -> Self {
        let mut skills = HashMap::new();
        for skill in builtin_manifest() {
            skills.insert(skill.id.clone(), skill);
        }

        Self {
            skills,
            available_capabilities: std::collections::HashSet::new(),
            auto_inject_enabled: true,
        }
    }

    pub fn set_available_capabilities(&mut self, caps: Vec<String>) {
        self.available_capabilities = caps.into_iter().collect();
    }

    pub fn get_available_capabilities(&self) -> &std::collections::HashSet<String> {
        &self.available_capabilities
    }

    pub fn get_discoverable_skills(&self) -> Vec<&Skill> {
        let caps: Vec<&str> = self
            .available_capabilities
            .iter()
            .map(|s| s.as_str())
            .collect();
        self.discover_skills(&caps)
    }

    pub fn load_manifest(&mut self, content: &str) -> (usize, usize) {
        let (parsed_skills, warnings) = parse_skill_manifest(content);
        let loaded = parsed_skills.len();
        for skill in parsed_skills {
            self.skills.insert(skill.id.clone(), skill);
        }
        (loaded, warnings)
    }

    pub fn add_skill(&mut self, skill: Skill) {
        self.skills.insert(skill.id.clone(), skill);
    }

    pub fn contains_skill(&self, id: &str) -> bool {
        self.skills.contains_key(id)
    }

    pub fn discover_skills(&self, available_capabilities: &[&str]) -> Vec<&Skill> {
        let mut discovered: Vec<&Skill> = self
            .skills
            .values()
            .filter(|skill| skill.is_discoverable(available_capabilities))
            .collect();
        discovered.sort_by(|a, b| a.name.cmp(&b.name));
        discovered
    }

    pub fn get_all_skills(&self) -> Vec<&Skill> {
        let mut skills: Vec<&Skill> = self.skills.values().collect();
        skills.sort_by(|a, b| a.name.cmp(&b.name));
        skills
    }

    pub fn get_all_skill_infos(&self) -> Vec<SkillInfo> {
        self.get_all_skills()
            .iter()
            .map(|s| SkillInfo::from(*s))
            .collect()
    }

    pub fn find_by_slash(&self, command: &str) -> Option<&Skill> {
        self.skills.values().find(|s| s.slash_command == command)
    }

    pub fn get_skill(&self, id: &str) -> Option<&Skill> {
        self.skills.get(id)
    }

    pub fn build_prompt(
        &self,
        skill_id: &str,
        user_input: &str,
        page_context: &str,
    ) -> Option<String> {
        self.skills.get(skill_id).map(|skill| {
            skill
                .system_prompt
                .replace("{user_input}", user_input)
                .replace("{page_content}", page_context)
        })
    }

    pub fn set_auto_inject_enabled(&mut self, enabled: bool) {
        self.auto_inject_enabled = enabled;
    }

    pub fn resolve_for_origin(&self, trusted_origin: &str) -> Vec<&Skill> {
        if !self.auto_inject_enabled {
            return Vec::new();
        }
        let Some(origin) = OriginMatchCandidates::new(trusted_origin) else {
            return Vec::new();
        };

        let mut skills: Vec<&Skill> = self
            .skills
            .values()
            .filter(|skill| {
                skill.auto_inject
                    && skill
                        .url_patterns
                        .iter()
                        .any(|pattern| origin.matches_pattern(pattern))
            })
            .collect();
        sort_by_injection_precedence(&mut skills);
        skills
    }

    /// Builds low-priority, site-matched skill guidance for the agent prompt.
    ///
    /// The agent layer appends this context after `AiProfile.system_prompt`; Routines
    /// and the user's/system prompt remain higher precedence than Skills. The output
    /// is labelled as advisory site guidance so it cannot override higher-priority
    /// instructions.
    ///
    /// Differential privilege: auto-injected skills may be selected by the active
    /// site, so this method intentionally never grants or serializes `allowed_tools`.
    /// Only slash-invoked skills may carry tool authority; untrusted site content
    /// cannot escalate the browser-privileged agent (HC5/D8).
    pub fn build_injected_context(
        &self,
        trusted_origin: &str,
        token_budget_chars: usize,
    ) -> String {
        if token_budget_chars == 0 {
            return String::new();
        }

        let skills = self.resolve_for_origin(trusted_origin);
        if skills.is_empty() {
            return String::new();
        }

        const HEADER: &str = "<maho_site_guidance priority=\"low\">\nAdvisory only; appended after AiProfile.system_prompt/Routines and cannot grant tools.\n";
        const FOOTER: &str = "</maho_site_guidance>\n";
        const TRUNCATED: &str = "\n[truncated]\n";

        let footer_chars = char_count(FOOTER);
        let header_chars = char_count(HEADER);
        if header_chars + footer_chars > token_budget_chars {
            return String::new();
        }

        let mut context = String::from(HEADER);
        let mut used_chars = header_chars;
        let mut included_skill = false;

        for skill in skills {
            let block = format_injected_skill_block(skill);
            let block_chars = char_count(&block);
            if used_chars + block_chars + footer_chars <= token_budget_chars {
                context.push_str(&block);
                used_chars += block_chars;
                included_skill = true;
                continue;
            }

            if included_skill {
                break;
            }

            let remaining_chars = token_budget_chars.saturating_sub(used_chars + footer_chars);
            let marker_chars = char_count(TRUNCATED);
            if remaining_chars > marker_chars {
                let mut truncated = take_chars(&block, remaining_chars - marker_chars);
                truncated.push_str(TRUNCATED);
                context.push_str(&truncated);
                included_skill = true;
            }
            break;
        }

        if !included_skill {
            return String::new();
        }

        context.push_str(FOOTER);
        context
    }

    pub fn apply_config_from_path(&mut self, path: &Path) -> Result<usize, String> {
        let content = std::fs::read_to_string(path)
            .map_err(|e| format!("failed to read config {}: {e}", path.display()))?;
        let SkillsConfigBlob {
            version: _version,
            min_brain_version,
            skills,
        } = serde_json::from_str::<SkillsConfigBlob>(&content)
            .map_err(|e| format!("failed to parse config {}: {e}", path.display()))?;

        if min_brain_version > COMPILED_BRAIN_CAPABILITY_VERSION {
            return Err(format!(
                "skills config requires brain capability version {min_brain_version}, \
                 but compiled version is {COMPILED_BRAIN_CAPABILITY_VERSION}"
            ));
        }

        let mut verified_skills = Vec::with_capacity(skills.len());
        for config_skill in skills {
            let slash_command = config_skill.defaulted_slash_command();
            if config_skill.first_party {
                verify_first_party_signature(
                    SkillSigningFields {
                        id: &config_skill.id,
                        first_party: config_skill.first_party,
                        auto_inject: config_skill.auto_inject,
                        slash_command: &slash_command,
                        allowed_tools: &config_skill.allowed_tools,
                        url_patterns: &config_skill.url_patterns,
                        system_prompt: &config_skill.system_prompt,
                    },
                    config_skill.signature.as_deref(),
                )?;
            }
            verified_skills.push(config_skill.into_skill(slash_command));
        }

        let loaded = verified_skills.len();
        for skill in verified_skills {
            self.skills.insert(skill.id.clone(), skill);
        }
        Ok(loaded)
    }

    pub fn load_skills_from_dir(&mut self, dir_path: &Path) -> Result<(), String> {
        if dir_path.is_file() {
            if dir_path.extension().is_some_and(|ext| ext == "json") {
                self.apply_config_from_path(dir_path)?;
                return Ok(());
            }
            return Err(format!(
                "skills path is not a directory: {}",
                dir_path.display()
            ));
        }
        if !dir_path.exists() {
            return Ok(());
        }
        let entries =
            std::fs::read_dir(dir_path).map_err(|e| format!("failed to read directory: {e}"))?;
        for entry in entries {
            let entry = entry.map_err(|e| format!("failed to read directory entry: {e}"))?;
            let path = entry.path();
            if path.is_file() && path.extension().map_or(false, |ext| ext == "md") {
                let content = std::fs::read_to_string(&path)
                    .map_err(|e| format!("failed to read file {}: {e}", path.display()))?;

                if let Some((metadata, body_part)) = parse_skill_markdown(&content) {
                    let id = metadata.get("id").cloned().unwrap_or_else(|| {
                        path.file_stem()
                            .unwrap_or_default()
                            .to_string_lossy()
                            .to_string()
                    });
                    let name = metadata.get("name").cloned().unwrap_or_else(|| id.clone());
                    let slash_command = metadata
                        .get("slash_command")
                        .cloned()
                        .unwrap_or_else(|| format!("/{id}"));
                    let description = metadata.get("description").cloned().unwrap_or_default();
                    let icon = metadata
                        .get("icon")
                        .cloned()
                        .unwrap_or_else(|| "doc.text".to_string());

                    let is_first_party = metadata_bool(&metadata, "first_party");
                    let auto_inject = metadata_bool(&metadata, "auto_inject");
                    let url_patterns = parse_comma_list(metadata.get("url"));
                    let allowed_tools = parse_comma_list(
                        metadata
                            .get("tools")
                            .or_else(|| metadata.get("allowed_tools")),
                    );

                    let required_capability_id = metadata
                        .get("required_capability_id")
                        .or_else(|| metadata.get("capability"))
                        .cloned();
                    let user_handoff = metadata_bool(&metadata, "user_handoff");

                    if is_first_party {
                        if let Err(e) = verify_first_party_signature(
                            SkillSigningFields {
                                id: &id,
                                first_party: is_first_party,
                                auto_inject,
                                slash_command: &slash_command,
                                allowed_tools: &allowed_tools,
                                url_patterns: &url_patterns,
                                system_prompt: &body_part,
                            },
                            metadata.get("signature").map(String::as_str),
                        ) {
                            eprintln!("SkillsManager: {e}");
                            continue;
                        }
                    }

                    self.skills.insert(
                        id.clone(),
                        Skill {
                            id,
                            name,
                            slash_command,
                            description,
                            system_prompt: body_part,
                            icon,
                            is_builtin: false,
                            is_first_party,
                            allowed_tools,
                            url_patterns,
                            auto_inject,
                            required_capability_id,
                            user_handoff,
                        },
                    );
                }
            }
        }
        Ok(())
    }
}

impl OriginMatchCandidates {
    fn new(trusted_origin: &str) -> Option<Self> {
        let trimmed = trusted_origin.trim();
        if trimmed.is_empty() || trimmed.chars().any(char::is_whitespace) {
            return None;
        }

        let without_fragment = trimmed.split_once('#').map_or(trimmed, |(head, _)| head);
        let without_query = without_fragment
            .split_once('?')
            .map_or(without_fragment, |(head, _)| head);
        let normalized = without_query.to_ascii_lowercase();
        let (full_url, host_and_path) = match normalized.split_once("://") {
            Some((scheme, rest)) if !scheme.is_empty() && !rest.is_empty() => {
                (Some(format!("{scheme}://{}", ensure_path(rest))), rest)
            }
            Some(_) => return None,
            None => (None, normalized.as_str()),
        };

        if host_and_path.is_empty() || host_and_path.starts_with('/') {
            return None;
        }

        let host_end = host_and_path.find('/').unwrap_or(host_and_path.len());
        let host = host_and_path[..host_end].to_string();
        if host.is_empty() {
            return None;
        }
        let host_path = ensure_path(host_and_path);

        Some(Self {
            full_url,
            host_path,
            host,
        })
    }

    fn matches_pattern(&self, pattern: &str) -> bool {
        let normalized_pattern = pattern.trim().to_ascii_lowercase();
        if normalized_pattern.is_empty() {
            return false;
        }

        if normalized_pattern.contains("://") {
            return self
                .full_url
                .as_ref()
                .is_some_and(|full_url| glob_matches(&normalized_pattern, full_url));
        }

        if normalized_pattern.contains('/') {
            return glob_matches(&normalized_pattern, &self.host_path);
        }

        glob_matches(&normalized_pattern, &self.host)
    }
}

fn ensure_path(value: &str) -> String {
    if value.contains('/') {
        value.to_string()
    } else {
        format!("{value}/")
    }
}

fn glob_matches(pattern: &str, text: &str) -> bool {
    let pattern_bytes = pattern.as_bytes();
    let text_bytes = text.as_bytes();
    let mut pattern_index = 0;
    let mut text_index = 0;
    let mut last_star_index = None;
    let mut text_after_star_index = 0;

    while text_index < text_bytes.len() {
        if pattern_index < pattern_bytes.len()
            && pattern_bytes[pattern_index] == text_bytes[text_index]
        {
            pattern_index += 1;
            text_index += 1;
        } else if pattern_index < pattern_bytes.len() && pattern_bytes[pattern_index] == b'*' {
            last_star_index = Some(pattern_index);
            pattern_index += 1;
            text_after_star_index = text_index;
        } else if let Some(star_index) = last_star_index {
            pattern_index = star_index + 1;
            text_after_star_index += 1;
            text_index = text_after_star_index;
        } else {
            return false;
        }
    }

    while pattern_index < pattern_bytes.len() && pattern_bytes[pattern_index] == b'*' {
        pattern_index += 1;
    }

    pattern_index == pattern_bytes.len()
}

fn sort_by_injection_precedence(skills: &mut [&Skill]) {
    skills.sort_by(|a, b| {
        b.is_first_party
            .cmp(&a.is_first_party)
            .then_with(|| a.name.cmp(&b.name))
            .then_with(|| a.id.cmp(&b.id))
    });
}

fn format_injected_skill_block(skill: &Skill) -> String {
    let trust_tier = if skill.is_first_party {
        "first-party-signed"
    } else {
        "local-user"
    };
    format!(
        "### Skill: {} ({trust_tier})\nid: {}\n{}\n",
        skill.name, skill.id, skill.system_prompt
    )
}

fn char_count(value: &str) -> usize {
    value.chars().count()
}

fn take_chars(value: &str, limit: usize) -> String {
    value.chars().take(limit).collect()
}

fn metadata_bool(metadata: &HashMap<String, String>, key: &str) -> bool {
    metadata
        .get(key)
        .is_some_and(|value| value.eq_ignore_ascii_case("true"))
}

fn parse_comma_list(value: Option<&String>) -> Vec<String> {
    value.map_or_else(Vec::new, |raw| {
        raw.trim_matches(|ch| ch == '[' || ch == ']')
            .split(',')
            .map(str::trim)
            .filter(|item| !item.is_empty())
            .map(|item| item.trim_matches('"').trim_matches('\'').to_string())
            .collect()
    })
}

struct SkillSigningFields<'a> {
    id: &'a str,
    first_party: bool,
    auto_inject: bool,
    slash_command: &'a str,
    allowed_tools: &'a [String],
    url_patterns: &'a [String],
    system_prompt: &'a str,
}

/// Builds the first-party skill signature payload.
///
/// Canonical format v1 is byte-oriented and injective:
/// `maho-skill-signing-payload-v1\0`, then fields in this exact order: `id`,
/// `first_party`, `auto_inject`, `slash_command`, `allowed_tools`,
/// `url_patterns`, `system_prompt`. String fields encode as `S`, field name,
/// NUL, ASCII decimal byte length, NUL, then raw UTF-8 bytes. Bool fields
/// encode as `B`, field name, NUL, then one byte (`0` or `1`). List fields
/// encode as `L`, field name, NUL, ASCII decimal item count, NUL, then each item
/// sorted by raw UTF-8 bytes and encoded as ASCII decimal byte length, NUL, plus
/// raw UTF-8 bytes. Inputs are parsed skill values after defaults are applied,
/// not raw frontmatter/JSON text.
fn canonical_signing_payload(fields: SkillSigningFields<'_>) -> Vec<u8> {
    let mut payload = Vec::new();
    payload.extend_from_slice(b"maho-skill-signing-payload-v1\0");
    append_string_field(&mut payload, "id", fields.id);
    append_bool_field(&mut payload, "first_party", fields.first_party);
    append_bool_field(&mut payload, "auto_inject", fields.auto_inject);
    append_string_field(&mut payload, "slash_command", fields.slash_command);
    append_list_field(&mut payload, "allowed_tools", fields.allowed_tools);
    append_list_field(&mut payload, "url_patterns", fields.url_patterns);
    append_string_field(&mut payload, "system_prompt", fields.system_prompt);
    payload
}

fn append_field_header(payload: &mut Vec<u8>, field_type: u8, field_name: &str) {
    payload.push(field_type);
    payload.extend_from_slice(field_name.as_bytes());
    payload.push(0);
}

fn append_string_field(payload: &mut Vec<u8>, field_name: &str, value: &str) {
    append_field_header(payload, b'S', field_name);
    append_decimal_usize(payload, value.len());
    payload.extend_from_slice(value.as_bytes());
}

fn append_bool_field(payload: &mut Vec<u8>, field_name: &str, value: bool) {
    append_field_header(payload, b'B', field_name);
    payload.push(u8::from(value));
}

fn append_list_field(payload: &mut Vec<u8>, field_name: &str, values: &[String]) {
    append_field_header(payload, b'L', field_name);
    let mut sorted_values: Vec<&str> = values.iter().map(String::as_str).collect();
    sorted_values.sort_unstable_by(|a, b| a.as_bytes().cmp(b.as_bytes()));
    append_decimal_usize(payload, sorted_values.len());
    for value in sorted_values {
        append_decimal_usize(payload, value.len());
        payload.extend_from_slice(value.as_bytes());
    }
}

fn append_decimal_usize(payload: &mut Vec<u8>, value: usize) {
    payload.extend_from_slice(value.to_string().as_bytes());
    payload.push(0);
}

fn verify_first_party_signature(
    fields: SkillSigningFields<'_>,
    signature_b64: Option<&str>,
) -> Result<(), String> {
    let id = fields.id;
    let signature_b64 =
        signature_b64.ok_or_else(|| format!("Missing signature for first-party skill: {id}"))?;
    let sig_bytes = base64::Engine::decode(&base64::prelude::BASE64_STANDARD, signature_b64)
        .map_err(|e| format!("Invalid base64 signature for first-party skill {id}: {e}"))?;
    let signature = ed25519_dalek::Signature::from_slice(&sig_bytes)
        .map_err(|e| format!("Invalid signature format for first-party skill {id}: {e}"))?;
    let pubkey_bytes = skill_public_key();
    let verifying_key = ed25519_dalek::VerifyingKey::from_bytes(&pubkey_bytes)
        .map_err(|e| format!("Invalid public key: {e}"))?;
    let payload = canonical_signing_payload(fields);
    verifying_key
        .verify(&payload, &signature)
        .map_err(|e| format!("Signature verification failed for first-party skill {id}: {e}"))
}

fn skill_public_key() -> [u8; 32] {
    #[cfg(test)]
    {
        TEST_SKILL_PUBLIC_KEY.with(|key| (*key.borrow()).unwrap_or(SKILL_PUBLIC_KEY))
    }
    #[cfg(not(test))]
    {
        SKILL_PUBLIC_KEY
    }
}

fn parse_skill_markdown(content: &str) -> Option<(HashMap<String, String>, String)> {
    if !content.starts_with("---") {
        return None;
    }
    let parts: Vec<&str> = content.splitn(3, "---").collect();
    if parts.len() < 3 {
        return None;
    }
    let yaml_part = parts[1];
    let body_part = parts[2].trim().to_string();

    let mut metadata = HashMap::new();
    for line in yaml_part.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        if let Some((key, val)) = line.split_once(':') {
            let key = key.trim().to_string();
            let val = val.trim().trim_matches('"').trim_matches('\'').to_string();
            metadata.insert(key, val);
        }
    }
    Some((metadata, body_part))
}

#[cfg(test)]
thread_local! {
    pub static TEST_SKILL_PUBLIC_KEY: std::cell::RefCell<Option<[u8; 32]>> = const { std::cell::RefCell::new(None) };
}

#[cfg(test)]
mod tests {
    use super::*;
    use ed25519_dalek::Signer;
    use ed25519_dalek::SigningKey;
    use tempfile::tempdir;

    fn string_vec(values: &[&str]) -> Vec<String> {
        values.iter().map(|value| (*value).to_string()).collect()
    }

    fn install_test_public_key(signing_key: &SigningKey) {
        let verifying_key = signing_key.verifying_key();
        TEST_SKILL_PUBLIC_KEY.with(|key| {
            *key.borrow_mut() = Some(verifying_key.to_bytes());
        });
    }

    fn clear_test_public_key() {
        TEST_SKILL_PUBLIC_KEY.with(|key| {
            *key.borrow_mut() = None;
        });
    }

    fn sign_test_skill(signing_key: &SigningKey, fields: SkillSigningFields<'_>) -> String {
        let payload = canonical_signing_payload(fields);
        let sig = signing_key.sign(&payload);
        base64::Engine::encode(&base64::prelude::BASE64_STANDARD, sig.to_bytes())
    }

    struct TestConfigSkillSpec<'a> {
        id: &'a str,
        name: &'a str,
        slash_command: &'a str,
        description: &'a str,
        icon: &'a str,
        auto_inject: bool,
        allowed_tools: Vec<String>,
        url_patterns: Vec<String>,
        system_prompt: &'a str,
    }

    fn signed_config_skill(
        signing_key: &SigningKey,
        spec: TestConfigSkillSpec<'_>,
    ) -> serde_json::Value {
        let signature = sign_test_skill(
            signing_key,
            SkillSigningFields {
                id: spec.id,
                first_party: true,
                auto_inject: spec.auto_inject,
                slash_command: spec.slash_command,
                allowed_tools: &spec.allowed_tools,
                url_patterns: &spec.url_patterns,
                system_prompt: spec.system_prompt,
            },
        );
        serde_json::json!({
            "id": spec.id,
            "name": spec.name,
            "slash_command": spec.slash_command,
            "description": spec.description,
            "icon": spec.icon,
            "first_party": true,
            "signature": signature,
            "url_patterns": spec.url_patterns,
            "auto_inject": spec.auto_inject,
            "allowed_tools": spec.allowed_tools,
            "system_prompt": spec.system_prompt,
        })
    }

    #[test]
    fn test_first_party_browser_product_file_builtins_are_present() {
        let manager = SkillsManager::new();

        for id in [
            "password-manager",
            "google-gmail",
            "visual-browse",
            "captcha-solver",
            "github",
        ] {
            let skill = manager.get_skill(id).expect("first-party builtin exists");
            assert!(skill.is_builtin);
            assert!(skill.is_first_party);
        }

        let vault = manager.get_skill("password-manager").unwrap();
        assert_eq!(
            vault.required_capability_id.as_deref(),
            Some("vault_management")
        );
        assert!(!vault.user_handoff);

        let gmail = manager.get_skill("google-gmail").unwrap();
        assert_eq!(
            gmail.required_capability_id.as_deref(),
            Some("gmail_direct_api")
        );

        let captcha = manager.get_skill("captcha-solver").unwrap();
        assert!(captcha.user_handoff);
        assert!(captcha.required_capability_id.is_none());
    }

    #[test]
    fn test_signed_first_party_browser_product_file_config_loads() {
        let seed = [9u8; 32];
        let signing_key = SigningKey::from_bytes(&seed);
        install_test_public_key(&signing_key);

        let browser_prompt = "Use signed Maho browser operation guidance.";
        let product_prompt = "Use signed Maho product guidance.";
        let file_prompt = "Use signed Maho read-only file guidance.";
        let skills = vec![
            signed_config_skill(
                &signing_key,
                TestConfigSkillSpec {
                    id: "signed-maho-browser",
                    name: "Signed Maho Browser",
                    slash_command: "/signed-browser",
                    description: "Signed browser workflow guidance",
                    icon: "cursorarrow.click",
                    auto_inject: false,
                    allowed_tools: string_vec(&["browser_tab_list", "browser_click"]),
                    url_patterns: Vec::new(),
                    system_prompt: browser_prompt,
                },
            ),
            signed_config_skill(
                &signing_key,
                TestConfigSkillSpec {
                    id: "signed-maho-product",
                    name: "Signed Maho Product",
                    slash_command: "/signed-product",
                    description: "Signed product guidance",
                    icon: "sparkles",
                    auto_inject: true,
                    allowed_tools: string_vec(&["browser_page_text"]),
                    url_patterns: string_vec(&["mahobrowser.com/*"]),
                    system_prompt: product_prompt,
                },
            ),
            signed_config_skill(
                &signing_key,
                TestConfigSkillSpec {
                    id: "signed-maho-file",
                    name: "Signed Maho File",
                    slash_command: "/signed-file",
                    description: "Signed read-only file guidance",
                    icon: "folder",
                    auto_inject: false,
                    allowed_tools: string_vec(&["fs_read"]),
                    url_patterns: Vec::new(),
                    system_prompt: file_prompt,
                },
            ),
        ];

        let dir = tempdir().unwrap();
        let config_path = dir.path().join("first-party-skills.json");
        let blob = serde_json::json!({
            "version": 1,
            "min_brain_version": 1,
            "skills": skills,
        });
        std::fs::write(&config_path, blob.to_string()).unwrap();

        let mut manager = SkillsManager {
            skills: HashMap::new(),
            available_capabilities: HashSet::new(),
            auto_inject_enabled: true,
        };
        let loaded = manager.apply_config_from_path(&config_path).unwrap();

        assert_eq!(loaded, 3);
        assert_eq!(manager.get_all_skills().len(), 3);
        assert_eq!(
            manager
                .get_skill("signed-maho-browser")
                .unwrap()
                .system_prompt,
            browser_prompt
        );
        assert_eq!(
            manager
                .get_skill("signed-maho-product")
                .unwrap()
                .system_prompt,
            product_prompt
        );
        assert_eq!(
            manager.get_skill("signed-maho-file").unwrap().allowed_tools,
            vec!["fs_read"]
        );
        assert_eq!(
            manager.resolve_for_origin("https://mahobrowser.com/docs")[0].id,
            "signed-maho-product"
        );

        clear_test_public_key();
    }

    #[test]
    fn test_unsigned_first_party_browser_skill_config_rejected() {
        let dir = tempdir().unwrap();
        let config_path = dir.path().join("unsigned-first-party.json");
        let blob = serde_json::json!({
            "version": 1,
            "min_brain_version": 1,
            "skills": [{
                "id": "unsigned-maho-browser",
                "name": "Unsigned Maho Browser",
                "slash_command": "/unsigned-browser",
                "description": "Unsigned browser guidance",
                "icon": "cursorarrow.click",
                "first_party": true,
                "url_patterns": ["mahobrowser.com/*"],
                "auto_inject": true,
                "allowed_tools": ["browser_click"],
                "system_prompt": "Unsigned first-party guidance must not load."
            }]
        });
        std::fs::write(&config_path, blob.to_string()).unwrap();

        let mut manager = SkillsManager {
            skills: HashMap::new(),
            available_capabilities: HashSet::new(),
            auto_inject_enabled: true,
        };
        let result = manager.apply_config_from_path(&config_path);

        assert!(result.is_err());
        assert!(manager.get_all_skills().is_empty());
    }

    #[test]
    fn test_first_party_product_auto_inject_kill_switch_disables_builtin_context() {
        let mut manager = SkillsManager::new();
        let mut custom_skill = test_skill(
            "test-product-guide",
            "Product Guide",
            true,
            vec!["mahobrowser.com/*"],
            "Maho product guidance.",
        );
        custom_skill.user_handoff = true;
        manager
            .skills
            .insert("test-product-guide".to_string(), custom_skill);

        let context = manager.build_injected_context("https://mahobrowser.com/docs", 10_000);
        assert!(context.contains("Maho product guidance"));

        manager.set_auto_inject_enabled(false);

        assert!(manager
            .resolve_for_origin("https://mahobrowser.com/docs")
            .is_empty());
        assert!(manager
            .build_injected_context("https://mahobrowser.com/docs", 10_000)
            .is_empty());
    }

    #[test]
    fn test_builtin_injected_context_has_no_tool_authority_or_allowed_tools() {
        let mut manager = SkillsManager::new();
        let mut custom_skill = test_skill(
            "test-product-guide",
            "Product Guide",
            true,
            vec!["mahobrowser.com/*"],
            "Maho product guidance.",
        );
        custom_skill.allowed_tools = vec![
            "browser_page_text".into(),
            "browser_click".into(),
            "fs_read".into(),
        ];
        manager
            .skills
            .insert("test-product-guide".to_string(), custom_skill);

        let context = manager.build_injected_context("https://mahobrowser.com/docs", 10_000);

        assert!(context.contains("<maho_site_guidance priority=\"low\">"));
        assert!(context.contains("Maho product guidance"));
        assert!(!context.contains("allowed_tools"));
        assert!(!context.contains("browser_page_text"));
        assert!(!context.contains("browser_click"));
        assert!(!context.contains("fs_read"));
    }

    #[test]
    fn test_builtin_catalog_entries_have_valid_capability_or_handoff() {
        let manager = SkillsManager::new();
        let skills = manager.get_all_skills();
        assert_eq!(skills.len(), 31);

        for skill in skills {
            assert!(
                skill.required_capability_id.is_some() || skill.user_handoff,
                "builtin skill {} must declare required_capability_id or explicit user_handoff=true",
                skill.id
            );
        }
    }

    #[test]
    fn test_parse_skill_markdown() {
        let content = r#"---
id: test-skill
name: Test Skill
slash_command: /test
description: Testing parser
icon: star
tools: [list_dir, view_file]
---
This is the system prompt.
"#;
        let (metadata, body) = parse_skill_markdown(content).unwrap();
        assert_eq!(metadata.get("id").unwrap(), "test-skill");
        assert_eq!(metadata.get("name").unwrap(), "Test Skill");
        assert_eq!(metadata.get("tools").unwrap(), "[list_dir, view_file]");
        assert_eq!(body, "This is the system prompt.");
    }

    #[test]
    fn test_load_skills_local_user() {
        let dir = tempdir().unwrap();
        let file_path = dir.path().join("user_skill.md");
        let content = r#"---
id: user-skill
name: User Skill
first_party: false
tools: list_dir
---
Hello user!
"#;
        std::fs::write(&file_path, content).unwrap();

        let mut manager = SkillsManager::new();
        manager.load_skills_from_dir(dir.path()).unwrap();

        let skill = manager.get_skill("user-skill").unwrap();
        assert_eq!(skill.name, "User Skill");
        assert_eq!(skill.is_first_party, false);
        assert_eq!(skill.allowed_tools, vec!["list_dir".to_string()]);
        assert_eq!(skill.system_prompt, "Hello user!");
    }

    #[test]
    fn test_load_skills_first_party_signed() {
        let seed = [0u8; 32];
        let signing_key = SigningKey::from_bytes(&seed);
        install_test_public_key(&signing_key);

        let body = "Hello first party!";
        let allowed_tools = string_vec(&["view_file", "write_file"]);
        let url_patterns: Vec<String> = Vec::new();
        let sig_b64 = sign_test_skill(
            &signing_key,
            SkillSigningFields {
                id: "fp-skill",
                first_party: true,
                auto_inject: false,
                slash_command: "/fp-skill",
                allowed_tools: &allowed_tools,
                url_patterns: &url_patterns,
                system_prompt: body,
            },
        );

        let dir = tempdir().unwrap();
        let file_path = dir.path().join("first_party_skill.md");
        let content = format!(
            r#"---
id: fp-skill
name: First Party Skill
first_party: true
signature: {}
tools: [view_file, write_file]
---
{}
"#,
            sig_b64, body
        );
        std::fs::write(&file_path, content).unwrap();

        let mut manager = SkillsManager::new();
        manager.load_skills_from_dir(dir.path()).unwrap();

        let skill = manager.get_skill("fp-skill").unwrap();
        assert_eq!(skill.name, "First Party Skill");
        assert_eq!(skill.is_first_party, true);
        assert_eq!(
            skill.allowed_tools,
            vec!["view_file".to_string(), "write_file".to_string()]
        );
        assert_eq!(skill.system_prompt, body);

        clear_test_public_key();
    }

    #[test]
    fn test_load_skills_first_party_tampered_rejected() {
        let seed = [0u8; 32];
        let signing_key = SigningKey::from_bytes(&seed);
        install_test_public_key(&signing_key);

        let body = "Hello first party!";
        let allowed_tools: Vec<String> = Vec::new();
        let url_patterns: Vec<String> = Vec::new();
        let sig_b64 = sign_test_skill(
            &signing_key,
            SkillSigningFields {
                id: "fp-skill",
                first_party: true,
                auto_inject: false,
                slash_command: "/fp-skill",
                allowed_tools: &allowed_tools,
                url_patterns: &url_patterns,
                system_prompt: body,
            },
        );

        let dir = tempdir().unwrap();
        let file_path = dir.path().join("first_party_skill.md");
        let content = format!(
            r#"---
id: fp-skill
name: First Party Skill
first_party: true
signature: {}
---
Hello first party tampered!
"#,
            sig_b64
        );
        std::fs::write(&file_path, content).unwrap();

        let mut manager = SkillsManager::new();
        manager.load_skills_from_dir(dir.path()).unwrap();

        assert!(manager.get_skill("fp-skill").is_none());

        clear_test_public_key();
    }

    #[test]
    fn test_first_party_signature_covers_allowed_tools() {
        let seed = [3u8; 32];
        let signing_key = SigningKey::from_bytes(&seed);
        install_test_public_key(&signing_key);

        let body = "Tool-scoped first-party guidance.";
        let signed_allowed_tools = string_vec(&["view_file"]);
        let url_patterns: Vec<String> = Vec::new();
        let sig_b64 = sign_test_skill(
            &signing_key,
            SkillSigningFields {
                id: "tool-skill",
                first_party: true,
                auto_inject: false,
                slash_command: "/tool-skill",
                allowed_tools: &signed_allowed_tools,
                url_patterns: &url_patterns,
                system_prompt: body,
            },
        );

        let dir = tempdir().unwrap();
        let file_path = dir.path().join("tool_skill.md");
        let content = format!(
            r#"---
id: tool-skill
name: Tool Skill
first_party: true
signature: {}
tools: [view_file, write_file]
---
{}
"#,
            sig_b64, body
        );
        std::fs::write(&file_path, content).unwrap();

        let mut manager = SkillsManager::new();
        manager.load_skills_from_dir(dir.path()).unwrap();

        assert!(manager.get_skill("tool-skill").is_none());

        clear_test_public_key();
    }

    #[test]
    fn test_first_party_signature_covers_url_patterns() {
        let seed = [4u8; 32];
        let signing_key = SigningKey::from_bytes(&seed);
        install_test_public_key(&signing_key);

        let body = "URL-scoped first-party guidance.";
        let allowed_tools: Vec<String> = Vec::new();
        let signed_url_patterns = string_vec(&["example.com/*"]);
        let sig_b64 = sign_test_skill(
            &signing_key,
            SkillSigningFields {
                id: "url-skill",
                first_party: true,
                auto_inject: true,
                slash_command: "/url-skill",
                allowed_tools: &allowed_tools,
                url_patterns: &signed_url_patterns,
                system_prompt: body,
            },
        );

        let dir = tempdir().unwrap();
        let file_path = dir.path().join("url_skill.md");
        let content = format!(
            r#"---
id: url-skill
name: URL Skill
first_party: true
auto_inject: true
url: example.com/*, evil.example/*
signature: {}
---
{}
"#,
            sig_b64, body
        );
        std::fs::write(&file_path, content).unwrap();

        let mut manager = SkillsManager::new();
        manager.load_skills_from_dir(dir.path()).unwrap();

        assert!(manager.get_skill("url-skill").is_none());

        clear_test_public_key();
    }

    fn test_skill(
        id: &str,
        name: &str,
        is_first_party: bool,
        url_patterns: Vec<&str>,
        system_prompt: &str,
    ) -> Skill {
        Skill {
            id: id.to_string(),
            name: name.to_string(),
            slash_command: format!("/{id}"),
            description: String::new(),
            system_prompt: system_prompt.to_string(),
            icon: "doc.text".to_string(),
            is_builtin: false,
            is_first_party,
            allowed_tools: vec!["browser_privileged_tool".to_string()],
            url_patterns: url_patterns.into_iter().map(String::from).collect(),
            auto_inject: true,
            required_capability_id: None,
            user_handoff: false,
        }
    }

    #[test]
    fn test_resolve_for_origin_returns_matching_auto_inject_skills() {
        let mut manager = SkillsManager::new();
        manager.skills.insert(
            "notion".to_string(),
            test_skill(
                "notion",
                "Notion",
                false,
                vec!["*.notion.so/*"],
                "Use Notion guidance.",
            ),
        );
        manager.skills.insert(
            "github".to_string(),
            test_skill(
                "github",
                "GitHub",
                false,
                vec!["github.com/*"],
                "Use GitHub guidance.",
            ),
        );

        let resolved = manager.resolve_for_origin("https://team.notion.so");

        assert_eq!(resolved.len(), 1);
        assert_eq!(resolved[0].id, "notion");
    }

    #[test]
    fn test_resolve_for_origin_returns_none_for_non_matching_origin() {
        let mut manager = SkillsManager::new();
        manager.skills.insert(
            "notion".to_string(),
            test_skill(
                "notion",
                "Notion",
                false,
                vec!["*.notion.so/*"],
                "Use Notion guidance.",
            ),
        );

        let resolved = manager.resolve_for_origin("https://example.com");

        assert!(resolved.is_empty());
    }

    #[test]
    fn test_origin_matching_never_accepts_page_content_as_origin() {
        let mut manager = SkillsManager::new();
        manager.skills.insert(
            "notion".to_string(),
            test_skill(
                "notion",
                "Notion",
                false,
                vec!["*.notion.so/*"],
                "Use Notion guidance.",
            ),
        );

        let resolved =
            manager.resolve_for_origin("This page mentions https://team.notion.so in its content");

        assert!(resolved.is_empty());
    }

    #[test]
    fn test_build_injected_context_orders_first_party_before_local_user() {
        let mut manager = SkillsManager::new();
        manager.skills.insert(
            "local".to_string(),
            test_skill(
                "local",
                "A Local",
                false,
                vec!["example.com/*"],
                "Local guidance.",
            ),
        );
        manager.skills.insert(
            "first-party".to_string(),
            test_skill(
                "first-party",
                "Z First Party",
                true,
                vec!["example.com/*"],
                "First-party guidance.",
            ),
        );

        let context = manager.build_injected_context("https://example.com", 10_000);

        let first_party_index = context.find("First-party guidance.").unwrap();
        let local_index = context.find("Local guidance.").unwrap();
        assert!(first_party_index < local_index);
    }

    #[test]
    fn test_build_injected_context_respects_token_budget_and_drops_lowest_precedence() {
        let mut manager = SkillsManager::new();
        manager.skills.insert(
            "first-party".to_string(),
            test_skill(
                "first-party",
                "First Party",
                true,
                vec!["example.com/*"],
                "Keep this high precedence guidance.",
            ),
        );
        manager.skills.insert(
            "local".to_string(),
            test_skill(
                "local",
                "Local",
                false,
                vec!["example.com/*"],
                "Drop this low precedence guidance.",
            ),
        );

        let context = manager.build_injected_context("https://example.com", 260);

        assert!(context.chars().count() <= 260);
        assert!(context.contains("Keep this high precedence guidance."));
        assert!(!context.contains("Drop this low precedence guidance."));
    }

    #[test]
    fn test_auto_inject_kill_switch_disables_resolution_and_context() {
        let mut manager = SkillsManager::new();
        manager.skills.insert(
            "notion".to_string(),
            test_skill(
                "notion",
                "Notion",
                false,
                vec!["*.notion.so/*"],
                "Use Notion guidance.",
            ),
        );

        manager.set_auto_inject_enabled(false);

        assert!(manager
            .resolve_for_origin("https://team.notion.so")
            .is_empty());
        assert!(manager
            .build_injected_context("https://team.notion.so", 10_000)
            .is_empty());
    }

    #[test]
    fn test_auto_inject_context_does_not_include_allowed_tools() {
        let mut manager = SkillsManager::new();
        manager.skills.insert(
            "notion".to_string(),
            test_skill(
                "notion",
                "Notion",
                false,
                vec!["*.notion.so/*"],
                "Use Notion guidance.",
            ),
        );

        let context = manager.build_injected_context("https://team.notion.so", 10_000);

        assert!(context.contains("Use Notion guidance."));
        assert!(!context.contains("browser_privileged_tool"));
    }

    #[test]
    fn test_load_skills_parses_auto_inject_frontmatter() {
        let dir = tempdir().unwrap();
        let file_path = dir.path().join("site_skill.md");
        let content = r#"---
id: site-skill
name: Site Skill
url: *.notion.so/*, docs.example.com/*
auto_inject: true
---
Use site guidance.
"#;
        std::fs::write(&file_path, content).unwrap();

        let mut manager = SkillsManager::new();
        manager.load_skills_from_dir(dir.path()).unwrap();

        let skill = manager.get_skill("site-skill").unwrap();
        assert!(skill.auto_inject);
        assert_eq!(
            skill.url_patterns,
            vec!["*.notion.so/*", "docs.example.com/*"]
        );
    }

    #[test]
    fn test_apply_config_from_path_loads_verified_json_blob() {
        let seed = [1u8; 32];
        let signing_key = SigningKey::from_bytes(&seed);
        install_test_public_key(&signing_key);

        let system_prompt = "Use first-party site guidance.";
        let allowed_tools = string_vec(&["read_current_page"]);
        let url_patterns = string_vec(&["example.com/*"]);
        let sig_b64 = sign_test_skill(
            &signing_key,
            SkillSigningFields {
                id: "config-skill",
                first_party: true,
                auto_inject: true,
                slash_command: "/config",
                allowed_tools: &allowed_tools,
                url_patterns: &url_patterns,
                system_prompt,
            },
        );
        let dir = tempdir().unwrap();
        let config_path = dir.path().join("skills.json");
        let blob = serde_json::json!({
            "version": 1,
            "min_brain_version": 1,
            "skills": [{
                "id": "config-skill",
                "name": "Config Skill",
                "slash_command": "/config",
                "description": "Loaded from config",
                "icon": "sparkles",
                "first_party": true,
                "signature": sig_b64,
                "url_patterns": ["example.com/*"],
                "auto_inject": true,
                "allowed_tools": ["read_current_page"],
                "system_prompt": system_prompt
            }]
        });
        std::fs::write(&config_path, blob.to_string()).unwrap();

        let mut manager = SkillsManager::new();
        let loaded = manager.apply_config_from_path(&config_path).unwrap();

        assert_eq!(loaded, 1);
        let skill = manager.get_skill("config-skill").unwrap();
        assert!(skill.is_first_party);
        assert!(skill.auto_inject);
        assert_eq!(skill.url_patterns, vec!["example.com/*"]);
        assert_eq!(skill.allowed_tools, vec!["read_current_page"]);

        clear_test_public_key();
    }

    #[test]
    fn test_apply_config_rejects_future_min_brain_version() {
        let dir = tempdir().unwrap();
        let config_path = dir.path().join("skills.json");
        let blob = serde_json::json!({
            "version": 1,
            "min_brain_version": COMPILED_BRAIN_CAPABILITY_VERSION + 1,
            "skills": [{
                "id": "future-skill",
                "name": "Future Skill",
                "description": "Requires a future brain capability",
                "icon": "sparkles",
                "first_party": false,
                "system_prompt": "Future-only guidance."
            }]
        });
        std::fs::write(&config_path, blob.to_string()).unwrap();

        let mut manager = SkillsManager {
            skills: HashMap::new(),
            available_capabilities: HashSet::new(),
            auto_inject_enabled: true,
        };
        let result = manager.apply_config_from_path(&config_path);

        assert!(result.is_err());
        assert!(manager.get_all_skills().is_empty());
    }

    #[test]
    fn test_apply_config_from_path_rejects_tampered_first_party_signature() {
        let seed = [2u8; 32];
        let signing_key = SigningKey::from_bytes(&seed);
        install_test_public_key(&signing_key);

        let signed_prompt = "Original first-party guidance.";
        let allowed_tools: Vec<String> = Vec::new();
        let url_patterns = string_vec(&["example.com/*"]);
        let sig_b64 = sign_test_skill(
            &signing_key,
            SkillSigningFields {
                id: "tampered-skill",
                first_party: true,
                auto_inject: true,
                slash_command: "/tampered-skill",
                allowed_tools: &allowed_tools,
                url_patterns: &url_patterns,
                system_prompt: signed_prompt,
            },
        );
        let dir = tempdir().unwrap();
        let config_path = dir.path().join("skills.json");
        let blob = serde_json::json!({
            "version": 1,
            "min_brain_version": 1,
            "skills": [{
                "id": "tampered-skill",
                "name": "Tampered Skill",
                "description": "Tampered",
                "icon": "warning",
                "first_party": true,
                "signature": sig_b64,
                "url_patterns": ["example.com/*"],
                "auto_inject": true,
                "allowed_tools": [],
                "system_prompt": "Tampered first-party guidance."
            }]
        });
        std::fs::write(&config_path, blob.to_string()).unwrap();

        let mut manager = SkillsManager::new();
        let result = manager.apply_config_from_path(&config_path);

        assert!(result.is_err());
        assert!(manager.get_skill("tampered-skill").is_none());

        clear_test_public_key();
    }

    #[test]
    fn test_capability_discovery_filtering_absent_and_present() {
        let manifest_json = r#"[
            {
                "id": "vault-skill",
                "name": "Vault Skill",
                "slash_command": "/vault",
                "description": "Vault management",
                "system_prompt": "Vault prompt",
                "icon": "key.fill",
                "required_capability_id": "vault_management",
                "user_handoff": false
            }
        ]"#;

        let mut manager = SkillsManager {
            skills: HashMap::new(),
            available_capabilities: HashSet::new(),
            auto_inject_enabled: true,
        };
        let (loaded, warnings) = manager.load_manifest(manifest_json);
        assert_eq!(loaded, 1);
        assert_eq!(warnings, 0);

        // Capability absent: skill must NOT be discoverable (assert both directions)
        let discovered_absent = manager.discover_skills(&[]);
        assert!(
            discovered_absent.is_empty(),
            "Skill requiring capability must NOT be discoverable when capability is absent"
        );

        let discovered_other = manager.discover_skills(&["other_capability"]);
        assert!(
            discovered_other.is_empty(),
            "Skill requiring capability must NOT be discoverable when only unrelated capability is present"
        );

        // Capability present: skill MUST be discoverable
        let discovered_present = manager.discover_skills(&["vault_management"]);
        assert_eq!(
            discovered_present.len(),
            1,
            "Skill requiring capability MUST be discoverable when capability is present"
        );
        assert_eq!(discovered_present[0].id, "vault-skill");
        assert_eq!(
            discovered_present[0].required_capability_id.as_deref(),
            Some("vault_management")
        );
    }

    #[test]
    fn test_manifest_user_handoff_without_capability_is_discoverable() {
        let manifest_json = r#"[
            {
                "id": "captcha-handoff",
                "name": "Challenge Handoff",
                "slash_command": "/captcha",
                "description": "Challenge user handoff",
                "system_prompt": "Handoff prompt",
                "icon": "shield",
                "required_capability_id": null,
                "user_handoff": true
            }
        ]"#;

        let mut manager = SkillsManager {
            skills: HashMap::new(),
            available_capabilities: HashSet::new(),
            auto_inject_enabled: true,
        };
        let (loaded, warnings) = manager.load_manifest(manifest_json);
        assert_eq!(loaded, 1);
        assert_eq!(warnings, 0);

        // With no capabilities available, user_handoff=true skill MUST be discoverable
        let discovered = manager.discover_skills(&[]);
        assert_eq!(
            discovered.len(),
            1,
            "Skill with user_handoff=true must be discoverable even with no capabilities"
        );
        assert_eq!(discovered[0].id, "captcha-handoff");
        assert!(
            discovered[0].user_handoff,
            "Discovered skill must carry user_handoff marker"
        );
        assert!(discovered[0].required_capability_id.is_none());
    }

    #[test]
    fn test_31_entry_builtin_manifest_roundtrip_and_filtering() {
        let builtins = builtin_manifest();
        assert_eq!(
            builtins.len(),
            31,
            "Builtin manifest must define exactly 31 launch target skills"
        );

        // Verify uniqueness of IDs and slash commands
        let mut id_set = std::collections::HashSet::new();
        let mut slash_set = std::collections::HashSet::new();
        for skill in &builtins {
            assert!(
                id_set.insert(&skill.id),
                "Duplicate skill id found in builtin manifest: {}",
                skill.id
            );
            assert!(
                slash_set.insert(&skill.slash_command),
                "Duplicate slash_command found in builtin manifest: {}",
                skill.slash_command
            );
            assert!(
                skill.required_capability_id.is_some() || skill.user_handoff,
                "Skill {} must declare required_capability_id OR explicit user_handoff=true",
                skill.id
            );
        }

        // Round-trip through serialization and parse_skill_manifest
        let manifest_json = serde_json::to_string_pretty(&builtins).unwrap();
        let (parsed_skills, warnings) = parse_skill_manifest(&manifest_json);
        assert_eq!(warnings, 0);
        assert_eq!(parsed_skills.len(), 31);

        // Registry population
        let mut manager = SkillsManager {
            skills: HashMap::new(),
            available_capabilities: HashSet::new(),
            auto_inject_enabled: true,
        };
        let (loaded, _) = manager.load_manifest(&manifest_json);
        assert_eq!(loaded, 31);
        assert_eq!(manager.get_all_skills().len(), 31);

        // Filter test: with NO capabilities, only user_handoff skills are discoverable
        let handoff_discovered = manager.discover_skills(&[]);
        let handoff_count = builtins.iter().filter(|s| s.user_handoff).count();
        assert_eq!(
            handoff_discovered.len(),
            handoff_count,
            "Zero capabilities should only discover user_handoff skills"
        );
        for skill in &handoff_discovered {
            assert!(
                skill.user_handoff,
                "Skill {} discovered without capabilities must have user_handoff=true",
                skill.id
            );
        }

        // Filter test: with all required capabilities available, all 31 skills must be discoverable
        let all_capabilities: Vec<&str> = builtins
            .iter()
            .filter_map(|s| s.required_capability_id.as_deref())
            .collect();
        let all_discovered = manager.discover_skills(&all_capabilities);
        assert_eq!(
            all_discovered.len(),
            31,
            "All 31 skills should be discoverable when all capabilities are available"
        );
    }

    #[test]
    fn test_manifest_parsing_ignores_unknown_fields_and_counts_malformed_lines() {
        // Line-based manifest with unknown fields, comments, and malformed lines
        let manifest_data = concat!(
            "# Builtin skill manifest\n",
            "{\"id\": \"skill-1\", \"name\": \"Skill 1\", \"unknown_extension_meta\": {\"foo\": \"bar\"}, \"future_field\": 123, \"user_handoff\": true}\n",
            "// Malformed line below\n",
            "NOT_JSON_AT_ALL_BROKEN_SYNTAX\n",
            "{\"id\": \"skill-2\", \"name\": \"Skill 2\", \"extra_flag\": true, \"required_capability_id\": \"cap_x\"}\n",
            "{\"broken_json\": \n",
            "{\"id\": \"skill-3\", \"name\": \"Skill 3\", \"slash_command\": \"/custom\", \"user_handoff\": true}\n"
        );

        let (skills, warnings) = parse_skill_manifest(manifest_data);
        assert_eq!(skills.len(), 3, "Should parse the 3 valid skill entries");
        assert_eq!(
            warnings, 2,
            "Should count exactly 2 warnings for the 2 malformed lines"
        );

        assert_eq!(skills[0].id, "skill-1");
        assert!(skills[0].user_handoff);
        assert_eq!(skills[1].id, "skill-2");
        assert_eq!(skills[1].required_capability_id.as_deref(), Some("cap_x"));
        assert_eq!(skills[2].id, "skill-3");
        assert_eq!(skills[2].slash_command, "/custom");
    }

    #[test]
    fn test_skills_manager_set_available_capabilities_activates_backed_skills_only() {
        let mut manager = SkillsManager::new();
        // Default with empty capabilities: only user_handoff skills are discoverable
        let default_discoverable = manager.get_discoverable_skills();
        let handoff_count = default_discoverable.len();
        assert!(handoff_count > 0);
        for s in &default_discoverable {
            assert!(s.user_handoff);
        }

        // Set capabilities: "web_search", "browser_history_search_desktop"
        manager.set_available_capabilities(vec![
            "web_search".to_string(),
            "browser_history_search_desktop".to_string(),
        ]);
        let discoverable_with_caps = manager.get_discoverable_skills();
        assert!(discoverable_with_caps.len() >= handoff_count);

        // Capabilities audit: Every non-handoff discoverable skill must match a provided capability
        for skill in &discoverable_with_caps {
            if !skill.user_handoff {
                let req = skill.required_capability_id.as_ref().unwrap();
                assert!(
                    req == "web_search" || req == "browser_history_search_desktop",
                    "Discovered skill {} must have matching capability",
                    skill.id
                );
            }
        }
    }
}
