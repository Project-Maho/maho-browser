# appium-tests

## OVERVIEW
Standalone Appium regression suite for Maho. Repo-local scripts cover iOS regression, edge cases, and Android smoke paths.

## STRUCTURE
- `run-tests.js` — canonical iOS regression runner
- `edge-case-tests.js` — edge-case regression pass
- `run-android-smoke.js` — Android smoke runner
- `tests/` — JS test cases
- `wdio.conf.js` — WebdriverIO config

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Canonical iOS run | `run-tests.js` | `bun run test` and `bun run test:ios` share this runner |
| Edge coverage | `edge-case-tests.js` | Separate on purpose |
| Android smoke | `run-android-smoke.js` | Short mobile smoke path |
| Test cases | `tests/` | Repo-local Appium specs |

## CONVENTIONS
- Use `bun run` for these repo scripts, and `bunx` for one-off package executables or CLIs.
- `bun run test` is the canonical iOS regression command.
- Keep this suite separate from product source trees.
- `APP_PATH` in `run-tests.js` may need adjustment for local build output.
- Edge cases are a separate script, not folded into the main runner.

## ANTI-PATTERNS
- Do not move Appium code into product crates or shell targets.
- Do not hide a failing iOS regression by rewriting the runner contract.
- Do not assume a single command covers all mobile coverage.

## COMMANDS
```bash
bun run test
bun run test:edge
bun run test:android
```
