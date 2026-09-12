import path from 'node:path';
import os from 'node:os';
import fs from 'node:fs';
import {fileURLToPath} from 'node:url';

import {build} from '../../../../website/node_modules/esbuild/lib/main.js';
import {nodeModulesGuardPlugin} from '../maho_common/react/bundle_guard.mjs';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const websiteNodeModules = path.resolve(__dirname, '../../../../website/node_modules');
const shadowRoot = path.join(os.homedir(), 'node_modules');
const [sourceDirArg, outputFileArg] = process.argv.slice(2);

if (!sourceDirArg || !outputFileArg) {
  throw new Error('Expected source directory and output file arguments.');
}

// The Chromium overlay mounts this tree via chromium/src/maho -> maho-chromium,
// so the GN-supplied path is a symlink. esbuild reports metafile inputs relative
// to absWorkingDir verbatim, so passing the symlinked path makes `..` segments
// climb out of /Volumes/T9-Mac and resolve to bogus roots like
// /Volumes/node_modules. Normalize to the real path up front.
let sourceDir = path.resolve(process.cwd(), sourceDirArg);
try {
  sourceDir = fs.realpathSync(sourceDir);
} catch {
  // Leave sourceDir as-is when the path cannot be resolved.
}
const outputFile = path.resolve(process.cwd(), outputFileArg);

await build({
  absWorkingDir: sourceDir,
  banner: {
    js: "(()=>{if(window.trustedTypes&&typeof window.trustedTypes.createPolicy==='function'){try{window.trustedTypes.createPolicy('default',{createHTML:s=>s,createScript:s=>s,createScriptURL:s=>s});}catch(e){}}})();",
  },
  bundle: true,
  entryPoints: [path.join(sourceDir, 'react/app.tsx')],
  format: 'esm',
  legalComments: 'none',
  loader: {
    '.ts': 'ts',
    '.tsx': 'tsx',
  },
  // Pin every specifier that a stray ~/node_modules install could shadow to the
  // canonical website/node_modules copy. esbuild walks node_modules upward from
  // the importer and only consults nodePaths after that walk fails, so a local
  // maho_*/node_modules without React lets the walk escape the workspace. The
  // list is derived at build time so transitive pulls (e.g. clsx via sonner)
  // are covered too, which a source-level scan cannot see.
  alias: Object.fromEntries(
      fs.existsSync(shadowRoot)
          ? fs.readdirSync(shadowRoot)
                .flatMap((entry) => entry.startsWith('@')
                    ? fs.readdirSync(path.join(shadowRoot, entry))
                          .map((sub) => `${entry}/${sub}`)
                    : [entry])
                .filter((pkg) => !pkg.startsWith('.') &&
                    fs.existsSync(path.join(websiteNodeModules, pkg)))
                .map((pkg) => [pkg, path.join(websiteNodeModules, pkg)])
          : []),
  metafile: true,
  minify: true,
  nodePaths: [websiteNodeModules],
  outfile: outputFile,
  platform: 'browser',
  plugins: [
    nodeModulesGuardPlugin(websiteNodeModules),
    {
      name: 'mojom-webui-external',
      setup(build) {
        build.onResolve({filter: /maho_ai\.mojom-webui\.js$/}, () => ({
          path: './maho_ai.mojom-webui.js',
          external: true,
        }));
      },
    },
  ],
  sourcemap: false,
  target: ['chrome120'],
  tsconfig: path.join(sourceDir, 'react/tsconfig.json'),
  external: [
    'chrome://resources/js/*',
  ],
});
