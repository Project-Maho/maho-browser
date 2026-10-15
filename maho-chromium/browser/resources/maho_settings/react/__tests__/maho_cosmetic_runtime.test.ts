import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';
import {readFileSync} from 'node:fs';
import {resolve, dirname} from 'node:path';
import {fileURLToPath} from 'node:url';

const net = resolve(dirname(fileURLToPath(import.meta.url)), '../../../../net');
const runtime = readFileSync(resolve(net, 'maho_cosmetic_runtime.js'), 'utf8');
const procedural = readFileSync(resolve(net, 'maho_cosmetic_procedural.js'), 'utf8');
const header = readFileSync(resolve(net, 'maho_cosmetic_runtime_string.h'), 'utf8');

function load() {
  window.__mahoCosmetic = undefined;
  window.eval(runtime + '\n' + procedural);
  return window.__mahoCosmetic;
}

describe('Maho cosmetic runtime', () => {
  beforeEach(() => {
    document.head.innerHTML = '';
    document.body.innerHTML = '<div id="ad" class="banner"></div>';
  });

  afterEach(() => vi.restoreAllMocks());

  it('applies hide CSS and stores exceptions and generichide', () => {
    const api = load();
    api.init({hideSelectors: ['.specific'], exceptions: ['.banner'], generichide: true});
    expect(document.querySelector('#maho-cosmetic-style')?.textContent).toContain('.specific');
    expect(api.exceptions).toEqual(['.banner']);
    expect(api.generichide).toBe(true);
  });

  it('skips class and id collection when generichide is enabled', () => {
    const api = load();
    api.init({generichide: true});
    expect(JSON.parse(api.drainBatches()).batches).toEqual([]);
  });

  it('deduplicates and caps collected tokens at 2048', () => {
    document.body.innerHTML = Array.from({length: 2100}, (_, i) =>
      `<i class="c${i}"></i>`).join('') + '<b class="c0"></b>';
    const api = load();
    api.init({generichide: false});
    const tokens = JSON.parse(api.drainBatches()).batches.flat();
    expect(tokens).toHaveLength(2048);
    expect(new Set(tokens).size).toBe(tokens.length);
    expect(tokens).toContain('.c0');
  });

  it('drains and clears pending tokens', () => {
    const api = load();
    api.init({generichide: false});
    expect(JSON.parse(api.drainBatches()).batches.flat().length).toBeGreaterThan(0);
    expect(JSON.parse(api.drainBatches()).batches).toEqual([]);
  });

  it('returns mutation count with each drained batch', () => {
    const api = load();
    api.init({generichide: false});
    const observed = new Promise<void>(resolve => {
      const observer = new MutationObserver(() => resolve());
      observer.observe(document.body, {childList: true});
    });
    document.body.appendChild(Object.assign(document.createElement('div'), {className: 'later'}));
    return observed.then(() => {
      const drained = JSON.parse(api.drainBatches());
      expect(drained).toHaveProperty('mutations', 1);
      expect(drained.batches.flat()).toContain('.later');
      expect(JSON.parse(api.drainBatches()).mutations).toBe(1);
    });
  });

  it('reads the mutation counter without draining pending tokens', async () => {
    const api = load();
    api.init({generichide: false});
    const changed = new Promise<void>(resolve => {
      const observer = new MutationObserver(() => resolve());
      observer.observe(document.body, {childList: true});
    });
    document.body.appendChild(Object.assign(document.createElement('div'), {className: 'queued'}));
    await changed;
    expect(api.readMutationCounter()).toBe(1);
    expect(JSON.parse(api.drainBatches()).batches.flat()).toContain('.queued');
  });

  it('applies generic selectors added by the browser poller', () => {
    const api = load();
    api.addGenericSelectors(['.generic-ad', '#generic-promo']);
    const style = document.querySelector('#maho-cosmetic-generic-style');
    expect(style?.textContent).toContain('.generic-ad, #generic-promo');
    expect(api.addGenericSelectors).toBeTypeOf('function');
  });

  it('drain returns both batch data and a stable mutation counter', () => {
    const api = load();
    api.init({generichide: false});
    const first = JSON.parse(api.drainBatches());
    const second = JSON.parse(api.drainBatches());
    expect(first).toHaveProperty('batches');
    expect(first).toHaveProperty('mutations', 0);
    expect(second).toEqual({batches: [], mutations: 0});
  });

  const operators = [
    ['has-text', 'Sponsored'],
    ['matches-attr', 'title:promo'],
    ['matches-css', 'display:block'],
    ['matches-css-before', 'content:promo'],
    ['matches-css-after', 'content:promo'],
    ['matches-path', '.*'],
    ['min-text-length', '1'],
    ['upward', '1'],
    ['xpath', '.'],
  ];

  it.each(operators)('interprets operator %s', (type, arg) => {
    document.body.innerHTML = '<div class="candidate" title="promo">Sponsored</div>';
    const api = load();
    if (type === 'matches-css-before' || type === 'matches-css-after') {
      const nativeGetComputedStyle = window.getComputedStyle.bind(window);
      vi.spyOn(window, 'getComputedStyle').mockImplementation((element, pseudo) => {
        if (pseudo) return {getPropertyValue: () => 'promo'} as CSSStyleDeclaration;
        return nativeGetComputedStyle(element);
      });
    }
    const result = api.runProcedural([{
      selector: [{type: 'css-selector', arg: '.candidate'}, {type, arg}],
      action: {type: 'remove'},
    }]);
    expect(result.applied).toBe(1);
    expect(document.querySelector('.candidate')).toBeNull();
  });

  it.each([
    [{type: 'remove'}, '<span class="candidate">Sponsored</span>', (el: Element) => expect(el.isConnected).toBe(false)],
    [{type: 'style', arg: 'color: red'}, '<span class="candidate">Sponsored</span>', (el: Element) => expect((el as HTMLElement).style.color).toBe('red')],
    [{type: 'remove-attr', arg: 'title'}, '<span class="candidate" title="x">Sponsored</span>', (el: Element) => expect(el.hasAttribute('title')).toBe(false)],
    [{type: 'remove-class', arg: 'advert'}, '<span class="candidate advert">Sponsored</span>', (el: Element) => expect(el.classList.contains('advert')).toBe(false)],
  ])('applies action %j', (action, markup, assertResult) => {
    document.body.innerHTML = markup;
    const api = load();
    const candidate = document.querySelector('.candidate')!;
    const result = api.runProcedural([{selector: [{type: 'css-selector', arg: '.candidate'}], action}]);
    expect(result.applied).toBe(1);
    assertResult(candidate);
  });

  it('ignores unsupported operator and action shapes', () => {
    const api = load();
    document.body.innerHTML = '<div class="candidate"></div><div class="candidate"></div>';
    const result = api.runProcedural([
      {selector: [{type: 'invented', arg: 'x'}]},
      {selector: [{type: 'css-selector', arg: '.candidate'}], action: {type: 'unknown'}},
    ]);
    expect(result.unsupported).toBeGreaterThan(0);
    expect(document.querySelector('.candidate')).not.toBeNull();
  });

  it('stops at the configured action budget', () => {
    const api = load();
    document.body.innerHTML = '<div class="candidate"></div><div class="candidate"></div>';
    const rules = Array.from({length: 5}, () => ({
      selector: [{type: 'css-selector', arg: '.candidate'}],
      action: {type: 'style', arg: 'display: block'},
    }));
    const result = api.runProcedural(rules, {maxActions: 2});
    expect(result.applied).toBe(2);
    expect(result.stopped).toBe(true);
  });

  it('preserves remaining rules when a procedural batch hits its budget', () => {
    vi.useFakeTimers();
    const api = load();
    const rules = Array.from({length: 5}, () => ({
      selector: [{type: 'css-selector', arg: '.candidate'}],
      action: {type: 'remove-class', arg: 'seen'},
    }));
    document.body.innerHTML = '<div class="candidate seen"></div>';
    const result = api.runProcedural(rules, {maxActions: 2});
    expect(result.stopped).toBe(true);
    vi.runOnlyPendingTimers();
    expect(document.querySelector('.candidate')?.classList.contains('seen')).toBe(false);
    vi.useRealTimers();
  });

  it('keeps embedded source byte-for-byte aligned with source modules', () => {
    const bodies = [...header.matchAll(/R"MAHO_JS\(([\s\S]*?)\)MAHO_JS";/g)];
    expect(bodies.map(match => match[1]).join('')).toBe('\n' + runtime + '\n' + procedural);
  });
});
