# maho-cli

Command-line interface for Maho — browser control, AI-agent loop, MCP host management, and routines execution from your terminal.

## Install

```bash
curl -fsSL https://mahobrowser.com/install-cli.sh | sh
```

Build from source instead:

```bash
cd maho && cargo build -p maho-cli --release
# Binary at: maho/target/release/maho
```

## Usage

### Authentication

```bash
# Log in with existing account
maho login

# Sign up for a new account
maho login --signup

# Check current user
maho whoami

# Log out
maho logout
```

### Chat (MahoManaged via billing proxy)

```bash
# Send a message through the Maho proxy
maho chat "What is the capital of France?"

# Override proxy URL
maho --proxy-url https://custom-proxy.example.com/v1 chat "hello"
```

### AI-Agent Loop (`maho run`)

Executes an autonomous agent session using your choice of BYOK model. CLI agent tool calls are always auto-approved; the browser capability sandbox still enforces its independent boundaries.

```bash
# Run a prompt on the current active tab
maho run "Find the checkout button on the page and buy the item"

# Custom provider and model
maho run "Extract all details from the page" --provider anthropic --model claude-3-5-sonnet-20241022

# Tool calls are auto-approved by default
maho run "Automate my task"
```

### Generic Tool Passthrough (`maho tool`)

Enables direct programmatic invocation of the underlying browser tools.

```bash
# List all 36 available browser tools
maho tool list

# Programmatically call a stateless tool
maho tool call browser_ping

# Programmatically call a tool with arguments
maho tool call browser_tab_new --args '{"url": "https://example.com"}'
```

### MCP Host Management (`maho mcp`)

Add, remove, and list external Model Context Protocol (MCP) servers connected to your workspace.

```bash
# List registered MCP servers
maho mcp list

# Add a new MCP server
maho mcp add my-mcp-server --command "npx -y @modelcontextprotocol/server-everything"

# Remove an MCP server
maho mcp remove my-mcp-server
```

#### MCP capability scope

Maho's MCP surface is **tools-only** for v1. Both the MCP server and the MCP host handle `tools` exclusively; the `resources` and `prompts` capabilities are intentionally out of scope for this release. This deferral is deliberate, not an oversight, and can be revisited if a concrete use case emerges. See decision [`docs/decisions/0014-oq2-resources-prompts.md`](../../../docs/decisions/0014-oq2-resources-prompts.md) for the rationale.

### Routines (`maho routine`)

Manage and run pre-defined automation routines. Note that executing routines is a Max-tier exclusive feature.

```bash
# List available routines
maho routine list

# Run a routine
maho routine run morning_briefing
```

### Configuration

```bash
# Show current config (tokens redacted)
maho config show
```

## Global Flags

| Flag | Default | Description |
|------|---------|-------------|
| `--json` | `false` | Output structured JSON for all commands |
| `--socket-path` | - | Override Unix Domain Socket path |
| `--proxy-url` | `https://proxy.maho.co/v1` | Billing proxy URL |
| `--relay-url` | `https://relay.maho.co` | Auth relay URL |

## Config Location

Credentials and configurations are stored at `~/.config/maho-cli/cli.toml` (XDG-compliant) with `0600` permissions.

## Supported BYOK Providers

Supported providers for `maho run` and `maho chat --byok`:

- `openai` (e.g., `gpt-4o`)
- `gemini` (e.g., `gemini-1.5-pro`)
- `anthropic` / `claude` (e.g., `claude-3-5-sonnet`)

## Distribution and Releases

Prebuilt binaries and official packaging (dmg, exe, deb, rpm, tar.gz, apk) are hosted exclusively in the release-only repository.

Download releases here: [Maho Browser Releases](https://github.com/Project-Maho/maho-browser/releases)

---

### Platform Scoping Notes

- **macOS / Linux / Windows:** Fully supports stdio and UDS external MCP server execution.
- **iOS:** The native sandbox blocks external process fork/execve operations. Consequently, iOS execution is scoped to in-process/UDS browser tools only. External process MCP servers are stubbed on iOS.
