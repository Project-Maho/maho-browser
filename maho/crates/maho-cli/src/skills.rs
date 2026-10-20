use anyhow::{bail, Context, Result};
use clap::Subcommand;
use serde_json::{json, Value};
use std::fs;
use std::path::{Path, PathBuf};

const SKILL: &str = include_str!("../skills/maho-browser/SKILL.md");
const GUIDES: &[(&str, &str)] = &[
    (
        "search.md",
        include_str!("../skills/maho-browser/search.md"),
    ),
    ("forms.md", include_str!("../skills/maho-browser/forms.md")),
    (
        "shopping.md",
        include_str!("../skills/maho-browser/shopping.md"),
    ),
];
const SERVER_NAME: &str = "maho-browser";
const SERVER_CONFIG: &str = r#"{"command":"maho-browser-mcp","args":[]}"#;

#[derive(Subcommand, Debug)]
pub enum SkillsCommands {
    /// Install the Maho browser skill into a supported coding agent
    Install {
        #[arg(long, value_parser = ["claude-code", "codex", "cursor"])]
        agent: String,
        /// Project root; required for Cursor's project-local rules
        #[arg(long)]
        project: Option<PathBuf>,
    },
    /// Show installation status
    Status {
        #[arg(long, value_parser = ["claude-code", "codex", "cursor"])]
        agent: String,
        #[arg(long)]
        project: Option<PathBuf>,
    },
    /// Refresh installed skill files and MCP config
    Refresh {
        #[arg(long, value_parser = ["claude-code", "codex", "cursor"])]
        agent: String,
        #[arg(long)]
        project: Option<PathBuf>,
    },
}

pub fn run(command: SkillsCommands) -> Result<()> {
    match command {
        SkillsCommands::Install { agent, project } | SkillsCommands::Refresh { agent, project } => {
            install(&agent, project.as_deref())
        }
        SkillsCommands::Status { agent, project } => status(&agent, project.as_deref()),
    }
}

fn locations(agent: &str, project: Option<&Path>) -> Result<(PathBuf, PathBuf)> {
    let home = std::env::var_os("HOME")
        .map(PathBuf::from)
        .context("HOME is not set")?;
    let (skill, config) = match agent {
        "claude-code" => (
            home.join(".claude/skills/maho-browser"),
            home.join(".claude.json"),
        ),
        "codex" => (
            home.join(".codex/skills/maho-browser"),
            home.join(".codex/config.toml"),
        ),
        "cursor" => {
            let root = project.context("Cursor requires an explicit --project root")?;
            (
                root.join(".cursor/rules/maho-browser"),
                root.join(".cursor/mcp.json"),
            )
        }
        _ => bail!("unsupported agent '{agent}'"),
    };
    Ok((skill, config))
}

fn install(agent: &str, project: Option<&Path>) -> Result<()> {
    let (skill_dir, config_path) = locations(agent, project)?;
    let config = merged_config(agent, &config_path)?;
    fs::create_dir_all(&skill_dir).with_context(|| format!("create {}", skill_dir.display()))?;
    fs::write(skill_dir.join("SKILL.md"), SKILL)?;
    for (name, content) in GUIDES {
        fs::write(skill_dir.join(name), content)?;
    }
    if let Some(parent) = config_path.parent() {
        fs::create_dir_all(parent)?;
    }
    match agent {
        "codex" => fs::write(&config_path, toml::to_string_pretty(&config)?)?,
        _ => fs::write(&config_path, serde_json::to_vec_pretty(&config)?)?,
    }
    println!("Installed Maho browser skill v1 for {agent}.");
    Ok(())
}

fn status(agent: &str, project: Option<&Path>) -> Result<()> {
    let (skill_dir, config_path) = locations(agent, project)?;
    let skill = fs::read_to_string(skill_dir.join("SKILL.md")).unwrap_or_default();
    let config = read_config(agent, &config_path)?;
    let server_exists = match agent {
        "codex" => config
            .get("mcp_servers")
            .and_then(Value::as_object)
            .map(|m| m.contains_key(SERVER_NAME))
            .unwrap_or(false),
        _ => config
            .get("mcpServers")
            .and_then(Value::as_object)
            .map(|m| m.contains_key(SERVER_NAME))
            .unwrap_or(false),
    };
    if skill.contains("version: 1") && server_exists {
        println!("Installed Maho browser skill v1 for {agent}.");
    } else {
        println!("Maho browser skill is not installed for {agent}.");
    }
    Ok(())
}

fn read_config(agent: &str, path: &Path) -> Result<Value> {
    if !path.exists() {
        return Ok(json!({}));
    }
    let text = fs::read_to_string(path).with_context(|| format!("read {}", path.display()))?;
    match agent {
        "codex" => toml::from_str(&text).with_context(|| format!("parse {}", path.display())),
        _ => serde_json::from_str(&text).with_context(|| format!("parse {}", path.display())),
    }
}

fn merged_config(agent: &str, path: &Path) -> Result<Value> {
    let mut config = read_config(agent, path)?;
    let key = if agent == "codex" {
        "mcp_servers"
    } else {
        "mcpServers"
    };
    if !config.is_object() {
        bail!("{} must contain a config object", path.display());
    }
    let root = config.as_object_mut().unwrap();
    let servers = root.entry(key).or_insert_with(|| json!({}));
    if agent == "codex" {
        if !servers.is_object() {
            bail!("{key} in {} must be a table", path.display());
        }
        servers
            .as_object_mut()
            .unwrap()
            .insert(SERVER_NAME.into(), serde_json::from_str(SERVER_CONFIG)?);
    } else {
        if !servers.is_object() {
            bail!("{key} in {} must be an object", path.display());
        }
        servers
            .as_object_mut()
            .unwrap()
            .insert(SERVER_NAME.into(), serde_json::from_str(SERVER_CONFIG)?);
    }
    Ok(config)
}
