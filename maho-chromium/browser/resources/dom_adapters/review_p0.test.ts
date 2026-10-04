import {expect, it} from 'bun:test';
import {PageAdapterRegistry} from './page_adapter_registry';

it('redacts complete decoded passwords including embedded quotes', () => {
  const registry = new PageAdapterRegistry();
  for (const password of ['a"secret123', 'abcd"secret123', "x'secret", 'x secret']) {
    const {sanitized, warnings} = registry.sanitizeAndBoundOutput({password, text: `password: ${password}`});
    expect(typeof sanitized).toBe('object');
    expect(JSON.stringify(sanitized)).not.toContain('secret');
    expect(warnings).toContain('SENSITIVE_DATA_REDACTED');
  }
});
