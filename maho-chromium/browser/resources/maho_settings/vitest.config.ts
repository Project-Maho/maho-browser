import {defineConfig} from 'vitest/config';

const featureUrl = new URL('./', import.meta.url);
const sharedNodeModulesUrl = new URL('../../../../website/node_modules/', featureUrl);

function featurePath(relativePath: string): string {
  return new URL(relativePath, featureUrl).pathname;
}

function sharedNodeModulePath(relativePath: string): string {
  return new URL(relativePath, sharedNodeModulesUrl).pathname;
}

export default defineConfig({
  test: {
    environment: 'jsdom',
    include: ['react/**/*.test.ts', 'react/**/*.test.tsx'],
  },
  resolve: {
    alias: [
      {
        find: /^.*maho_boost\.mojom-webui\.js$/,
        replacement: featurePath('../maho_boost/testing/mojo_stub.ts'),
      },
      {
        find: /^\.\.\/mojo\.js$/,
        replacement: featurePath('react/__tests__/mojo_stub.ts'),
      },
      {
        find: /^.*maho_settings\.mojom-webui\.js$/,
        replacement: featurePath('react/__tests__/mojo_stub.ts'),
      },
      {
        find: /^react$/,
        replacement: sharedNodeModulePath('react'),
      },
      {
        find: /^react\/jsx-runtime$/,
        replacement: sharedNodeModulePath('react/jsx-runtime.js'),
      },
      {
        find: /^react\/jsx-dev-runtime$/,
        replacement: sharedNodeModulePath('react/jsx-dev-runtime.js'),
      },
      {
        find: /^react-dom\/client$/,
        replacement: sharedNodeModulePath('react-dom/client.js'),
      },
      {
        find: /^react-dom$/,
        replacement: sharedNodeModulePath('react-dom'),
      },
      {
        find: /^@lib\/utils$/,
        replacement: featurePath('../maho_common/react/lib/utils.ts'),
      },
      {
        find: /^@ui\/([^.]+)(?:\.js)?$/,
        replacement: featurePath('../maho_common/react/ui/$1.tsx'),
      },
      {
        find: /^@theme\/([^.]+)(?:\.js)?$/,
        replacement: featurePath('../maho_common/react/theme/$1.ts'),
      },
      {
        find: /^@icons\/lucide$/,
        replacement: featurePath('../maho_common/react/icons/lucide.ts'),
      },
      {
        find: /^lucide-react$/,
        replacement: sharedNodeModulePath('lucide-react/dist/esm/lucide-react.js'),
      },
      {
        find: /^class-variance-authority$/,
        replacement: sharedNodeModulePath('class-variance-authority'),
      },
      {
        find: /^tailwind-merge$/,
        replacement: sharedNodeModulePath('tailwind-merge'),
      },
      {
        find: /^clsx$/,
        replacement: sharedNodeModulePath('clsx'),
      },
      {
        find: /^sonner$/,
        replacement: sharedNodeModulePath('sonner/dist/index.mjs'),
      },
      {
        find: /^@radix-ui\/(.+)$/,
        replacement: sharedNodeModulePath('@radix-ui/$1'),
      },
    ],
  },
});
