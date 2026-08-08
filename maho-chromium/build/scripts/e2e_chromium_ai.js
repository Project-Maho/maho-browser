#!/usr/bin/env node
// Chromium desktop macOS E2E — AI panel (chrome://maho-ai).
//
// Companion to e2e_chromium_sidebar.py. Where the sidebar harness drives
// Accessibility APIs, this one drives the AI WebUI (React) over the Chrome
// DevTools Protocol, which is far easier for DOM/typing on a web surface.
//
// It seeds a temp profile with the maho.ai provider config (read from the
// real profile by default, override via env) so real agent turns run against
// the configured LLM endpoint. No credentials are hardcoded.
//
//   node .../e2e_chromium_ai.js --list
//   node .../e2e_chromium_ai.js                        # launch, run all, exit
//   node .../e2e_chromium_ai.js --scenario ai_real_turn
//   node .../e2e_chromium_ai.js --serve                # keep a persistent instance alive
//   MAHO_E2E_CDP_PORT=9422 node .../e2e_chromium_ai.js --attach --scenario ai_panel_renders
//
// SESSION-INDEPENDENT TESTING (why --serve/--attach + a dedicated out dir):
//   Other dev sessions constantly rebuild chromium/src/out/Default and may kill
//   Maho. Two empirically-verified facts drive this design:
//     1. A RUNNING macOS process keeps its mmap'd dylib inodes even when the
//        files on disk are relinked — so a `--serve`d instance SURVIVES
//        out/Default file churn (rebuilds). Drive it repeatedly via `--attach`.
//     2. Relocating a component build (copying Maho.app + its ~530 sibling
//        dylibs elsewhere) does NOT work — the relocated app binds its port but
//        hangs during init and never serves /json (verified: even after
//        codesign --force --deep). So do NOT snapshot/clone the runtime set;
//        there is no supported way to freeze a component build off out/Default.
//   For a binary that is ALSO immune to out/Default rebuilds AT REST, build into
//   a dedicated dir once and point MAHO_APP_PATH at it:
//     python3 maho-chromium/build/scripts/build_maho.py --out-dir out/E2E
//     MAHO_APP_PATH=.../chromium/src/out/E2E/Maho.app node .../e2e_chromium_ai.js --serve
//   CAVEAT: a broad `pkill Maho` from another session still kills any instance
//   (shared process name) — unavoidable. Restart --serve to recover.
//
// Env:
//   MAHO_APP_PATH            app bundle (default: out/Default/Maho.app)
//   MAHO_E2E_AI_PROVIDER/_API_KEY/_BASE_URL/_MODEL   provider overrides
//   MAHO_E2E_SOURCE_PROFILE  profile to read ai config from (default: ~/Library/Application Support/Maho)
//   MAHO_E2E_CDP_PORT        remote-debugging port (default: 9422)
//   MAHO_E2E_KEEP_PROFILE=1  keep the temp profile after the run

const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn } = require('child_process');
const mockLlm = require('./e2e_mock_llm_server.js');

// Set in --mock mode: a running local mock LLM server the panel is pointed at.
let MOCK = null;

if (typeof WebSocket === 'undefined' || typeof fetch === 'undefined') {
  console.error(`FATAL: needs Node >= 21 for global WebSocket/fetch (running ${process.version}).`);
  process.exit(2);
}

const REPO = path.resolve(__dirname, '..', '..', '..');
const APP = process.env.MAHO_APP_PATH ||
  path.join(REPO, 'chromium/src/out/Default/Maho.app');
const BIN = path.join(APP, 'Contents/MacOS/Maho');
const PORT = parseInt(process.env.MAHO_E2E_CDP_PORT || '9422', 10);
const SOURCE_PROFILE = process.env.MAHO_E2E_SOURCE_PROFILE ||
  path.join(os.homedir(), 'Library/Application Support/Maho');

function resolveAiConfig() {
  let ai = {};
  try {
    const prefs = JSON.parse(
      fs.readFileSync(path.join(SOURCE_PROFILE, 'Default/Preferences'), 'utf8'));
    ai = (prefs.maho && prefs.maho.ai) || {};
  } catch (e) { /* no source profile; rely on env */ }
  return {
    provider: process.env.MAHO_E2E_AI_PROVIDER || ai.provider || '',
    api_key: process.env.MAHO_E2E_AI_API_KEY || ai.api_key || '',
    base_url: process.env.MAHO_E2E_AI_BASE_URL || ai.base_url || '',
    model: process.env.MAHO_E2E_AI_MODEL || ai.model || '',
  };
}

function seedProfile(dir, ai) {
  fs.mkdirSync(path.join(dir, 'Default'), { recursive: true });
  const prefs = { maho: { ai } };
  fs.writeFileSync(path.join(dir, 'Default/Preferences'), JSON.stringify(prefs));
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function cdpHttp(p, port) {
  const r = await fetch(`http://127.0.0.1:${port}${p}`,
    { method: p.startsWith('/json/new') ? 'PUT' : 'GET' });
  return r.json();
}

async function waitForCdp(timeoutMs, port) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try { await cdpHttp('/json/version', port); return true; } catch (e) { await sleep(1000); }
  }
  return false;
}

// Minimal CDP page session over the built-in WebSocket.
class Page {
  constructor(wsUrl) {
    this.ws = new WebSocket(wsUrl);
    this.id = 0;
    this.pending = new Map();
    this._openReject = null;
  }
  open() {
    return new Promise((res, rej) => {
      this._openReject = rej;
      this.ws.addEventListener('open', () => { this._openReject = null; res(); });
      this.ws.addEventListener('error', () => this._rejectAll(new Error('ws error')));
      this.ws.addEventListener('close', () => this._rejectAll(new Error('ws closed')));
      this.ws.addEventListener('message', (ev) => {
        const o = JSON.parse(ev.data);
        const p = o.id && this.pending.get(o.id);
        if (p) { clearTimeout(p.timer); this.pending.delete(o.id); p.res(o); }
      });
    });
  }
  // Settle every in-flight request when the socket drops, so a mid-scenario
  // `pkill Maho` (an expected event) surfaces as a rejection instead of an
  // infinite hang that no scenario deadline can interrupt.
  _rejectAll(err) {
    if (this._openReject) { this._openReject(err); this._openReject = null; }
    for (const [, p] of this.pending) { clearTimeout(p.timer); p.rej(err); }
    this.pending.clear();
  }
  send(method, params = {}, timeoutMs = 15000) {
    const i = ++this.id;
    return new Promise((res, rej) => {
      const timer = setTimeout(() => { this.pending.delete(i); rej(new Error(`CDP ${method} timed out after ${timeoutMs}ms`)); }, timeoutMs);
      this.pending.set(i, { res, rej, timer });
      try { this.ws.send(JSON.stringify({ id: i, method, params })); }
      catch (e) { clearTimeout(timer); this.pending.delete(i); rej(e); }
    });
  }
  async evalJs(expression) {
    const r = await this.send('Runtime.evaluate', { expression, returnByValue: true });
    return r.result && r.result.result ? r.result.result.value : undefined;
  }
  close() { try { this.ws.close(); } catch (e) {} }
}

async function openAiPanel(port) {
  // chrome://maho-ai is a browser_ui SINGLETON: /json/new opens it but is
  // idempotent (repeated calls never accumulate tabs — /json always shows
  // exactly one), and the PUT response id does NOT match the stable target in
  // /json. So open it, let it settle, then attach to the singleton found by url.
  // Staleness across repeated --attach is handled by ai_real_turn's marker
  // baseline-diff, not by tab isolation.
  await cdpHttp('/json/new?chrome://maho-ai', port);
  await sleep(1500);
  let target;
  const deadline = Date.now() + 8000;
  while (Date.now() < deadline) {
    const targets = await cdpHttp('/json', port);
    target = targets.find((x) => x.url && x.url.startsWith('chrome://maho-ai') && x.webSocketDebuggerUrl);
    if (target) break;
    await sleep(500);
  }
  if (!target) throw new Error('no chrome://maho-ai target with a debugger url');
  const page = new Page(target.webSocketDebuggerUrl);
  try {
    await page.open();
    await page.send('Runtime.enable');
    await page.send('Page.enable');
    await sleep(2000);
    return page;
  } catch (e) {
    page.close();
    throw e;
  }
}

async function openPage(url, port) {
  await cdpHttp(`/json/new?${encodeURIComponent(url)}`, port);
  const deadline = Date.now() + 8000;
  let target;
  while (Date.now() < deadline) {
    const targets = await cdpHttp('/json', port);
    target = targets.find((candidate) => candidate.url === url && candidate.webSocketDebuggerUrl);
    if (target) break;
    await sleep(250);
  }
  if (!target) throw new Error(`no debugger target for ${url}`);
  const page = new Page(target.webSocketDebuggerUrl);
  try {
    await page.open();
    await page.send('Runtime.enable');
    await page.send('Page.enable');
    return page;
  } catch (e) {
    page.close();
    throw e;
  }
}

// --- Scenarios ---------------------------------------------------------------

async function scenario_ai_panel_renders(port) {
  const page = await openAiPanel(port);
  try {
    const state = JSON.parse(await page.evalJs(`JSON.stringify({
      title: document.title,
      hasInput: !!document.querySelector('textarea'),
      bodyLen: document.body ? document.body.innerHTML.length : 0
    })`));
    if (state.title !== 'Maho AI') throw new Error(`title=${state.title}`);
    if (!state.hasInput) throw new Error('no prompt textarea');
    if (state.bodyLen < 500) throw new Error(`body too small (${state.bodyLen}) — WebUI did not render`);
    return `rendered (title="${state.title}", dom=${state.bodyLen}B, input present)`;
  } finally { await page.close(); }
}

async function scenario_ai_real_turn(port) {
  const marker = 'E2E_TURN_OK';
  const page = await openAiPanel(port);
  try {
    await page.evalJs(`document.querySelector('textarea').focus()`);
    await page.send('Input.insertText', { text: `Reply with exactly this token and nothing else: ${marker}` });
    await page.send('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13 });
    await page.send('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13 });
    const countMarkers = async () =>
      ((await page.evalJs(`document.body.innerText`) || '').match(new RegExp(marker, 'g')) || []).length;
    // The prompt echoes the marker, so it already appears in the rendered user
    // message. Baseline that count once the prompt shows, then require a real
    // model reply to push it strictly higher — robust against UIs that render
    // the prompt in 1..N places.
    await sleep(1500);
    const baseline = await countMarkers();
    const deadline = Date.now() + 40000;
    while (Date.now() < deadline) {
      await sleep(2000);
      if ((await countMarkers()) > baseline) return `live turn completed; model echoed "${marker}"`;
      const text = await page.evalJs(`document.body.innerText`) || '';
      if (/Execution error|Something went wrong/i.test(text)) {
        throw new Error('turn errored: ' + text.replace(/\s+/g, ' ').slice(0, 200));
      }
    }
    throw new Error(`no new model reply within 40s (marker baseline ${baseline} unchanged)`);
  } finally { await page.close(); }
}

async function submitPromptViaComposer(page, text) {
  await page.evalJs(`document.querySelector('textarea').focus()`);
  await page.send('Input.insertText', { text });
  await page.send('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13 });
  await page.send('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13 });
}

async function waitForBodyText(page, needle, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const text = await page.evalJs(`document.body.innerText`) || '';
    if (text.includes(needle)) return text;
    await sleep(1000);
  }
  return null;
}

// mock scenarios require a running MOCK (set by --mock). MOCK.marker only ever
// appears in a model reply (never in the echoed prompt), so its presence in the
// DOM proves a completed streaming turn.
async function scenario_ai_mock_general_turn(port) {
  if (!MOCK) throw new Error('ai_mock_general_turn requires --mock');
  MOCK.reset();
  const page = await openAiPanel(port);
  try {
    // When: a general message is submitted through the composer.
    await submitPromptViaComposer(page, 'Explain quantum tunneling in one sentence.');
    const body = await waitForBodyText(page, MOCK.marker, 40000);
    if (!body) throw new Error(`no streamed reply (marker "${MOCK.marker}") within 40s`);

    // Then: the mock received a request with the configured provider/model and prompt.
    const captured = MOCK.captured();
    const req = captured[captured.length - 1];
    if (!req) throw new Error('mock captured no request');
    if (req.family !== 'openai') throw new Error(`unexpected family ${req.family}`);
    if (!req.model) throw new Error('request carried no model');
    if (!(req.raw || '').includes('quantum tunneling')) throw new Error('request did not carry the prompt');
    return `general turn: model=${req.model}, prompt forwarded, reply streamed to DOM`;
  } finally { await page.close(); }
}

async function scenario_ai_mock_quick_action_context(port) {
  if (!MOCK) throw new Error('ai_mock_quick_action_context requires --mock');
  MOCK.reset();
  const fixture = await openPage(MOCK.fixtureUrl, port);
  const panel = await openAiPanel(port);
  try {
    await fixture.send('Page.bringToFront');
    const fixtureReady = await fixture.evalJs(
      `document.body.innerText.includes(${JSON.stringify(MOCK.pageMarker)})`);
    if (!fixtureReady) throw new Error('fixture page did not render its context marker');

    const clicked = await panel.evalJs(`(() => {
      const button = Array.from(document.querySelectorAll('button'))
        .find((candidate) => candidate.textContent.trim() === 'Summarize');
      if (!button) return false;
      button.click();
      return true;
    })()`);
    if (!clicked) throw new Error('Summarize quick action was not visible');

    const body = await waitForBodyText(panel, MOCK.marker, 40000);
    if (!body) throw new Error(`no streamed quick-action reply (marker "${MOCK.marker}") within 40s`);
    const request = MOCK.captured().find((candidate) =>
      /\/chat\/completions$|\/messages$/.test(candidate.path));
    if (!request) throw new Error('mock captured no quick-action LLM request');
    if (!request.raw.includes('Summarize this page.')) {
      throw new Error('quick-action intent was absent from the LLM request');
    }
    if (!request.raw.includes(MOCK.pageMarker)) {
      throw new Error('active page context was absent from the LLM request');
    }
    return 'Summarize forwarded its intent and active-tab page context to the mock LLM';
  } finally {
    panel.close();
    fixture.close();
  }
}

async function scenario_ai_mock_server_error(port) {
  if (!MOCK) throw new Error('ai_mock_server_error requires --mock');
  MOCK.reset();
  MOCK.failNext();
  const page = await openAiPanel(port);
  try {
    // When: the mock returns HTTP 500 for the turn.
    await submitPromptViaComposer(page, 'Trigger a server error please.');
    const errText = await waitForBodyTextMatching(page, /Execution error|Something went wrong|Issue/i, 40000);
    if (!errText) throw new Error('panel did not surface an error UI after a 500 response');
    if (!MOCK.captured().length) throw new Error('mock never received the failing request');
    return 'server error surfaced in panel error UI';
  } finally { await page.close(); }
}

async function waitForBodyTextMatching(page, regex, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const text = await page.evalJs(`document.body.innerText`) || '';
    if (regex.test(text)) return text;
    await sleep(1000);
  }
  return null;
}

// Runs against a NO-KEY profile (--mock with MAHO_E2E_AI_API_KEY=""). With no
// credential the agent must surface a credential error and send ZERO requests.
async function scenario_ai_mock_key_cleared_no_request(port) {
  if (!MOCK) throw new Error('ai_mock_key_cleared_no_request requires --mock');
  MOCK.reset();
  const page = await openAiPanel(port);
  try {
    // When: a message is submitted while no key is configured.
    await submitPromptViaComposer(page, 'This must not reach the LLM endpoint.');
    await sleep(6000);
    // Then: the mock received no LLM chat request.
    const chatReqs = MOCK.captured().filter(r => /\/chat\/completions$|\/messages$/.test(r.path));
    if (chatReqs.length !== 0) throw new Error(`expected 0 LLM requests with no key, saw ${chatReqs.length}`);
    return 'no LLM request sent when key is absent';
  } finally { await page.close(); }
}

const SCENARIOS = {
  ai_panel_renders: scenario_ai_panel_renders,
  ai_real_turn: scenario_ai_real_turn,
  ai_mock_general_turn: scenario_ai_mock_general_turn,
  ai_mock_quick_action_context: scenario_ai_mock_quick_action_context,
  ai_mock_server_error: scenario_ai_mock_server_error,
  ai_mock_key_cleared_no_request: scenario_ai_mock_key_cleared_no_request,
};

// --- Instance lifecycle (reused by default/serve) ----------------------------

function rmProfile(dir) {
  if (process.env.MAHO_E2E_KEEP_PROFILE === '1') { console.log(`profile kept: ${dir}`); return; }
  try { fs.rmSync(dir, { recursive: true, force: true, maxRetries: 8, retryDelay: 250 }); } catch (e) {}
}

function launchInstance({ bin, port, profileDir, aiConfig }) {
  seedProfile(profileDir, aiConfig || resolveAiConfig());
  const logPath = path.join(profileDir, 'launch.log');
  const logFd = fs.openSync(logPath, 'w');
  const child = spawn(bin, [
    `--remote-debugging-port=${port}`,
    `--user-data-dir=${profileDir}`,
    '--no-first-run',
    '--disable-features=GlassFrame',
  ], { stdio: ['ignore', logFd, logFd], detached: true });
  child.on('error', (e) => { console.log(`FATAL launch: ${e.message}`); });
  const cleanup = () => {
    try { process.kill(-child.pid); } catch (e) {}
    try { fs.closeSync(logFd); } catch (e) {}
  };
  return { child, cleanup, logPath };
}

async function runScenarios(names, port) {
  let failures = 0;
  for (const name of names) {
    const fn = SCENARIOS[name];
    if (!fn) { console.log(`SKIP ${name} (unknown)`); continue; }
    try {
      const detail = await fn(port);
      console.log(`PASS ${name} — ${detail}`);
    } catch (e) {
      failures++;
      console.log(`FAIL ${name} — ${e.message}`);
    }
  }
  return failures;
}

// --- Runner ------------------------------------------------------------------

function hasFlag(args, f) { return args.includes(f); }

async function main() {
  const args = process.argv.slice(2);
  if (hasFlag(args, '--list')) {
    Object.keys(SCENARIOS).forEach((n) => { console.log(n); });
    return 0;
  }
  const selected = [];
  for (let i = 0; i < args.length; i++) {
    if (args[i] === '--scenario') selected.push(args[++i]);
  }
  const names = selected.length ? selected : Object.keys(SCENARIOS);
  const serve = hasFlag(args, '--serve');
  const attach = hasFlag(args, '--attach');
  const mock = hasFlag(args, '--mock');

  if (mock) {
    MOCK = await mockLlm.start(0);
    console.log(`MOCK_LLM base_url=${MOCK.url}`);
  }
  const closeMock = async () => { if (MOCK) { try { await MOCK.close(); } catch (e) {} MOCK = null; } };

  if (attach) {
    console.log(`ATTACH port=${PORT}`);
    const failures = await runScenarios(names, PORT);
    await closeMock();
    return failures === 0 ? 0 : 1;
  }

  const ai = mock ? {
    provider: 'openai',
    api_key: ('MAHO_E2E_AI_API_KEY' in process.env) ? process.env.MAHO_E2E_AI_API_KEY : 'test-key-mock',
    base_url: MOCK.url,
    model: process.env.MAHO_E2E_AI_MODEL || 'gpt-4o-mini',
  } : resolveAiConfig();
  const needsLlm = names.includes('ai_real_turn') && !serve && !mock;
  if (needsLlm && (!ai.provider || !ai.api_key || !ai.base_url)) {
    console.error('FATAL: ai_real_turn needs provider/api_key/base_url (from real profile or env).');
    return 2;
  }

  console.log(`BIN=${BIN}`);

  const profileDir = fs.mkdtempSync(path.join(os.tmpdir(), 'maho-ai-e2e-'));
  const { child, cleanup } = launchInstance({ bin: BIN, port: PORT, profileDir, aiConfig: ai });

  if (serve) {
    if (!(await waitForCdp(45000, PORT))) {
      cleanup(); rmProfile(profileDir);
      console.log('FATAL serve: CDP endpoint never came up');
      return 1;
    }
    console.log(`CDP_PORT=${PORT}`);
    console.log(`PID=${child.pid}`);
    console.log(`PROFILE=${profileDir}`);
    console.log('SERVING (send SIGINT/SIGTERM to stop)');
    const shutdown = () => { cleanup(); rmProfile(profileDir); process.exit(0); };
    process.on('SIGINT', shutdown);
    process.on('SIGTERM', shutdown);
    await new Promise(() => {});
    return 0;
  }

  let failures = 0;
  const shutdown = () => { cleanup(); rmProfile(profileDir); process.exit(130); };
  process.on('SIGINT', shutdown);
  process.on('SIGTERM', shutdown);
  try {
    if (!(await waitForCdp(45000, PORT))) throw new Error('CDP endpoint never came up');
    failures = await runScenarios(names, PORT);
  } catch (e) {
    console.log(`FATAL ${e.message}`);
    failures++;
  } finally {
    cleanup();
    rmProfile(profileDir);
    await closeMock();
  }
  return failures === 0 ? 0 : 1;
}

main().then((code) => process.exit(code)).catch((e) => { console.error(e); process.exit(1); });
