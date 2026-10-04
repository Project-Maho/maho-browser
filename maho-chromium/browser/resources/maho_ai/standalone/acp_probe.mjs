/**
 * acp_probe.mjs — Maho ACP end-to-end protocol probe
 *
 * Spawns `opencode acp`, exercises the full JSON-RPC wire flow, and exits
 * with code 0 on success or non-zero on any protocol / logical failure.
 *
 * Run from the workspace root:
 *   node maho-chromium/browser/resources/maho_ai/standalone/acp_probe.mjs
 *
 * See standalone/ACP_PROBE.md for full documentation.
 */

import {spawn} from 'node:child_process';
import {writeFileSync, mkdirSync} from 'node:fs';
import {dirname} from 'node:path';

// ---------------------------------------------------------------------------
// Configuration via environment variables
// ---------------------------------------------------------------------------

const OPENCODE_BINARY = process.env.OPENCODE_BINARY || 'opencode';
const PROBE_PROMPT = process.env.OPENCODE_PROBE_PROMPT ||
    'Reply with the single word PROBE_OK and nothing else.';
const TIMEOUT_MS = Number(process.env.OPENCODE_PROBE_TIMEOUT_MS || '60000');
const LOG_FILE = '/var/folders/zh/7cc25lt91b1_dj577306nwdh0000gn/T/opencode/maho-ai-acp-probe.log';

// ---------------------------------------------------------------------------
// Structured logging
// ---------------------------------------------------------------------------

const logLines = [];

function log(msg) {
  const line = `[probe] ${msg}`;
  process.stdout.write(line + '\n');
  logLines.push(line);
}

function logAcpStderr(data) {
  const text = data.toString('utf8').replace(/\n$/, '');
  for (const line of text.split('\n')) {
    const prefixed = `[acp stderr] ${line}`;
    process.stderr.write(prefixed + '\n');
    logLines.push(prefixed);
  }
}

function saveLog() {
  try {
    const dir = dirname(LOG_FILE);
    mkdirSync(dir, {recursive: true});
    writeFileSync(LOG_FILE, logLines.join('\n') + '\n', 'utf8');
  } catch (err) {
    process.stderr.write(`[probe] WARNING: could not write log file: ${err.message}\n`);
  }
}

// ---------------------------------------------------------------------------
// Child process management
// ---------------------------------------------------------------------------

/** Gracefully shut down the child, then SIGTERM / SIGKILL as fallback. */
async function shutdownChild(child) {
  if (child.exitCode !== null || child.signalCode !== null) return;

  // Signal EOF on stdin so the child can self-terminate cleanly.
  try { child.stdin.end(); } catch (_) {}

  // Wait up to 1.5 s for natural exit.
  const exitedNaturally = await new Promise(resolve => {
    const tid = setTimeout(() => resolve(false), 1500);
    child.once('exit', () => { clearTimeout(tid); resolve(true); });
  });

  if (!exitedNaturally && child.exitCode === null && child.signalCode === null) {
    log('child did not exit after stdin close — sending SIGTERM');
    try { child.kill('SIGTERM'); } catch (_) {}
    const exitedOnTerm = await new Promise(resolve => {
      const tid = setTimeout(() => resolve(false), 1000);
      child.once('exit', () => { clearTimeout(tid); resolve(true); });
    });

    if (!exitedOnTerm && child.exitCode === null && child.signalCode === null) {
      log('child still alive after SIGTERM — sending SIGKILL');
      try { child.kill('SIGKILL'); } catch (_) {}
    }
  }
}

// ---------------------------------------------------------------------------
// nd-JSON transport (line-buffered, handles chunked reads)
// ---------------------------------------------------------------------------

/**
 * Creates a transport bound to a child process.
 * Returns { sendRequest, sendNotification, onNotification, onRequest, close }.
 */
function createTransport(child) {
  let nextId = 1;
  const pending = new Map();         // id -> { resolve, reject }
  const notificationHandlers = new Map();  // method -> handler fn
  const requestHandlers = new Map();       // method -> handler fn
  let lineBuffer = '';
  let closed = false;

  function sendRaw(obj) {
    if (closed) throw new Error('transport closed');
    const line = JSON.stringify(obj) + '\n';
    child.stdin.write(line, 'utf8');
  }

  function sendRequest(method, params) {
    const id = nextId++;
    return new Promise((resolve, reject) => {
      pending.set(id, {resolve, reject});
      sendRaw({jsonrpc: '2.0', id, method, params});
    });
  }

  function sendNotification(method, params) {
    // Notifications have no id and expect no response.
    const msg = {jsonrpc: '2.0', method};
    if (params !== undefined) msg.params = params;
    sendRaw(msg);
  }

  function sendResponse(id, result) {
    sendRaw({jsonrpc: '2.0', id, result});
  }

  function sendErrorResponse(id, code, message) {
    sendRaw({jsonrpc: '2.0', id, error: {code, message}});
  }

  function dispatchLine(rawLine) {
    const trimmed = rawLine.trim();
    if (trimmed === '') return;

    let msg;
    try {
      msg = JSON.parse(trimmed);
    } catch (e) {
      log(`FAIL inbound line is not valid JSON: ${e.message} — raw: ${trimmed.slice(0, 120)}`);
      return;
    }

    if (typeof msg !== 'object' || msg === null || msg.jsonrpc !== '2.0') {
      log(`FAIL inbound message is not JSON-RPC 2.0: ${trimmed.slice(0, 120)}`);
      return;
    }

    // Inbound response to one of our requests.
    if ('id' in msg && 'id' in msg && (msg.result !== undefined || msg.error !== undefined)) {
      // But only if it has no 'method' (distinguishes from inbound requests).
      if (!('method' in msg)) {
        const entry = pending.get(msg.id);
        if (!entry) {
          log(`WARNING: received response for unknown id ${msg.id}`);
          return;
        }
        pending.delete(msg.id);
        if (msg.error !== undefined) {
          entry.reject(Object.assign(new Error(msg.error.message || 'JSON-RPC error'), {
            code: msg.error.code,
            data: msg.error.data,
          }));
        } else {
          entry.resolve(msg.result);
        }
        return;
      }
    }

    // Inbound request from the agent (has id + method, expects a reply).
    if ('id' in msg && 'method' in msg) {
      const handler = requestHandlers.get(msg.method);
      if (!handler) {
        log(`received inbound request for unhandled method "${msg.method}" (id ${msg.id}) — replying Method not found`);
        sendErrorResponse(msg.id, -32601, 'Method not found');
        return;
      }
      Promise.resolve()
          .then(() => handler(msg.params, msg.id))
          .then(result => sendResponse(msg.id, result))
          .catch(err => {
            log(`handler for "${msg.method}" threw: ${err.message}`);
            sendErrorResponse(msg.id, -32603, err.message || 'Internal error');
          });
      return;
    }

    // Inbound notification (no id).
    if ('method' in msg && !('id' in msg)) {
      const handler = notificationHandlers.get(msg.method);
      if (handler) {
        try { handler(msg.params); } catch (e) {
          log(`notification handler for "${msg.method}" threw: ${e.message}`);
        }
      }
      // Unknown notifications are silently ignored (additive-safe).
      return;
    }

    log(`WARNING: could not classify inbound message: ${trimmed.slice(0, 120)}`);
  }

  // Attach stdout reader using chunked/buffered reading.
  child.stdout.setEncoding('utf8');
  child.stdout.on('data', chunk => {
    lineBuffer += chunk;
    let nl;
    while ((nl = lineBuffer.indexOf('\n')) !== -1) {
      const line = lineBuffer.slice(0, nl);
      lineBuffer = lineBuffer.slice(nl + 1);
      dispatchLine(line);
    }
  });

  child.stdout.on('end', () => {
    closed = true;
    // Reject any still-pending requests.
    for (const [id, entry] of pending) {
      entry.reject(new Error('transport closed (stdout EOF)'));
    }
    pending.clear();
  });

  return {
    sendRequest,
    sendNotification,
    onNotification(method, handler) { notificationHandlers.set(method, handler); },
    onRequest(method, handler) { requestHandlers.set(method, handler); },
    close() {
      closed = true;
      try { child.stdin.end(); } catch (_) {}
    },
  };
}

// ---------------------------------------------------------------------------
// Main probe sequence
// ---------------------------------------------------------------------------

async function runProbe() {
  log(`spawning: ${OPENCODE_BINARY} acp`);

  let child;
  try {
    child = spawn(OPENCODE_BINARY, ['acp'], {
      stdio: ['pipe', 'pipe', 'pipe'],
      env: process.env,
    });
  } catch (err) {
    if (err.code === 'ENOENT') {
      log(`FAIL binary not found: "${OPENCODE_BINARY}" — ensure opencode is on PATH or set OPENCODE_BINARY`);
      saveLog();
      process.exit(1);
    }
    throw err;
  }

  child.on('error', err => {
    if (err.code === 'ENOENT') {
      log(`FAIL binary not found: "${OPENCODE_BINARY}" — ensure opencode is on PATH or set OPENCODE_BINARY`);
    } else {
      log(`FAIL child process error: ${err.message}`);
    }
  });

  child.stderr.on('data', logAcpStderr);

  const transport = createTransport(child);

  let probeTimer = null;
  let timedOut = false;

  try {
    // Set up the global timeout.
    const timeoutPromise = new Promise((_resolve, reject) => {
      probeTimer = setTimeout(() => {
        timedOut = true;
        reject(new Error(`probe timed out after ${TIMEOUT_MS}ms`));
      }, TIMEOUT_MS);
    });

    // Accumulate assistant text across all agent_message_chunk notifications.
    let assistantBuffer = '';
    let currentSessionId = null;

    // Register session/update notification handler.
    transport.onNotification('session/update', (params) => {
      const upd = params && params.update;
      if (!upd) {
        log(`session/update: missing update field — params: ${JSON.stringify(params).slice(0, 120)}`);
        return;
      }
      const kind = upd.sessionUpdate;
      switch (kind) {
        case 'agent_message_chunk': {
          // Text may be in upd.text (string) or upd.content which may be:
          //   - a plain string
          //   - a single content part: {type:"text", text:"..."}
          //   - an array of content parts: [{type:"text",text:"..."},...]
          let text = '';
          if (typeof upd.text === 'string') {
            text = upd.text;
          } else if (typeof upd.content === 'string') {
            text = upd.content;
          } else if (Array.isArray(upd.content)) {
            text = upd.content.map(p => (typeof p === 'string' ? p : (p && p.text) || '')).join('');
          } else if (upd.content != null && typeof upd.content === 'object') {
            // Single content part object like {type:"text", text:"..."}
            text = upd.content.text || '';
          }
          assistantBuffer += text;
          const preview = text.slice(0, 80);
          process.stdout.write(`[probe] session/update agent_message_chunk: "${preview}"\n`);
          logLines.push(`[probe] session/update agent_message_chunk: "${preview}"`);
          break;
        }
        case 'agent_thought_chunk': {
          let text = typeof upd.text === 'string' ? upd.text : (typeof upd.content === 'string' ? upd.content : JSON.stringify(upd.content || ''));
          log(`session/update agent_thought_chunk: "${text.slice(0, 80)}"`);
          break;
        }
        case 'user_message_chunk': {
          let text = typeof upd.text === 'string' ? upd.text : (typeof upd.content === 'string' ? upd.content : JSON.stringify(upd.content || ''));
          log(`session/update user_message_chunk: "${text.slice(0, 80)}"`);
          break;
        }
        case 'tool_call': {
          log(`session/update tool_call: id=${upd.toolCallId || '?'} tool=${upd.tool || '?'}`);
          break;
        }
        case 'tool_call_update': {
          log(`session/update tool_call_update: id=${upd.toolCallId || '?'} status=${upd.status || '?'}`);
          break;
        }
        case 'plan': {
          log(`session/update plan: ${JSON.stringify(upd).slice(0, 120)}`);
          break;
        }
        case 'usage_update': {
          const cost = upd.cost != null ? (typeof upd.cost === 'object' ? JSON.stringify(upd.cost) : upd.cost) : '?';
          log(`session/update usage_update: inputTokens=${upd.inputTokens ?? '?'} outputTokens=${upd.outputTokens ?? '?'} cost=${cost}`);
          break;
        }
        case 'available_commands_update': {
          log(`session/update available_commands_update: ${(upd.commands || []).length} commands`);
          break;
        }
        case 'config_option_update': {
          log(`session/update config_option_update: ${JSON.stringify(upd).slice(0, 120)}`);
          break;
        }
        default: {
          log(`session/update unknown discriminator "${kind}": ${JSON.stringify(upd).slice(0, 120)}`);
          break;
        }
      }
    });

    // Register session/request_permission inbound request handler.
    transport.onRequest('session/request_permission', (params, id) => {
      log(`session/request_permission id=${id}: tool=${params && params.tool ? params.tool : '?'} — auto-rejecting`);
      return {outcome: {outcome: 'selected', optionId: 'reject'}};
    });

    // -----------------------------------------------------------------------
    // Step 1: initialize
    // -----------------------------------------------------------------------
    log('step 1: sending initialize');
    const initResult = await Promise.race([
      transport.sendRequest('initialize', {
        protocolVersion: 1,
        clientCapabilities: {
          fs: {readTextFile: true, writeTextFile: true},
        },
        clientInfo: {
          name: 'maho-acp-probe',
          title: 'Maho ACP probe',
          version: '0.1.0',
        },
      }),
      timeoutPromise,
    ]);

    log(`initialize ok — protocolVersion: ${initResult.protocolVersion}`);
    log(`agentCapabilities: ${JSON.stringify(initResult.agentCapabilities || initResult.capabilities || {})}`);
    log(`authMethods: ${JSON.stringify(initResult.authMethods || [])}`);

    // -----------------------------------------------------------------------
    // Step 2: session/new
    // -----------------------------------------------------------------------
    log('step 2: sending session/new');
    const sessionResult = await Promise.race([
      transport.sendRequest('session/new', {
        cwd: process.cwd(),
        mcpServers: [],
      }),
      timeoutPromise,
    ]);

    currentSessionId = sessionResult.sessionId || sessionResult.id;
    log(`session/new ok — sessionId: ${currentSessionId}`);
    if (sessionResult.models) log(`models: ${JSON.stringify(sessionResult.models).slice(0, 200)}`);
    if (sessionResult.modes) log(`modes: ${JSON.stringify(sessionResult.modes).slice(0, 200)}`);
    if (sessionResult.configOptions) log(`configOptions: ${JSON.stringify(sessionResult.configOptions).slice(0, 200)}`);

    // -----------------------------------------------------------------------
    // Step 3 + 4: session/prompt (with streaming updates handled above)
    // -----------------------------------------------------------------------
    log(`step 3: sending session/prompt (timeout ${TIMEOUT_MS}ms)`);
    log(`prompt text: "${PROBE_PROMPT}"`);

    const promptResult = await Promise.race([
      transport.sendRequest('session/prompt', {
        sessionId: currentSessionId,
        prompt: [
          {type: 'text', text: PROBE_PROMPT},
        ],
      }),
      // On timeout, send session/cancel before rejecting.
      timeoutPromise.catch(async err => {
        if (timedOut && currentSessionId) {
          log('timeout reached — sending session/cancel');
          try {
            transport.sendNotification('session/cancel', {sessionId: currentSessionId});
          } catch (_) {}
        }
        throw err;
      }),
    ]);

    clearTimeout(probeTimer);

    // -----------------------------------------------------------------------
    // Step 6: evaluate result
    // -----------------------------------------------------------------------
    const stopReason = promptResult.stopReason || promptResult.stop_reason || '(missing)';
    const usage = promptResult.usage || {};
    log(`session/prompt resolved — stopReason: ${stopReason}`);
    log(`usage: inputTokens=${usage.inputTokens ?? '?'} outputTokens=${usage.outputTokens ?? '?'}`);
    log(`accumulated assistant text: "${assistantBuffer.trim()}"`);

    // Graceful shutdown.
    await shutdownChild(child);

    const exitCode = await new Promise(resolve => {
      if (child.exitCode !== null) { resolve(child.exitCode); return; }
      child.once('exit', (code) => resolve(code ?? 0));
      setTimeout(() => resolve(0), 500);
    });
    log(`child exited with code ${exitCode}`);

    // Verdict.
    const containsProbeOk = assistantBuffer.includes('PROBE_OK');
    const isTurnEnd = stopReason === 'end_turn';

    if (isTurnEnd && containsProbeOk) {
      log('PASS');
      saveLog();
      process.exit(0);
    } else {
      const reasons = [];
      if (!isTurnEnd) reasons.push(`stopReason="${stopReason}" (expected "end_turn")`);
      if (!containsProbeOk) reasons.push(`assistant response did not contain "PROBE_OK"`);
      log(`FAIL ${reasons.join('; ')}`);
      saveLog();
      process.exit(1);
    }

  } catch (err) {
    clearTimeout(probeTimer);
    log(`FAIL ${err.message}`);
    if (child) await shutdownChild(child);
    saveLog();
    process.exit(1);
  }
}

runProbe();
