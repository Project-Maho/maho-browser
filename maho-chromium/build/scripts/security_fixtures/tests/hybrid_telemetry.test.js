'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const vm = require('node:vm');

const { PAGE_BUILDERS, ROUTES } = require('../../security_fixtures/pages.js');
const {
  validateKeyboardTelemetry,
  validateMotionTelemetry,
} = require('../../security_fixtures/telemetry_validator.js');

function loadTelemetryPage() {
  const html = PAGE_BUILDERS['/telemetry']({});
  const source = html.match(/<script id="telemetry-recorder">([\s\S]*?)<\/script>/)[1];
  const listeners = new Map();
  const posted = [];
  let clock = 0;
  const target = {
    getBoundingClientRect() {
      return { left: 100, top: 100, width: 200, height: 120 };
    },
  };
  const output = { textContent: '' };
  const document = {
    addEventListener(type, listener) {
      const registered = listeners.get(type) || [];
      registered.push(listener);
      listeners.set(type, registered);
    },
    querySelector(selector) {
      return selector === '#telemetry-target' ? target : output;
    },
  };
  const context = {
    document,
    fetch(path, options) {
      posted.push({ path, body: JSON.parse(options.body) });
      return Promise.resolve({ ok: true });
    },
    Math,
    Object,
    performance: { now: () => clock },
    window: {},
  };
  vm.runInNewContext(source, context);
  return {
    html,
    posted,
    metrics: context.window.__mahoTelemetryMetrics,
    emit(type, values, t) {
      clock = t;
      for (const listener of listeners.get(type) || []) {
        listener({
          type,
          clientX: 0,
          clientY: 0,
          key: '',
          code: '',
          altKey: false,
          ctrlKey: false,
          metaKey: false,
          shiftKey: false,
          ...values,
        });
      }
    },
  };
}

test('Task 15: telemetry fixture derives and posts recorded pointer and key events', () => {
  const page = loadTelemetryPage();
  assert.ok(ROUTES.includes('/telemetry'));
  assert.match(page.html, /id="telemetry-target"/);
  assert.match(page.html, /id="telemetry-input"/);

  // Target center is (200, 160). The three clicks create 100 ms of dwell,
  // while the move sequence goes 3 px beyond the center then settles there.
  for (const [x, y, t] of [
    [120, 160, 0],
    [160, 160.5, 20],
    [203, 160, 40],
    [200, 160, 60],
  ]) page.emit('pointermove', { clientX: x, clientY: y }, t);
  for (const t of [70, 120, 160]) {
    page.emit('pointerdown', { clientX: 200, clientY: 160 }, t);
    page.emit('pointerup', { clientX: 200, clientY: 160 }, t + 10);
  }
  page.emit('keydown', { key: 'a', code: 'KeyA', shiftKey: false }, 180);
  page.emit('keyup', { key: 'a', code: 'KeyA', shiftKey: false }, 190);

  assert.equal(page.posted.length, 1);
  assert.equal(page.posted[0].path, '/telemetry/trace');
  const trace = page.posted[0].body;
  assert.equal(trace.moves.length, 4);
  assert.equal(trace.pointerdowns.length, 3);
  assert.equal(trace.pointerups.length, 3);
  assert.equal(trace.dwell_ms, 100);
  assert.equal(trace.overshoot_px, 3);
  assert.ok(Math.abs(trace.jitter_css_px - 0.5) < 0.001,
    `expected hand-computed 0.5px jitter, got ${trace.jitter_css_px}`);
  assert.deepEqual(trace.keyboard.map(({ type, key, code, shiftKey }) =>
    ({ type, key, code, shiftKey })), [
    { type: 'keydown', key: 'a', code: 'KeyA', shiftKey: false },
    { type: 'keyup', key: 'a', code: 'KeyA', shiftKey: false },
  ]);
  assert.equal(validateMotionTelemetry(trace).valid, true);
  assert.equal(validateKeyboardTelemetry(trace.keyboard).valid, true);
});

test('Task 15: page computation produces validator-rejected out-of-bounds trace', () => {
  const page = loadTelemetryPage();
  const moves = [
    { x: 120, y: 160, t: 0 },
    { x: 160, y: 162, t: 20 },
    { x: 206, y: 160, t: 40 },
    { x: 200, y: 160, t: 60 },
  ];
  const pointerdowns = [{ t: 70 }];
  const pointerups = [{ t: 100 }];
  // metrics closes over the fixture's recorded arrays, so feed the same
  // synthetic sequence through its actual event listeners before asserting.
  for (const move of moves) {
    page.emit('pointermove', { clientX: move.x, clientY: move.y }, move.t);
  }
  page.emit('pointerdown', { clientX: 200, clientY: 160 }, pointerdowns[0].t);
  page.emit('pointerup', { clientX: 200, clientY: 160 }, pointerups[0].t);
  const derived = page.metrics({ x: 200, y: 160 });
  assert.ok(Math.abs(derived.jitter_css_px - 2) < 0.001);
  assert.equal(derived.overshoot_px, 6);
  assert.equal(derived.dwell_ms, 30);
  assert.equal(validateMotionTelemetry({ moves, ...derived }).valid, false);
});

test('Task 15: motion telemetry parameters adhere to bounded contract', () => {
  const { validateMotionTelemetry } = require('../../security_fixtures/telemetry_validator.js');

  const validTrace = {
    moves: [
      { x: 100.0, y: 100.0, t: 10 },
      { x: 100.4, y: 100.3, t: 30 },
      { x: 101.2, y: 101.0, t: 50 },
      { x: 102.8, y: 102.5, t: 70 },
      { x: 103.5, y: 103.2, t: 90 },
    ],
    jitter_css_px: 0.5,
    overshoot_px: 2.5,
    dwell_ms: 120,
    down_timestamp: 100,
    up_timestamp: 220,
  };

  const result = validateMotionTelemetry(validTrace);
  assert.equal(result.valid, true);
  assert.ok(result.jitter_bounded);
  assert.ok(result.overshoot_bounded);
  assert.ok(result.dwell_bounded);
});

test('Task 15: out-of-bounds jitter, overshoot, or dwell are rejected', () => {
  const { validateMotionTelemetry } = require('../../security_fixtures/telemetry_validator.js');

  const excessiveJitter = {
    jitter_css_px: 1.5,
    overshoot_px: 2.5,
    dwell_ms: 120,
    down_timestamp: 100,
    up_timestamp: 220,
  };
  assert.equal(validateMotionTelemetry(excessiveJitter).valid, false);

  const excessiveOvershoot = {
    jitter_css_px: 0.5,
    overshoot_px: 6.0,
    dwell_ms: 120,
    down_timestamp: 100,
    up_timestamp: 220,
  };
  assert.equal(validateMotionTelemetry(excessiveOvershoot).valid, false);

  const outOfRangeDwell = {
    jitter_css_px: 0.5,
    overshoot_px: 2.5,
    dwell_ms: 40,
    down_timestamp: 100,
    up_timestamp: 140,
  };
  assert.equal(validateMotionTelemetry(outOfRangeDwell).valid, false);
});

test('Task 15: keyboard telemetry preserves ordering and modifiers', () => {
  const { validateKeyboardTelemetry } = require('../../security_fixtures/telemetry_validator.js');

  const validKeySequence = [
    { type: 'keydown', key: 'Shift', code: 'ShiftLeft', shiftKey: true, t: 10 },
    { type: 'keydown', key: 'A', code: 'KeyA', shiftKey: true, t: 40 },
    { type: 'keyup', key: 'A', code: 'KeyA', shiftKey: true, t: 130 },
    { type: 'keyup', key: 'Shift', code: 'ShiftLeft', shiftKey: false, t: 160 },
  ];

  const result = validateKeyboardTelemetry(validKeySequence);
  assert.equal(result.valid, true);
  assert.equal(result.ordered, true);
  assert.equal(result.modifiers_preserved, true);
});
