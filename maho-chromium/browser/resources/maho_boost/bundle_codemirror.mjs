// Copyright 2026 Maho Browser. All rights reserved.

import path from 'node:path';
import {fileURLToPath} from 'node:url';

import {build} from '../../../../website/node_modules/esbuild/lib/main.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const websiteNodeModules = path.resolve(__dirname, '../../../../website/node_modules');
const [sourceDirArg, outputFileArg] = process.argv.slice(2);

if (!sourceDirArg || !outputFileArg) {
  throw new Error('Expected source directory and output file arguments.');
}

const sourceDir = path.resolve(process.cwd(), sourceDirArg);
const outputFile = path.resolve(process.cwd(), outputFileArg);

await build({
  absWorkingDir: sourceDir,
  bundle: true,
  entryPoints: [path.join(sourceDir, 'codemirror_entry.ts')],
  format: 'iife',
  legalComments: 'none',
  loader: {
    '.ts': 'ts',
  },
  minify: true,
  nodePaths: [websiteNodeModules],
  outfile: outputFile,
  platform: 'browser',
  sourcemap: false,
  target: ['chrome120'],
});
