import path from 'node:path';
import os from 'node:os';
import fs from 'node:fs';
import {fileURLToPath} from 'node:url';
import {execSync} from 'node:child_process';

import {build} from '../../../../website/node_modules/esbuild/lib/main.js';
import {nodeModulesGuardPlugin} from '../maho_common/react/bundle_guard.mjs';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Run the drift check first to ensure mojom and d.ts are aligned
try {
  execSync(`bun ${path.join(__dirname, 'drift_check.js')}`, { stdio: 'inherit' });
} catch (e) {
  process.exit(1);
}

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

const settingsMojomExternalPlugin = {
  name: 'maho-settings-mojom-external',
  setup(build) {
    build.onResolve({filter: /maho_settings\.mojom-webui\.js$/}, () => {
      return {path: './maho_settings.mojom-webui.js', external: true};
    });
  },
};

const result = await build({
  absWorkingDir: sourceDir,
  banner: {
    js: "(()=>{if(window.trustedTypes&&typeof window.trustedTypes.createPolicy==='function'){try{window.trustedTypes.createPolicy('default',{createHTML:s=>s,createScript:s=>s,createScriptURL:s=>s});}catch(e){}}})();",
  },
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
    settingsMojomExternalPlugin,
  ],
  sourcemap: false,
  target: ['chrome120'],
  tsconfig: path.join(sourceDir, 'react/tsconfig.json'),
  external: [
    'chrome://resources/js/*',
  ],
});

if (result.metafile) {
  const fs = await import('node:fs');
  const buildGnPath = path.join(sourceDir, 'BUILD.gn');
  if (fs.existsSync(buildGnPath)) {
    const buildGnContent = fs.readFileSync(buildGnPath, 'utf8');
    const bundleReactIdx = buildGnContent.indexOf('action("bundle_react")');
    if (bundleReactIdx !== -1) {
      const bundleReactContent = buildGnContent.substring(bundleReactIdx);
      const inputsMatch = bundleReactContent.match(/inputs\s*=\s*\[([\s\S]*?)\]/);
      if (inputsMatch) {
        const inputsBlock = inputsMatch[1];
        const gnInputs = [];
        const regex = /"([^"]+)"/g;
        let match;
        while ((match = regex.exec(inputsBlock)) !== null) {
          gnInputs.push(match[1]);
        }

        let workspaceRoot = sourceDir;
        while (workspaceRoot && workspaceRoot !== '/' && !fs.existsSync(path.join(workspaceRoot, 'maho-chromium'))) {
          workspaceRoot = path.dirname(workspaceRoot);
        }
        if (!fs.existsSync(path.join(workspaceRoot, 'maho-chromium'))) {
          workspaceRoot = path.resolve(sourceDir, '../../../..');
        }
        const chromiumSrc = path.join(workspaceRoot, 'chromium/src');

        const absGnInputs = new Set(gnInputs.map(p => {
          if (p.startsWith('//')) {
            return path.resolve(chromiumSrc, p.slice(2));
          }
          return path.resolve(sourceDir, p);
        }));

        for (const inputPath of Object.keys(result.metafile.inputs)) {
          if (inputPath.includes('node_modules')) continue;
          if (inputPath.startsWith('chrome://')) continue;

          const absolutePath = path.resolve(sourceDir, inputPath);
          if (!fs.existsSync(absolutePath)) continue;

          if (!absGnInputs.has(absolutePath)) {
            console.error(`[Guard Error] Source file "${inputPath}" is bundled by esbuild but missing from action("bundle_react") inputs in BUILD.gn.`);
            console.error(`Please add "${path.relative(sourceDir, absolutePath)}" to inputs in BUILD.gn.`);
            process.exit(1);
          }
        }
      }
    }
  }
}
