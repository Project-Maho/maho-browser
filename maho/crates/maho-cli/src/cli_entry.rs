use std::ffi::{OsStr, OsString};

use anyhow::{bail, Result};
use serde_json::{json, Value};

use crate::browser::refactor::{scroll_args, SEMANTIC_MARKER};

const DEFAULT_AGENT_SYSTEM_PROMPT: &str = "You are Maho's browser agent, driving a real browser through tools.\n\
Use the browser tools to inspect and act on live pages; never answer a question about page content from memory.\n\
Your final message must contain the actual answer: quote the exact text, value, or result you observed. Saying that you found or verified something without stating it is a failed response.\n\
If a task cannot be completed, say precisely what blocked it.";

const ROBOTS_POLICY_REQUIRE_CONSENT: &str = "robots.txt policy: Do not intentionally bypass or ignore robots.txt unless the user explicitly approves that bypass. If the requested task requires a robots.txt bypass and no approval is present in the conversation, ask for explicit approval and stop before bypassing. After the user explicitly approves, you may continue in the same persisted session; robots.txt is not a permanent hard block.";
const ROBOTS_POLICY_PREAPPROVED: &str = "robots.txt policy: The user explicitly consented in this CLI invocation to bypassing or ignoring robots.txt when the requested task requires it. This consent is limited to robots.txt; all other browser capability, permission, authentication, and safety boundaries still apply. Prefer compliant access when it can complete the task, but robots.txt is not a hard block for this run.";

fn is_http_url(value: &str) -> bool {
    let value = value.trim();
    value.starts_with("http://") || value.starts_with("https://")
}

fn remove_known_robots_policy(base: &str) -> String {
    let mut cleaned = base.to_string();
    for policy in [ROBOTS_POLICY_REQUIRE_CONSENT, ROBOTS_POLICY_PREAPPROVED] {
        cleaned = cleaned.replace(policy, "");
    }
    cleaned.trim().to_string()
}

fn compose_agent_system_prompt(base: &str, allow_robots_bypass: bool) -> String {
    let robots_policy = if allow_robots_bypass {
        ROBOTS_POLICY_PREAPPROVED
    } else {
        ROBOTS_POLICY_REQUIRE_CONSENT
    };
    let base = remove_known_robots_policy(base);
    if base.is_empty() {
        robots_policy.to_string()
    } else {
        format!("{}\n\n{}", base.trim_end(), robots_policy)
    }
}

pub(crate) fn effective_agent_system_prompt(allow_robots_bypass: bool) -> String {
    let base = std::env::var("MAHO_AGENT_SYSTEM_PROMPT")
        .ok()
        .filter(|prompt| !prompt.trim().is_empty())
        .unwrap_or_else(|| DEFAULT_AGENT_SYSTEM_PROMPT.to_string());
    compose_agent_system_prompt(&base, allow_robots_bypass)
}

fn os(value: &str) -> OsString {
    OsString::from(value)
}

fn arg_text(arg: &OsString) -> String {
    arg.to_string_lossy().into_owned()
}

fn arg_is(arg: &OsString, expected: &str) -> bool {
    arg.as_os_str() == OsStr::new(expected)
}

fn arg_utf8(arg: &OsString) -> Option<&str> {
    arg.to_str()
}

fn is_global_flag_without_value(arg: &OsString) -> bool {
    arg_is(arg, "--json") || arg_is(arg, "--isolated")
}

fn is_global_flag_with_value(arg: &OsString) -> bool {
    arg_is(arg, "--socket-path") || arg_is(arg, "--proxy-url") || arg_is(arg, "--relay-url")
}

fn is_global_assignment(arg: &OsString) -> bool {
    arg_utf8(arg).is_some_and(|value| {
        ["--socket-path=", "--proxy-url=", "--relay-url="]
            .iter()
            .any(|prefix| value.starts_with(prefix))
    })
}

fn is_agent_session_assignment(arg: &OsString) -> bool {
    arg_utf8(arg).is_some_and(|value| value.starts_with("--session="))
}

fn extract_root_robots_consent(args: Vec<OsString>) -> (Vec<OsString>, bool) {
    if args.len() <= 1 {
        return (args, false);
    }

    let mut cleaned = Vec::with_capacity(args.len());
    cleaned.push(args[0].clone());
    let mut allow_robots_bypass = false;
    let mut index = 1usize;

    // Only the root/global prefix is eligible. Once a subcommand/prompt or `--`
    // is reached, every remaining byte is positional/subcommand data and is
    // preserved exactly. Values belonging to global options are never inspected
    // as flags, so a value literally named `--allow-robots-bypass` stays a value.
    while index < args.len() {
        let current = &args[index];
        if arg_is(current, "--") {
            cleaned.extend_from_slice(&args[index..]);
            break;
        }
        if arg_is(current, "--allow-robots-bypass") {
            allow_robots_bypass = true;
            index += 1;
            continue;
        }
        if is_global_flag_without_value(current) || is_global_assignment(current) {
            cleaned.push(current.clone());
            index += 1;
            continue;
        }
        if is_global_flag_with_value(current) {
            cleaned.push(current.clone());
            if let Some(value) = args.get(index + 1) {
                cleaned.push(value.clone());
                index += 2;
            } else {
                index += 1;
            }
            continue;
        }
        // `--session` is an agent-first root option, so it participates in the
        // root prefix for consent scanning even though it is normalized to the
        // existing `run --session` surface later.
        if arg_is(current, "--session") {
            cleaned.push(current.clone());
            if let Some(value) = args.get(index + 1) {
                cleaned.push(value.clone());
                index += 2;
            } else {
                index += 1;
            }
            continue;
        }
        if is_agent_session_assignment(current) {
            cleaned.push(current.clone());
            index += 1;
            continue;
        }

        cleaned.extend_from_slice(&args[index..]);
        break;
    }

    (cleaned, allow_robots_bypass)
}

fn first_root_token(args: &[OsString]) -> Option<usize> {
    let mut index = 1usize;
    while index < args.len() {
        let current = &args[index];
        if arg_is(current, "--") {
            return Some(index);
        }
        if is_global_flag_without_value(current) || is_global_assignment(current) {
            index += 1;
            continue;
        }
        if is_global_flag_with_value(current) {
            index += if args.get(index + 1).is_some() { 2 } else { 1 };
            continue;
        }
        return Some(index);
    }
    None
}

fn is_existing_subcommand(value: &OsString) -> bool {
    [
        "auth",
        "chat",
        "billing",
        "tab",
        "history",
        "bookmarks",
        "mail",
        "page",
        "headless",
        "browser",
        "mcp",
        "routine",
        "vault",
        "version",
        "run",
        "agent",
        "tool",
        "session",
        "artifact",
        "research",
        "login",
        "signup",
        "logout",
        "whoami",
        "config",
        "balance",
        "buy-credits",
    ]
    .iter()
    .any(|candidate| arg_is(value, candidate))
}

fn semantic_verb(value: &OsString) -> Option<&'static str> {
    ["click", "type", "key", "scroll", "hover", "select"]
        .into_iter()
        .find(|candidate| arg_is(value, candidate))
}

fn semantic_help(verb: &str) -> Option<&'static str> {
    match verb {
        "click" => Some("Usage: maho click [--tab <id>] <selector>\n\nClick an element selected by CSS. Use @<ref> from a fresh accessibility snapshot when duplicate controls are indistinguishable."),
        "type" => Some("Usage: maho type [--tab <id>] [--submit] <text>\n\nType into the currently focused element. --submit presses Enter after typing."),
        "key" => Some("Usage: maho key [--tab <id>] <key-name>\n\nPress a browser key such as Enter or ArrowDown."),
        "scroll" => Some("Usage: maho scroll [--tab <id>] [--amount N | --to top|bottom]\n\nScroll vertically. Positive --amount scrolls down; negative scrolls up; the default is 300 pixels."),
        "hover" => Some("Usage: maho hover [--tab <id>] <selector>\n\nHover an element selected by CSS. Use @<ref> as an explicit accessibility-ref selector when needed."),
        "select" => Some("Usage: maho select [--tab <id>] <selector> <value>\n\nSelect a value in an element selected by CSS. Use @<ref> as an explicit accessibility-ref selector when needed."),
        _ => None,
    }
}

fn extra_root_help() -> &'static str {
    "\nAgent-first entry:\n  maho <prompt>                         Run the autonomous agent (same as `maho run <prompt>`)\n  maho -- <prompt>                      Treat a leading-dash value as the agent prompt\n  maho --session <id> <prompt>          Continue a persisted agent session\n  maho --session=<id> <prompt>          Equivalent session continuation spelling\n  maho <http(s)://url>                  Open the URL in a new browser tab\n\nSemantic browser interaction:\n  maho click [--tab <id>] <selector>\n  maho type [--tab <id>] [--submit] <text>\n  maho key [--tab <id>] <key-name>\n  maho scroll [--tab <id>] [--amount N | --to top|bottom]\n  maho hover [--tab <id>] <selector>\n  maho select [--tab <id>] <selector> <value>\n\nConsent:\n  --allow-robots-bypass                 Root option: explicit robots.txt bypass consent for this agent invocation\n"
}

enum ParseAction {
    Parse(Vec<OsString>),
    RootHelp,
    PrintAndExit(String),
}

struct PreprocessedArgs {
    action: ParseAction,
    allow_robots_bypass: bool,
}

fn take_value(tokens: &[OsString], index: &mut usize, option: &str) -> Result<String> {
    *index += 1;
    tokens
        .get(*index)
        .map(arg_text)
        .ok_or_else(|| anyhow::anyhow!("{option} requires a value"))
}

fn parse_i64_option(value: &str, option: &str) -> Result<i64> {
    value
        .parse::<i64>()
        .map_err(|_| anyhow::anyhow!("{option} expects an integer, got '{value}'"))
}

fn build_semantic_payload(
    verb: &str,
    semantic_tokens: &[OsString],
) -> Result<(String, Value, Vec<OsString>)> {
    let mut tab_id: Option<i64> = None;
    let mut submit = false;
    let mut amount: Option<i64> = None;
    let mut to: Option<String> = None;
    let mut positional: Vec<String> = Vec::new();
    let mut global_tail: Vec<OsString> = Vec::new();
    let mut positional_only = false;
    let mut index = 0usize;

    while index < semantic_tokens.len() {
        let current = &semantic_tokens[index];
        let token = arg_text(current);
        if !positional_only && arg_is(current, "--") {
            positional_only = true;
            index += 1;
            continue;
        }
        if !positional_only && is_global_flag_without_value(current) {
            global_tail.push(current.clone());
            index += 1;
            continue;
        }
        if !positional_only && is_global_assignment(current) {
            global_tail.push(current.clone());
            index += 1;
            continue;
        }
        if !positional_only && is_global_flag_with_value(current) {
            global_tail.push(current.clone());
            let value = semantic_tokens
                .get(index + 1)
                .ok_or_else(|| anyhow::anyhow!("{token} requires a value"))?
                .clone();
            global_tail.push(value);
            index += 2;
            continue;
        }
        if !positional_only && (arg_is(current, "-h") || arg_is(current, "--help")) {
            if let Some(help) = semantic_help(verb) {
                bail!("__MAHO_HELP__{help}");
            }
        }
        if !positional_only && arg_is(current, "--tab") {
            let value = take_value(semantic_tokens, &mut index, "--tab")?;
            tab_id = Some(parse_i64_option(&value, "--tab")?);
            index += 1;
            continue;
        }
        if !positional_only && token.starts_with("--tab=") {
            tab_id = Some(parse_i64_option(&token[6..], "--tab")?);
            index += 1;
            continue;
        }
        if !positional_only && arg_is(current, "--submit") {
            submit = true;
            index += 1;
            continue;
        }
        if !positional_only && arg_is(current, "--amount") {
            let value = take_value(semantic_tokens, &mut index, "--amount")?;
            amount = Some(parse_i64_option(&value, "--amount")?);
            index += 1;
            continue;
        }
        if !positional_only && token.starts_with("--amount=") {
            amount = Some(parse_i64_option(&token[9..], "--amount")?);
            index += 1;
            continue;
        }
        if !positional_only && arg_is(current, "--to") {
            to = Some(take_value(semantic_tokens, &mut index, "--to")?);
            index += 1;
            continue;
        }
        if !positional_only && token.starts_with("--to=") {
            to = Some(token[5..].to_string());
            index += 1;
            continue;
        }
        if !positional_only && token.starts_with('-') && verb != "scroll" {
            bail!("unknown option for maho {verb}: {token}");
        }
        positional.push(token);
        index += 1;
    }

    if submit && verb != "type" {
        bail!("--submit is only valid with `maho type`");
    }
    if (amount.is_some() || to.is_some()) && verb != "scroll" {
        bail!("--amount/--to are only valid with `maho scroll`");
    }
    if amount.is_some() && to.is_some() {
        bail!("--amount and --to cannot be used together");
    }

    let mut payload = json!({ SEMANTIC_MARKER: true });
    if let Some(tab_id) = tab_id {
        payload["tab_id"] = Value::Number(tab_id.into());
    }

    let capability = match verb {
        "click" => {
            if positional.len() != 1 {
                bail!("Usage: maho click [--tab <id>] <selector>");
            }
            payload["selector"] = Value::String(positional.remove(0));
            "input.click"
        }
        "type" => {
            if positional.len() != 1 {
                bail!("Usage: maho type [--tab <id>] [--submit] <text>");
            }
            payload["selector"] = Value::String(":focus".to_string());
            payload["text"] = Value::String(positional.remove(0));
            if submit {
                payload["submit"] = Value::Bool(true);
            }
            "input.type"
        }
        "key" => {
            if positional.len() != 1 {
                bail!("Usage: maho key [--tab <id>] <key-name>");
            }
            payload["key"] = Value::String(positional.remove(0));
            "input.key_press"
        }
        "scroll" => {
            if !positional.is_empty() {
                bail!("Usage: maho scroll [--tab <id>] [--amount N | --to top|bottom]");
            }
            let scroll = scroll_args(tab_id, amount, to.as_deref())?;
            if let Some(object) = scroll.as_object() {
                for (key, value) in object {
                    payload[key] = value.clone();
                }
            }
            "input.scroll"
        }
        "hover" => {
            if positional.len() != 1 {
                bail!("Usage: maho hover [--tab <id>] <selector>");
            }
            payload["selector"] = Value::String(positional.remove(0));
            "input.hover"
        }
        "select" => {
            if positional.len() != 2 {
                bail!("Usage: maho select [--tab <id>] <selector> <value>");
            }
            payload["selector"] = Value::String(positional.remove(0));
            payload["value"] = Value::String(positional.remove(0));
            "input.select"
        }
        _ => bail!("unknown semantic verb: {verb}"),
    };
    Ok((capability.to_string(), payload, global_tail))
}

fn rewrite_semantic(args: Vec<OsString>, root_index: usize, verb: &str) -> Result<ParseAction> {
    match build_semantic_payload(verb, &args[root_index + 1..]) {
        Ok((capability, payload, global_tail)) => {
            let mut rewritten = args[..root_index].to_vec();
            rewritten.extend([
                os("tool"),
                os("run"),
                os(&capability),
                os("--args"),
                OsString::from(serde_json::to_string(&payload)?),
            ]);
            rewritten.extend(global_tail);
            Ok(ParseAction::Parse(rewritten))
        }
        Err(error) => {
            let rendered = error.to_string();
            if let Some(help) = rendered.strip_prefix("__MAHO_HELP__") {
                Ok(ParseAction::PrintAndExit(help.to_string()))
            } else {
                Err(error)
            }
        }
    }
}

fn rewrite_root_entry(mut args: Vec<OsString>, root_index: usize) -> Result<ParseAction> {
    let root = args[root_index].clone();
    if arg_is(&root, "--") {
        // `maho -- <prompt>` is the explicit escape for prompts beginning with
        // `-`. Keep clap's `--` immediately after `run` so the following bytes
        // are unambiguously positional prompt data.
        if args.get(root_index + 1).is_some() {
            args.insert(root_index, os("run"));
        }
        return Ok(ParseAction::Parse(args));
    }
    if arg_is(&root, "-h") || arg_is(&root, "--help") {
        return Ok(ParseAction::RootHelp);
    }
    if is_existing_subcommand(&root) || arg_is(&root, "-V") || arg_is(&root, "--version") {
        return Ok(ParseAction::Parse(args));
    }
    if let Some(verb) = semantic_verb(&root) {
        return rewrite_semantic(args, root_index, verb);
    }
    if arg_is(&root, "--session") || is_agent_session_assignment(&root) {
        args.insert(root_index, os("run"));
        return Ok(ParseAction::Parse(args));
    }
    if arg_utf8(&root).is_some_and(|value| value.starts_with('-')) {
        return Ok(ParseAction::Parse(args));
    }
    if arg_utf8(&root).is_some_and(is_http_url) {
        let root_text = arg_text(&root);
        args.splice(
            root_index..=root_index,
            [os("tab"), os("new"), os("--url"), OsString::from(root_text)],
        );
        return Ok(ParseAction::Parse(args));
    }
    args.insert(root_index, os("run"));
    Ok(ParseAction::Parse(args))
}

fn preprocess_cli_args(raw: Vec<OsString>) -> Result<PreprocessedArgs> {
    let (args, allow_robots_bypass) = extract_root_robots_consent(raw);
    let action = if let Some(root_index) = first_root_token(&args) {
        rewrite_root_entry(args, root_index)?
    } else {
        ParseAction::Parse(args)
    };
    Ok(PreprocessedArgs {
        action,
        allow_robots_bypass,
    })
}

// `Cli::parse()` in main.rs resolves to this inherent method before clap's trait
// method. The additive surface is normalized into the existing command tree, so
// all established commands/deprecation shims retain their original clap shapes.
impl crate::Cli {
    pub fn parse() -> Self {
        match preprocess_cli_args(std::env::args_os().collect()) {
            Ok(PreprocessedArgs {
                action: ParseAction::Parse(args),
                allow_robots_bypass,
            }) => {
                let mut cli = <Self as clap::Parser>::parse_from(args);
                cli.allow_robots_bypass = allow_robots_bypass;
                cli
            }
            Ok(PreprocessedArgs {
                action: ParseAction::RootHelp,
                ..
            }) => {
                let mut command = <Self as clap::CommandFactory>::command();
                print!("{}{}", command.render_long_help(), extra_root_help());
                std::process::exit(0);
            }
            Ok(PreprocessedArgs {
                action: ParseAction::PrintAndExit(text),
                ..
            }) => {
                println!("{text}");
                std::process::exit(0);
            }
            Err(error) => {
                eprintln!("error: {error}");
                std::process::exit(2);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn strings(values: &[&str]) -> Vec<OsString> {
        values.iter().map(|value| os(value)).collect()
    }

    fn preprocessed(values: &[&str]) -> PreprocessedArgs {
        preprocess_cli_args(strings(values)).unwrap()
    }

    fn rewritten(values: &[&str]) -> Vec<String> {
        match preprocessed(values).action {
            ParseAction::Parse(args) => args.iter().map(arg_text).collect(),
            ParseAction::RootHelp | ParseAction::PrintAndExit(_) => panic!("unexpected help exit"),
        }
    }

    #[test]
    fn http_url_routing_is_explicit() {
        assert!(is_http_url("https://example.com/path"));
        assert!(is_http_url("http://localhost:8080"));
        assert!(!is_http_url("open https://example.com"));
        assert!(!is_http_url("ftp://example.com"));
    }

    #[test]
    fn root_prompt_rewrites_to_existing_run_surface() {
        assert_eq!(
            rewritten(&["maho", "summarize this page"]),
            vec!["maho", "run", "summarize this page"]
        );
        assert_eq!(
            rewritten(&["maho", "--session", "abc", "continue"]),
            vec!["maho", "run", "--session", "abc", "continue"]
        );
        assert_eq!(
            rewritten(&["maho", "--session=abc", "continue"]),
            vec!["maho", "run", "--session=abc", "continue"]
        );
    }

    #[test]
    fn root_double_dash_escapes_leading_dash_prompt() {
        assert_eq!(
            rewritten(&["maho", "--", "--summarize-this-page"]),
            vec!["maho", "run", "--", "--summarize-this-page"]
        );
    }

    #[test]
    fn root_http_url_rewrites_to_new_tab() {
        assert_eq!(
            rewritten(&["maho", "--json", "https://example.com"]),
            vec![
                "maho",
                "--json",
                "tab",
                "new",
                "--url",
                "https://example.com"
            ]
        );
    }

    #[test]
    fn existing_session_flags_are_not_stolen() {
        assert_eq!(
            rewritten(&["maho", "artifact", "list", "--session", "abc"]),
            vec!["maho", "artifact", "list", "--session", "abc"]
        );
        assert_eq!(
            rewritten(&["maho", "run", "hello", "--session", "abc"]),
            vec!["maho", "run", "hello", "--session", "abc"]
        );
    }

    #[test]
    fn semantic_click_rewrites_to_canonical_existing_tool() {
        let args = rewritten(&[
            "maho",
            "--socket-path",
            "/tmp/m.sock",
            "click",
            "--tab",
            "7",
            "#submit",
        ]);
        assert_eq!(
            &args[..6],
            &[
                "maho",
                "--socket-path",
                "/tmp/m.sock",
                "tool",
                "run",
                "input.click"
            ]
        );
        let payload: Value = serde_json::from_str(args.last().unwrap()).unwrap();
        assert_eq!(payload[SEMANTIC_MARKER], true);
        assert_eq!(payload["selector"], "#submit");
        assert_eq!(payload["tab_id"], 7);
    }

    #[test]
    fn semantic_type_and_submit_are_encoded_without_backend_changes() {
        let args = rewritten(&["maho", "type", "--submit", "hello world"]);
        assert_eq!(args[1], "tool");
        assert_eq!(args[3], "input.type");
        let payload: Value = serde_json::from_str(args.last().unwrap()).unwrap();
        assert_eq!(payload["selector"], ":focus");
        assert_eq!(payload["text"], "hello world");
        assert_eq!(payload["submit"], true);
    }

    #[test]
    fn robots_policy_requires_consent_but_does_not_hard_block() {
        let default = compose_agent_system_prompt("base", false);
        assert!(default.contains("ask for explicit approval"));
        assert!(default.contains("not a permanent hard block"));
        let approved = compose_agent_system_prompt("base", true);
        assert!(approved.contains("explicitly consented"));
        assert!(approved.contains("not a hard block"));
    }

    #[test]
    fn robots_policy_is_not_duplicated_on_existing_override() {
        let once = compose_agent_system_prompt("custom", true);
        let twice = compose_agent_system_prompt(&once, false);
        assert_eq!(twice.matches("robots.txt policy:").count(), 1);
        assert!(twice.contains("ask for explicit approval"));
        assert!(!twice.contains("explicitly consented in this CLI invocation"));
    }

    #[test]
    fn robots_bypass_is_only_consumed_in_root_global_prefix() {
        let root = preprocessed(&["maho", "--json", "--allow-robots-bypass", "summarize"]);
        assert!(root.allow_robots_bypass);
        match root.action {
            ParseAction::Parse(args) => assert_eq!(
                args.iter().map(arg_text).collect::<Vec<_>>(),
                vec!["maho", "--json", "run", "summarize"]
            ),
            _ => panic!("unexpected action"),
        }

        let after_root_session = preprocessed(&[
            "maho",
            "--session",
            "abc",
            "--allow-robots-bypass",
            "continue",
        ]);
        assert!(after_root_session.allow_robots_bypass);
        match after_root_session.action {
            ParseAction::Parse(args) => assert_eq!(
                args.iter().map(arg_text).collect::<Vec<_>>(),
                vec!["maho", "run", "--session", "abc", "continue"]
            ),
            _ => panic!("unexpected action"),
        }

        let after_subcommand = preprocessed(&["maho", "run", "summarize", "--allow-robots-bypass"]);
        assert!(!after_subcommand.allow_robots_bypass);
        match after_subcommand.action {
            ParseAction::Parse(args) => {
                assert!(args.iter().any(|arg| arg_is(arg, "--allow-robots-bypass")))
            }
            _ => panic!("unexpected action"),
        }

        let after_double_dash = preprocessed(&["maho", "--", "--allow-robots-bypass"]);
        assert!(!after_double_dash.allow_robots_bypass);
        match after_double_dash.action {
            ParseAction::Parse(args) => assert_eq!(
                args.iter().map(arg_text).collect::<Vec<_>>(),
                vec!["maho", "run", "--", "--allow-robots-bypass"]
            ),
            _ => panic!("unexpected action"),
        }
    }

    #[test]
    fn robots_bypass_name_used_as_global_option_value_is_preserved() {
        let parsed = preprocessed(&[
            "maho",
            "--socket-path",
            "--allow-robots-bypass",
            "run",
            "hello",
        ]);
        assert!(!parsed.allow_robots_bypass);
        match parsed.action {
            ParseAction::Parse(args) => assert_eq!(
                args.iter().map(arg_text).collect::<Vec<_>>(),
                vec![
                    "maho",
                    "--socket-path",
                    "--allow-robots-bypass",
                    "run",
                    "hello"
                ]
            ),
            _ => panic!("unexpected action"),
        }
    }
}
