import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {defineConfig} from 'vitest/config';

const featureDir = path.dirname(fileURLToPath(import.meta.url));

const sharedExclude = [
  '**/node_modules/**',
  '**/dist/**',
  // Temporarily parked: mail onboarding WIP depends on a newer store/types API
  // not present in the current active onboarding. Re-enable when reconciled.
  'react/__tests__/mail_onboarding_lifecycle.test.ts',
];

export default defineConfig({
  test: {
    projects: [
      {
        extends: true,
        test: {
          name: 'unit',
          environment: 'node',
          include: ['react/__tests__/**/*.test.ts'],
          exclude: sharedExclude,
        },
      },
      {
        extends: true,
        test: {
          // Component tests render real React trees and need a DOM.
          name: 'component',
          environment: 'jsdom',
          include: ['react/__tests__/**/*.test.tsx'],
          exclude: sharedExclude,
        },
      },
    ],
  },
  resolve: {
    alias: [
      {
        find: /^@radix-ui\/(.+)$/,
        replacement: path.join(featureDir, '../../../../website/node_modules/@radix-ui/$1'),
      },
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
        find: 'react/jsx-dev-runtime',
        replacement: path.join(
            featureDir,
            '../../../../website/node_modules/react/jsx-dev-runtime.js'),
      },
      {
        find: 'react-dom/client',
        replacement: path.join(
            featureDir,
            '../../../../website/node_modules/react-dom/client.js'),
      },
      {
        find: 'react-dom',
        replacement: path.join(
            featureDir,
            '../../../../website/node_modules/react-dom/index.js'),
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
      {
        find: 'class-variance-authority',
        replacement: path.join(
            featureDir,
            '../../../../website/node_modules/class-variance-authority'),
      },
      {
        find: 'tailwind-merge',
        replacement: path.join(
            featureDir, '../../../../website/node_modules/tailwind-merge'),
      },
      {
        find: 'clsx',
        replacement: path.join(
            featureDir, '../../../../website/node_modules/clsx'),
      },
    ],
  },
});
