const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const {join} = require('node:path');
const test = require('node:test');
const {runInNewContext} = require('node:vm');

test('baseline receipts capture viewport changes at each action', () => {
  const html = readFileSync(join(__dirname, '../baseline.html'), 'utf8');
  const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
  const events = new Map();
  const receipts = [];
  const count = {value: '0'};
  const context = {
    screen: {width: 1920, height: 1080},
    innerWidth: 1280,
    innerHeight: 720,
    devicePixelRatio: 2,
    document: {
      title: 'Maho ordinary form baseline',
      querySelector(selector) {
        if (selector === '#baseline-count') return count;
        return {addEventListener: (event, listener) => events.set(event, listener)};
      },
    },
    fetch(url, options) {
      assert.equal(url, '/receipt');
      receipts.push(JSON.parse(options.body));
      return Promise.resolve();
    },
  };
  runInNewContext(script, context);
  assert.equal(receipts[0].display.viewport_width, 1280);
  context.innerWidth = 920;
  events.get('click')({isTrusted: true});
  assert.equal(count.value, '1');
  assert.deepEqual(receipts[1], {
    kind: 'click', value: 1, isTrusted: true,
    display: {screen_width: 1920, screen_height: 1080,
      viewport_width: 920, viewport_height: 720, device_pixel_ratio: 2},
  });
  context.devicePixelRatio = 1;
  events.get('input')({isTrusted: true, target: {value: 'baseline-0'}});
  assert.equal(receipts[2].display.device_pixel_ratio, 1);
  assert.equal(receipts[0].display.device_pixel_ratio, 2);
});

test('shortcut receipts distinguish selection and prevented default', () => {
  const html = readFileSync(join(__dirname, '../baseline.html'), 'utf8');
  const events = new Map();
  const receipts = [];
  const field = {value: 'keyboard-check', selectionStart: 14, selectionEnd: 14,
    addEventListener: (name, listener) => events.set(name, listener)};
  runInNewContext(html.match(/<script>([\s\S]*?)<\/script>/)[1], {
    screen: {width: 1920, height: 1080}, innerWidth: 912, innerHeight: 813,
    devicePixelRatio: 1,
    document: {title: 'baseline', querySelector: selector =>
      selector === '#baseline-input' ? field : {addEventListener() {}}},
    fetch: (_url, options) => {
      receipts.push(JSON.parse(options.body));
      return Promise.resolve();
    },
  });
  assert.equal(typeof events.get('keydown'), 'function');
  const key = {key: 'a', metaKey: true, isTrusted: true, target: field,
    preventDefault() { this.defaultPrevented = true; }};
  events.get('keydown')(key);
  assert.notEqual(key.defaultPrevented, true);
  field.selectionStart = 0;
  events.get('keyup')(key);
  const selected = receipts.at(-1);
  assert.equal(selected.kind, 'shortcut');
  assert.equal(selected.value, 1);
  assert.deepEqual(selected.selection, [0, 14]);
  assert.deepEqual(selected.before_selection, [14, 14]);
  field.selectionStart = 14;
  events.get('keydown')(key);
  assert.equal(key.defaultPrevented, true);
  events.get('keyup')(key);
  const prevented = receipts.at(-1);
  assert.equal(prevented.value, 2);
  assert.deepEqual(prevented.selection, [14, 14]);
  assert.equal(prevented.text, 'keyboard-check');
  assert.equal(prevented.input_events, prevented.before_input_events);
});
