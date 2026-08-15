import path from 'node:path';
import {fileURLToPath} from 'node:url';

import {build} from '../../../../website/node_modules/esbuild/lib/main.js';
import {nodeModulesGuardPlugin} from '../maho_common/react/bundle_guard.mjs';

import fs from 'node:fs';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const websiteNodeModules = path.resolve(__dirname, '../../../../website/node_modules');
const [sourceDirArg, outputFileArg, depfileArg] = process.argv.slice(2);

if (!sourceDirArg || !outputFileArg) {
  throw new Error('Expected source directory and output file arguments.');
}

const sourceDir = path.resolve(process.cwd(), sourceDirArg);
const outputFile = path.resolve(process.cwd(), outputFileArg);

const mojomExternalPlugin = {
  name: 'maho-mail-mojom-external',
  setup(build) {
    build.onResolve({filter: /maho_mail\.mojom-webui\.js$/}, () => {
      return {path: './maho_mail.mojom-webui.js', external: true};
    });
  },
};

await build({
  absWorkingDir: sourceDir,
  alias: {
    react: path.join(websiteNodeModules, 'react'),
    'react-dom': path.join(websiteNodeModules, 'react-dom'),
    tailwindcss: path.join(websiteNodeModules, 'tailwindcss'),
    'tw-animate-css': path.join(websiteNodeModules, 'tw-animate-css'),
  },
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
  metafile: true,
  minify: true,
  nodePaths: [websiteNodeModules],
  outfile: outputFile,
  platform: 'browser',
  conditions: ['style'],
  plugins: [nodeModulesGuardPlugin([websiteNodeModules]), mojomExternalPlugin],
  sourcemap: false,
  target: ['chrome120'],
  tsconfig: path.join(sourceDir, 'react/tsconfig.json'),
  external: [
    'chrome://resources/js/*',
  ],
}).then(result => {
  if (depfileArg && result.metafile) {
    const depfilePath = path.resolve(process.cwd(), depfileArg);
    // Paths must stay relative to the build output dir (process.cwd()) and use
    // forward slashes: a Windows absolute path puts a second colon on the dep
    // line ("C:\..."), which ninja/siso reject as a malformed depfile.
    const toDepPath = absolutePath =>
        path.relative(process.cwd(), absolutePath).split(path.sep).join('/');
    const inputs = Object.keys(result.metafile.inputs);
    const resolvedInputs = inputs
        // node_modules lives outside the source tree, so siso rejects it as a
        // missing dep. Bundled third-party code is pinned by the lockfile and
        // never edited in place, so it does not need to trigger rebuilds. Filter
        // on the raw esbuild input path segment: the resolved absolute path can
        // diverge from websiteNodeModules through the chromium/src/maho build
        // symlink (lexical vs realpath), which defeats a startsWith() check.
        .filter(input => !input.split('/').includes('node_modules'))
        .map(input => path.resolve(sourceDir, input))
        .map(toDepPath);
    const depContent =
        `${toDepPath(outputFile)}: ${resolvedInputs.join(' ')}\n`;
    fs.writeFileSync(depfilePath, depContent);
  }
});
