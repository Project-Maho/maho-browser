'use strict';

// TEST-ONLY self-signed certificate generation for the local security
// fixtures. These certs exist solely to give the fixture servers an HTTPS
// origin for deterministic local testing.
//
// SECURITY SCOPE (read before touching):
//   - The generated leaf is self-signed, short-lived, and written to a
//     process-local temp dir. It is NEVER installed into any OS/browser trust
//     store. Callers verify it by passing the returned PEM as an explicit `ca`
//     to their HTTPS client, or (for Playwright) by scoping
//     `ignoreHTTPSErrors` to a single browser context.
//   - This module MUST NOT be used to weaken global TLS. Do not set
//     NODE_TLS_REJECT_UNAUTHORIZED here or add production trust exceptions.

const { execFileSync } = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const SAN = 'subjectAltName=DNS:localhost,DNS:maho-fixture.localhost,IP:127.0.0.1,IP:127.0.0.2';
const SUBJECT = '/CN=maho-security-fixtures.test/O=Maho Test Fixtures (DO NOT TRUST)';

let cached = null;

// Generate (once per process) a self-signed cert+key covering the loopback
// hosts the fixtures bind to. Returns { dir, certPath, keyPath, cert, key }.
function ensureTestCert() {
  if (cached && fs.existsSync(cached.certPath) && fs.existsSync(cached.keyPath)) {
    return cached;
  }

  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'maho-security-fixtures-certs-'));
  const keyPath = path.join(dir, 'fixture-key.pem');
  const certPath = path.join(dir, 'fixture-cert.pem');

  execFileSync(
    'openssl',
    [
      'req', '-x509',
      '-newkey', 'rsa:2048',
      '-nodes',
      '-keyout', keyPath,
      '-out', certPath,
      '-days', '2',
      '-subj', SUBJECT,
      '-addext', SAN,
    ],
    { stdio: ['ignore', 'ignore', 'ignore'] },
  );

  const cert = fs.readFileSync(certPath, 'utf8');
  const key = fs.readFileSync(keyPath, 'utf8');
  const fingerprint = crypto.createHash('sha256').update(cert).digest('hex');

  cached = { dir, certPath, keyPath, cert, key, fingerprint };
  return cached;
}

// Remove the temp cert dir. Registered as a cleanup receipt by callers.
function cleanupTestCert() {
  if (cached) {
    fs.rmSync(cached.dir, { recursive: true, force: true });
    cached = null;
  }
}

module.exports = { ensureTestCert, cleanupTestCert };
