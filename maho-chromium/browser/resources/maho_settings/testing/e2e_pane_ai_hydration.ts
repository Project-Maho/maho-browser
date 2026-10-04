#!/usr/bin/env bun
// Owns a headless browser and disposable profile; never attaches to port 9222.
import {spawn} from 'node:child_process';
import {mkdtemp, mkdir, writeFile, rm} from 'node:fs/promises';
import {resolve} from 'node:path';
import {CDP, waitForCDPEvent} from './cdp_harness';
import {strict as assert} from 'node:assert';

const root = resolve(import.meta.dir, '../../../../..');
const binary = resolve(root, process.env.PHASE0_BINARY ?? 'chromium/src/out/Default/Maho.app/Contents/MacOS/Maho');
const artifacts = resolve(root, '.omo/qa/phase0');
await mkdir(artifacts, {recursive: true});
const profile = await mkdtemp(`${artifacts}/profile-`);
await mkdir(`${profile}/Default`);
await writeFile(`${profile}/Default/Preferences`, JSON.stringify({maho: {ai: {
  provider: 'openai-compatible', base_url: 'http://127.0.0.1:18801',
  model: 'gpt-oss-120b-medium', api_key: 'phase0-disposable-non-secret',
  settings_migrated_from_macos_defaults: true, settings_migrated_from_google_provider: true,
}}}));
const args = ['--headless=new', `--user-data-dir=${profile}`, '--remote-debugging-port=0', '--no-first-run', '--no-default-browser-check', '--use-mock-keychain', '--password-store=basic', '--enable-logging=stderr', 'about:blank'];
console.log(JSON.stringify({binary, args, profile}));
const child = spawn(binary, args, {stdio: ['ignore', 'ignore', 'pipe']});
const exited = new Promise<void>(resolve => child.once('exit', () => resolve()));
let ws: WebSocket | undefined;
try {
  const endpoint = await new Promise<string>((resolve, reject) => {
    let text = '';
    const timeout = setTimeout(() => reject(new Error(`Headless CDP startup timeout: ${text.slice(-4000)}`)), 20000);
    child.stderr.on('data', chunk => {
      text += String(chunk);
      const match = text.match(/DevTools listening on (ws:\/\/[^\s]+)/);
      if (match) { clearTimeout(timeout); resolve(match[1]); }
    });
    child.once('error', error => { clearTimeout(timeout); reject(error); });
    child.once('exit', code => { clearTimeout(timeout); reject(new Error(`Browser exited ${code}: ${text.slice(-4000)}`)); });
  });
  const host = new URL(endpoint).origin.replace('ws:', 'http:');
  // Create our page explicitly after CDP startup; the initial target may not exist yet.
  const response = await fetch(`${host}/json/new?about:blank`, {method: 'PUT'});
  assert(response.ok, 'Isolated CDP target creation succeeds');
  const target = await response.json() as {webSocketDebuggerUrl: string};
  ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise<void>((resolve, reject) => { ws!.addEventListener('open', () => resolve(), {once: true}); ws!.addEventListener('error', () => reject(new Error('CDP connection failed')), {once: true}); });
  const cdp = new CDP(ws);
  await cdp.send('Page.enable');
  await cdp.send('Runtime.enable');
  await cdp.send('Page.addScriptToEvaluateOnNewDocument', {source: `
    window.__providerWrites = [];
    let store;
    Object.defineProperty(window, 'settingsStore', {configurable: true,
      get: () => store,
      set: value => {
        store = value;
        const handler = store.getHandler();
        const query = new URLSearchParams(location.search);
        if (query.has('phase0Provider')) {
          const getAISettings = handler.getAISettings.bind(handler);
          handler.getAISettings = async () => {
            const result = await getAISettings();
            return {...result, settings: {...result.settings, provider: query.get('phase0Provider')}};
          };
        }
        handler.setAIProvider = value => { window.__providerWrites.push(value); return Promise.resolve(); };
      }
    });
  `});
  const unresolved: {phase: string; display: string}[] = [];
  for (const phase of ['initial', 'reload', 'empty', 'unknown']) {
    const forcedProvider = phase === 'empty' ? '' : 'phase0-unknown-provider';
    const url = 'chrome://maho-settings/?pane=maho-ai' +
        (phase === 'empty' || phase === 'unknown' ? `&phase0Provider=${encodeURIComponent(forcedProvider)}` : '');
    await waitForCDPEvent(cdp, 'Page.loadEventFired', () => cdp.send(phase === 'reload' ? 'Page.reload' : 'Page.navigate', phase === 'reload' ? {} : {url}));
    const result = await cdp.eval(`new Promise((resolve, reject) => {
      const timeout = setTimeout(() => { observer.disconnect(); reject(new Error('AI hydration did not complete')); }, 10000);
      const inspect = async () => {
        const control = document.querySelector('button[role="combobox"][aria-label="Provider"]');
        if (!control) return;
        observer.disconnect(); clearTimeout(timeout);
        const {settings} = await window.settingsStore.getHandler().getAISettings();
        resolve({url: location.href, provider: settings.provider, baseUrl: settings.baseUrl, model: settings.model,
          hasApiKey: settings.hasApiKey, display: control.textContent,
          baseControl: document.querySelector('input[aria-label="Base URL"]')?.value,
          keyPresent: !!document.querySelector('input[aria-label="API key"]')?.value,
          anthropicKey: !!document.querySelector('input[aria-label="Anthropic BYOK Key"]'),
          anthropicSignIn: document.body.textContent.includes('Sign in with Anthropic'),
          writes: window.__providerWrites});
      };
      const observer = new MutationObserver(inspect);
      observer.observe(document, {subtree: true, childList: true, attributes: true});
      inspect();
    })`, true) as any;
    console.log(phase, JSON.stringify(result));
    if (phase === 'empty' || phase === 'unknown') {
      assert.equal(result.provider, forcedProvider, `${phase}: injected unresolved snapshot reached the pane`);
      assert.equal(result.anthropicKey, false, `${phase}: no Anthropic BYOK field`);
      assert.equal(result.anthropicSignIn, false, `${phase}: no Anthropic sign-in`);
      assert.deepEqual(result.writes, [], `${phase}: no provider writes during unresolved hydration`);
      for (const label of ['Anthropic', 'OpenAI', 'OpenAI-compatible (custom URL)', 'Local Server (Ollama)', 'Sign in with Anthropic']) {
        assert.equal(result.display.includes(label), false, `${phase}: unresolved trigger must not display ${label}`);
      }
      console.log(`PASS: ${phase} trigger does not masquerade as Anthropic or any real provider`);
      unresolved.push({phase, display: result.display});
      continue;
    }
    assert.equal(result.provider, 'openai-compatible', `${phase}: Mojo snapshot provider matches seeded prefs`);
    assert.equal(result.display, 'OpenAI-compatible (custom URL)', `${phase}: provider combobox must reflect openai-compatible`);
    assert.equal(result.baseControl, 'http://127.0.0.1:18801', `${phase}: custom base URL control`);
    assert.equal(result.keyPresent, true, `${phase}: custom key presence`);
    assert.equal(result.anthropicKey, false, `${phase}: no Anthropic BYOK field`);
    assert.equal(result.anthropicSignIn, false, `${phase}: no Anthropic sign-in`);
    assert.deepEqual(result.writes, [], `${phase}: no provider writes during hydration`);
    assert.equal(result.model, 'gpt-oss-120b-medium', `${phase}: model preserved`);
  }
  console.log('PASS: isolated AI hydration and reload');
  for (const result of unresolved) {
    assert.notEqual(result.display.trim(), '', `${result.phase}: unresolved provider trigger must show an explicit unresolved marker, not a blank value`);
  }
} finally {
  ws?.close();
  child.kill('SIGTERM');
  await exited;
  await rm(profile, {recursive: true, force: true});
  console.log(`CLEANUP: owned PID ${child.pid} exited; disposable profile removed: ${profile}`);
}
