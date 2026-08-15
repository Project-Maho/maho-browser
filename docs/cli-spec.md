# Maho CLI & MCP Specification

## Overview

The Maho CLI (`maho`) provides a developer command-line interface and stdio/UDS MCP tool server bridging desktop AI, routines, mail, and local browser capabilities.

Mail commands connect to the active Maho Browser profile. They do not open the
encrypted Mail database directly and do not start a standalone daemon.
Argument and subcommand errors exit with status `2`; runtime availability or
authorization denials exit with status `1`. `--help` is available regardless
of runtime Mail state.

Mail tool discovery reports `feature_disabled`, `read_consent_required`,
`helper_starting`, or `helper_unavailable`. Global approval does not bypass the
profile-level Mail-read permission, and writes retain typed approval.

## CLI Subcommands

### 1. `maho agent`
Run agent prompts or execute automated routine files.

- **Run Prompt:**
  ```bash
  maho agent run "Summarize my active space tabs"
  ```
- **Execute Routine File:**
  ```bash
  maho agent --routine /path/to/routine.json
  ```
  *Note:* Routine files must contain valid JSON. Invalid files will fail closed with exit code `1`.

### 2. `maho mcp`
Manage local MCP server registrations.

- **List MCP Servers:**
  ```bash
  maho mcp list
  ```
- **Add MCP Server:**
  ```bash
  maho mcp add <name> --command "<command>"
  ```
- **Remove MCP Server:**
  ```bash
  maho mcp remove <name>
  ```

### 3. `maho mail`
Bridge to Maho Mail commands and local workspace configuration.

## MCP Protocol Capabilities

- **Protocol Version:** `2025-03-26`
- **Capabilities Advertised:** `tools: {}` (**Tools-only scope**; `resources` and `prompts` are intentionally disabled per ADR 0014).
- **Transports:** Stdio, UDS (`/tmp/maho-mcp.sock`), and HTTP.

## BYOK & Managed Billing Setup

- **Managed Default Model:** `google/gemini-3-flash-lite:free`
- **CLI Self-Balance (`maho balance`):** `GET /auth/subscription` with the **user bearer token**. The CLI reads `credit_balance_usd` (and `lifetime_purchased_usd`) from the `SubscriptionResponse`. This is the only balance path the CLI uses; it never calls the service-token routes below.
- **Service-Token Credit Balance (Worker-only):** `POST/GET /billing/credit_balance`, gated by `MAHO_RELAY_SERVICE_TOKEN`. Called by the billing-proxy Worker, not by the CLI.
- **Service-Token Credit Consumption (Worker-only):** `POST /billing/consume_credit`, gated by `MAHO_RELAY_SERVICE_TOKEN`. Deducts PAYG credit atomically after an upstream response is committed.
- **BYOK Support:** OpenAI, Anthropic, OpenRouter custom keys stored securely via system keychain.
