# Maho local security fixtures

Deterministic, repo-local HTTPS fixtures + evidence-capture plumbing for
Vault/agent security testing (plan `maho-agent-safe-vault`, Todo 2). They give
downstream capability-grant, injector, and sentinel-negative tests real local
routes to drive — with **no external network dependency** (no `example.com`,
no `httpbin.org`).

## Two loopback origins over HTTPS

`startSecurityFixtures()` binds two genuinely cross-origin loopback servers:

- Origin A: `https://localhost:<ephemeral>`
- Origin B: `https://127.0.0.1:<ephemeral>`

Distinct hosts make them cross-origin for iframe/frame-binding tests. Ports are
ephemeral (OS-assigned) to avoid stale-port collisions; the resolved origins are
returned and printed. Both servers share a **test-only self-signed cert**
(`certs.js`, SAN `localhost`/`127.0.0.1`) generated into a process-local temp
dir. TLS is accepted only by giving the returned PEM to the HTTPS client as an
explicit `ca`, or via Playwright context-scoped `ignoreHTTPSErrors`. There is
**no global TLS bypass and no OS/browser trust-store install.**

## Routes (each carries `data-maho-fixture-route="<name>"`)

| Route | Purpose | Key controls |
| --- | --- | --- |
| `/login` | HTTPS login | `autocomplete=username`, `autocomplete=current-password` (seeded with the sentinel) |
| `/password-change` | Password change | `current-password` + two `new-password` fields |
| `/totp` | TOTP enrollment + login | RFC 6238 test-vector base32 secret + `autocomplete=one-time-code` |
| `/cross-origin-iframe` | Cross-origin child frame | `<iframe src="<peer-origin>/login">` |
| `/payment` | Consequential action | `autocomplete=cc-number` + `data-maho-action="payment-submit"` |
| `/post` | Consequential action | message `<textarea>` + `data-maho-action="post-submit"` |
| `/prompt-injection` | Malicious instruction content | visible + hidden override text; **sentinel absent** |

Support endpoints: `/healthz` (deterministic readiness) and
`/__fixture_manifest` (routes + authorized sentinel locations).

## Sentinel policy

The test-only sentinel `S3NTINEL-maho-vault-9F4C` is seeded **only** as the
`/login` current-password value (both origins) — simulating an autofilled
credential. That is its sole authorized location. Because the fixtures are
UNREDACTED, the sentinel legitimately appears in the raw browser surfaces of
`/login` (serialized DOM and the ARIA/AX tree) — this is exactly the surface the
later redaction task must scrub. It must NOT appear in agent-facing egress
artifacts: page-context text, screenshots, OCR text, HAR (bodies omitted), model
events, permission payloads, WebUI events, or the audit log.

`capture.js` provides the capture helpers for every artifact class plus
`scanForSentinel(dir)` and `assertSentinelOnlyIn(dir, allowed)` to enforce that
policy.

## Usage

```bash
# Unit/smoke tests (fast, no browser):
node --test "maho-chromium/build/scripts/security_fixtures/tests/*.test.js"

# Real-browser QA -> artifacts + PASS/FAIL verdict (exit 0 iff all pass):
node maho-chromium/build/scripts/security_fixtures/playwright_qa.js \
  --artifact-dir .omo/start-work/artifacts/maho-agent-safe-vault/task-2

# Standalone server for manual/other-language consumers:
node maho-chromium/build/scripts/security_fixtures/run_security_fixtures.js
#   --once      start, print manifest JSON, exit 0
#   --ca-out P  also write the CA PEM to P
```

Programmatic:

```js
const { startSecurityFixtures } = require('./fixture_server');
const fx = await startSecurityFixtures();
// fx.a.origin, fx.b.origin, fx.ca (PEM), fx.manifest, fx.sentinel
await fx.close(); // closes both servers AND removes the temp cert dir
```

## Determinism & cleanup

Readiness is signalled by the `listen` callbacks (no sleeps). `fx.close()` (and
the CLI's SIGINT/SIGTERM handlers) shut down both servers and remove the temp
cert dir; the Playwright QA also removes its temp browser profile and wipes the
artifact dir at the start of each run, so re-runs are clean.
