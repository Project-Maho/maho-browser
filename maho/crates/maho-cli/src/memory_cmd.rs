//! Read-only access to the Maho agent's omo memory repo (`maho memory ...`).
//!
//! The omo plugin is launched with `OMO_MEMORY_HOME=<agent_dir>/memory`. Each agent keeps a
//! git-backed markdown repo at `<memory_home>/agents/<agent-id>/repo`; every memory file is
//! markdown with a YAML frontmatter block that carries a `description`. Nothing here writes.

use anyhow::{bail, Context, Result};
use maho_agent::omo::factory::agent_dir_for_workspace;
use serde_json::{json, Value};
use std::fs;
use std::io::{self, Write};
use std::path::{Component, Path, PathBuf};

const SNIPPET_WIDTH: usize = 120;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum MemoryAction {
    List,
    Search { query: String, limit: usize },
    Show { path: String },
}

/// One memory file. `path` is relative to the agent's repo and always uses `/` separators.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MemoryItem {
    pub agent: String,
    pub path: String,
    pub description: String,
    pub snippet: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MemoryFile {
    pub agent: String,
    pub path: String,
    pub content: String,
}

/// Where the omo plugin keeps memory for a workspace (`OMO_MEMORY_HOME`).
pub fn default_memory_home(workspace_root: &Path) -> PathBuf {
    agent_dir_for_workspace(workspace_root).join("memory")
}

pub fn run(action: MemoryAction, memory_home: &Path, json: bool) -> Result<()> {
    let output = execute(&action, memory_home, json)?;
    write_stdout(&output)
}

/// Renders the output of `action` without printing, so it can be tested.
pub fn execute(action: &MemoryAction, memory_home: &Path, json: bool) -> Result<String> {
    match action {
        MemoryAction::List => {
            let items = list_items(memory_home)?;
            render_items(&items, json, multiple_agents(memory_home)?)
        }
        MemoryAction::Search { query, limit } => {
            let items = search_items(memory_home, query, *limit)?;
            render_items(&items, json, multiple_agents(memory_home)?)
        }
        MemoryAction::Show { path } => {
            let file = read_file(memory_home, path)?;
            if json {
                json_line(&json!({ "path": file.path, "content": file.content }))
            } else {
                Ok(file.content)
            }
        }
    }
}

/// Every agent that has a repo, sorted by agent id. A missing `agents/` dir means no agents.
pub fn agent_repos(memory_home: &Path) -> Result<Vec<(String, PathBuf)>> {
    let agents_dir = memory_home.join("agents");
    let entries = match fs::read_dir(&agents_dir) {
        Ok(entries) => entries,
        Err(err) if err.kind() == io::ErrorKind::NotFound => return Ok(Vec::new()),
        Err(err) => {
            return Err(err).with_context(|| format!("failed to read {}", agents_dir.display()))
        }
    };
    let mut repos = Vec::new();
    for entry in entries {
        let entry = entry.with_context(|| format!("failed to read {}", agents_dir.display()))?;
        let Some(agent) = entry.file_name().to_str().map(str::to_owned) else {
            continue;
        };
        let repo = entry.path().join("repo");
        if repo.is_dir() {
            repos.push((agent, repo));
        }
    }
    repos.sort_by(|a, b| a.0.cmp(&b.0));
    Ok(repos)
}

/// Every `.md` file under every `agents/*/repo` (skipping `.git`), sorted by agent then path.
pub fn list_items(memory_home: &Path) -> Result<Vec<MemoryItem>> {
    let mut items = Vec::new();
    for (agent, repo) in agent_repos(memory_home)? {
        for (path, abs) in markdown_files(&repo)? {
            let content = read_lossy(&abs)?;
            let (frontmatter, _) = split_frontmatter(&content);
            items.push(MemoryItem {
                agent: agent.clone(),
                path,
                description: frontmatter.map(frontmatter_description).unwrap_or_default(),
                snippet: None,
            });
        }
    }
    Ok(items)
}

/// Case-insensitive substring search over path, description and body (frontmatter excluded).
/// Ranked by total number of hits, ties broken by agent then path.
pub fn search_items(memory_home: &Path, query: &str, limit: usize) -> Result<Vec<MemoryItem>> {
    let needle = query.trim().to_lowercase();
    if needle.is_empty() {
        bail!("search query must not be empty");
    }
    let mut hits: Vec<(usize, MemoryItem)> = Vec::new();
    for (agent, repo) in agent_repos(memory_home)? {
        for (path, abs) in markdown_files(&repo)? {
            let content = read_lossy(&abs)?;
            let (frontmatter, body) = split_frontmatter(&content);
            let description = frontmatter.map(frontmatter_description).unwrap_or_default();
            let score = count_hits(&path, &needle)
                + count_hits(&description, &needle)
                + count_hits(body, &needle);
            if score == 0 {
                continue;
            }
            let snippet = body
                .lines()
                .find(|line| line.to_lowercase().contains(&needle))
                .map(|line| make_snippet(line, &needle));
            hits.push((
                score,
                MemoryItem {
                    agent: agent.clone(),
                    path,
                    description,
                    snippet,
                },
            ));
        }
    }
    hits.sort_by(|a, b| {
        b.0.cmp(&a.0)
            .then_with(|| a.1.agent.cmp(&b.1.agent))
            .then_with(|| a.1.path.cmp(&b.1.path))
    });
    Ok(hits.into_iter().take(limit).map(|(_, item)| item).collect())
}

/// Reads one memory file. `spec` is a repo-relative path, optionally prefixed `<agent>:` to pick
/// an agent; without a prefix the path must exist in exactly one agent's repo.
pub fn read_file(memory_home: &Path, spec: &str) -> Result<MemoryFile> {
    let (agent_hint, raw_path) = split_agent_prefix(memory_home, spec);
    let rel = validate_relative(raw_path)?;
    let repos: Vec<_> = agent_repos(memory_home)?
        .into_iter()
        .filter(|(agent, _)| agent_hint.as_deref().is_none_or(|hint| hint == agent))
        .collect();

    let mut found: Vec<(String, PathBuf)> = Vec::new();
    for (agent, repo) in &repos {
        let candidate = repo.join(&rel);
        let Ok(canonical) = fs::canonicalize(&candidate) else {
            continue;
        };
        let canonical_repo = fs::canonicalize(repo)
            .with_context(|| format!("failed to resolve {}", repo.display()))?;
        if !canonical.starts_with(&canonical_repo) {
            bail!("memory path escapes the memory repo: {spec}");
        }
        if canonical.is_file() {
            found.push((agent.clone(), canonical));
        }
    }

    let path = rel_to_string(&rel);
    match found.len() {
        0 => bail!("memory file not found: {spec}"),
        1 => {
            let (agent, abs) = found.remove(0);
            Ok(MemoryFile {
                agent,
                path,
                content: read_lossy(&abs)?,
            })
        }
        _ => {
            let agents: Vec<_> = found.iter().map(|(agent, _)| agent.as_str()).collect();
            bail!(
                "memory path {path} exists in several agents ({}); use <agent>:{path}",
                agents.join(", ")
            )
        }
    }
}

fn multiple_agents(memory_home: &Path) -> Result<bool> {
    Ok(agent_repos(memory_home)?.len() > 1)
}

fn render_items(items: &[MemoryItem], json: bool, label_agent: bool) -> Result<String> {
    if json {
        let items: Vec<Value> = items.iter().map(item_json).collect();
        return json_line(&json!({ "items": items }));
    }
    let mut out = String::new();
    for item in items {
        let label = if label_agent {
            format!("{}:{}", item.agent, item.path)
        } else {
            item.path.clone()
        };
        if item.description.is_empty() {
            out.push_str(&label);
        } else {
            out.push_str(&format!("{label} - {}", item.description));
        }
        out.push('\n');
        if let Some(snippet) = &item.snippet {
            out.push_str(&format!("    {snippet}\n"));
        }
    }
    Ok(out)
}

fn item_json(item: &MemoryItem) -> Value {
    let mut value = json!({
        "agent": item.agent,
        "path": item.path,
        "description": item.description,
    });
    if let Some(snippet) = &item.snippet {
        value["snippet"] = Value::String(snippet.clone());
    }
    value
}

fn json_line(value: &Value) -> Result<String> {
    Ok(format!("{}\n", serde_json::to_string_pretty(value)?))
}

fn write_stdout(output: &str) -> Result<()> {
    if output.is_empty() {
        return Ok(());
    }
    let mut stdout = io::stdout().lock();
    match stdout
        .write_all(output.as_bytes())
        .and_then(|()| stdout.flush())
    {
        Ok(()) => Ok(()),
        Err(err) if err.kind() == io::ErrorKind::BrokenPipe => Ok(()),
        Err(err) => Err(err.into()),
    }
}

fn read_lossy(path: &Path) -> Result<String> {
    let bytes = fs::read(path).with_context(|| format!("failed to read {}", path.display()))?;
    Ok(String::from_utf8_lossy(&bytes).into_owned())
}

/// Repo-relative `(path, absolute path)` of every `.md` file, `.git` skipped, symlinks not
/// followed, sorted by path.
fn markdown_files(repo: &Path) -> Result<Vec<(String, PathBuf)>> {
    let mut files = Vec::new();
    let mut stack = vec![(repo.to_path_buf(), String::new())];
    while let Some((dir, prefix)) = stack.pop() {
        let entries =
            fs::read_dir(&dir).with_context(|| format!("failed to read {}", dir.display()))?;
        for entry in entries {
            let entry = entry.with_context(|| format!("failed to read {}", dir.display()))?;
            let Some(name) = entry.file_name().to_str().map(str::to_owned) else {
                continue;
            };
            if name == ".git" {
                continue;
            }
            let rel = if prefix.is_empty() {
                name.clone()
            } else {
                format!("{prefix}/{name}")
            };
            let file_type = entry
                .file_type()
                .with_context(|| format!("failed to stat {}", entry.path().display()))?;
            if file_type.is_dir() {
                stack.push((entry.path(), rel));
            } else if file_type.is_file()
                && Path::new(&name)
                    .extension()
                    .is_some_and(|ext| ext.eq_ignore_ascii_case("md"))
            {
                files.push((rel, entry.path()));
            }
        }
    }
    files.sort_by(|a, b| a.0.cmp(&b.0));
    Ok(files)
}

/// Splits `<agent>:<path>` when `<agent>` names an existing agent repo.
fn split_agent_prefix<'a>(memory_home: &Path, spec: &'a str) -> (Option<String>, &'a str) {
    if let Some((agent, rest)) = spec.split_once(':') {
        let is_plain_name = !agent.is_empty() && !agent.contains(['/', '\\']) && agent != "..";
        if is_plain_name && memory_home.join("agents").join(agent).join("repo").is_dir() {
            return (Some(agent.to_owned()), rest);
        }
    }
    (None, spec)
}

/// Rejects anything that is not a plain path inside the repo: absolute paths, `..`, `.git`.
fn validate_relative(raw: &str) -> Result<PathBuf> {
    let mut rel = PathBuf::new();
    for component in Path::new(raw).components() {
        match component {
            Component::Normal(part) => {
                if part.eq_ignore_ascii_case(".git") {
                    bail!("memory path escapes the memory repo: {raw}");
                }
                rel.push(part);
            }
            Component::CurDir => {}
            Component::ParentDir | Component::RootDir | Component::Prefix(_) => {
                bail!("memory path escapes the memory repo: {raw}")
            }
        }
    }
    if rel.as_os_str().is_empty() {
        bail!("memory path must not be empty");
    }
    Ok(rel)
}

fn rel_to_string(rel: &Path) -> String {
    rel.components()
        .map(|component| component.as_os_str().to_string_lossy().into_owned())
        .collect::<Vec<_>>()
        .join("/")
}

/// Returns `(frontmatter, body)`. Content without a closed `---` block has no frontmatter.
fn split_frontmatter(content: &str) -> (Option<&str>, &str) {
    let content = content.strip_prefix('\u{feff}').unwrap_or(content);
    let Some(rest) = content
        .strip_prefix("---\n")
        .or_else(|| content.strip_prefix("---\r\n"))
    else {
        return (None, content);
    };
    let mut offset = 0;
    for line in rest.split_inclusive('\n') {
        if line.trim_end() == "---" {
            return (Some(&rest[..offset]), &rest[offset + line.len()..]);
        }
        offset += line.len();
    }
    (None, content)
}

/// Reads the top-level `description` key as one line. Handles plain, quoted and block scalars.
fn frontmatter_description(frontmatter: &str) -> String {
    let mut lines = frontmatter.lines().peekable();
    while let Some(line) = lines.next() {
        let Some(value) = line.strip_prefix("description:") else {
            continue;
        };
        let value = value.trim();
        let is_block = value.starts_with(['>', '|'])
            && value[1..]
                .chars()
                .all(|c| c == '+' || c == '-' || c.is_ascii_digit());
        if let Some(text) = unquote(value) {
            return collapse_whitespace(&text);
        }
        let mut parts = Vec::new();
        if !is_block {
            parts.push(strip_comment(value).to_owned());
        }
        while let Some(next) = lines.peek() {
            if next.starts_with([' ', '\t']) || (is_block && next.trim().is_empty()) {
                parts.push(next.trim().to_owned());
                lines.next();
            } else {
                break;
            }
        }
        return collapse_whitespace(&parts.join(" "));
    }
    String::new()
}

fn unquote(value: &str) -> Option<String> {
    let inner = |quote: char| {
        value
            .strip_prefix(quote)
            .and_then(|rest| rest.strip_suffix(quote))
    };
    if let Some(text) = inner('"') {
        return Some(text.replace("\\\"", "\"").replace("\\\\", "\\"));
    }
    inner('\'').map(|text| text.replace("''", "'"))
}

fn strip_comment(value: &str) -> &str {
    value.find(" #").map_or(value, |at| value[..at].trim_end())
}

fn collapse_whitespace(text: &str) -> String {
    text.split_whitespace().collect::<Vec<_>>().join(" ")
}

fn count_hits(haystack: &str, needle_lower: &str) -> usize {
    haystack.to_lowercase().matches(needle_lower).count()
}

/// One line around the first match, whitespace collapsed, at most ~`SNIPPET_WIDTH` chars.
fn make_snippet(line: &str, needle_lower: &str) -> String {
    let line = collapse_whitespace(line);
    let chars: Vec<(usize, char)> = line.char_indices().collect();
    if chars.len() <= SNIPPET_WIDTH {
        return line;
    }
    let lower = line.to_lowercase();
    let match_char = if lower.len() == line.len() {
        lower
            .find(needle_lower)
            .filter(|at| line.is_char_boundary(*at))
            .map_or(0, |at| line[..at].chars().count())
    } else {
        0
    };
    let start = match_char
        .saturating_sub(SNIPPET_WIDTH / 4)
        .min(chars.len() - SNIPPET_WIDTH);
    let end = start + SNIPPET_WIDTH;
    let from = chars[start].0;
    let to = chars.get(end).map_or(line.len(), |(at, _)| *at);
    let mut snippet = String::new();
    if start > 0 {
        snippet.push_str("...");
    }
    snippet.push_str(&line[from..to]);
    if end < chars.len() {
        snippet.push_str("...");
    }
    snippet
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    fn write(home: &Path, rel: &str, content: &str) {
        let path = home.join(rel);
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, content).unwrap();
    }

    fn memory_file(description: &str, body: &str) -> String {
        format!("---\ndescription: {description}\nkind: note\n---\n{body}\n")
    }

    /// Two agents with nested dirs; `.git`, non-markdown files, runtime state and an agent
    /// without a repo must all be ignored.
    fn fixture() -> TempDir {
        let dir = TempDir::new().unwrap();
        let home = dir.path();
        let alpha = "agents/alpha/repo";
        write(
            home,
            &format!("{alpha}/system/human.md"),
            &memory_file("Person - Human", "Likes Rust and tea."),
        );
        write(
            home,
            &format!("{alpha}/skills/rust-style/SKILL.md"),
            &memory_file(
                "\"Rust style: formatting, naming\"",
                "Rust rust RUST everywhere.\nSecond line.",
            ),
        );
        write(
            home,
            &format!("{alpha}/notes/deep/nested/idea.md"),
            "---\ndescription: >\n  A folded\n  description line\n---\nOne mention of rust here.\n",
        );
        write(
            home,
            &format!("{alpha}/plain.md"),
            "No frontmatter, mentions tea.\n",
        );
        write(
            home,
            &format!("{alpha}/.git/config.md"),
            "rust inside git must be skipped\n",
        );
        write(
            home,
            &format!("{alpha}/readme.txt"),
            "rust but not markdown\n",
        );

        let beta = "agents/beta/repo";
        write(
            home,
            &format!("{beta}/system/human.md"),
            &memory_file("Person - Beta human", "Prefers tea over coffee."),
        );
        write(
            home,
            &format!("{beta}/project.md"),
            &memory_file("'It''s a project'", "Project notes."),
        );
        write(
            home,
            "agents/beta/runtime/ignored.md",
            "rust in runtime must be skipped\n",
        );
        fs::create_dir_all(home.join("agents/gamma/runtime")).unwrap();
        dir
    }

    fn paths(items: &[MemoryItem]) -> Vec<String> {
        items
            .iter()
            .map(|item| format!("{}:{}", item.agent, item.path))
            .collect()
    }

    #[test]
    fn default_memory_home_is_agent_dir_memory() {
        assert_eq!(
            default_memory_home(Path::new("/tmp/ws")),
            Path::new("/tmp/ws/.maho/omo-agent/memory")
        );
    }

    #[test]
    fn list_walks_every_agent_repo_and_reads_descriptions() {
        let dir = fixture();
        let items = list_items(dir.path()).unwrap();
        assert_eq!(
            paths(&items),
            vec![
                "alpha:notes/deep/nested/idea.md",
                "alpha:plain.md",
                "alpha:skills/rust-style/SKILL.md",
                "alpha:system/human.md",
                "beta:project.md",
                "beta:system/human.md",
            ]
        );
        let described: Vec<_> = items.iter().map(|i| i.description.as_str()).collect();
        assert_eq!(
            described,
            vec![
                "A folded description line",
                "",
                "Rust style: formatting, naming",
                "Person - Human",
                "It's a project",
                "Person - Beta human",
            ]
        );
        assert!(items.iter().all(|item| item.snippet.is_none()));
    }

    #[test]
    fn missing_or_empty_memory_home_lists_nothing() {
        let dir = TempDir::new().unwrap();
        let missing = dir.path().join("does-not-exist");
        assert!(list_items(&missing).unwrap().is_empty());
        assert_eq!(execute(&MemoryAction::List, &missing, false).unwrap(), "");
        assert_eq!(
            serde_json::from_str::<Value>(&execute(&MemoryAction::List, &missing, true).unwrap())
                .unwrap(),
            json!({ "items": [] })
        );

        fs::create_dir_all(dir.path().join("agents")).unwrap();
        assert!(list_items(dir.path()).unwrap().is_empty());
        let search = MemoryAction::Search {
            query: "rust".into(),
            limit: 5,
        };
        assert_eq!(execute(&search, &missing, false).unwrap(), "");
    }

    #[test]
    fn list_text_prefixes_agent_only_when_several_agents_exist() {
        let dir = fixture();
        let text = execute(&MemoryAction::List, dir.path(), false).unwrap();
        assert!(text.contains("alpha:system/human.md - Person - Human\n"));
        assert!(text.contains("alpha:plain.md\n"));

        let single = TempDir::new().unwrap();
        write(
            single.path(),
            "agents/solo/repo/system/human.md",
            &memory_file("Person - Human", "x"),
        );
        let text = execute(&MemoryAction::List, single.path(), false).unwrap();
        assert_eq!(text, "system/human.md - Person - Human\n");
    }

    #[test]
    fn list_json_shape() {
        let dir = fixture();
        let value: Value =
            serde_json::from_str(&execute(&MemoryAction::List, dir.path(), true).unwrap()).unwrap();
        let items = value["items"].as_array().unwrap();
        assert_eq!(items.len(), 6);
        assert_eq!(
            items[3],
            json!({"agent": "alpha", "path": "system/human.md", "description": "Person - Human"})
        );
    }

    #[test]
    fn search_ranks_by_hit_count_case_insensitively() {
        let dir = fixture();
        let items = search_items(dir.path(), "RuSt", 10).unwrap();
        // skills/rust-style: path 1 + description 1 + body 3 = 5; idea.md: body 1;
        // alpha human.md: body 1. Ties break by agent then path.
        assert_eq!(
            paths(&items),
            vec![
                "alpha:skills/rust-style/SKILL.md",
                "alpha:notes/deep/nested/idea.md",
                "alpha:system/human.md",
            ]
        );
        assert_eq!(items[0].description, "Rust style: formatting, naming");
        assert_eq!(
            items[0].snippet.as_deref(),
            Some("Rust rust RUST everywhere.")
        );
        assert_eq!(items[2].snippet.as_deref(), Some("Likes Rust and tea."));
    }

    #[test]
    fn search_matches_path_description_and_body_but_not_frontmatter_keys() {
        let dir = fixture();
        // description-only match, no body snippet
        let by_description = search_items(dir.path(), "folded", 10).unwrap();
        assert_eq!(
            paths(&by_description),
            vec!["alpha:notes/deep/nested/idea.md"]
        );
        assert!(by_description[0].snippet.is_none());
        // path-only match
        let by_path = search_items(dir.path(), "project.md", 10).unwrap();
        assert_eq!(paths(&by_path), vec!["beta:project.md"]);
        // body match in a file without frontmatter
        let by_body = search_items(dir.path(), "coffee", 10).unwrap();
        assert_eq!(paths(&by_body), vec!["beta:system/human.md"]);
        // `kind: note` lives in frontmatter and is not part of the body
        assert!(search_items(dir.path(), "kind", 10).unwrap().is_empty());
        // .git, runtime and non-markdown content never match
        assert!(search_items(dir.path(), "skipped", 10).unwrap().is_empty());
        assert!(search_items(dir.path(), "not markdown", 10)
            .unwrap()
            .is_empty());
    }

    #[test]
    fn search_limit_and_validation() {
        let dir = fixture();
        assert_eq!(search_items(dir.path(), "tea", 2).unwrap().len(), 2);
        assert!(search_items(dir.path(), "tea", 0).unwrap().is_empty());
        assert!(search_items(dir.path(), "no-such-term", 5)
            .unwrap()
            .is_empty());
        let err = search_items(dir.path(), "   ", 5).unwrap_err();
        assert!(err.to_string().contains("must not be empty"));
    }

    #[test]
    fn search_output_text_and_json() {
        let dir = fixture();
        let action = MemoryAction::Search {
            query: "rust".into(),
            limit: 1,
        };
        let text = execute(&action, dir.path(), false).unwrap();
        assert_eq!(
            text,
            "alpha:skills/rust-style/SKILL.md - Rust style: formatting, naming\n    Rust rust RUST everywhere.\n"
        );
        let value: Value =
            serde_json::from_str(&execute(&action, dir.path(), true).unwrap()).unwrap();
        assert_eq!(
            value,
            json!({"items": [{
                "agent": "alpha",
                "path": "skills/rust-style/SKILL.md",
                "description": "Rust style: formatting, naming",
                "snippet": "Rust rust RUST everywhere.",
            }]})
        );
    }

    #[test]
    fn long_lines_are_trimmed_around_the_match() {
        let long = format!("{} needle {}", "a".repeat(300), "b".repeat(300));
        let snippet = make_snippet(&long, "needle");
        assert!(snippet.contains("needle"));
        assert!(snippet.starts_with("..."));
        assert!(snippet.ends_with("..."));
        assert!(snippet.chars().count() <= SNIPPET_WIDTH + 6);
        // multi-byte text must not panic or split a char
        let unicode = format!("{}needle{}", "é".repeat(200), "ü".repeat(200));
        assert!(make_snippet(&unicode, "needle").contains("needle"));
    }

    #[test]
    fn show_reads_file_by_unique_path_and_agent_prefix() {
        let dir = fixture();
        let file = read_file(dir.path(), "skills/rust-style/SKILL.md").unwrap();
        assert_eq!(file.agent, "alpha");
        assert_eq!(file.path, "skills/rust-style/SKILL.md");
        assert!(file.content.starts_with("---\ndescription: \"Rust style"));

        let file = read_file(dir.path(), "./project.md").unwrap();
        assert_eq!(
            (file.agent.as_str(), file.path.as_str()),
            ("beta", "project.md")
        );

        let alpha = read_file(dir.path(), "alpha:system/human.md").unwrap();
        let beta = read_file(dir.path(), "beta:system/human.md").unwrap();
        assert!(alpha.content.contains("Likes Rust"));
        assert!(beta.content.contains("Prefers tea"));

        let err = read_file(dir.path(), "system/human.md")
            .unwrap_err()
            .to_string();
        assert!(err.contains("alpha, beta"), "{err}");
        assert!(err.contains("<agent>:system/human.md"), "{err}");
    }

    #[test]
    fn show_output_text_is_exact_content_and_json_has_path_and_content() {
        let dir = fixture();
        let action = MemoryAction::Show {
            path: "alpha:plain.md".into(),
        };
        assert_eq!(
            execute(&action, dir.path(), false).unwrap(),
            "No frontmatter, mentions tea.\n"
        );
        let value: Value =
            serde_json::from_str(&execute(&action, dir.path(), true).unwrap()).unwrap();
        assert_eq!(
            value,
            json!({"path": "plain.md", "content": "No frontmatter, mentions tea.\n"})
        );
    }

    #[test]
    fn show_rejects_paths_escaping_the_repo() {
        let dir = fixture();
        write(dir.path(), "secret.md", "top secret\n");
        write(dir.path(), "agents/alpha/secret.md", "alpha secret\n");
        let outside = dir.path().join("secret.md");
        for bad in [
            "../secret.md",
            "../../secret.md",
            "notes/../../secret.md",
            "alpha:../secret.md",
            "alpha:../../secret.md",
            "../repo/system/human.md",
            ".git/config.md",
            "alpha:.git/config.md",
            outside.to_str().unwrap(),
            "/etc/passwd",
            "alpha:/etc/passwd",
        ] {
            let err = read_file(dir.path(), bad).unwrap_err().to_string();
            assert!(err.contains("escapes"), "{bad}: {err}");
        }
        // rejected before any lookup, so a missing memory home gives the same error
        let missing = dir.path().join("nope");
        let err = read_file(&missing, "../x.md").unwrap_err().to_string();
        assert!(err.contains("escapes"), "{err}");
    }

    #[cfg(unix)]
    #[test]
    fn show_rejects_symlinks_that_leave_the_repo() {
        let dir = fixture();
        write(dir.path(), "outside/secret.md", "top secret\n");
        let repo = dir.path().join("agents/alpha/repo");
        std::os::unix::fs::symlink(dir.path().join("outside/secret.md"), repo.join("link.md"))
            .unwrap();
        std::os::unix::fs::symlink(dir.path().join("outside"), repo.join("linkdir")).unwrap();

        let err = read_file(dir.path(), "link.md").unwrap_err().to_string();
        assert!(err.contains("escapes"), "{err}");
        let err = read_file(dir.path(), "linkdir/secret.md")
            .unwrap_err()
            .to_string();
        assert!(err.contains("escapes"), "{err}");
        // listing does not follow or report symlinks
        let items = list_items(dir.path()).unwrap();
        assert!(items
            .iter()
            .all(|i| i.path != "link.md" && !i.path.starts_with("linkdir")));
    }

    #[test]
    fn show_reports_missing_empty_and_directory_paths() {
        let dir = fixture();
        let err = read_file(dir.path(), "nope.md").unwrap_err().to_string();
        assert!(err.contains("not found"), "{err}");
        let err = read_file(dir.path(), "alpha:system")
            .unwrap_err()
            .to_string();
        assert!(err.contains("not found"), "{err}");
        for empty in ["", ".", "alpha:"] {
            let err = read_file(dir.path(), empty).unwrap_err().to_string();
            assert!(err.contains("must not be empty"), "{empty}: {err}");
        }
        let missing = dir.path().join("nope");
        let err = read_file(&missing, "system/human.md")
            .unwrap_err()
            .to_string();
        assert!(err.contains("not found"), "{err}");
    }

    #[test]
    fn frontmatter_parsing_edge_cases() {
        let (fm, body) = split_frontmatter("---\r\ndescription: Hi\r\n---\r\nBody\r\n");
        assert_eq!(frontmatter_description(fm.unwrap()), "Hi");
        assert_eq!(body, "Body\r\n");

        let (fm, body) = split_frontmatter("---\ndescription: unclosed\nBody");
        assert!(fm.is_none());
        assert!(body.starts_with("---"));

        let (fm, body) = split_frontmatter("\u{feff}---\ndescription: bom\n---\nB");
        assert_eq!(frontmatter_description(fm.unwrap()), "bom");
        assert_eq!(body, "B");

        assert_eq!(frontmatter_description("name: x\nkind: y"), "");
        assert_eq!(
            frontmatter_description("description: plain # comment"),
            "plain"
        );
        assert_eq!(
            frontmatter_description("description: wraps\n  onto next\nkind: x"),
            "wraps onto next"
        );
        assert_eq!(
            frontmatter_description("description: |\n  line one\n  line two\nkind: x"),
            "line one line two"
        );
        assert_eq!(
            frontmatter_description("description: \"say \\\"hi\\\"\""),
            "say \"hi\""
        );
        assert_eq!(
            frontmatter_description("  description: nested\ndescription: top"),
            "top"
        );
    }
}
