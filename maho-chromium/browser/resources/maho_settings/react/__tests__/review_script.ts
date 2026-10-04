import {execFileSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';

export function readResource(path: string): string {
  return readFileSync(resolve(process.cwd(), '..', path), 'utf8');
}

// esbuild must run in Node's realm, not jsdom's mismatched typed-array realm.
export function compileScript(source: string): string {
  return execFileSync('node', ['-e',
    'process.stdout.write(require("esbuild").transformSync(require("fs").readFileSync(0,"utf8"), {loader:"ts"}).code)'],
    {input: source, encoding: 'utf8', timeout: 10000});
}
