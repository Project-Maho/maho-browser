import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {defineConfig} from 'vitest/config';

const featureDir = path.dirname(fileURLToPath(import.meta.url));

export default defineConfig({
  test: {
    environment: 'node',
    include: ['react/__tests__/**/*.test.ts'],
    // Temporarily parked: mail onboarding WIP depends on a newer store/types API
    // not present in the current active onboarding. Re-enable when reconciled.
    exclude: [
      '**/node_modules/**',
      '**/dist/**',
      'react/__tests__/mail_onboarding_lifecycle.test.ts',
    ],
  },
  resolve: {
    alias: [
      {
        find: /^\.\.\/maho_welcome\.mojom-webui\.js$/,
        replacement: path.join(featureDir, 'standalone/maho_welcome.mojom-webui.js'),
      },
      {
        find: /^\.\.\/\.\.\/maho_welcome\.mojom-webui\.js$/,
        replacement: path.join(featureDir, 'standalone/maho_welcome.mojom-webui.js'),
      },
      {
        find: /^\/\/resources\/maho_common\/react\/store_utils\.js$/,
        replacement: path.join(featureDir, '../maho_common/react/store_utils.ts'),
      },
      {
        find: /^@lib\/utils$/,
        replacement: path.join(featureDir, 'standalone/vitest-plan-picker-stubs.ts'),
      },
      {
        find: /^@ui\/button$/,
        replacement: path.join(featureDir, 'standalone/vitest-plan-picker-stubs.ts'),
      },
      {
        find: /^@ui\/badge$/,
        replacement: path.join(featureDir, 'standalone/vitest-plan-picker-stubs.ts'),
      },
      {
        find: '@ui',
        replacement: path.join(featureDir, '../maho_common/react/ui'),
      },
      {
        find: '@lib',
        replacement: path.join(featureDir, '../maho_common/react/lib'),
      },
      {
        find: 'react/jsx-runtime',
        replacement: path.join(
            featureDir,
            '../../../../website/node_modules/react/jsx-runtime.js'),
      },
      {
        find: 'react',
        replacement: path.join(
            featureDir,
            '../../../../website/node_modules/react/index.js'),
      },
      {
        find: 'lucide-react',
        replacement: path.join(
            featureDir,
            '../../../../website/node_modules/lucide-react/dist/esm/lucide-react.js'),
      },
    ],
  },
});
