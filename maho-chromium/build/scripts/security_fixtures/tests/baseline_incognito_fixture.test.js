'use strict';

// Baseline characterization test (Todo 2, maho-agent-safe-vault).
//
// Pins the CURRENT observable behavior of the repo-local fixture surface
// BEFORE the security fixtures are added, so a regression is detectable:
//   1. The existing incognito fixture_server still serves its known routes.
//   2. That same server returns 404 for the security route `/login` — i.e. the
//      two-origin security fixtures do NOT exist yet. This is the gap Todo 2
//      fills; the failing security-fixtures test then drives the new code.
//   3. `security_test.rs` still declares the shared sentinel + the 7 route
//      names, documenting the coupling this framework must satisfy without
//      editing that browser-dependent Rust file.
//
// This file must PASS on unchanged code.

const test = require('node:test');
const assert = require('node:assert/strict');
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');

const { createFixtureServer } = require('../../incognito_e2e/fixture_server');

const SECURITY_TEST_RS = path.resolve(
  __dirname,
  '../../../../../maho/crates/maho-browser-mcp/tests/integration/security_test.rs',
);

function httpGet(port, urlPath) {
  return new Promise((resolve, reject) => {
    const req = http.request(
      { host: '127.0.0.1', port, path: urlPath, method: 'GET' },
      (res) => {
        let body = '';
        res.on('data', (c) => (body += c));
        res.on('end', () => resolve({ status: res.statusCode, body }));
      },
    );
    req.on('error', reject);
    req.end();
  });
}

test('baseline: incognito fixture server serves its existing routes', async () => {
  const server = await createFixtureServer(0);
  const { port } = server.address();
  try {
    const regular = await httpGet(port, '/downloads/regular.bin');
    assert.equal(regular.status, 200);
    assert.match(regular.body, /REGULAR_DOWNLOAD_SECRET/);

    const state = await httpGet(port, '/state/');
    assert.equal(state.status, 200);
    assert.match(state.body, /otr_session/);
  } finally {
    server.close();
  }
});

test('baseline: incognito fixture server does NOT yet serve /login (gap Todo 2 fills)', async () => {
  const server = await createFixtureServer(0);
  const { port } = server.address();
  try {
    const login = await httpGet(port, '/login');
    assert.equal(login.status, 404, 'security /login route must not exist on the incognito server');
  } finally {
    server.close();
  }
});

test('baseline: security_test.rs still declares the shared sentinel + 7 route names', () => {
  const src = fs.readFileSync(SECURITY_TEST_RS, 'utf8');
  assert.match(src, /S3NTINEL-maho-vault-9F4C/, 'sentinel constant must be present');
  for (const route of [
    '/login',
    '/password-change',
    '/totp',
    '/cross-origin-iframe',
    '/payment',
    '/post',
    '/prompt-injection',
  ]) {
    assert.ok(src.includes(route), `security_test.rs must document route ${route}`);
  }
});
