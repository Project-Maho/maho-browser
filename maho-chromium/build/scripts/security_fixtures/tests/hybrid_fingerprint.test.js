'use strict';

const assert = require('node:assert/strict');
const { existsSync, readFileSync } = require('node:fs');
const { join } = require('node:path');
const test = require('node:test');
const { runInNewContext } = require('node:vm');

const CREEPJS_DIR = join(__dirname, '../creepjs');
const PAGES_PATH = join(__dirname, '../pages.js');

const { createHash } = require('node:crypto');
const { readdirSync } = require('node:fs');

// Provenance contract: an asset directory that carries the upstream CreepJS
// LICENSE and revision must actually contain upstream bytes, and every file
// must declare who authored it. A project-authored script presented as a
// CreepJS subset is a provenance defect, not a vendored dependency.
test('Task 13: manifest declares authorship and upstream provenance for every asset', () => {
  const manifest = JSON.parse(readFileSync(join(CREEPJS_DIR, 'manifest.json'), 'utf8'));
  const entries = manifest.files;

  const onDisk = readdirSync(CREEPJS_DIR).filter((name) => name !== 'manifest.json');
  const declared = new Set(entries.map((file) => file.path));
  for (const name of onDisk) {
    assert.ok(declared.has(name), `file ${name} is present but undeclared in manifest.json`);
  }

  for (const file of entries) {
    assert.ok(['upstream', 'maho'].includes(file.origin),
      `${file.path} must declare origin "upstream" or "maho", got ${JSON.stringify(file.origin)}`);

    const bytes = readFileSync(join(CREEPJS_DIR, file.path));
    assert.equal(createHash('sha256').update(bytes).digest('hex'), file.sha256,
      `${file.path} sha256 must match its actual bytes`);
    assert.equal(bytes.length, file.bytes, `${file.path} byte count must match its actual size`);

    if (file.origin === 'upstream') {
      assert.ok(file.upstream_path, `${file.path} must record its upstream path`);
      assert.match(file.upstream_sha256 || '', /^[0-9a-f]{64}$/,
        `${file.path} must record the original upstream sha256`);
      assert.ok(Array.isArray(file.transformations),
        `${file.path} must record local transformations (use [] when byte-identical)`);
      if (file.transformations.length === 0) {
        assert.equal(file.sha256, file.upstream_sha256,
          `${file.path} claims no transformation, so local and upstream sha256 must match`);
      }
    }
  }

  const upstreamAssets = entries.filter(
    (file) => file.origin === 'upstream' && file.path !== 'LICENSE');
  assert.ok(upstreamAssets.length > 0,
    'a directory shipping the upstream CreepJS LICENSE and revision must vendor real upstream audit assets');
  assert.ok(upstreamAssets.some((file) => file.path.endsWith('.js')),
    'the vendored upstream audit implementation must be present as a .js asset');
});

test('Task 13: vendored CreepJS directory has README, manifest, and LICENSE', () => {
  assert.ok(existsSync(join(CREEPJS_DIR, 'README.md')), 'README.md must exist');
  assert.ok(existsSync(join(CREEPJS_DIR, 'manifest.json')), 'manifest.json must exist');
  assert.ok(existsSync(join(CREEPJS_DIR, 'LICENSE')), 'LICENSE must exist');

  const manifest = JSON.parse(readFileSync(join(CREEPJS_DIR, 'manifest.json'), 'utf8'));
  assert.equal(manifest.name, 'creepjs-offline-subset');
  assert.ok(manifest.upstream_revision, 'upstream revision must be recorded');
  assert.ok(Array.isArray(manifest.files), 'manifest.files must be an array');
  assert.ok(manifest.files.length > 0, 'must record vendored files');
  for (const file of manifest.files) {
    assert.ok(file.path, 'file must have path');
    assert.ok(file.sha256 && file.sha256.length === 64, 'file must have 64-char sha256');
    assert.ok(existsSync(join(CREEPJS_DIR, file.path)), `vendored file ${file.path} must exist`);
  }
});

test('Task 13: every active CreepJS network sink is guarded or disabled', () => {
  const bundle = readFileSync(join(CREEPJS_DIR, 'creep.js'), 'utf8');

  assert.match(bundle, /return fetch\(assertMahoLocalURL\(url\)\)/,
    'the script-size fetch must enforce the same-origin guard');
  assert.match(bundle, /new Worker\(assertMahoLocalURL\(scriptSource\)\)/,
    'dedicated worker loading must enforce the same-origin guard');
  assert.match(bundle, /new SharedWorker\(assertMahoLocalURL\(scriptSource\)\)/,
    'shared worker loading must enforce the same-origin guard');
  assert.match(bundle, /serviceWorker\.register\(assertMahoLocalURL\(scriptSource\)\)/,
    'service worker loading must enforce the same-origin guard');
  assert.match(bundle, /iceServers: \[\]/,
    'WebRTC must use host-only ICE gathering with no STUN or TURN server');

  const activeSinkPatterns = [
    /fetch\(/g,
    /new Worker\(/g,
    /new SharedWorker\(/g,
    /navigator\.serviceWorker\.register\(/g,
    /new XMLHttpRequest\(/g,
    /new WebSocket\(/g,
    /new EventSource\(/g,
    /\.sendBeacon\(/g,
  ];
  const expectedCounts = [1, 1, 1, 1, 0, 0, 0, 0];
  activeSinkPatterns.forEach((pattern, index) => {
    assert.equal((bundle.match(pattern) || []).length, expectedCounts[index],
      `unexpected active network sink matching ${pattern}`);
  });
  assert.doesNotMatch(bundle, /['"](?:stun|turn|stuns|turns):/,
    'the transformed bundle must not contain a live STUN or TURN URL');
});

test('Task 13: CreepJS same-origin guard blocks external traffic at runtime', () => {
  const bundle = readFileSync(join(CREEPJS_DIR, 'creep.js'), 'utf8');
  const functionStart = bundle.indexOf('const assertMahoLocalURL =');
  const functionEnd = bundle.indexOf('    // @ts-expect-error', functionStart);
  assert.ok(functionStart > -1 && functionEnd > functionStart, 'same-origin guard must be present');

  const calls = [];
  const context = {
    URL,
    self: { location: { href: 'https://127.0.0.1:9443/creepjs', origin: 'https://127.0.0.1:9443' } },
    fetch: (url) => { calls.push(url); },
  };
  const guardSource = bundle.slice(functionStart, functionEnd);
  runInNewContext(`${guardSource}\nthis.guard = assertMahoLocalURL;`, context);

  assert.throws(() => context.fetch(context.guard('https://collector.invalid/audit')), /blocked non-local URL/);
  assert.equal(calls.length, 0, 'an external URL must be rejected before reaching the network sink');
  context.fetch(context.guard('/creep.js'));
  assert.deepEqual(calls, ['https://127.0.0.1:9443/creep.js']);
});

test('Task 13: pages.js provides /creepjs and /creepjs/results routes', () => {
  const { PAGE_BUILDERS, ROUTES } = require(PAGES_PATH);
  assert.ok(ROUTES.includes('/creepjs'), 'ROUTES must include /creepjs');
  assert.ok(ROUTES.includes('/creepjs/results'), 'ROUTES must include /creepjs/results');
  assert.equal(typeof PAGE_BUILDERS['/creepjs'], 'function');
  assert.equal(typeof PAGE_BUILDERS['/creepjs/results'], 'function');

  const html = PAGE_BUILDERS['/creepjs']({});
  assert.ok(html.includes('data-maho-fixture-route="creepjs"'), 'must carry route attribute');
  assert.ok(html.includes('navigator.webdriver'), 'must probe navigator.webdriver');
  assert.ok(html.includes('cdc_'), 'must probe cdc_* markers');
});

test('Task 13: known-bad control causes fingerprint audit failure', () => {
  const { PAGE_BUILDERS } = require(PAGES_PATH);
  const html = PAGE_BUILDERS['/creepjs']({});
  const scriptMatch = html.match(/<script>([\s\S]*?)<\/script>/);
  assert.ok(scriptMatch, 'script tag must be present in /creepjs page');

  function executeWrapper(navigator, windowMarkers) {
    let result = null;
    let completionListener = null;
    const window = {
      ...windowMarkers,
      addEventListener: (name, listener) => {
        if (name === 'maho-creepjs-complete') completionListener = listener;
      },
    };
    const context = {
      navigator,
      window,
      document: {
        querySelector: () => ({ textContent: '' }),
        ...windowMarkers,
      },
      fetch: (url, opts) => {
        assert.equal(url, '/creepjs/results');
        result = JSON.parse(opts.body);
        return Promise.resolve();
      },
    };
    runInNewContext(scriptMatch[1], context);
    assert.ok(completionListener, 'wrapper must await the upstream audit completion signal');
    completionListener({ detail: { Fingerprint: { navigator: {} }, Creep: { navigator: {} } } });
    return result;
  }

  const cleanResult = executeWrapper({ webdriver: false }, {});
  assert.ok(cleanResult, 'cleanContext must post result');
  assert.equal(cleanResult.passed, true);
  assert.equal(cleanResult.webdriver, false);
  assert.equal(cleanResult.cdc_markers_found, false);
  assert.ok(cleanResult.upstream_creepjs.audit.Fingerprint,
    'posted JSON must carry the upstream-derived fingerprint separately');
  assert.equal(cleanResult.maho_observations.webdriver, false,
    'posted JSON must label project-owned marker observations separately');

  const badResult = executeWrapper(
    { webdriver: true },
    { $cdc_asdjflasutopfhvcZLmcfl_: true },
  );
  assert.ok(badResult, 'badContext must post result');
  assert.equal(badResult.passed, false);
  assert.equal(badResult.webdriver, true);
  assert.equal(badResult.cdc_markers_found, true);
});
