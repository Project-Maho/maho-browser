use maho_types::events::core_update::SkillInfo;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use ed25519_dalek::Verifier;

const SKILL_PUBLIC_KEY: [u8; 32] = [
    0x8b, 0x07, 0x6b, 0x75, 0xcb, 0x78, 0x96, 0x81,
    0x09, 0x97, 0xc5, 0x9b, 0x1e, 0xc7, 0xe1, 0x84,
    0x73, 0x0d, 0x71, 0x4c, 0xa0, 0x67, 0x9e, 0x0d,
    0xea, 0xe2, 0x2f, 0x7a, 0xba, 0x0d, 0x4d, 0xbc
];

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
            },
        ];

        for skill in builtins {
            skills.insert(skill.id.clone(), skill);
        }

        Self { skills }
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

    pub fn load_skills_from_dir(&mut self, dir_path: &std::path::Path) -> Result<(), String> {
        if !dir_path.exists() {
            return Ok(());
        }
        let entries = std::fs::read_dir(dir_path)
            .map_err(|e| format!("failed to read directory: {e}"))?;
        for entry in entries {
            let entry = entry.map_err(|e| format!("failed to read directory entry: {e}"))?;
            let path = entry.path();
            if path.is_file() && path.extension().map_or(false, |ext| ext == "md") {
                let content = std::fs::read_to_string(&path)
                    .map_err(|e| format!("failed to read file {}: {e}", path.display()))?;
                
                if let Some((metadata, body_part)) = parse_skill_markdown(&content) {
                    let id = metadata.get("id").cloned().unwrap_or_else(|| {
                        path.file_stem().unwrap_or_default().to_string_lossy().to_string()
                    });
                    let name = metadata.get("name").cloned().unwrap_or_else(|| id.clone());
                    let slash_command = metadata.get("slash_command").cloned().unwrap_or_else(|| format!("/{id}"));
                    let description = metadata.get("description").cloned().unwrap_or_default();
                    let icon = metadata.get("icon").cloned().unwrap_or_else(|| "doc.text".to_string());
                    
                    let is_first_party = metadata.get("first_party")
                        .map(|v| v == "true")
                        .unwrap_or(false);
                    
                    if is_first_party {
                        let sig_b64 = match metadata.get("signature") {
                            Some(sig) => sig,
                            None => {
                                eprintln!("MahoConfigManager: Missing signature for first-party skill: {id}");
                                continue;
                            }
                        };
                        let sig_bytes = match base64::Engine::decode(&base64::prelude::BASE64_STANDARD, sig_b64) {
                            Ok(bytes) => bytes,
                            Err(e) => {
                                eprintln!("MahoConfigManager: Invalid base64 signature for first-party skill {id}: {e}");
                                continue;
                            }
                        };
                        let signature = match ed25519_dalek::Signature::from_slice(&sig_bytes) {
                            Ok(sig) => sig,
                            Err(e) => {
                                eprintln!("MahoConfigManager: Invalid signature format for first-party skill {id}: {e}");
                                continue;
                            }
                        };
                        let pubkey_bytes = {
                            #[cfg(test)]
                            {
                                TEST_SKILL_PUBLIC_KEY.with(|k| k.borrow().clone()).unwrap_or(SKILL_PUBLIC_KEY)
                            }
                            #[cfg(not(test))]
                            {
                                SKILL_PUBLIC_KEY
                            }
                        };
                        let verifying_key = match ed25519_dalek::VerifyingKey::from_bytes(&pubkey_bytes) {
                            Ok(key) => key,
                            Err(e) => {
                                eprintln!("MahoConfigManager: Invalid public key: {e}");
                                continue;
                            }
                        };
                        if let Err(e) = verifying_key.verify(body_part.as_bytes(), &signature) {
                            eprintln!("MahoConfigManager: Signature verification failed for first-party skill {id}: {e}");
                            continue;
                        }
                    }
                    
                    let tools_str = metadata.get("tools").cloned().unwrap_or_default();
                    let allowed_tools: Vec<String> = tools_str
                        .trim_matches(|c| c == '[' || c == ']')
                        .split(',')
                        .map(|s| s.trim().to_string())
                        .filter(|s| !s.is_empty())
                        .collect();
                    
                    self.skills.insert(id.clone(), Skill {
                        id,
                        name,
                        slash_command,
                        description,
                        system_prompt: body_part,
                        icon,
                        is_builtin: false,
                        is_first_party,
                        allowed_tools,
                    });
                }
            }
        }
        Ok(())
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
    use tempfile::tempdir;
    use ed25519_dalek::SigningKey;
    use ed25519_dalek::Signer;

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
        // Generate test keys
        let seed = [0u8; 32];
        let signing_key = SigningKey::from_bytes(&seed);
        let verifying_key = signing_key.verifying_key();
        
        // Inject public key into test hook
        TEST_SKILL_PUBLIC_KEY.with(|k| {
            *k.borrow_mut() = Some(verifying_key.to_bytes());
        });

        let body = "Hello first party!";
        let sig = signing_key.sign(body.as_bytes());
        let sig_b64 = base64::Engine::encode(&base64::prelude::BASE64_STANDARD, sig.to_bytes());

        let dir = tempdir().unwrap();
        let file_path = dir.path().join("first_party_skill.md");
        let content = format!(r#"---
id: fp-skill
name: First Party Skill
first_party: true
signature: {}
tools: [view_file, write_file]
---
{}
"#, sig_b64, body);
        std::fs::write(&file_path, content).unwrap();

        let mut manager = SkillsManager::new();
        manager.load_skills_from_dir(dir.path()).unwrap();

        let skill = manager.get_skill("fp-skill").unwrap();
        assert_eq!(skill.name, "First Party Skill");
        assert_eq!(skill.is_first_party, true);
        assert_eq!(skill.allowed_tools, vec!["view_file".to_string(), "write_file".to_string()]);
        assert_eq!(skill.system_prompt, body);

        // Reset test hook
        TEST_SKILL_PUBLIC_KEY.with(|k| {
            *k.borrow_mut() = None;
        });
    }

    #[test]
    fn test_load_skills_first_party_tampered_rejected() {
        // Generate test keys
        let seed = [0u8; 32];
        let signing_key = SigningKey::from_bytes(&seed);
        let verifying_key = signing_key.verifying_key();
        
        // Inject public key into test hook
        TEST_SKILL_PUBLIC_KEY.with(|k| {
            *k.borrow_mut() = Some(verifying_key.to_bytes());
        });

        let body = "Hello first party!";
        let sig = signing_key.sign(body.as_bytes());
        let sig_b64 = base64::Engine::encode(&base64::prelude::BASE64_STANDARD, sig.to_bytes());

        let dir = tempdir().unwrap();
        let file_path = dir.path().join("first_party_skill.md");
        // Tamper body to "Hello first party tampered!"
        let content = format!(r#"---
id: fp-skill
name: First Party Skill
first_party: true
signature: {}
---
Hello first party tampered!
"#, sig_b64);
        std::fs::write(&file_path, content).unwrap();

        let mut manager = SkillsManager::new();
        manager.load_skills_from_dir(dir.path()).unwrap();

        // Skill should be rejected and NOT loaded
        assert!(manager.get_skill("fp-skill").is_none());

        // Reset test hook
        TEST_SKILL_PUBLIC_KEY.with(|k| {
            *k.borrow_mut() = None;
        });
    }
}

