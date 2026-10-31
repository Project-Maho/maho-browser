use serde_json::Value;
use std::fs;
use std::path::Path;
use std::process::Command;
use tempfile::TempDir;

struct TestEnv {
    _home: TempDir,
    project: TempDir,
}

impl TestEnv {
    fn new() -> Self {
        Self {
            _home: tempfile::tempdir().unwrap(),
            project: tempfile::tempdir().unwrap(),
        }
    }

    fn run(&self, args: &[&str]) -> std::process::Output {
        Command::new(env!("CARGO_BIN_EXE_maho"))
            .args(args)
            .env("HOME", self._home.path())
            .output()
            .expect("run maho")
    }

    fn install(&self, agent: &str) -> std::process::Output {
        self.run(&[
            "skills",
            "install",
            "--agent",
            agent,
            "--project",
            self.project.path().to_str().unwrap(),
        ])
    }
}

#[test]
fn install_status_and_refresh_work_for_all_agents() {
    let env = TestEnv::new();
    for agent in ["claude-code", "codex", "cursor"] {
        assert!(env.install(agent).status.success(), "install {agent}");
        let status = env.run(&[
            "skills",
            "status",
            "--agent",
            agent,
            "--project",
            env.project.path().to_str().unwrap(),
        ]);
        assert!(status.status.success());
        assert!(String::from_utf8_lossy(&status.stdout).contains("Installed Maho browser skill v1"));
        let (skill_dir, config_path) = paths(&env, agent);
        let skill = fs::read_to_string(skill_dir.join("SKILL.md")).unwrap();
        assert!(skill.contains("version: 1"));
        assert!(env
            .run(&[
                "skills",
                "refresh",
                "--agent",
                agent,
                "--project",
                env.project.path().to_str().unwrap()
            ])
            .status
            .success());
        assert!(config_has_server(agent, &config_path));
    }
}

#[test]
fn install_preserves_other_mcp_servers_for_each_agent() {
    let env = TestEnv::new();
    for agent in ["claude-code", "codex", "cursor"] {
        let (_, path) = paths(&env, agent);
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        let original = match agent {
            "codex" => "[mcp_servers.other]\ncommand = 'other-tool'\nargs = ['keep']\n",
            _ => r#"{"mcpServers":{"other":{"command":"other-tool","args":["keep"]}},"custom":42}"#,
        };
        fs::write(&path, original).unwrap();
        assert!(env.install(agent).status.success());
        let text = fs::read_to_string(&path).unwrap();
        assert!(text.contains("other-tool"));
        if agent != "codex" {
            assert!(text.contains("\"custom\": 42"));
        }
        assert!(config_has_server(agent, &path));
    }
}

#[test]
fn malformed_config_fails_without_writing_skill_or_changing_config() {
    let env = TestEnv::new();
    for agent in ["claude-code", "codex", "cursor"] {
        let (skill, config) = paths(&env, agent);
        fs::create_dir_all(config.parent().unwrap()).unwrap();
        let malformed = "{not valid";
        fs::write(&config, malformed).unwrap();
        let result = env.install(agent);
        assert!(
            !result.status.success(),
            "malformed config must fail for {agent}"
        );
        assert_eq!(fs::read_to_string(&config).unwrap(), malformed);
        assert!(!skill.exists(), "must not write skill files for {agent}");
    }
}

#[test]
fn unknown_mcp_config_shape_fails_without_writing_skill_or_changing_config() {
    let env = TestEnv::new();
    for agent in ["claude-code", "codex", "cursor"] {
        let (skill, config) = paths(&env, agent);
        fs::create_dir_all(config.parent().unwrap()).unwrap();
        let invalid = if agent == "codex" {
            "mcp_servers = 'not-a-table'\n"
        } else {
            r#"{"mcpServers":"not-an-object"}"#
        };
        fs::write(&config, invalid).unwrap();
        let result = env.install(agent);
        assert!(
            !result.status.success(),
            "invalid MCP config shape must fail for {agent}"
        );
        assert_eq!(fs::read_to_string(&config).unwrap(), invalid);
        assert!(!skill.exists(), "must not write skill files for {agent}");
    }
}

#[test]
fn unsupported_agent_fails_and_cursor_without_project_installs_user_level() {
    let env = TestEnv::new();
    assert!(!env
        .run(&["skills", "install", "--agent", "unknown"])
        .status
        .success());
    assert!(env
        .run(&["skills", "install", "--agent", "cursor"])
        .status
        .success());
    assert!(!env.project.path().join(".cursor").exists());
    let home = env._home.path();
    assert!(home.join(".cursor/skills/maho-browser/SKILL.md").exists());
    let config = home.join(".cursor/mcp.json");
    assert!(config_has_server("cursor", &config));
    let value: Value = serde_json::from_str(&fs::read_to_string(&config).unwrap()).unwrap();
    assert_eq!(
        value["mcpServers"]["maho-browser"]["env"]["MAHO_MCP_PUBLIC_ENABLED"],
        "true"
    );
}

fn paths(env: &TestEnv, agent: &str) -> (std::path::PathBuf, std::path::PathBuf) {
    match agent {
        "claude-code" => (
            env._home.path().join(".claude/skills/maho-browser"),
            env._home.path().join(".claude.json"),
        ),
        "codex" => (
            env._home.path().join(".codex/skills/maho-browser"),
            env._home.path().join(".codex/config.toml"),
        ),
        "cursor" => (
            env.project.path().join(".cursor/rules/maho-browser"),
            env.project.path().join(".cursor/mcp.json"),
        ),
        _ => unreachable!(),
    }
}

fn config_has_server(agent: &str, path: &Path) -> bool {
    let text = fs::read_to_string(path).unwrap();
    if agent == "codex" {
        text.contains("[mcp_servers.maho-browser]")
            && text.contains("command = \"maho-browser-mcp\"")
    } else {
        let value: Value = serde_json::from_str(&text).unwrap();
        value["mcpServers"]["maho-browser"]["command"] == "maho-browser-mcp"
    }
}
