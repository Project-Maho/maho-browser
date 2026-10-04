import {existsSync, readFileSync} from 'node:fs';
import {resolve, dirname} from 'node:path';
import {expect, it} from 'vitest';

it('can resolve every executable target in the aggregate runner manifest', () => {
  const runner = resolve(process.cwd(), 'testing/run_all.ts');
  const source = readFileSync(runner, 'utf8');
  const manifest = JSON.parse(source.match(/const tests = (\[[\s\S]*?\]);/)![1]) as string[];
  const missing = manifest.filter(name => !existsSync(resolve(dirname(runner), name)));
  expect(missing).toEqual([]);
});
