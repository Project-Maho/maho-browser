# Crash QA — Orphan Child Verification

## Purpose

Verify that killing Maho (or the standalone bridge) does not leave an orphaned
`opencode acp` child process.

## Steps (macOS arm64)

```bash
# 1. Launch Maho and open chrome://maho-ai. Send a prompt so the ACP child spawns.

# 2. Confirm the child is running:
pgrep -fl 'opencode acp'
# Should show one PID.

# 3. Kill Maho abnormally:
kill -9 $(pgrep -x Maho)

# 4. Wait 3 seconds, then check for orphans:
sleep 3 && pgrep -fl 'opencode acp'
# Should return nothing (exit code 1). If a PID remains, the lifecycle contract is broken.
```

## Expected result

No `opencode acp` process survives more than ~2.5s after Maho dies. The child
detects stdin EOF and exits; `kill_on_drop(true)` in the Rust crate is the
final backstop.

## Standalone bridge variant

```bash
# Kill the Node bridge process instead of Maho:
kill -9 $(pgrep -f 'acp_bridge.mjs')
sleep 3 && pgrep -fl 'opencode acp'
```
