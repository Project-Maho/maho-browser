import {compileScript, readResource} from './review_script';
import {afterEach, expect, it, vi} from 'vitest';

afterEach(() => {
  (window as any).__mahoTranslate?.restorePage();
  document.body.innerHTML = '';
  delete (window as any).__mahoTranslate;
  delete (window as any).Translator;
  vi.restoreAllMocks();
});
function loadTranslation() {
  // Expose the existing promise chain to await exact completion, not a polling delay.
  const source = readResource('maho_translate_content_script/content_script.ts').replace('void resolveSource', 'return resolveSource');
  new Function(compileScript(source.replace('export {};', '')))();
  return (window as any).__mahoTranslate;
}
it.each(['restore', 'failure'])('translation respects %s on pending work', async scenario => {
  document.body.innerHTML = '<p>original</p>';
  document.documentElement.lang = 'en';
  let resolve!: (s: string) => void;
  let reject!: (e: Error) => void;
  let started!: () => void;
  const called = new Promise<void>(r => {started = r;});
  const pending = new Promise<string>((r, j) => {resolve = r; reject = j;});
  (window as any).Translator = {create: async () => ({translate: () => {started(); return pending;}})};
  const api = loadTranslation();
  const finished = api.translatePage('ko');
  await called;
  if (scenario === 'restore') {api.restorePage(); resolve('translated');}
  else reject(new Error('translation failed'));
  await finished;
  expect(document.body.textContent).toBe('original');
  expect(api.status()).toBe(scenario === 'restore' ? 'idle' : 'error');
});
it.each([1, 5])('confirms the previewed Related parent at level %s', level => {
  new Function(compileScript(readResource('maho_boost_content_script/selector_component.ts')))();
  document.body.innerHTML = '<div class="group"><span id="child">one</span></div><div class="group"><span>two</span></div>';
  const select = vi.fn();
  const component = new (window as any).__mahoBoost.SelectorComponent(document, [], select);
  component.initialize();
  component.relatedValueIndex = level;
  component.setState('selected', document.getElementById('child'));
  const preview = component.relatedSelection;
  expect(preview.kind).toBe('valid');
  component.selectRelatedButton.click();
  expect(select).toHaveBeenCalledWith(preview.selector);
  component.tearDown();
});
