import path from 'node:path';
import {fileURLToPath} from 'node:url';

import {build} from '../../../../website/node_modules/esbuild/lib/main.js';
import {nodeModulesGuardPlugin} from '../maho_common/react/bundle_guard.mjs';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const websiteNodeModules = path.resolve(__dirname, '../../../../website/node_modules');
const [sourceDirArg, outputFileArg] = process.argv.slice(2);

if (!sourceDirArg || !outputFileArg) {
  throw new Error('Expected source directory and output file arguments.');
}

const sourceDir = path.resolve(process.cwd(), sourceDirArg);
const outputFile = path.resolve(process.cwd(), outputFileArg);

const mojomExternalPlugin = {
  name: 'maho-boost-mojom-external',
  setup(esbuild) {
    esbuild.onResolve({filter: /maho_boost\.mojom-webui\.js$/}, () => ({
      path: './maho_boost.mojom-webui.js',
      external: true,
    }));
  },
};

await build({
  absWorkingDir: sourceDir,
  bundle: true,
  entryPoints: [path.join(sourceDir, 'react/app.tsx')],
  format: 'esm',
  jsx: 'automatic',
  jsxImportSource: 'react',
  legalComments: 'none',
  loader: {
    '.ts': 'ts',
    '.tsx': 'tsx',
  },
  metafile: true,
  minify: true,
  nodePaths: [websiteNodeModules],
  outfile: outputFile,
  platform: 'browser',
  plugins: [nodeModulesGuardPlugin(websiteNodeModules), mojomExternalPlugin],
  sourcemap: false,
  target: ['chrome120'],
  tsconfig: path.join(sourceDir, 'react/tsconfig.json'),
  external: [
    'chrome://resources/js/*',
  ],
});
