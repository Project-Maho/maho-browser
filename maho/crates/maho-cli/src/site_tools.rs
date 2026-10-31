//! Seed site tool packs shipped with the Maho browser skill and the
//! `maho browser script` runner that executes page-API scripts.

use std::io::Read;
use std::path::{Path, PathBuf};

use anyhow::{bail, Context, Result};
use maho_agent::agent_tools::{
    validate_site_tool_entry, validate_site_tool_script, AgentToolsServer, NoSearchBackend,
    PeerFactory, StdioBrowserPeer, ToolError,
};
use serde_json::{json, Value};

/// Seed packs, as (relative path, contents) under `<skill>/sites/`.
pub const SITE_PACK_FILES: &[(&str, &str)] = &[
    (
        "github/manifest.json",
        include_str!("../skills/maho-browser/sites/github/manifest.json"),
    ),
    (
        "github/open_issues.js",
        include_str!("../skills/maho-browser/sites/github/open_issues.js"),
    ),
    (
        "github/search_repos.js",
        include_str!("../skills/maho-browser/sites/github/search_repos.js"),
    ),
    (
        "google/manifest.json",
        include_str!("../skills/maho-browser/sites/google/manifest.json"),
    ),
    (
        "google/web_search.js",
        include_str!("../skills/maho-browser/sites/google/web_search.js"),
    ),
    (
        "x/compose_post.js",
        include_str!("../skills/maho-browser/sites/x/compose_post.js"),
    ),
    (
        "x/manifest.json",
        include_str!("../skills/maho-browser/sites/x/manifest.json"),
    ),
    (
        "x/search_posts.js",
        include_str!("../skills/maho-browser/sites/x/search_posts.js"),
    ),
];

/// Every `site_tools` entry of every manifest, validated with its script.
pub fn validate_site_packs(files: &[(&str, &str)]) -> Result<usize> {
    let mut count = 0;
    for (path, text) in files.iter().filter(|(path, _)| path.ends_with("manifest.json")) {
        let site = path.trim_end_matches("/manifest.json");
        let manifest: Value =
            serde_json::from_str(text).with_context(|| format!("{path} is not valid JSON"))?;
        let tools = manifest["site_tools"]
            .as_array()
            .with_context(|| format!("{path} has no site_tools array"))?;
        for entry in tools {
            validate_site_tool_entry(entry).map_err(|error| anyhow::anyhow!("{path}: {error}"))?;
            let file = format!("{site}/{}", entry["file"].as_str().unwrap_or_default());
            let script = files
                .iter()
                .find(|(candidate, _)| *candidate == file)
                .map(|(_, script)| *script)
                .with_context(|| format!("{path}: missing script {file}"))?;
            validate_site_tool_script(script).map_err(|error| anyhow::anyhow!("{file}: {error}"))?;
            count += 1;
        }
    }
    Ok(count)
}

pub fn install_site_packs(skill_dir: &Path) -> Result<()> {
    validate_site_packs(SITE_PACK_FILES)?;
    for (path, text) in SITE_PACK_FILES {
        let target = skill_dir.join("sites").join(path);
        if let Some(parent) = target.parent() {
            std::fs::create_dir_all(parent)?;
        }
        std::fs::write(&target, text).with_context(|| format!("write {}", target.display()))?;
    }
    Ok(())
}

fn browser_mcp_binary() -> Result<PathBuf> {
    if let Some(path) = std::env::var_os(maho_agent::agent_tools::BROWSER_MCP_BINARY_ENV) {
        return Ok(PathBuf::from(path));
    }
    let mut candidates = Vec::new();
    if let Some(dir) = std::env::current_exe().ok().and_then(|exe| exe.parent().map(Path::to_path_buf)) {
        candidates.push(dir.join("maho-browser-mcp"));
    }
    candidates.push(PathBuf::from("/Applications/Maho.app/Contents/Helpers/maho-browser-mcp"));
    candidates
        .into_iter()
        .find(|path| path.exists())
        .context("maho-browser-mcp not found next to `maho` or in /Applications/Maho.app")
}

/// `maho browser script <file|->`: runs the script with the page API and
/// returns its last expression.
pub fn run_script_command(
    source: &str,
    input: Option<&str>,
    max_browser_calls: Option<u64>,
    site_tool: bool,
) -> Result<Value> {
    let script = if source == "-" {
        let mut text = String::new();
        std::io::stdin().read_to_string(&mut text).context("read script from stdin")?;
        text
    } else {
        std::fs::read_to_string(source).with_context(|| format!("read {source}"))?
    };
    if site_tool {
        validate_site_tool_script(&script).map_err(|error| anyhow::anyhow!("{error}"))?;
    }
    let input: Value = match input {
        Some(text) => serde_json::from_str(text).context("--input must be JSON")?,
        None => Value::Null,
    };
    let binary = browser_mcp_binary()?;
    let factory: PeerFactory = Box::new(move || {
        StdioBrowserPeer::spawn(&binary).map(|peer| Box::new(peer) as Box<dyn maho_agent::agent_tools::BrowserPeer>)
    });
    let server = AgentToolsServer::new(std::env::temp_dir(), factory, Box::new(NoSearchBackend));
    let mut arguments = json!({ "script": script, "input": input });
    if let Some(budget) = max_browser_calls {
        arguments["max_browser_calls"] = json!(budget);
    }
    server.run_script(&arguments).map_err(|error: ToolError| {
        anyhow::anyhow!("{}: {error}", error.code())
    })
}

pub fn ensure_script_source(source: &str) -> Result<()> {
    if source != "-" && !Path::new(source).is_file() {
        bail!("script file {source} does not exist");
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn seed_packs_are_valid() {
        assert_eq!(validate_site_packs(SITE_PACK_FILES).unwrap(), 5);
        for site in ["github", "google", "x"] {
            assert!(SITE_PACK_FILES
                .iter()
                .any(|(path, _)| *path == format!("{site}/manifest.json")));
        }
    }

    #[test]
    fn packs_with_ephemeral_refs_or_missing_scripts_are_rejected() {
        let manifest = r#"{"site":"t","site_tools":[{"name":"go","file":"go.js","kind":"action"}]}"#;
        let bad = [("t/manifest.json", manifest), ("t/go.js", "browser.call('browser_click', {ref: 3})")];
        assert!(validate_site_packs(&bad).is_err());
        let missing = [("t/manifest.json", manifest)];
        assert!(validate_site_packs(&missing).is_err());
    }
}
