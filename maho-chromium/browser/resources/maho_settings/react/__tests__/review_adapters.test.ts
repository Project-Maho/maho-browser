import {afterEach, expect, it} from 'vitest';
import {PageAdapterRegistry} from '../../../dom_adapters/page_adapter_registry';
import {ReactFiberInspector} from '../../../dom_adapters/react_fiber_inspector';
import {GoogleDocsAdapter} from '../../../dom_adapters/google_docs_adapter';

afterEach(() => {document.body.innerHTML = '';});
it('fails closed rather than returning an over-budget non-ASCII extraction', () => {
  const registry = new PageAdapterRegistry();
  const adapter = new ReactFiberInspector();
  adapter.Extract = () => ({success: true, source_adapter: adapter.descriptor().adapter_id, data: {text: '漢'.repeat(100000)}});
  registry.registerAdapter(adapter);
  const result = registry.executeExtract(adapter.descriptor().adapter_id, {operation: 'InspectElement'}, 'https://example.com', {hasReactFiber: true});
  expect(result.success).toBe(false);
  expect(result.data).toBeUndefined();
  expect(result.error).toBeTruthy();
});
it('does not invent a Docs target for absent text', () => {
  document.body.innerHTML = '<div class="kix-appview-editor"></div>';
  const editor = document.body.firstElementChild!;
  editor.getBoundingClientRect = () => ({left: 10, top: 20, width: 800, height: 600} as DOMRect);
  expect(new GoogleDocsAdapter().ResolveTarget('absent text').found).toBe(false);
});
it('dispatches the advertised React ResolveTarget operation', () => {
  document.body.innerHTML = '<button id="save">Save</button>';
  const registry = new PageAdapterRegistry();
  const adapter = new ReactFiberInspector();
  registry.registerAdapter(adapter);
  const result = registry.executeExtract(adapter.descriptor().adapter_id,
    {operation: 'ResolveTarget', params: {selector: '#save'}}, 'https://example.com', {hasReactFiber: true});
  expect(result.success).toBe(true);
  expect(result.data).toMatchObject({found: true});
});
it.each(['data-testid', 'data-test-id', 'data-cy', 'data-qa'])('round-trips quoted %s values through DOM selectors', attr => {
  const target = document.createElement('button');
  target.setAttribute(attr, 'save"draft');
  document.body.append(target);
  const selector = new ReactFiberInspector().generateSafeSelector(target);
  expect(document.querySelector(selector)).toBe(target);
});
