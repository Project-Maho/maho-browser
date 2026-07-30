# MAHO MAIL KNOWLEDGE BASE

## OVERVIEW
Maho Mail is an isolated Tauri v2 desktop workspace with a React 19 frontend, and it is deferred from the current ship.

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
- Keep frontend code in TypeScript and use the existing React component and state patterns.
- Prefer typed Tauri command payloads and explicit error results over unstructured strings.
- Keep native commands small. Put mail domain logic in focused Rust modules rather than command handlers.
- Run frontend checks with `bun run test` and Rust checks with `cargo test` from `mail/`.

## ANTI-PATTERNS
- Do not expose, reactivate, or advertise Maho Mail without explicit approval and a completed security and integrity remediation pass.
- Do not bypass Tauri capability or permission controls to make a native API call work.
- Do not pass unvalidated user input directly from React into filesystem, shell, or network operations.
- Do not add `mail/` crates to the browser workspace. Its dependency graph is intentionally isolated.
- Do not put business logic in React event handlers or Tauri command glue.
