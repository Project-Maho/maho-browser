import path from 'node:path';
import os from 'node:os';
import {fileURLToPath} from 'node:url';

import {build} from '../../../../website/node_modules/esbuild/lib/main.js';
import {nodeModulesGuardPlugin} from '../maho_common/react/bundle_guard.mjs';

import fs from 'node:fs';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const websiteNodeModules = path.resolve(__dirname, '../../../../website/node_modules');
const shadowRoot = path.join(os.homedir(), 'node_modules');
const [sourceDirArg, outputFileArg, depfileArg] = process.argv.slice(2);

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

const mojomExternalPlugin = {
  name: 'maho-mail-mojom-external',
  setup(build) {
    build.onResolve({filter: /maho_mail\.mojom-webui\.js$/}, () => {
      return {path: './maho_mail.mojom-webui.js', external: true};
    });
  },
};

// Trusted Types: chrome:// pages enforce `require-trusted-types-for 'script'`,
// so every HTML sink (iframe srcdoc, document.write, innerHTML) demands
// TrustedHTML — with no default policy the reader iframe and print path throw
// at runtime. Install a default policy that routes HTML sinks through the
// sanitizer registered by react/utils/sanitizeHtml.ts: pipeline output marked
// with MAHO_SINK_MARKER passes through, anything else is sanitized at the
// sink. Script sinks deliberately keep NO default policy and still throw —
// this is finding M-1's replacement for the old pass-through banner.
const trustedTypesBanner =
    "(() => { if (window.trustedTypes && typeof window.trustedTypes.createPolicy === 'function') { try { window.trustedTypes.createPolicy('default', { createHTML: (s) => (globalThis.__mahoSanitizeHtml ? globalThis.__mahoSanitizeHtml(String(s)) : String(s)) }); } catch (e) {} } })();";

await build({
  absWorkingDir: sourceDir,
  banner: { js: trustedTypesBanner },
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
  plugins: [
    nodeModulesGuardPlugin([websiteNodeModules, path.join(__dirname, 'node_modules')]), mojomExternalPlugin],
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
