use std::ffi::{OsStr, OsString};

use anyhow::{bail, Result};
use serde_json::{json, Value};

use crate::browser::refactor::{scroll_args, SEMANTIC_MARKER};

fn is_http_url(value: &str) -> bool {
    let value = value.trim();
    value.starts_with("http://") || value.starts_with("https://")
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
    arg_is(arg, "--json")
}

fn is_global_flag_with_value(arg: &OsString) -> bool {
    arg_is(arg, "--socket-path") || arg_is(arg, "--relay-url")
}

fn is_global_assignment(arg: &OsString) -> bool {
    arg_utf8(arg).is_some_and(|value| {
        ["--socket-path=", "--relay-url="]
            .iter()
            .any(|prefix| value.starts_with(prefix))
    })
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
        "agent",
        "vault",
        "version",
        "tool",
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
        "type" => Some("Usage: maho type [--tab <id>] [--submit] [--allow-credentials] <text>\n\nType into the currently focused element. --submit presses Enter after typing. --allow-credentials allows typing into password/credential fields."),
        "key" => Some("Usage: maho key [--tab <id>] <key-name>\n\nPress a browser key such as Enter or ArrowDown."),
        "scroll" => Some("Usage: maho scroll [--tab <id>] [--amount N | --to top|bottom]\n\nScroll vertically. Positive --amount scrolls down; negative scrolls up; the default is 300 pixels."),
        "hover" => Some("Usage: maho hover [--tab <id>] <selector>\n\nHover an element selected by CSS. Use @<ref> as an explicit accessibility-ref selector when needed."),
        "select" => Some("Usage: maho select [--tab <id>] <selector> <value>\n\nSelect a value in an element selected by CSS. Use @<ref> as an explicit accessibility-ref selector when needed."),
        _ => None,
    }
}

/// Printed above the command list. Agents truncate `--help` with `head -40`,
/// which previously cut off the semantic verbs that clap renders after Options.
pub const AGENT_PREAMBLE: &str = "\
Agent contract (read this before the command list):
  delegate:   maho agent ask <goal> (runs in browser AI panel)
  fallback:   explicit direct control for precise single steps:
  one step:   maho click --tab <id> <selector>
              maho type --tab <id> [--submit] <text>
  many steps: maho browser pipe
              one JSON object per line; required field op
              ops: observe, click, type, navigate, wait, act
  CLI agent:  maho agent task <goal> --approve-all (separate runtime)
";

fn extra_root_help() -> &'static str {
    "\nQuick entry:\n  maho <http(s)://url>                  Open the URL in a new browser tab\n\nSemantic browser interaction:\n  maho click [--tab <id>] <selector>\n  maho type [--tab <id>] [--submit] <text>\n  maho key [--tab <id>] <key-name>\n  maho scroll [--tab <id>] [--amount N | --to top|bottom]\n  maho hover [--tab <id>] <selector>\n  maho select [--tab <id>] <selector> <value>\n"
}

enum ParseAction {
    Parse(Vec<OsString>),
    RootHelp,
    PrintAndExit(String),
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
    let mut allow_credentials = false;
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
        if !positional_only && arg_is(current, "--allow-credentials") {
            allow_credentials = true;
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
                bail!("Usage: maho type [--tab <id>] [--submit] [--allow-credentials] <text>");
            }
            payload["selector"] = Value::String(":focus".to_string());
            payload["text"] = Value::String(positional.remove(0));
            if submit {
                payload["submit"] = Value::Bool(true);
            }
            if allow_credentials {
                payload["allow_credentials"] = Value::Bool(true);
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

fn rewrite_root_entry(args: Vec<OsString>, root_index: usize) -> Result<ParseAction> {
    let root = args[root_index].clone();
    if arg_is(&root, "-h") || arg_is(&root, "--help") {
        return Ok(ParseAction::RootHelp);
    }
    if is_existing_subcommand(&root) || arg_is(&root, "-V") || arg_is(&root, "--version") {
        return Ok(ParseAction::Parse(args));
    }
    if let Some(verb) = semantic_verb(&root) {
        return rewrite_semantic(args, root_index, verb);
    }
    if arg_utf8(&root).is_some_and(|value| value.starts_with('-')) {
        return Ok(ParseAction::Parse(args));
    }
    if arg_utf8(&root).is_some_and(is_http_url) {
        let root_text = arg_text(&root);
        let mut rewritten = args.clone();
        rewritten.splice(
            root_index..=root_index,
            [os("tab"), os("new"), os("--url"), OsString::from(root_text)],
        );
        return Ok(ParseAction::Parse(rewritten));
    }
    // Deliberate no-op: bare words no longer route anywhere; clap rejects them.
    Ok(ParseAction::Parse(args))
}

fn preprocess_cli_args(raw: Vec<OsString>) -> Result<ParseAction> {
    if let Some(root_index) = first_root_token(&raw) {
        rewrite_root_entry(raw, root_index)
    } else {
        Ok(ParseAction::Parse(raw))
    }
}

// `Cli::parse()` in main.rs resolves to this inherent method before clap's trait
// method. The additive surface is normalized into the existing command tree, so
// all established commands/deprecation shims retain their original clap shapes.
impl crate::Cli {
    pub fn parse() -> Self {
        match preprocess_cli_args(std::env::args_os().collect()) {
            Ok(ParseAction::Parse(args)) => <Self as clap::Parser>::parse_from(args),
            Ok(ParseAction::RootHelp) => {
                let mut command = <Self as clap::CommandFactory>::command();
                command = command.before_help(AGENT_PREAMBLE);
                print!("{}{}", command.render_long_help(), extra_root_help());
                std::process::exit(0);
            }
            Ok(ParseAction::PrintAndExit(text)) => {
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

    fn rewritten(values: &[&str]) -> Vec<String> {
        match preprocess_cli_args(strings(values)).unwrap() {
            ParseAction::Parse(args) => args.iter().map(arg_text).collect(),
            ParseAction::RootHelp | ParseAction::PrintAndExit(_) => panic!("unexpected help exit"),
        }
    }

    #[test]
    fn root_help_puts_agent_contract_above_options() {
        let mut command =
            <crate::Cli as clap::CommandFactory>::command().before_help(AGENT_PREAMBLE);
        let help = command.render_long_help().to_string();
        let contract = help
            .find("maho agent task")
            .expect("agent handoff must be in the preamble");
        let click = help
            .find("maho click --tab")
            .expect("one-step command must be in the preamble");
        let pipe = help
            .find("maho browser pipe")
            .expect("pipe must be in the preamble");
        let options = help.find("\nOptions:").expect("options heading");
        assert!(contract < options, "head -40 must still see the handoff");
        assert!(click < options);
        assert!(pipe < options);
    }

    #[test]
    fn http_url_routing_is_explicit() {
        assert!(is_http_url("https://example.com/path"));
        assert!(is_http_url("http://localhost:8080"));
        assert!(!is_http_url("open https://example.com"));
        assert!(!is_http_url("ftp://example.com"));
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
    fn unknown_bare_words_pass_through_to_clap() {
        assert_eq!(rewritten(&["maho", "hello"]), vec!["maho", "hello"]);
        assert_eq!(
            rewritten(&["maho", "--session", "abc", "continue"]),
            vec!["maho", "--session", "abc", "continue"]
        );
    }

    #[test]
    fn existing_subcommands_pass_through() {
        assert_eq!(
            rewritten(&["maho", "tool", "list"]),
            vec!["maho", "tool", "list"]
        );
        assert_eq!(
            rewritten(&["maho", "--", "--flag"]),
            vec!["maho", "--", "--flag"]
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
}
