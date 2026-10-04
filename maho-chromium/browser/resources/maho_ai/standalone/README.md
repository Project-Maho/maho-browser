# Maho AI standalone dev harness

Dev-iteration loop for the Maho AI React UI. **Not used in production** —
production uses the ACP transport via the `maho-acp` Rust crate behind
`maho-ffi`.

## Two dev paths

### 1. React UI dev server (HTTP+SSE, fastest iteration)

Start OpenCode:

```bash
opencode serve --hostname 127.0.0.1 --port 4096
```

Start the standalone dev server from the workspace root:

```bash
node maho-chromium/browser/resources/maho_ai/standalone/dev_server.mjs
```

Open `http://127.0.0.1:4071`. This path uses HTTP+SSE against
`opencode serve` for the fastest React UI iteration cycle. Browser-context
attachment is unavailable in this mode.

### 2. ACP wire validation (Node probe)

```bash
node maho-chromium/browser/resources/maho_ai/standalone/acp_probe.mjs
```

Validates ACP wire format against a running OpenCode instance. See
`ACP_PROBE.md` for details.

## Production path

The canonical ACP wire implementation lives in `maho/crates/maho-acp/` and
is exposed to Chromium C++ through `maho-ffi`. The `MahoOpenCodeAcpAdapter`
in `browser/ai/maho_opencode_acp_adapter.cc` is the sole runtime adapter.

## Environment variables

- `OPENCODE_BASE_URL` — override the backend URL (default:
  `http://127.0.0.1:4096/`)
- `MAHO_AI_STANDALONE_HOST` — override the dev server bind host
- `MAHO_AI_STANDALONE_PORT` — override the dev server bind port

## See also

- `ACP_PROBE.md` — ACP wire validation probe documentation
- `.sisyphus/plans/maho-ai-acp-migration-plan.md` — ACP migration plan
