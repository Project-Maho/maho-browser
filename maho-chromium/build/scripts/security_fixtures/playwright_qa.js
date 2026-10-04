#!/usr/bin/env node
'use strict';

// Real-browser QA for the local security fixtures (Todo 2, maho-agent-safe-vault).
//
// Starts the two-origin HTTPS fixtures in-process, drives a real Chromium via
// Playwright over every route, and captures evidence (raw DOM, ARIA/AX tree,
// page context, screenshots, OCR text, HAR) into the artifact directory. It
// then proves the sentinel appears ONLY in the authorized /login raw-DOM
// snapshots and in no redacted-egress artifact.
//
// TLS: the self-signed fixture cert is trusted ONLY via context-scoped
// `ignoreHTTPSErrors` — no global trust bypass.
//
// Usage: node playwright_qa.js [--artifact-dir <dir>] [--headed]
// Exit 0 iff every assertion passes.

const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const PW = path.resolve(__dirname, '../../../../mail/node_modules/playwright');
const { chromium } = require(PW);
const { startSecurityFixtures, ROUTES } = require('./fixture_server');
const capture = require('./capture');

const DEFAULT_ARTIFACT_DIR = path.resolve(
  __dirname,
  '../../../../.omo/start-work/artifacts/maho-agent-safe-vault/task-2',
);

function parseArgs(argv) {
  const args = { artifactDir: DEFAULT_ARTIFACT_DIR, headed: false };
  for (let i = 2; i < argv.length; i += 1) {
    if (argv[i] === '--artifact-dir') args.artifactDir = path.resolve(argv[i + 1]);
    else if (argv[i] === '--headed') args.headed = true;
  }
  return args;
}

const checks = [];
function check(name, cond, detail) {
  checks.push({ name, pass: !!cond, detail: detail || '' });
  if (!cond) process.stderr.write(`ASSERT FAIL: ${name} — ${detail || ''}\n`);
}

async function captureRoute(page, origin, route, dirs, label) {
  const name = route.slice(1);
  const id = label || name;
  const url = `${origin}${route}`;
  await page.goto(url, { waitUntil: 'networkidle' });

  const html = await page.content();
  check(`${id}: route marker present`, html.includes(`data-maho-fixture-route="${name}"`), url);

  capture.writeArtifact(dirs.dom, `${id}.html`, html);

  const aria = await page.locator('body').ariaSnapshot();
  capture.writeArtifact(dirs.ax, `${id}.aria.yaml`, aria);

  const ctx = {
    url: page.url(),
    title: await page.title(),
    text: await page.evaluate(() => document.body.innerText),
  };
  capture.writeArtifact(dirs.pageContext, `${id}.json`, ctx);

  const shotPath = path.join(dirs.screenshots, `${id}.png`);
  await page.screenshot({ path: shotPath, fullPage: true });

  let ocrText = '';
  try {
    ocrText = capture.runOcr(shotPath);
  } catch (err) {
    ocrText = `OCR_UNAVAILABLE: ${err.message}`;
  }
  capture.captureOcrText(dirs.ocr, `${id}.txt`, ocrText);

  // Raw browser surfaces (DOM + AX) reflect the page verbatim, so a seeded
  // credential legitimately appears here PRE-redaction; page-context/OCR/HAR do
  // not. Return the raw surfaces so the caller can allowlist them for /login.
  return { rawSurfaces: [path.join('dom', `${id}.html`), path.join('ax', `${id}.aria.yaml`)], html, aria };
}

async function main() {
  const args = parseArgs(process.argv);
  const runStartedAt = new Date().toISOString();
  fs.rmSync(args.artifactDir, { recursive: true, force: true });
  const dirs = {
    root: args.artifactDir,
    dom: path.join(args.artifactDir, 'dom'),
    ax: path.join(args.artifactDir, 'ax'),
    pageContext: path.join(args.artifactDir, 'page-context'),
    screenshots: path.join(args.artifactDir, 'screenshots'),
    ocr: path.join(args.artifactDir, 'ocr'),
  };
  for (const d of Object.values(dirs)) fs.mkdirSync(d, { recursive: true });

  const fx = await startSecurityFixtures();
  const profileDir = fs.mkdtempSync(path.join(os.tmpdir(), 'maho-fx-profile-'));
  const harPath = path.join(args.artifactDir, 'network.har');

  const browser = await chromium.launch({ headless: !args.headed });
  const context = await browser.newContext({
    ignoreHTTPSErrors: true,
    // HAR is a network-metadata artifact: response bodies are omitted so it
    // never carries page secrets, matching the agent-facing HAR contract.
    recordHar: { path: harPath, content: 'omit' },
  });

  const authorizedRaw = [];
  try {
    const page = await context.newPage();

    // Capture every route on origin A; also capture /login on origin B so both
    // authorized sentinel surfaces are represented.
    for (const route of ROUTES) {
      const { rawSurfaces, html, aria } = await captureRoute(page, fx.a.origin, route, dirs);
      if (route === '/login') {
        authorizedRaw.push(...rawSurfaces);
        check('login: sentinel seeded in raw DOM', html.includes(fx.sentinel), fx.a.origin);
        check('login: raw AX exposes seeded credential (pre-redaction surface)', aria.includes(fx.sentinel));
      } else {
        check(`${route.slice(1)}: no sentinel in page`, !html.includes(fx.sentinel), route);
      }
    }
    const loginB = await captureRoute(page, fx.b.origin, '/login', dirs, 'login-b');
    authorizedRaw.push(...loginB.rawSurfaces);

    // Cross-origin iframe really loads the peer origin.
    await page.goto(`${fx.a.origin}/cross-origin-iframe`, { waitUntil: 'networkidle' });
    const childFrame = page.frames().find((f) => f.url().startsWith(fx.b.origin));
    check('cross-origin iframe loads peer origin', !!childFrame, `expected child on ${fx.b.origin}`);

    // Prompt-injection content is present and sentinel-free.
    await page.goto(`${fx.a.origin}/prompt-injection`, { waitUntil: 'networkidle' });
    const injHtml = await page.content();
    check('prompt-injection: attack marker', injHtml.includes('data-maho-fixture-attack="prompt-injection"'));
    check('prompt-injection: override text', /ignore all previous instructions/i.test(injHtml));
    check('prompt-injection: no sentinel', !injHtml.includes(fx.sentinel));

    // Feed representative redacted-egress artifacts through the capture plumbing
    // to demonstrate the full leak-scan path with real data alongside them.
    capture.captureModelEvents(args.artifactDir, [{ role: 'assistant', text: 'fill requested (handle only)' }]);
    capture.capturePermissionPayloads(args.artifactDir, [{ tool: 'vault_fill_credential', fields: ['current-password'] }]);
    capture.captureWebuiEvents(args.artifactDir, [{ type: 'agent_event', summary: 'credential fill requested' }]);
    capture.captureAuditLog(args.artifactDir, [{ op: 'grant.mint', origin: fx.a.origin, ts: runStartedAt }]);
  } finally {
    await context.close(); // flushes HAR
    await browser.close();
    await fx.close();
    fs.rmSync(profileDir, { recursive: true, force: true });
  }

  // Sentinel must appear ONLY in the authorized /login raw-DOM snapshots.
  let sentinelVerdict = 'pass';
  try {
    capture.assertSentinelOnlyIn(args.artifactDir, authorizedRaw);
  } catch (err) {
    sentinelVerdict = err.message;
    check('sentinel only in authorized artifacts', false, err.message);
  }
  check('sentinel only in authorized artifacts', sentinelVerdict === 'pass', sentinelVerdict);

  const allPass = checks.every((c) => c.pass);
  const summary = {
    task: 'task-2-maho-agent-safe-vault',
    runStartedAt,
    finishedAt: new Date().toISOString(),
    origins: { a: fx.a.origin, b: fx.b.origin },
    caFingerprint: fx.caFingerprint,
    authorizedSentinelArtifacts: authorizedRaw,
    sentinelOccurrences: capture.scanForSentinel(args.artifactDir),
    checks,
    verdict: allPass ? 'PASS' : 'FAIL',
  };
  capture.writeArtifact(args.artifactDir, 'result.json', summary);
  process.stdout.write(`${JSON.stringify(summary, null, 2)}\n`);
  process.exit(allPass ? 0 : 1);
}

main().catch((err) => {
  process.stderr.write(`playwright QA failed: ${err.stack || err.message}\n`);
  process.exit(1);
});
