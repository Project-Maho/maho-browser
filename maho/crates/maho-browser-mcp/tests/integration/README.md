# MCP Integration Tests

These tests require a running Maho browser instance. They are `#[ignore]`d by default.

## Run:
```bash
# Ensure Maho.app is running with MCP socket at default path
cargo test -p maho-browser-mcp --tests -- --ignored
```

## Test Matrix
- **mcp_tools_test.rs**: every MCP tool callable
- **security_test.rs**: 4-vector firewall (OQ-4) regression
- **lease_test.rs**: TTL + heartbeat + force-steal
- **allowed_domains_test.rs**: domain enforcement (D1)
- **headless_test.rs**: auto-launch orchestration
- **repl_test.rs**: interactive session

Golden snapshot files under `snapshots/` are consumed by Wave 7.4 blog rewrite.

## Snapshots
- `tab_info_active.txt` — expected `maho tab info` output for a canonical active tab
- `tab_list_two_tabs.txt` — expected `maho tab list` output
- `history_list_grouped.txt` — expected `maho history list --since 7d` output
- `headless_extract_title.txt` — expected `maho headless --url https://example.com --extract title` output
