'use strict';

const assert = require('node:assert/strict');
const { join } = require('node:path');
const test = require('node:test');
const { runInNewContext } = require('node:vm');

const PAGES_PATH = join(__dirname, '../pages.js');

test('Task 14: pages.js provides /challenge in offline ROUTES and isolates /challenge/turnstile in ONLINE_ROUTES', () => {
  const { PAGE_BUILDERS, ROUTES, ONLINE_ROUTES, ALL_ROUTES } = require(PAGES_PATH);
  assert.ok(ROUTES.includes('/challenge'), 'ROUTES must include /challenge');
  assert.equal(ROUTES.includes('/challenge/turnstile'), false, 'ROUTES must NOT include online /challenge/turnstile');
  assert.ok(ONLINE_ROUTES.includes('/challenge/turnstile'), 'ONLINE_ROUTES must include /challenge/turnstile');
  assert.ok(ALL_ROUTES.includes('/challenge') && ALL_ROUTES.includes('/challenge/turnstile'), 'ALL_ROUTES includes both');
  assert.equal(typeof PAGE_BUILDERS['/challenge'], 'function');
  assert.equal(typeof PAGE_BUILDERS['/challenge/turnstile'], 'function');

  const html = PAGE_BUILDERS['/challenge']({});
  assert.ok(html.includes('data-maho-fixture-route="challenge"'), 'must carry route attribute');
  assert.ok(html.includes('id="challenge-widget"'), 'must include challenge widget container');
  assert.ok(html.includes('id="challenge-target"'), 'must include challenge target button');
  assert.ok(html.includes('id="challenge-misclick-zone"'), 'must include misclick zone');
});

test('Task 14: local challenge lifecycle transitions from shown to cleared on target hit', async () => {
  const { PAGE_BUILDERS } = require(PAGES_PATH);
  const html = PAGE_BUILDERS['/challenge']({});
  const scriptMatch = html.match(/<script>([\s\S]*?)<\/script>/);
  assert.ok(scriptMatch, 'script tag must be present in /challenge page');

  const events = new Map();
  const postedReceipts = [];
  const statusEl = { textContent: 'shown' };
  const successEl = { style: { display: 'none' } };

  const context = {
    window: {},
    document: {
      title: 'Maho Challenge Fixture',
      querySelector: (selector) => {
        if (selector === '#challenge-status') return statusEl;
        if (selector === '#challenge-success') return successEl;
        return {
          addEventListener: (event, cb) => {
            events.set(selector + ':' + event, cb);
          },
          getAttribute: (attr) => attr === 'data-episode-id' ? 'ep-12345' : null
        };
      }
    },
    fetch: (url, opts) => {
      postedReceipts.push({ url, body: JSON.parse(opts.body) });
      return Promise.resolve({
        ok: true,
        json: () => Promise.resolve({ verified: true, token: 'local-token-ep-12345' })
      });
    }
  };

  runInNewContext(scriptMatch[1], context);

  assert.ok(events.has('#challenge-target:click'), 'must register target click handler');
  const clickHandler = events.get('#challenge-target:click');

  clickHandler({ isTrusted: true, target: { id: 'challenge-target' } });
  await new Promise((resolve) => setTimeout(resolve, 10));

  assert.equal(statusEl.textContent, 'cleared');
  assert.equal(successEl.style.display, 'block');
  assert.ok(postedReceipts.some(r => r.url === '/challenge/verify' && r.body.target === 'challenge-target'));
});

test('Task 14: misclick outside target rejects verification and emits misclick event', async () => {
  const { PAGE_BUILDERS } = require(PAGES_PATH);
  const html = PAGE_BUILDERS['/challenge']({});
  const scriptMatch = html.match(/<script>([\s\S]*?)<\/script>/);
  assert.ok(scriptMatch);

  const events = new Map();
  const postedReceipts = [];
  const statusEl = { textContent: 'shown' };
  const misclickCountEl = { textContent: '0' };

  const context = {
    window: {},
    document: {
      title: 'Maho Challenge Fixture',
      querySelector: (selector) => {
        if (selector === '#challenge-status') return statusEl;
        if (selector === '#misclick-count') return misclickCountEl;
        return {
          addEventListener: (event, cb) => {
            events.set(selector + ':' + event, cb);
          },
          getAttribute: () => 'ep-12345'
        };
      }
    },
    fetch: (url, opts) => {
      postedReceipts.push({ url, body: JSON.parse(opts.body) });
      return Promise.resolve({
        ok: false,
        json: () => Promise.resolve({ verified: false, error: 'misclick' })
      });
    }
  };

  runInNewContext(scriptMatch[1], context);

  assert.ok(events.has('#challenge-misclick-zone:click'), 'must register misclick handler');
  const misclickHandler = events.get('#challenge-misclick-zone:click');

  misclickHandler({ isTrusted: true, target: { id: 'challenge-misclick-zone' } });
  await new Promise((resolve) => setTimeout(resolve, 10));

  assert.equal(statusEl.textContent, 'shown');
  assert.equal(misclickCountEl.textContent, '1');
  assert.ok(postedReceipts.some(r => r.url === '/challenge/misclick'));
});

test('Task 14: Turnstile fixture page enforces offline safety and loads Cloudflare dummy sitekey online', () => {
  const { PAGE_BUILDERS } = require(PAGES_PATH);
  
  // Default offline check: external script suppressed
  delete process.env.MAHO_ALLOW_ONLINE;
  const offlineHtml = PAGE_BUILDERS['/challenge/turnstile']({});
  assert.ok(offlineHtml.includes('data-maho-fixture-route="challenge-turnstile"'));
  assert.ok(offlineHtml.includes('3x00000000000000000000FF'), 'must use force-interactive dummy sitekey');
  assert.equal(offlineHtml.includes('challenges.cloudflare.com/turnstile/v0/api.js'), false, 'must NOT load external script in offline mode');
  assert.ok(offlineHtml.includes('offline-blocked'), 'must set offline-blocked status');

  // Explicit online check: external script enabled
  process.env.MAHO_ALLOW_ONLINE = '1';
  try {
    const onlineHtml = PAGE_BUILDERS['/challenge/turnstile']({});
    assert.ok(onlineHtml.includes('challenges.cloudflare.com/turnstile/v0/api.js'), 'must load Turnstile api script in online mode');
    assert.ok(onlineHtml.includes('awaiting-interaction'), 'must set awaiting-interaction status in online mode');
  } finally {
    delete process.env.MAHO_ALLOW_ONLINE;
  }
});

test('Task 14: fixture_server verifies trusted input, active episode, and rejects replay attacks', async () => {
  const { makeHandler } = require(join(__dirname, '../fixture_server.js'));
  const http = require('node:http');

  const handler = makeHandler({ selfOrigin: 'http://127.0.0.1:0', peerOrigin: 'http://127.0.0.1:0' });
  const server = http.createServer(handler);
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const port = server.address().port;
  const baseUrl = `http://127.0.0.1:${port}`;

  try {
    // 1. Fetch challenge page to issue active episode
    const pageRes = await fetch(`${baseUrl}/challenge`);
    const pageHtml = await pageRes.text();
    const match = pageHtml.match(/data-episode-id="([^"]+)"/);
    assert.ok(match, 'page must contain data-episode-id');
    const episodeId = match[1];

    // 2. Untrusted POST must be rejected
    const untrustedRes = await fetch(`${baseUrl}/challenge/verify`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ target: 'challenge-target', episodeId, isTrusted: false })
    }).then(r => r.json());
    assert.equal(untrustedRes.verified, false);
    assert.equal(untrustedRes.error, 'untrusted_input');

    // 3. Unissued episode must be rejected
    const unissuedRes = await fetch(`${baseUrl}/challenge/verify`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ target: 'challenge-target', episodeId: 'unissued-ep-999', isTrusted: true })
    }).then(r => r.json());
    assert.equal(unissuedRes.verified, false);
    assert.equal(unissuedRes.error, 'unissued_or_expired_episode');

    // 4. Valid trusted POST with issued episode must succeed
    const validRes = await fetch(`${baseUrl}/challenge/verify`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ target: 'challenge-target', episodeId, isTrusted: true })
    }).then(r => r.json());
    assert.equal(validRes.verified, true);
    assert.equal(validRes.token, 'local-token-' + episodeId);

    // 5. Replaying the same episode must be rejected
    const replayRes = await fetch(`${baseUrl}/challenge/verify`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ target: 'challenge-target', episodeId, isTrusted: true })
    }).then(r => r.json());
    assert.equal(replayRes.verified, false);
    assert.equal(replayRes.error, 'unissued_or_expired_episode');
  } finally {
    await new Promise(resolve => server.close(resolve));
  }
});
