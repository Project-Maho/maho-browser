# MAHO MAIL KNOWLEDGE BASE

## OVERVIEW
This Tauri v2 workspace is legacy/reference-only. It builds locally for reference, and nothing built here is distributed. There is no shipped standalone Mail application and no `maho-mail` binary in any release artifact.

The shipped product and owning implementation are Browser-integrated Mail under `maho-chromium/browser/resources/maho_mail/`, backed by the Chromium Mail helper (`maho-chromium/browser/mail_helper/`) and `maho/mail-core`. Users reach it at `chrome://maho-mail`. Browser-integrated Mail is an opt-in Beta: new profiles default off and users enable it from Maho Settings.

User-facing Mail surfaces all resolve to the browser:

| Surface | Canonical form | Not shipped |
|---------|----------------|-------------|
| GUI | `chrome://maho-mail` in Maho Browser | standalone Mail app, Tauri build artifact |
| CLI | `maho mail <subcommand>` | `maho-mail` binary |
| Agent/MCP | Browser's in-process MCP server, `mail_*` tools | `maho-mcp` binary, standalone Mail daemon |

## STRUCTURE
```
mail/
├── src/                    # React 19 application
├── src-tauri/              # Tauri v2 Rust application and native configuration
├── public/                 # Static assets served by Vite
├── package.json            # Frontend scripts and JavaScript dependencies
├── Cargo.toml              # Isolated Rust workspace manifest
└── vite.config.*           # Vite development and build configuration
```

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Mail UI and client state | `src/` | React components, hooks, and application styles |
| Native commands and state | `src-tauri/src/` | Expose narrow Tauri commands to the frontend |
| Tauri capabilities and settings | `src-tauri/` | Review configuration before adding native access |
| Frontend tests | `src/**/*.test.*` | Run with the workspace's test script |
| Rust tests | `src-tauri/src/` | Keep command and state tests near their code |

## CONVENTIONS
- Treat `mail/Cargo.toml` as a separate workspace. Do not add its crates to another Cargo workspace.
- Treat this Tauri application as legacy/reference material, not the shipped Mail owner. Product behavior changes belong in Browser-integrated Mail unless a task explicitly targets the legacy workspace.
- Mail agent access in the shipped product is gated by `maho.mail.enabled`, helper readiness, and `maho.ai.mail_read_allowed`. Denial reason codes (`mail_feature_disabled`, `mail_helper_unavailable`, `mail_global_policy_denied`, `mail_read_consent_required`, `mail_typed_approval_required`, `mail_typed_approval_denied`, `mail_tool_unknown`) are a shared contract across C++, Rust, and the website docs. This workspace does not define or override them.
- Keep frontend code in TypeScript and use the existing React component and state patterns.
- Prefer typed Tauri command payloads and explicit error results over unstructured strings.
- Keep native commands small. Put mail domain logic in focused Rust modules rather than command handlers.
- Run frontend checks with `bun run test` and Rust checks with `cargo test` from `mail/`.

## ANTI-PATTERNS
- Do not describe this workspace as a shipped app, a downloadable Mail client, or the source of a `maho-mail` binary, in docs, marketing copy, or code comments.
- Do not expose, reactivate, or advertise this legacy Tauri application without explicit approval and a completed security and integrity remediation pass.
- Do not bypass Tauri capability or permission controls to make a native API call work.
- Do not pass unvalidated user input directly from React into filesystem, shell, or network operations.
- Do not add `mail/` crates to the browser workspace. Its dependency graph is intentionally isolated.
- Do not put business logic in React event handlers or Tauri command glue.
