use ed25519_dalek::Verifier;
use maho_types::events::core_update::SkillInfo;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
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

#[derive(Clone, Debug, Serialize, Deserialize)]
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

pub struct SkillsManager {
    skills: HashMap<String, Skill>,
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

        let builtins = vec![
            Skill {
                id: "summarize".into(),
                name: "Summarize".into(),
                slash_command: "/summarize".into(),
                description: "Summarize the current page".into(),
                system_prompt: "Summarize this page concisely. Focus on key points.\n\nPage content:\n{page_content}".into(),
                icon: "doc.text".into(),
                is_builtin: true,
                is_first_party: true,
                allowed_tools: vec![],
                url_patterns: vec![],
                auto_inject: false,
            },
            Skill {
                id: "translate".into(),
                name: "Translate".into(),
                slash_command: "/translate".into(),
                description: "Translate text to another language".into(),
                system_prompt: "Translate the following text to {user_input}. If no language specified, translate to English.\n\nPage content:\n{page_content}".into(),
                icon: "globe".into(),
                is_builtin: true,
                is_first_party: true,
                allowed_tools: vec![],
                url_patterns: vec![],
                auto_inject: false,
            },
            Skill {
                id: "explain".into(),
                name: "Explain".into(),
                slash_command: "/explain".into(),
                description: "Explain this page in simple terms".into(),
                system_prompt: "Explain this page's content in simple terms, as if to someone unfamiliar with the topic.\n\nPage content:\n{page_content}".into(),
                icon: "lightbulb".into(),
                is_builtin: true,
                is_first_party: true,
                allowed_tools: vec![],
                url_patterns: vec![],
                auto_inject: false,
            },
            Skill {
                id: "rewrite".into(),
                name: "Rewrite".into(),
                slash_command: "/rewrite".into(),
                description: "Rewrite text with improved clarity".into(),
                system_prompt: "Rewrite the following text: {user_input}. Improve clarity, grammar, and flow.\n\nPage content:\n{page_content}".into(),
                icon: "pencil.line".into(),
                is_builtin: true,
                is_first_party: true,
                allowed_tools: vec![],
                url_patterns: vec![],
                auto_inject: false,
            },
            Skill {
                id: "fact-check".into(),
                name: "Fact Check".into(),
                slash_command: "/fact-check".into(),
                description: "Analyze claims for accuracy".into(),
                system_prompt: "Analyze the claims in this page. Identify key claims, assess their likely accuracy, and note any that seem questionable.\n\nPage content:\n{page_content}".into(),
                icon: "checkmark.shield".into(),
                is_builtin: true,
                is_first_party: true,
                allowed_tools: vec![],
                url_patterns: vec![],
                auto_inject: false,
            },
            Skill {
                id: "code-review".into(),
                name: "Code Review".into(),
                slash_command: "/code-review".into(),
                description: "Review code for bugs and improvements".into(),
                system_prompt: "Review the code on this page. Identify bugs, suggest improvements, and note best practices.\n\nPage content:\n{page_content}".into(),
                icon: "chevron.left.forwardslash.chevron.right".into(),
                is_builtin: true,
                is_first_party: true,
                allowed_tools: vec![],
                url_patterns: vec![],
                auto_inject: false,
            },
        ];

        for skill in builtins {
            skills.insert(skill.id.clone(), skill);
        }

        Self {
            skills,
            auto_inject_enabled: true,
        }
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
}
