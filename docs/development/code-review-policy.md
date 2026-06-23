# Code Review & Branch Protection Policy

## 1. Overview & Scope

This policy defines the change management and code review standards for the Maho Browser workspaces, repositories, and release pipelines. This policy supports the AICPA SOC2 Change Management criteria (**CC8.1**).

This policy applies to all engineers, developers, and administrators who commit code to the Maho Browser codebase, including native shells, Rust core crates, Chromium overlays, and WebUI resources.

---

## 2. Branch Protection Configuration

The following rules are configured and programmatically enforced on the primary branch (`main`) within our version control platform (GitHub):

### 2.1. Mandatory Pull Requests & Reviews
- All code changes, fixes, and updates must be submitted via a Pull Request (PR) from a feature or fix branch.
- Committing directly to the `main` branch is blocked for all users, including administrators.
- A minimum of **one approving review** from a designated code owner is strictly required before a PR can be merged.
- If a file is mapped in `.github/CODEOWNERS` to `@maho/security-reviewers`, the review must be completed by a qualified security reviewer.

### 2.2. Automated Status Checks
- All status checks defined in GitHub Actions workflows (e.g., `.github/workflows/oss-ci.yml`) must pass successfully before a PR can be merged.
- Required checks include:
  - Rust workspace compilation: `bun run typecheck`
  - Integration and unit tests: `bun run test`
  - Cargo linter and formatter: `cargo clippy` and `cargo fmt`
  - Daily vulnerability auditing checks: `bun run security-audit`

### 2.3. Anti-Bypass Enforcements
- **Restrict Force-Pushes:** Force-pushing (`git push --force`) to `main` is disabled globally to preserve git commit audit history.
- **Block Branch Deletions:** Deletion of the `main` branch is disabled.
- **Dismiss Stale Approvals:** Any new commits pushed to an open PR will automatically dismiss prior approvals, requiring a fresh review.

---

## 3. Code Review Guidelines (Review Checklist)

To ensure consistency and security, code reviewers must audit code submissions against the following checklist:

| Category | Review Check | Objective |
|---|---|---|
| **Memory Safety** | Verify zeroization on drop for Restricted secrets. | Prevent plaintext secret leftovers in memory dumps. |
| **Audit Trails** | Confirm target `maho_agent::audit` logging exists. | Ensure session start, end, and allowed tool calls are logged. |
| **FFI Safety** | Inspect raw pointers and array boundary conversions. | Prevent Buffer Overflow or Use-After-Free vulnerabilities. |
| **Tool Sanitization** | Ensure inputs to `FsReadTool` or `ShellExecTool` are sanitized. | Prevent arbitrary directory traversals or command injection. |
| **No Passwords in Plaintext** | Confirm no plaintext API keys or credentials are in code/configs. | Direct violation of Level 3 classification controls. |

---

## 4. Emergency Change Management Procedure

In the event of an active P1 security incident or outage:
1. A hotfix PR can be merged with temporary approval by the CTO or Security Lead.
2. The code review must be retroactively documented and approved within **24 hours** of deployment.
3. Post-mortem analysis must be conducted per the incident response procedure.
