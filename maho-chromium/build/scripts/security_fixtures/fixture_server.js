'use strict';

// Two-origin HTTPS security fixture server for local Vault/agent testing.
//
// Extends the repo-local fixture pattern (see ../incognito_e2e/fixture_server.js)
// with: HTTPS, two DISTINCT loopback origins (localhost + 127.0.0.1 by
// default), the seven security routes defined in pages.js, a /healthz
// readiness probe, and a /__fixture_manifest describing routes + authorized
// sentinel locations. Strictly local: no external network, no example.com.

const https = require('node:https');

const { ensureTestCert, cleanupTestCert } = require('./certs');
const { PAGE_BUILDERS, ROUTES, ONLINE_ROUTES, ALL_ROUTES } = require('./pages');

const SENTINEL = 'S3NTINEL-maho-vault-9F4C';
const DEFAULT_HOST_A = 'localhost';
const DEFAULT_HOST_B = '127.0.0.1';

function sendHtml(res, body) {
  res.writeHead(200, {
    'content-type': 'text/html; charset=utf-8',
    'cache-control': 'no-store',
    'x-maho-fixture': 'security',
  });
  res.end(body);
}

function sendJson(res, obj) {
  res.writeHead(200, { 'content-type': 'application/json', 'cache-control': 'no-store' });
  res.end(JSON.stringify(obj));
}

function buildManifest(originA, originB, allowOnline = false) {
  const routes = allowOnline || process.env.MAHO_ALLOW_ONLINE === '1'
    ? ALL_ROUTES
    : ROUTES;
  return {
    fixture: 'maho-security-fixtures',
    origins: [originA, originB],
    routes: routes,
    sentinel: SENTINEL,
    // The ONLY locations the fixture authorizes the sentinel to appear in.
    authorizedSentinelLocations: [`${originA}/login`, `${originB}/login`],
  };
}

// Build a request handler bound to this server's own origin + its peer origin
// (peer is needed so the cross-origin iframe page can target the other host).
function makeHandler({ selfOrigin, peerOrigin }) {
  const manifest = buildManifest(selfOrigin, peerOrigin);
  const activeEpisodes = new Set();
  return (req, res) => {
    const urlPath = (req.url || '/').split('?')[0];

    if (urlPath === '/healthz') {
      return sendJson(res, { status: 'ok', origin: selfOrigin });
    }
    if (urlPath === '/__fixture_manifest') {
      // Manifest lists origins in a stable [A, B] order regardless of server.
      return sendJson(res, manifest.origins[0] < manifest.origins[1]
        ? manifest
        : { ...manifest, origins: [peerOrigin, selfOrigin] });
    }

    if (req.method === 'POST' && urlPath === '/creepjs/results') {
      let body = '';
      req.on('data', chunk => { body += chunk; });
      req.on('end', () => {
        try {
          const parsed = JSON.parse(body);
          sendJson(res, { ok: true, received: parsed });
        } catch (e) {
          sendJson(res, { ok: false, error: e.message });
        }
      });
      return;
    }

    if (req.method === 'POST' && urlPath === '/challenge/verify') {
      let body = '';
      req.on('data', chunk => { body += chunk; });
      req.on('end', () => {
        try {
          const parsed = JSON.parse(body);
          if (parsed.isTrusted !== true) {
            sendJson(res, { verified: false, error: 'untrusted_input' });
            return;
          }
          if (parsed.target !== 'challenge-target') {
            sendJson(res, { verified: false, error: 'invalid_target' });
            return;
          }
          if (!parsed.episodeId || !activeEpisodes.has(parsed.episodeId)) {
            sendJson(res, { verified: false, error: 'unissued_or_expired_episode' });
            return;
          }
          activeEpisodes.delete(parsed.episodeId);
          sendJson(res, { verified: true, token: 'local-token-' + parsed.episodeId });
        } catch (e) {
          sendJson(res, { verified: false, error: e.message });
        }
      });
      return;
    }

    if (req.method === 'POST' && urlPath === '/challenge/misclick') {
      let body = '';
      req.on('data', chunk => { body += chunk; });
      req.on('end', () => {
        try {
          const parsed = JSON.parse(body);
          sendJson(res, { verified: false, misclick_logged: true, count: parsed.count });
        } catch (e) {
          sendJson(res, { verified: false, error: e.message });
        }
      });
      return;
    }

    if (req.method === 'POST' && urlPath === '/challenge/turnstile-verify') {
      let body = '';
      req.on('data', chunk => { body += chunk; });
      req.on('end', () => {
        try {
          const parsed = JSON.parse(body);
          sendJson(res, { success: true, dummy: true, sitekey: parsed.sitekey });
        } catch (e) {
          sendJson(res, { success: false, error: e.message });
        }
      });
      return;
    }

    const builder = PAGE_BUILDERS[urlPath];
    if (builder) {
      const episodeId = 'ep-' + Date.now() + '-' + Math.random().toString(36).slice(2, 8);
      activeEpisodes.add(episodeId);
      return sendHtml(res, builder({ selfOrigin, peerOrigin, sentinel: SENTINEL, episodeId }));
    }

    res.writeHead(404, { 'content-type': 'text/plain' });
    res.end('not found');
  };
}

function listen(server, host, port) {
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, host, () => {
      server.removeListener('error', reject);
      resolve(server.address().port);
    });
  });
}

// Start both HTTPS origins. Resolves only after BOTH are listening
// (deterministic readiness — no sleeps). Returns handles + a close() that also
// removes the temp cert dir (cleanup receipt).
async function startSecurityFixtures(opts = {}) {
  const hostA = opts.hostA || DEFAULT_HOST_A;
  const hostB = opts.hostB || DEFAULT_HOST_B;
  const { cert, key, fingerprint } = ensureTestCert();
  const tls = { cert, key };

  // Create servers first (need ports), then rebind handlers with real origins.
  const serverA = https.createServer(tls);
  const serverB = https.createServer(tls);

  const portA = await listen(serverA, hostA, opts.portA || 0);
  const portB = await listen(serverB, hostB, opts.portB || 0);

  const originA = `https://${hostA}:${portA}`;
  const originB = `https://${hostB}:${portB}`;

  serverA.on('request', makeHandler({ selfOrigin: originA, peerOrigin: originB }));
  serverB.on('request', makeHandler({ selfOrigin: originB, peerOrigin: originA }));

  let closed = false;
  const close = async () => {
    if (closed) return;
    closed = true;
    await Promise.all([
      new Promise((r) => serverA.close(r)),
      new Promise((r) => serverB.close(r)),
    ]);
    cleanupTestCert();
  };

  return {
    sentinel: SENTINEL,
    ca: cert,
    caFingerprint: fingerprint,
    a: { host: hostA, port: portA, origin: originA },
    b: { host: hostB, port: portB, origin: originB },
    origins: [originA, originB],
    manifest: buildManifest(originA, originB),
    close,
  };
}

module.exports = { startSecurityFixtures, makeHandler, buildManifest, SENTINEL, ROUTES, ONLINE_ROUTES, ALL_ROUTES };
