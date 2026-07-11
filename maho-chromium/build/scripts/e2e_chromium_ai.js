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
//   node maho/build/scripts/e2e_chromium_ai.js --list
//   node maho/build/scripts/e2e_chromium_ai.js
//   node maho/build/scripts/e2e_chromium_ai.js --scenario ai_real_turn
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

async function cdpHttp(p) {
  const r = await fetch(`http://127.0.0.1:${PORT}${p}`, { method: p.startsWith('/json/new') ? 'PUT' : 'GET' });
  return r.json();
}

async function waitForCdp(timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try { await cdpHttp('/json/version'); return true; } catch (e) { await sleep(1000); }
  }
  return false;
}

// Minimal CDP page session over the built-in WebSocket.
class Page {
  constructor(wsUrl) { this.ws = new WebSocket(wsUrl); this.id = 0; this.pending = new Map(); }
  open() {
    return new Promise((res, rej) => {
      this.ws.addEventListener('open', () => res());
      this.ws.addEventListener('error', (e) => rej(new Error('ws error')));
      this.ws.addEventListener('message', (ev) => {
        const o = JSON.parse(ev.data);
        if (o.id && this.pending.has(o.id)) { this.pending.get(o.id)(o); this.pending.delete(o.id); }
      });
    });
  }
  send(method, params = {}) {
    const i = ++this.id;
    return new Promise((res) => { this.pending.set(i, res); this.ws.send(JSON.stringify({ id: i, method, params })); });
  }
  async evalJs(expression) {
    const r = await this.send('Runtime.evaluate', { expression, returnByValue: true });
    return r.result && r.result.result ? r.result.result.value : undefined;
  }
  close() { try { this.ws.close(); } catch (e) {} }
}

async function openAiPanel() {
  await cdpHttp('/json/new?chrome://maho-ai');
  await sleep(1500);
  const targets = await cdpHttp('/json');
  const t = targets.find((x) => x.url && x.url.startsWith('chrome://maho-ai'));
  if (!t) throw new Error('no chrome://maho-ai target');
  const page = new Page(t.webSocketDebuggerUrl);
  await page.open();
  await page.send('Runtime.enable');
  await page.send('Page.enable');
  await sleep(2000);
  return page;
}

// --- Scenarios ---------------------------------------------------------------

async function scenario_ai_panel_renders() {
  const page = await openAiPanel();
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
  } finally { page.close(); }
}

async function scenario_ai_real_turn() {
  const marker = 'E2E_TURN_OK';
  const page = await openAiPanel();
  try {
    await page.evalJs(`document.querySelector('textarea').focus()`);
    await page.send('Input.insertText', { text: `Reply with exactly this token and nothing else: ${marker}` });
    await page.send('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13 });
    await page.send('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13 });
    const deadline = Date.now() + 40000;
    let lastText = '';
    while (Date.now() < deadline) {
      await sleep(2000);
      lastText = await page.evalJs(`document.body.innerText`) || '';
      // marker echoed by the user prompt too; require it to appear at least twice
      // (prompt + model reply) OR appear after a MAHO author label.
      const count = (lastText.match(new RegExp(marker, 'g')) || []).length;
      if (count >= 2) return `live turn completed; model echoed "${marker}"`;
      if (/Execution error|Something went wrong/i.test(lastText)) {
        throw new Error('turn errored: ' + lastText.replace(/\s+/g, ' ').slice(0, 200));
      }
    }
    throw new Error('no model reply within 40s; last: ' + lastText.replace(/\s+/g, ' ').slice(0, 200));
  } finally { page.close(); }
}

const SCENARIOS = {
  ai_panel_renders: scenario_ai_panel_renders,
  ai_real_turn: scenario_ai_real_turn,
};

// --- Runner ------------------------------------------------------------------

async function main() {
  const args = process.argv.slice(2);
  if (args.includes('--list')) {
    Object.keys(SCENARIOS).forEach((n) => console.log(n));
    return 0;
  }
  const selected = [];
  for (let i = 0; i < args.length; i++) {
    if (args[i] === '--scenario') selected.push(args[++i]);
  }
  const names = selected.length ? selected : Object.keys(SCENARIOS);

  const ai = resolveAiConfig();
  const needsLlm = names.includes('ai_real_turn');
  if (needsLlm && (!ai.provider || !ai.api_key || !ai.base_url)) {
    console.error('FATAL: ai_real_turn needs provider/api_key/base_url (from real profile or env).');
    return 2;
  }

  const profileDir = fs.mkdtempSync(path.join(os.tmpdir(), 'maho-ai-e2e-'));
  seedProfile(profileDir, ai);
  const logPath = path.join(profileDir, 'launch.log');
  const logFd = fs.openSync(logPath, 'w');
  const child = spawn(BIN, [
    `--remote-debugging-port=${PORT}`,
    `--user-data-dir=${profileDir}`,
    '--no-first-run',
    '--disable-features=GlassFrame',
  ], { stdio: ['ignore', logFd, logFd], detached: true });

  let failures = 0;
  try {
    if (!(await waitForCdp(45000))) throw new Error('CDP endpoint never came up');
    for (const name of names) {
      const fn = SCENARIOS[name];
      if (!fn) { console.log(`SKIP ${name} (unknown)`); continue; }
      try {
        const detail = await fn();
        console.log(`PASS ${name} — ${detail}`);
      } catch (e) {
        failures++;
        console.log(`FAIL ${name} — ${e.message}`);
      }
    }
  } catch (e) {
    console.log(`FATAL ${e.message} (see ${logPath})`);
    failures++;
  } finally {
    try { process.kill(-child.pid); } catch (e) {}
    try { fs.closeSync(logFd); } catch (e) {}
    if (process.env.MAHO_E2E_KEEP_PROFILE !== '1') {
      fs.rmSync(profileDir, { recursive: true, force: true });
    } else {
      console.log(`profile kept: ${profileDir}`);
    }
  }
  return failures === 0 ? 0 : 1;
}

main().then((code) => process.exit(code)).catch((e) => { console.error(e); process.exit(1); });
