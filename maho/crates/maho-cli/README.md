# maho-cli

Command-line interface for Maho — chat with AI from your terminal.

## Install

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

### Chat (BYOK — Bring Your Own Key)

```bash
# Direct to OpenAI (single-shot, key not stored)
maho chat --byok openai --api-key sk-... "Explain monads"
```

### Configuration

```bash
# Show current config (tokens redacted)
maho config show
```

### Global Flags

| Flag | Default | Description |
|------|---------|-------------|
| `--proxy-url` | `https://proxy.maho.co/v1` | Billing proxy URL |
| `--relay-url` | `https://relay.maho.co` | Auth relay URL |

## Config Location

Credentials are stored at `~/.config/maho-cli/cli.toml` (XDG-compliant) with `0600` permissions.

## Supported BYOK Providers

- `openai` — OpenAI API (gpt-4o default model)

More providers coming soon.
