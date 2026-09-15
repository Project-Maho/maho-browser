import {defineConfig} from 'vitest/config';

// Mirrors react/tsconfig.json's `paths`. The WebUI is compiled by GN + esbuild,
// not by a bundler with a resolver config, so tsconfig paths are the only
// existing alias map and vitest needs its own copy of it.
const featureUrl = new URL('./', import.meta.url);
const sharedNodeModulesUrl =
    new URL('../../../../website/node_modules/', featureUrl);
const localNodeModulesUrl = new URL('./node_modules/', featureUrl);

function featurePath(relativePath: string): string {
  return new URL(relativePath, featureUrl).pathname;
}

function sharedNodeModulePath(relativePath: string): string {
  return new URL(relativePath, sharedNodeModulesUrl).pathname;
}

function localNodeModulePath(relativePath: string): string {
  return new URL(relativePath, localNodeModulesUrl).pathname;
}

export default defineConfig({
  test: {
    environment: 'jsdom',
    globals: true,
    include: ['react/**/*.test.ts', 'react/**/*.test.tsx'],
    setupFiles: [featurePath('react/__tests__/setup.ts')],
  },
  resolve: {
    alias: [
      {find: /^@theme\/(.+)$/, replacement: featurePath('../maho_common/react/theme/$1')},
      {find: /^@ui\/(.+)$/, replacement: featurePath('../maho_common/react/ui/$1')},
      {find: /^@icons\/(.+)$/, replacement: featurePath('../maho_common/react/icons/$1')},
      {find: /^@lib\/(.+)$/, replacement: featurePath('react/lib/$1')},
      {find: /^@\/(.+)$/, replacement: featurePath('react/$1')},

      {
        find: /^@testing-library\/react$/,
        replacement: localNodeModulePath('@testing-library/react'),
      },
      {
        find: /^@testing-library\/user-event$/,
        replacement: localNodeModulePath('@testing-library/user-event'),
      },
      {
        find: /^@testing-library\/jest-dom$/,
        replacement: localNodeModulePath('@testing-library/jest-dom'),
      },

      {find: /^react$/, replacement: sharedNodeModulePath('react')},
      {
        find: /^react\/jsx-runtime$/,
        replacement: sharedNodeModulePath('react/jsx-runtime.js'),
      },
      {
        find: /^react\/jsx-dev-runtime$/,
        replacement: sharedNodeModulePath('react/jsx-dev-runtime.js'),
      },
      {find: /^react-dom$/, replacement: sharedNodeModulePath('react-dom')},
      {
        find: /^react-dom\/client$/,
        replacement: sharedNodeModulePath('react-dom/client.js'),
      },
      {
        find: /^react-dom\/test-utils$/,
        replacement: sharedNodeModulePath('react-dom/test-utils.js'),
      },
      {
        find: /^lucide-react$/,
        replacement:
            sharedNodeModulePath('lucide-react/dist/esm/lucide-react.js'),
      },
      {find: /^react-i18next$/, replacement: sharedNodeModulePath('react-i18next')},
      {find: /^i18next$/, replacement: sharedNodeModulePath('i18next')},
      {find: /^date-fns$/, replacement: sharedNodeModulePath('date-fns')},
      {
        find: /^date-fns\/locale$/,
        replacement: sharedNodeModulePath('date-fns/locale'),
      },
      {find: /^date-fns-tz$/, replacement: sharedNodeModulePath('date-fns-tz')},
      {find: /^chrono-node$/, replacement: sharedNodeModulePath('chrono-node')},
      {find: /^dompurify$/, replacement: sharedNodeModulePath('dompurify')},
      {find: /^clsx$/, replacement: sharedNodeModulePath('clsx')},
      {
        find: /^tailwind-merge$/,
        replacement: sharedNodeModulePath('tailwind-merge'),
      },
      {
        find: /^class-variance-authority$/,
        replacement: sharedNodeModulePath('class-variance-authority'),
      },
      {find: /^sonner$/, replacement: sharedNodeModulePath('sonner/dist/index.mjs')},
      {find: /^vaul$/, replacement: sharedNodeModulePath('vaul')},
      {find: /^cmdk$/, replacement: sharedNodeModulePath('cmdk')},
      {find: /^motion$/, replacement: sharedNodeModulePath('motion')},
      {
        find: /^@tanstack\/react-virtual$/,
        replacement: sharedNodeModulePath('@tanstack/react-virtual'),
      },
      {find: /^@radix-ui\/(.+)$/, replacement: sharedNodeModulePath('@radix-ui/$1')},
      {find: /^@dnd-kit\/(.+)$/, replacement: sharedNodeModulePath('@dnd-kit/$1')},
      {find: /^@tiptap\/(.+)$/, replacement: sharedNodeModulePath('@tiptap/$1')},
      {find: /^@sentry\/browser$/, replacement: sharedNodeModulePath('@sentry/browser')},
    ],
  },
});
