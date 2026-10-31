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
const SERVER_COMMAND: &str = "maho-browser-mcp";
/// Environment the MCP server needs to dispatch tool calls: without
/// `MAHO_MCP_PUBLIC_ENABLED` the server stays frozen (fail-closed), and
/// `MAHO_MCP_APPROVAL=browser` routes approvals through the Maho browser.
const PUBLIC_ENABLED_KEY: &str = "MAHO_MCP_PUBLIC_ENABLED";
const SERVER_ENV: [(&str, &str); 2] = [
    (PUBLIC_ENABLED_KEY, "true"),
    ("MAHO_MCP_APPROVAL", "browser"),
];

#[derive(Subcommand, Debug)]
pub enum SkillsCommands {
    /// Install the Maho browser skill into a supported coding agent
    Install {
        #[arg(long, value_parser = ["claude-code", "codex", "cursor", "opencode", "agents"])]
        agent: String,
        /// Project root; Cursor installs project-local rules when given, user-level otherwise
        #[arg(long)]
        project: Option<PathBuf>,
    },
    /// Show installation status
    Status {
        #[arg(long, value_parser = ["claude-code", "codex", "cursor", "opencode", "agents"])]
        agent: String,
        #[arg(long)]
        project: Option<PathBuf>,
    },
    /// Refresh installed skill files and MCP config
    Refresh {
        #[arg(long, value_parser = ["claude-code", "codex", "cursor", "opencode", "agents"])]
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
        SkillsCommands::Status { agent, project } => {
            println!("{}", status_message(&agent, project.as_deref())?);
            Ok(())
        }
    }
}

/// The full guide for this exact build: `SKILL.md` followed by every
/// site-class guide, framed by `[maho:guide v<version>] BEGIN` as the first
/// line and `[maho:guide v<version>] END` as the last line (no trailing
/// newline, so `println!` yields a clean last line).
pub fn guide_text() -> String {
    let version = env!("CARGO_PKG_VERSION");
    let mut out = format!("[maho:guide v{version}] BEGIN\n");
    out.push_str(SKILL.trim_end());
    out.push('\n');
    for (name, content) in GUIDES {
        out.push_str(&format!("\n<!-- file: {name} -->\n"));
        out.push_str(content.trim_end());
        out.push('\n');
    }
    out.push_str(&format!("[maho:guide v{version}] END"));
    out
}

/// How an agent stores its MCP server table.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum ConfigKind {
    /// JSON file with a top-level `mcpServers` object (Claude Code, Cursor).
    McpServersJson,
    /// TOML file with an `mcp_servers` table (Codex).
    CodexToml,
    /// JSON file with a top-level `mcp` object and OpenCode's server schema.
    OpencodeJson,
}

impl ConfigKind {
    fn servers_key(self) -> &'static str {
        match self {
            ConfigKind::McpServersJson => "mcpServers",
            ConfigKind::CodexToml => "mcp_servers",
            ConfigKind::OpencodeJson => "mcp",
        }
    }

    fn env_key(self) -> &'static str {
        match self {
            ConfigKind::OpencodeJson => "environment",
            _ => "env",
        }
    }

    /// The server entry this kind of config stores under `SERVER_NAME`.
    fn server_entry(self) -> Value {
        let env: serde_json::Map<String, Value> = SERVER_ENV
            .iter()
            .map(|(k, v)| ((*k).to_string(), json!(v)))
            .collect();
        match self {
            ConfigKind::OpencodeJson => json!({
                "type": "local",
                "command": [SERVER_COMMAND],
                "enabled": true,
                "environment": env,
            }),
            _ => json!({
                "command": SERVER_COMMAND,
                "args": [],
                "env": env,
            }),
        }
    }
}

/// Where one agent keeps its skill files and (optionally) its MCP config.
struct Target {
    skill_dir: PathBuf,
    config: Option<(PathBuf, ConfigKind)>,
}

fn locations(agent: &str, project: Option<&Path>) -> Result<Target> {
    let home = std::env::var_os("HOME")
        .map(PathBuf::from)
        .context("HOME is not set")?;
    let target = match agent {
        "claude-code" => Target {
            skill_dir: home.join(".claude/skills/maho-browser"),
            config: Some((home.join(".claude.json"), ConfigKind::McpServersJson)),
        },
        "codex" => Target {
            skill_dir: home.join(".codex/skills/maho-browser"),
            config: Some((home.join(".codex/config.toml"), ConfigKind::CodexToml)),
        },
        "cursor" => match project {
            Some(root) => Target {
                skill_dir: root.join(".cursor/rules/maho-browser"),
                config: Some((root.join(".cursor/mcp.json"), ConfigKind::McpServersJson)),
            },
            None => Target {
                skill_dir: home.join(".cursor/skills/maho-browser"),
                config: Some((home.join(".cursor/mcp.json"), ConfigKind::McpServersJson)),
            },
        },
        "opencode" => Target {
            skill_dir: home.join(".config/opencode/skills/maho-browser"),
            config: Some((
                home.join(".config/opencode/opencode.json"),
                ConfigKind::OpencodeJson,
            )),
        },
        "agents" => Target {
            skill_dir: home.join(".agents/skills/maho-browser"),
            config: None,
        },
        _ => bail!("unsupported agent '{agent}'"),
    };
    Ok(target)
}

fn install(agent: &str, project: Option<&Path>) -> Result<()> {
    let target = locations(agent, project)?;
    // Render the merged config before touching the disk so a malformed config
    // aborts the install with every file left exactly as it was.
    let rendered = match &target.config {
        Some((path, kind)) => Some((path, render_config(*kind, path)?)),
        None => None,
    };
    fs::create_dir_all(&target.skill_dir)
        .with_context(|| format!("create {}", target.skill_dir.display()))?;
    fs::write(target.skill_dir.join("SKILL.md"), SKILL)?;
    for (name, content) in GUIDES {
        fs::write(target.skill_dir.join(name), content)?;
    }
    crate::site_tools::install_site_packs(&target.skill_dir)?;
    if let Some((path, text)) = rendered {
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)?;
        }
        fs::write(path, text).with_context(|| format!("write {}", path.display()))?;
    }
    println!("Installed Maho browser skill v1 for {agent}.");
    Ok(())
}

fn status_message(agent: &str, project: Option<&Path>) -> Result<String> {
    let target = locations(agent, project)?;
    let skill = fs::read_to_string(target.skill_dir.join("SKILL.md")).ok();
    let not_installed = format!("Maho browser skill is not installed for {agent}.");
    let Some((path, kind)) = &target.config else {
        // Skill-files-only agents have no MCP config to check.
        return Ok(if skill.is_some() {
            format!("Installed Maho browser skill v1 for {agent}.")
        } else {
            not_installed
        });
    };
    let skill_ok = skill.is_some_and(|s| s.contains("version: 1"));
    let Some(entry) = read_server_entry(*kind, path)? else {
        return Ok(not_installed);
    };
    if !skill_ok {
        return Ok(not_installed);
    }
    let dispatch_capable = entry
        .get(kind.env_key())
        .and_then(Value::as_object)
        .is_some_and(|env| env.contains_key(PUBLIC_ENABLED_KEY));
    Ok(if dispatch_capable {
        format!("Installed Maho browser skill v1 for {agent}.")
    } else {
        format!(
            "Maho browser skill is installed for {agent}, but its MCP config does not set \
             {PUBLIC_ENABLED_KEY} so tool calls stay frozen; run `maho skills refresh --agent {agent}`."
        )
    })
}

fn read_text(path: &Path) -> Result<Option<String>> {
    if !path.exists() {
        return Ok(None);
    }
    fs::read_to_string(path)
        .map(Some)
        .with_context(|| format!("read {}", path.display()))
}

/// Parses a JSON config whose top level must be an object.
fn load_json_root(path: &Path) -> Result<serde_json::Map<String, Value>> {
    let Some(text) = read_text(path)? else {
        return Ok(serde_json::Map::new());
    };
    match serde_json::from_str::<Value>(&text)
        .with_context(|| format!("parse {}", path.display()))?
    {
        Value::Object(map) => Ok(map),
        _ => bail!(
            "{} must contain a JSON object at the top level",
            path.display()
        ),
    }
}

fn load_toml_root(path: &Path) -> Result<toml::Table> {
    let Some(text) = read_text(path)? else {
        return Ok(toml::Table::new());
    };
    toml::from_str(&text).with_context(|| format!("parse {}", path.display()))
}

/// Existing `maho-browser` server entry in the agent config, as JSON.
fn read_server_entry(kind: ConfigKind, path: &Path) -> Result<Option<Value>> {
    let key = kind.servers_key();
    match kind {
        ConfigKind::CodexToml => {
            let root = load_toml_root(path)?;
            Ok(root
                .get(key)
                .and_then(toml::Value::as_table)
                .and_then(|servers| servers.get(SERVER_NAME))
                .map(|entry| serde_json::to_value(entry))
                .transpose()?)
        }
        _ => {
            let root = load_json_root(path)?;
            Ok(root
                .get(key)
                .and_then(Value::as_object)
                .and_then(|servers| servers.get(SERVER_NAME))
                .cloned())
        }
    }
}

/// Merges the Maho server entry into the agent's existing config (all other
/// keys preserved) and returns the text to write. Never writes.
fn render_config(kind: ConfigKind, path: &Path) -> Result<String> {
    let key = kind.servers_key();
    let entry = kind.server_entry();
    match kind {
        ConfigKind::CodexToml => {
            let mut root = load_toml_root(path)?;
            let servers = root
                .entry(key)
                .or_insert_with(|| toml::Value::Table(toml::Table::new()))
                .as_table_mut()
                .with_context(|| format!("{key} in {} must be a table", path.display()))?;
            servers.insert(SERVER_NAME.into(), toml::Value::try_from(entry)?);
            Ok(toml::to_string_pretty(&root)?)
        }
        _ => {
            let mut root = load_json_root(path)?;
            let servers = root
                .entry(key)
                .or_insert_with(|| json!({}))
                .as_object_mut()
                .with_context(|| format!("{key} in {} must be an object", path.display()))?;
            servers.insert(SERVER_NAME.into(), entry);
            Ok(serde_json::to_string_pretty(&Value::Object(root))? + "\n")
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use clap::Parser;
    use std::ffi::OsString;
    use std::sync::{Mutex, MutexGuard};

    /// `HOME` is process-global: every test that touches it holds this lock.
    static HOME_LOCK: Mutex<()> = Mutex::new(());

    struct TempHome {
        dir: tempfile::TempDir,
        previous: Option<OsString>,
        _guard: MutexGuard<'static, ()>,
    }

    impl TempHome {
        fn new() -> Self {
            let guard = HOME_LOCK.lock().unwrap_or_else(|e| e.into_inner());
            let dir = tempfile::tempdir().unwrap();
            let previous = std::env::var_os("HOME");
            std::env::set_var("HOME", dir.path());
            Self {
                dir,
                previous,
                _guard: guard,
            }
        }

        fn path(&self, rel: &str) -> PathBuf {
            self.dir.path().join(rel)
        }

        fn write(&self, rel: &str, text: &str) -> PathBuf {
            let path = self.path(rel);
            fs::create_dir_all(path.parent().unwrap()).unwrap();
            fs::write(&path, text).unwrap();
            path
        }
    }

    impl Drop for TempHome {
        fn drop(&mut self) {
            match &self.previous {
                Some(value) => std::env::set_var("HOME", value),
                None => std::env::remove_var("HOME"),
            }
        }
    }

    fn read_json(path: &Path) -> Value {
        serde_json::from_str(&fs::read_to_string(path).unwrap()).unwrap()
    }

    fn read_toml(path: &Path) -> toml::Value {
        toml::from_str(&fs::read_to_string(path).unwrap()).unwrap()
    }

    fn env_json() -> Value {
        json!({"MAHO_MCP_PUBLIC_ENABLED": "true", "MAHO_MCP_APPROVAL": "browser"})
    }

    fn standard_entry() -> Value {
        json!({"command": "maho-browser-mcp", "args": [], "env": env_json()})
    }

    fn assert_skill_files(dir: &Path) {
        for name in ["SKILL.md", "search.md", "forms.md", "shopping.md"] {
            assert!(
                dir.join(name).is_file(),
                "{} missing",
                dir.join(name).display()
            );
        }
    }

    #[test]
    fn claude_code_entry_carries_dispatch_env() {
        let home = TempHome::new();
        install("claude-code", None).unwrap();
        let config = read_json(&home.path(".claude.json"));
        assert_eq!(config["mcpServers"]["maho-browser"], standard_entry());
        assert_skill_files(&home.path(".claude/skills/maho-browser"));
    }

    #[test]
    fn codex_entry_carries_dispatch_env() {
        let home = TempHome::new();
        install("codex", None).unwrap();
        let config = read_toml(&home.path(".codex/config.toml"));
        let entry = &config["mcp_servers"]["maho-browser"];
        assert_eq!(entry["command"].as_str(), Some("maho-browser-mcp"));
        assert_eq!(entry["args"].as_array().map(Vec::len), Some(0));
        let env = entry["env"].as_table().expect("env table");
        assert_eq!(env.len(), 2);
        assert_eq!(env["MAHO_MCP_PUBLIC_ENABLED"].as_str(), Some("true"));
        assert_eq!(env["MAHO_MCP_APPROVAL"].as_str(), Some("browser"));
        assert_skill_files(&home.path(".codex/skills/maho-browser"));
    }

    #[test]
    fn cursor_without_project_installs_user_level() {
        let home = TempHome::new();
        install("cursor", None).unwrap();
        let config = read_json(&home.path(".cursor/mcp.json"));
        assert_eq!(config["mcpServers"]["maho-browser"], standard_entry());
        assert_skill_files(&home.path(".cursor/skills/maho-browser"));
        assert!(!home.path(".cursor/rules").exists());
        assert!(status_message("cursor", None)
            .unwrap()
            .starts_with("Installed Maho browser skill v1"));
    }

    #[test]
    fn cursor_with_project_stays_project_local() {
        let home = TempHome::new();
        let project = tempfile::tempdir().unwrap();
        install("cursor", Some(project.path())).unwrap();
        let config = read_json(&project.path().join(".cursor/mcp.json"));
        assert_eq!(config["mcpServers"]["maho-browser"], standard_entry());
        assert_skill_files(&project.path().join(".cursor/rules/maho-browser"));
        assert!(
            !home.path(".cursor").exists(),
            "must not touch user-level cursor dir"
        );
        assert!(status_message("cursor", Some(project.path()))
            .unwrap()
            .starts_with("Installed Maho browser skill v1"));
        assert!(status_message("cursor", None)
            .unwrap()
            .contains("not installed"));
    }

    #[test]
    fn opencode_uses_mcp_key_with_local_schema() {
        let home = TempHome::new();
        install("opencode", None).unwrap();
        let config = read_json(&home.path(".config/opencode/opencode.json"));
        assert_eq!(
            config["mcp"]["maho-browser"],
            json!({
                "type": "local",
                "command": ["maho-browser-mcp"],
                "enabled": true,
                "environment": env_json(),
            })
        );
        assert!(config.get("mcpServers").is_none());
        assert_skill_files(&home.path(".config/opencode/skills/maho-browser"));
        assert!(status_message("opencode", None)
            .unwrap()
            .starts_with("Installed Maho browser skill v1"));
    }

    #[test]
    fn agents_target_writes_skill_files_only() {
        let home = TempHome::new();
        assert!(status_message("agents", None)
            .unwrap()
            .contains("not installed"));
        install("agents", None).unwrap();
        assert_skill_files(&home.path(".agents/skills/maho-browser"));
        let mut written = Vec::new();
        for entry in fs::read_dir(home.path(".agents")).unwrap() {
            written.push(entry.unwrap().file_name());
        }
        assert_eq!(
            written,
            vec![OsString::from("skills")],
            "no MCP config for agents"
        );
        assert_eq!(fs::read_dir(home.path("")).unwrap().count(), 1);
        assert!(status_message("agents", None)
            .unwrap()
            .starts_with("Installed Maho browser skill v1"));
    }

    #[test]
    fn install_preserves_every_other_key() {
        let home = TempHome::new();
        let claude = home.write(
            ".claude.json",
            r#"{"theme":"dark","nested":{"a":[1,2]},"mcpServers":{"other":{"command":"x","args":["y"]},"maho-browser":{"command":"stale"}}}"#,
        );
        let opencode = home.write(
            ".config/opencode/opencode.json",
            r#"{"theme":"tokyo","provider":{"p":{"k":1}},"mcp":{"other":{"type":"remote","url":"https://e.test"}}}"#,
        );
        let codex = home.write(
            ".codex/config.toml",
            "model = \"gpt\"\n[mcp_servers.other]\ncommand = \"other-tool\"\nargs = [\"keep\"]\n[profiles.p]\nmodel = \"m\"\n",
        );
        for agent in ["claude-code", "opencode", "codex"] {
            install(agent, None).unwrap();
        }

        let claude = read_json(&claude);
        assert_eq!(claude["theme"], "dark");
        assert_eq!(claude["nested"]["a"], json!([1, 2]));
        assert_eq!(
            claude["mcpServers"]["other"],
            json!({"command": "x", "args": ["y"]})
        );
        assert_eq!(claude["mcpServers"]["maho-browser"], standard_entry());

        let opencode = read_json(&opencode);
        assert_eq!(opencode["theme"], "tokyo");
        assert_eq!(opencode["provider"]["p"]["k"], 1);
        assert_eq!(opencode["mcp"]["other"]["url"], "https://e.test");
        assert_eq!(opencode["mcp"]["maho-browser"]["environment"], env_json());

        let codex = read_toml(&codex);
        assert_eq!(codex["model"].as_str(), Some("gpt"));
        assert_eq!(codex["profiles"]["p"]["model"].as_str(), Some("m"));
        assert_eq!(
            codex["mcp_servers"]["other"]["command"].as_str(),
            Some("other-tool")
        );
        assert_eq!(
            codex["mcp_servers"]["other"]["args"]
                .as_array()
                .map(Vec::len),
            Some(1)
        );
        assert_eq!(
            codex["mcp_servers"]["maho-browser"]["env"]["MAHO_MCP_PUBLIC_ENABLED"].as_str(),
            Some("true")
        );
    }

    #[test]
    fn refresh_is_idempotent() {
        let home = TempHome::new();
        install("claude-code", None).unwrap();
        let first = fs::read(home.path(".claude.json")).unwrap();
        install("claude-code", None).unwrap();
        assert_eq!(fs::read(home.path(".claude.json")).unwrap(), first);
    }

    #[test]
    fn unusable_config_fails_and_leaves_bytes_untouched() {
        let home = TempHome::new();
        let cases: &[(&str, &str, &str)] = &[
            ("claude-code", ".claude.json", "[1,2,3]"),
            ("claude-code", ".claude.json", "\"just a string\""),
            ("claude-code", ".claude.json", "null"),
            ("claude-code", ".claude.json", "{not valid json"),
            ("claude-code", ".claude.json", r#"{"mcpServers":"nope"}"#),
            ("cursor", ".cursor/mcp.json", "42"),
            ("opencode", ".config/opencode/opencode.json", "[]"),
            (
                "opencode",
                ".config/opencode/opencode.json",
                r#"{"mcp":[]}"#,
            ),
            (
                "codex",
                ".codex/config.toml",
                "mcp_servers = 'not-a-table'\n",
            ),
            ("codex", ".codex/config.toml", "= broken toml"),
        ];
        for (agent, rel, original) in cases {
            let path = home.write(rel, original);
            let skill_dir = locations(agent, None).unwrap().skill_dir;
            let err = install(agent, None).expect_err(&format!("{agent} {original}"));
            assert!(!err.to_string().is_empty());
            assert_eq!(
                fs::read(&path).unwrap(),
                original.as_bytes(),
                "{agent} config modified for {original:?}"
            );
            assert!(
                !skill_dir.exists(),
                "{agent} wrote skill files for {original:?}"
            );
        }
    }

    #[test]
    fn status_flags_legacy_config_without_dispatch_env() {
        let home = TempHome::new();
        install("claude-code", None).unwrap();
        home.write(
            ".claude.json",
            r#"{"mcpServers":{"maho-browser":{"command":"maho-browser-mcp","args":[]}}}"#,
        );
        let message = status_message("claude-code", None).unwrap();
        assert!(
            !message.starts_with("Installed Maho browser skill v1"),
            "{message}"
        );
        assert!(message.contains("MAHO_MCP_PUBLIC_ENABLED"), "{message}");
        install("claude-code", None).unwrap();
        assert!(status_message("claude-code", None)
            .unwrap()
            .starts_with("Installed Maho browser skill v1"));
    }

    #[test]
    fn status_reports_missing_install() {
        let _home = TempHome::new();
        for agent in ["claude-code", "codex", "cursor", "opencode", "agents"] {
            assert_eq!(
                status_message(agent, None).unwrap(),
                format!("Maho browser skill is not installed for {agent}.")
            );
        }
    }

    #[test]
    fn guide_text_is_framed_and_bounded() {
        let text = guide_text();
        let version = env!("CARGO_PKG_VERSION");
        let first = text.lines().next().unwrap();
        let last = text.lines().last().unwrap();
        assert_eq!(first, format!("[maho:guide v{version}] BEGIN"));
        assert_eq!(last, format!("[maho:guide v{version}] END"));
        assert!(
            text.chars().count() <= 25_000,
            "guide is {} chars",
            text.chars().count()
        );
        assert!(text.contains("name: maho-browser"));
        for (name, content) in GUIDES {
            assert!(
                text.contains(content.trim_end()),
                "{name} missing from guide"
            );
        }
    }

    #[test]
    fn skill_file_keeps_status_version_marker() {
        assert!(SKILL.contains("version: 1"));
        assert!(SKILL.contains("maho guide"));
    }

    #[derive(Parser)]
    struct TestCli {
        #[command(subcommand)]
        command: SkillsCommands,
    }

    #[test]
    fn every_subcommand_accepts_exactly_the_supported_agents() {
        for sub in ["install", "status", "refresh"] {
            for agent in ["claude-code", "codex", "cursor", "opencode", "agents"] {
                TestCli::try_parse_from(["maho", sub, "--agent", agent])
                    .unwrap_or_else(|e| panic!("{sub} --agent {agent}: {e}"));
            }
            assert!(TestCli::try_parse_from(["maho", sub, "--agent", "windsurf"]).is_err());
        }
    }
}
