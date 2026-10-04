import path from 'node:path';
import fs from 'node:fs';

const WEBSITE_ONLY_PACKAGES = [
  '@astrojs/',
  'astro',
  'starlight',
  'vite',
];

export function nodeModulesGuardPlugin(allowedBases) {
  const bases = Array.isArray(allowedBases) ? allowedBases : [allowedBases];
  const normalizedBases = bases.map(b => {
    try {
      return fs.realpathSync(b);
    } catch {
      return path.resolve(b);
    }
  });
  return {
    name: 'maho-node-modules-guard',
    setup(esbuild) {
      esbuild.onEnd((result) => {
        if (!result.metafile) return;
        // esbuild reports metafile inputs relative to the REAL working dir, so
        // the overlay symlink (chromium/src/maho -> maho-chromium) must be
        // resolved here. Resolving against the raw symlinked path instead sends
        // the `..` segments up /Volumes/T9-Mac/chromium/src and yields bogus
        // absolutes like /Volumes/node_modules/react.
        const rawWorkingDir = esbuild.initialOptions.absWorkingDir || process.cwd();
        let absWorkingDir;
        try {
          absWorkingDir = fs.realpathSync(rawWorkingDir);
        } catch {
          absWorkingDir = rawWorkingDir;
        }
        for (const inputPath of Object.keys(result.metafile.inputs)) {
          if (!inputPath.includes('node_modules')) continue;
          const absolute = path.resolve(absWorkingDir, inputPath);
          let allowed = false;
          for (const normalizedBase of normalizedBases) {
            if (absolute.startsWith(normalizedBase)) {
              allowed = true;
              break;
            }
          }
          if (!allowed) {
            throw new Error(
                `[H1 Guard] Resolved module from unexpected node_modules:\n` +
                `  ${absolute}\n` +
                `  Expected all node_modules resolutions under one of:\n` +
                `  ${normalizedBases.join('\n  ')}`);
          }
          for (const pkg of WEBSITE_ONLY_PACKAGES) {
            if (absolute.includes(`node_modules/${pkg}`)) {
              throw new Error(
                  `[H1 Guard] WebUI bundle pulled website-only package "${pkg}":\n` +
                  `  ${absolute}`);
            }
          }
        }
      });
    },
  };
}

