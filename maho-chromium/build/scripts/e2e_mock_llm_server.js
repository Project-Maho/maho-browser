#!/usr/bin/env node
// Local deterministic mock LLM server for the desktop AI E2E (chrome://maho-ai).
//
// Companion to e2e_chromium_ai.js. Where `ai_real_turn` needs live credentials
// and a real endpoint, this server lets `--mock` scenarios run a full agent
// turn OFFLINE: it speaks the OpenAI-compatible streaming API the Maho agent
// uses (`{base_url}/chat/completions`, SSE `data: {json}\n\n` + `data: [DONE]`),
// plus an Anthropic-compatible `/v1/messages` endpoint for completeness.
//
// It is deterministic (echoes a token derived from the last user message so the
// panel DOM shows a detectable reply), captures every request for assertions,
// and supports error injection to exercise the panel error UI. No external deps.
//
// Programmatic use (from the harness):
//   const { start } = require('./e2e_mock_llm_server.js');
//   const mock = await start(0);            // 0 = ephemeral port
//   ...; mock.captured(); mock.reset(); mock.failNext(); await mock.close();
//
// Standalone (manual/debug):
//   node e2e_mock_llm_server.js --port 8899

'use strict';

const http = require('http');

const MARKER = 'E2E_MOCK_OK';
const PAGE_MARKER = 'MAHO_QUICK_ACTION_PAGE_CONTEXT_7F3A';

function readBody(req) {
  return new Promise((resolve) => {
    let raw = '';
    req.on('data', (c) => { raw += c; });
    req.on('end', () => resolve(raw));
  });
}

function lastUserText(body) {
  // OpenAI: {messages:[{role,content}]}. Anthropic: {messages:[{role,content}]}.
  try {
    const msgs = Array.isArray(body.messages) ? body.messages : [];
    for (let i = msgs.length - 1; i >= 0; i--) {
      if (msgs[i] && msgs[i].role === 'user') {
        const c = msgs[i].content;
        if (typeof c === 'string') return c;
        if (Array.isArray(c)) {
          return c.map((p) => (typeof p === 'string' ? p : (p && p.text) || '')).join(' ');
        }
      }
    }
  } catch (e) { /* fall through */ }
  return '';
}

// Deterministic reply text: always includes the marker and echoes the prompt so
// the panel's rendered assistant message is detectable and attributable.
function replyText(body) {
  const prompt = lastUserText(body).slice(0, 200);
  return `${MARKER} :: ${prompt}`;
}

function writeSseHeaders(res) {
  res.writeHead(200, {
    'Content-Type': 'text/event-stream',
    'Cache-Control': 'no-cache',
    'Connection': 'keep-alive',
  });
}

// Stream an OpenAI chat.completion.chunk SSE sequence for `text`.
function streamOpenAi(res, model, text) {
  writeSseHeaders(res);
  const id = 'chatcmpl-mock';
  const created = Math.floor(Date.now() / 1000);
  const base = { id, object: 'chat.completion.chunk', created, model };
  // Role delta first, then content in a couple of chunks, then finish + [DONE].
  const chunks = [
    { ...base, choices: [{ index: 0, delta: { role: 'assistant' }, finish_reason: null }] },
  ];
  const words = text.split(' ');
  const mid = Math.ceil(words.length / 2);
  chunks.push({ ...base, choices: [{ index: 0, delta: { content: words.slice(0, mid).join(' ') + ' ' }, finish_reason: null }] });
  chunks.push({ ...base, choices: [{ index: 0, delta: { content: words.slice(mid).join(' ') }, finish_reason: null }] });
  chunks.push({ ...base, choices: [{ index: 0, delta: {}, finish_reason: 'stop' }] });
  for (const c of chunks) res.write(`data: ${JSON.stringify(c)}\n\n`);
  res.write('data: [DONE]\n\n');
  res.end();
}

// Stream an Anthropic Messages SSE sequence for `text`.
function streamAnthropic(res, model, text) {
  writeSseHeaders(res);
  const ev = (name, obj) => res.write(`event: ${name}\ndata: ${JSON.stringify(obj)}\n\n`);
  ev('message_start', { type: 'message_start', message: { id: 'msg_mock', role: 'assistant', model, content: [], stop_reason: null } });
  ev('content_block_start', { type: 'content_block_start', index: 0, content_block: { type: 'text', text: '' } });
  ev('content_block_delta', { type: 'content_block_delta', index: 0, delta: { type: 'text_delta', text } });
  ev('content_block_stop', { type: 'content_block_stop', index: 0 });
  ev('message_delta', { type: 'message_delta', delta: { stop_reason: 'end_turn' } });
  ev('message_stop', { type: 'message_stop' });
  res.end();
}

function createServer(state) {
  return http.createServer(async (req, res) => {
    const url = req.url || '';
    const method = req.method || 'GET';

    if (url === '/fixture-page' && method === 'GET') {
      res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
      res.end(`<!doctype html>
<html><head><title>Maho Quick Action Fixture</title></head>
<body><main><h1>Deterministic page context</h1><p>${PAGE_MARKER}</p></main></body></html>`);
      return;
    }

    // --- Control plane (test-only) ---
    if (url === '/__captured' && method === 'GET') {
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify(state.captured));
      return;
    }
    if (url === '/__reset' && method === 'POST') {
      state.captured.length = 0;
      state.failNext = false;
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end('{"ok":true}');
      return;
    }
    if (url === '/__fail' && method === 'POST') {
      state.failNext = true;
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end('{"ok":true}');
      return;
    }

    // --- LLM endpoints (tolerate /v1 prefix variations) ---
    const isOpenAi = url.endsWith('/chat/completions');
    const isAnthropic = url.endsWith('/messages') || url.endsWith('/v1/messages');
    if (method === 'POST' && (isOpenAi || isAnthropic)) {
      const raw = await readBody(req);
      let body = {};
      try { body = JSON.parse(raw || '{}'); } catch (e) { body = {}; }
      state.captured.push({
        path: url,
        family: isOpenAi ? 'openai' : 'anthropic',
        model: body.model || null,
        messages: body.messages || null,
        raw,
        at: Date.now(),
      });

      if (state.failNext) {
        state.failNext = false;
        res.writeHead(500, { 'Content-Type': 'application/json' });
        res.end('{"error":{"message":"mock injected failure","type":"server_error"}}');
        return;
      }

      const model = body.model || 'mock-model';
      const text = replyText(body);
      if (isOpenAi) streamOpenAi(res, model, text);
      else streamAnthropic(res, model, text);
      return;
    }

    res.writeHead(404, { 'Content-Type': 'application/json' });
    res.end('{"error":"not found"}');
  });
}

// Start the mock server. Returns { url, port, captured(), reset(), failNext(), close() }.
function start(port = 0) {
  const state = { captured: [], failNext: false };
  const server = createServer(state);
  return new Promise((resolve, reject) => {
    server.on('error', reject);
    server.listen(port, '127.0.0.1', () => {
      const actualPort = server.address().port;
      resolve({
        port: actualPort,
        // base_url the agent should be pointed at (OpenAI client appends
        // `/chat/completions`; Anthropic path also resolves under this root).
        url: `http://127.0.0.1:${actualPort}/v1`,
        marker: MARKER,
        fixtureUrl: `http://127.0.0.1:${actualPort}/fixture-page`,
        pageMarker: PAGE_MARKER,
        captured: () => state.captured.slice(),
        reset: () => { state.captured.length = 0; state.failNext = false; },
        failNext: () => { state.failNext = true; },
        close: () => new Promise((r) => server.close(() => r())),
      });
    });
  });
}

module.exports = { start, MARKER, PAGE_MARKER };

// Standalone entry for manual debugging.
if (require.main === module) {
  const args = process.argv.slice(2);
  let port = 0;
  const i = args.indexOf('--port');
  if (i >= 0) port = parseInt(args[i + 1], 10) || 0;
  start(port).then((m) => {
    console.log(`MOCK_LLM base_url=${m.url} marker=${m.marker}`);
    console.log('Endpoints: POST /v1/chat/completions, POST /v1/messages');
    console.log('Control:   GET /__captured, POST /__reset, POST /__fail');
  });
}
