use maho_types::events::core_update::SkillInfo;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Skill {
    pub id: String,
    pub name: String,
    pub slash_command: String,
    pub description: String,
    pub system_prompt: String,
    pub icon: String,
    pub is_builtin: bool,
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
            },
            Skill {
                id: "translate".into(),
                name: "Translate".into(),
                slash_command: "/translate".into(),
                description: "Translate text to another language".into(),
                system_prompt: "Translate the following text to {user_input}. If no language specified, translate to English.\n\nPage content:\n{page_content}".into(),
                icon: "globe".into(),
                is_builtin: true,
            },
            Skill {
                id: "explain".into(),
                name: "Explain".into(),
                slash_command: "/explain".into(),
                description: "Explain this page in simple terms".into(),
                system_prompt: "Explain this page's content in simple terms, as if to someone unfamiliar with the topic.\n\nPage content:\n{page_content}".into(),
                icon: "lightbulb".into(),
                is_builtin: true,
            },
            Skill {
                id: "rewrite".into(),
                name: "Rewrite".into(),
                slash_command: "/rewrite".into(),
                description: "Rewrite text with improved clarity".into(),
                system_prompt: "Rewrite the following text: {user_input}. Improve clarity, grammar, and flow.\n\nPage content:\n{page_content}".into(),
                icon: "pencil.line".into(),
                is_builtin: true,
            },
            Skill {
                id: "fact-check".into(),
                name: "Fact Check".into(),
                slash_command: "/fact-check".into(),
                description: "Analyze claims for accuracy".into(),
                system_prompt: "Analyze the claims in this page. Identify key claims, assess their likely accuracy, and note any that seem questionable.\n\nPage content:\n{page_content}".into(),
                icon: "checkmark.shield".into(),
                is_builtin: true,
            },
            Skill {
                id: "code-review".into(),
                name: "Code Review".into(),
                slash_command: "/code-review".into(),
                description: "Review code for bugs and improvements".into(),
                system_prompt: "Review the code on this page. Identify bugs, suggest improvements, and note best practices.\n\nPage content:\n{page_content}".into(),
                icon: "chevron.left.forwardslash.chevron.right".into(),
                is_builtin: true,
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
}
