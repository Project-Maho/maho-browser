import path from 'node:path';
import fs from 'node:fs';

const WEBSITE_ONLY_PACKAGES = [
  '@astrojs/',
  'astro',
  'starlight',
  'vite',
];

export function nodeModulesGuardPlugin(websiteNodeModules) {
  let normalizedBase;
  try {
    normalizedBase = fs.realpathSync(websiteNodeModules);
  } catch {
    normalizedBase = path.resolve(websiteNodeModules);
  }
  return {
    name: 'maho-node-modules-guard',
    setup(esbuild) {
      esbuild.onEnd((result) => {
        if (!result.metafile) return;
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
          if (!absolute.startsWith(normalizedBase)) {
            throw new Error(
                `[H1 Guard] Resolved module from unexpected node_modules:\n` +
                `  ${absolute}\n` +
                `  Expected all node_modules resolutions under:\n` +
                `  ${normalizedBase}`);
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
